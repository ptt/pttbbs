package importer

import (
	"bytes"
	"context"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
	"unicode/utf8"

	"github.com/syndtr/goleveldb/leveldb"
	"github.com/syndtr/goleveldb/leveldb/opt"

	"pttbbs/big5uao"
	"pttbbs/nest/model"
	"pttbbs/nest/storage"
)

// MaxArticleFileSize is the absolute upper limit for article file size,
// aligned with mbbsd/edit.c EDIT_SIZE_LIMIT (32MB).
const (
	FileHeaderSize     = 128
	MaxArticleFileSize = 32 * 1024 * 1024 // 32MB

	// Filemode flags in legacy announce (.DIR)
	FileHide uint16 = 0x0020 // FILE_HIDE: hide, in announce
	FileBM   uint16 = 0x0040 // FILE_BM: BM only, in announce
)

// ReadArticleFile reads an article file safely according to PTT BBS rules:
// (1) Sparse file detection: if allocated blocks * 512 is less than claimed size, cap read to allocated size.
// (2) NUL truncation: BBS text files never contain NUL bytes; if NUL is found, discard everything from NUL onward.
// (3) Absolute limit: cap maximum read size to 32MB (EDIT_SIZE_LIMIT).
func ReadArticleFile(path string) ([]byte, error) {
	fi, err := os.Stat(path)
	if err != nil {
		return nil, err
	}

	maxRead := fi.Size()
	// Rule 1: Sparse file detection
	if stat, ok := fi.Sys().(*syscall.Stat_t); ok {
		allocated := int64(stat.Blocks) * 512
		if allocated < maxRead {
			// If allocated == 0 and size <= 4096, it could be ext4 inline data
			if !(allocated == 0 && maxRead <= 4096) {
				maxRead = allocated
			}
		}
	}

	// Rule 3: 32MB absolute limit
	if maxRead > MaxArticleFileSize {
		maxRead = MaxArticleFileSize
	}

	if maxRead <= 0 {
		return []byte{}, nil
	}

	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	rawBytes, err := io.ReadAll(io.LimitReader(f, maxRead))
	if err != nil {
		return nil, err
	}

	// Rule 2: Truncate at first NUL byte
	if idx := bytes.IndexByte(rawBytes, 0); idx >= 0 {
		rawBytes = rawBytes[:idx]
	}

	return rawBytes, nil
}

// FileHeader represents the 128-byte BBS fileheader_t
type FileHeader struct {
	Filename  string
	Modified  uint32
	Score     int16
	Downvote  uint8
	Upvote    uint8
	Recommend int8
	Owner     string
	Date      string
	Title     string
	Filemode  uint16
	Multi     uint32 // fileheader_t.multi; see model.UnpackMulti
	Comments  uint16
}

func cString(b []byte) string {
	idx := bytes.IndexByte(b, 0)
	if idx >= 0 {
		b = b[:idx]
	}
	return string(b)
}

func UnpackFileHeader(buf []byte) (*FileHeader, error) {
	if len(buf) < FileHeaderSize {
		return nil, io.ErrUnexpectedEOF
	}
	fn := cString(buf[0:22]) // FNSIZE; 22..28 are reserved
	mod := binary.LittleEndian.Uint32(buf[28:32])
	score := int16(binary.LittleEndian.Uint16(buf[32:34]))
	downvote := buf[32]
	upvote := buf[33]
	rec := int8(score)
	owner := cString(buf[34:48])
	date := cString(buf[48:54])
	title := cString(buf[54:119])
	multi := binary.LittleEndian.Uint32(buf[120:124])
	fmode := binary.LittleEndian.Uint16(buf[124:126])
	comments := binary.LittleEndian.Uint16(buf[126:128])

	return &FileHeader{
		Filename:  fn,
		Modified:  mod,
		Score:     score,
		Downvote:  downvote,
		Upvote:    upvote,
		Recommend: rec,
		Owner:     owner,
		Date:      date,
		Title:     title,
		Filemode:  fmode,
		Multi:     multi,
		Comments:  comments,
	}, nil
}

const (
	ipv4Pattern = `\d{1,3}\.\d{1,3}\.\d{1,3}\.(?:\d{1,3}|\*)`
	ipv6Pattern = `[0-9a-fA-F]{1,4}(?::[0-9a-fA-F]{1,4})*::(?:[0-9a-fA-F]{1,4}(?::[0-9a-fA-F]{1,4})*)?|::(?:[0-9a-fA-F]{1,4}(?::[0-9a-fA-F]{1,4})*)?|(?:[0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}`
	ipPattern   = `(?:` + ipv4Pattern + `|` + ipv6Pattern + `)`
)

// isValidIP checks if a candidate string is a valid IPv4 or IPv6 address.
func isValidIP(s string) bool {
	if strings.HasSuffix(s, ".*") {
		prefix := strings.TrimSuffix(s, ".*")
		parts := strings.Split(prefix, ".")
		if len(parts) == 3 {
			return net.ParseIP(prefix+".1") != nil
		}
	}
	return net.ParseIP(s) != nil
}

// Regex patterns
var (
	pushRegex = regexp.MustCompile(`^([推噓→★])\s*([A-Za-z0-9_]+)\s*:(.*)$`)

	// OLDRECOMMEND (PTT2 / older BBS):
	//   snprintf(buf, szbuf, ANSI_COLOR(1;31) "→  " ANSI_COLOR(33) "%s" ANSI_RESET
	//            ANSI_COLOR(33) ":%s%*s" ANSI_RESET "推%s\n", myid, msg, pad, "", tail);
	oldRecommendTailRegex = regexp.MustCompile(
		`\s+推(?:(?:\s*(` + ipPattern + `)\s+)|(?:\s{1,3}))(\d{2}/\d{2}(?:\s+\d{2}:\d{2}(?::\d{2})?)?)\s*$`,
	)

	// Non-OLD (PTT1 standard):
	//   snprintf(buf, szbuf, "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33)
	//            ":%s%*s" ANSI_RESET "%s\n", ctype_attr2[type], ctype[type], myid, msg, pad, "", tail);
	nonOldTailRegex = regexp.MustCompile(
		`\s+(?:(` + ipPattern + `)\s+)?(\d{2}/\d{2}(?:\s+\d{2}:\d{2}(?::\d{2})?)?)\s*$`,
	)

	crosspostLogRegex = regexp.MustCompile(`^※\s*([A-Za-z0-9_]+)\s*:\s*轉錄至看板\s+([A-Za-z0-9_\-\.]+)(.*)$`)
	// The header of a copy: "※ [本文轉錄自 <board> 看板 #<AID> ]" (older
	// ones have no AID, or the file name); also "某隱形看板" or "<user> 信箱".
	copyHeaderRegex        = regexp.MustCompile(`^※\s*\[本文轉錄自`)
	crosspostFromRegex     = regexp.MustCompile(`^※\s*\[本文轉錄自\s+([A-Za-z0-9_\-\.]+)\s+看板\s*(?:#([0-9A-Za-z_\-]{8})|([MG]\.\d+\.A(?:\.[0-9A-Fa-f]+)?))?\s*\]`)
	postFilenameCtimeRegex = regexp.MustCompile(`^[A-Z]\.(\d+)\.`)
	editMarkRegex          = regexp.MustCompile(`^※\s*編輯:`)
	copierRegex            = regexp.MustCompile(`^※\s*轉錄者:`)

	// The comments nest.svc writes into the articles (FormatCommentBlock in
	// storage): a floor is "RS[*][-|!]author|[~]No|MM/DD/YYYY HH:MM|[ip]|text",
	// its other lines "RS[*]<spaces>|text"; the OP mark "*" is ignored here.
	// The author is a userid, or the name of a non-user owner (e.g.
	// "[系統安全局]") for the OP's text among the comments.
	modernHeadRegex = regexp.MustCompile(`^\x1e\*?([-!]?)([^|\s][^|]*)\|(~?)(\d+)\|(\d{2}/\d{2}/\d{4} \d{2}:\d{2})\|([^|]*)\|(.*)$`)
	modernMoreRegex = regexp.MustCompile(`^\x1e\*? +\|(.*)$`)
)

// spamCommentsRemovedNote is the line mbbsd leaves among the comments where
// it removed spam ones; imports drop it.
const spamCommentsRemovedNote = "[部份違規或廣告推文已被系統自動刪除]"

// isCommentLine tells whether a line begins the comments: a push line of
// mbbsd, or a floor written by nest.svc.
func isCommentLine(clean string) bool {
	return isPushLine(clean) || modernHeadRegex.MatchString(clean)
}

// isPushLine checks if a line is a genuine push comment line.
func isPushLine(clean string) bool {
	if !pushRegex.MatchString(clean) {
		return false
	}
	return nonOldTailRegex.MatchString(clean) || oldRecommendTailRegex.MatchString(clean)
}

// ParsedComment represents an extracted comment
type ParsedComment struct {
	CreatorName string
	Content     string
	IP          string
	CTime       int64
	LastCTime   int64
	CommentType string // "推", "噓", "→", "white"
	LegacyType  uint8  // 0: OLD_RECOMMEND, 1: 推, 2: 噓, 3: 箭頭
	// A floor nest.svc wrote (modernHeadRegex): never merged with others,
	// as it is one floor already; it may be deleted (by its creator) or
	// removed (with Reason), or edited.
	Floor   bool
	Deleted bool
	Removed bool
	Reason  string
	Edited  bool
}

// ParsedCrosspost represents an extracted crosspost record
type ParsedCrosspost struct {
	SrcBoard    string
	SrcFile     string
	TargetBoard string
	TargetFile  string
	CreatorName string
	CTime       int64
}

// ParsedArticle contains extracted post body, comments, and crossposts
type ParsedArticle struct {
	BodyContent string
	Comments    []*ParsedComment
	Crossposts  []*ParsedCrosspost // of this post (its cplog lines)
	// Where this copy was copied from (its header), if it names the post;
	// the copy is the target.
	CopiedFrom *ParsedCrosspost
}

// cplogSlack is how much later than the copy a cplog line in it may be
// dated and still be taken as copied from the original.
const cplogSlack = 60

// cplogBeforeCopy tells whether a cplog line dated dateStr ("MM/DD
// [HH:MM]") in a copy created at ctime is older than the copy, taking the
// year that puts the date nearest to ctime.
func cplogBeforeCopy(dateStr string, ctime int64) bool {
	parts := strings.Fields(dateStr)
	if len(parts) == 0 {
		return true
	}
	var month, day, hour, min int
	if _, err := fmt.Sscanf(parts[0], "%d/%d", &month, &day); err != nil {
		return true
	}
	if len(parts) > 1 {
		fmt.Sscanf(parts[1], "%d:%d", &hour, &min)
	}
	base := time.Unix(ctime, 0)
	best := int64(0)
	for y := base.Year() - 1; y <= base.Year()+1; y++ {
		t := time.Date(y, time.Month(month), day, hour, min, 0, 0, time.Local).Unix()
		if best == 0 || absInt64(t-ctime) < absInt64(best-ctime) {
			best = t
		}
	}
	return best <= ctime+cplogSlack
}

// clampCrosspostTime keeps a crosspost time that RollDate put in the
// future in the past: a year earlier if that is not before the post, else
// the time of the post.
func clampCrosspostTime(ts, postCtime, now int64) int64 {
	if ts <= now {
		return ts
	}
	if back := time.Unix(ts, 0).AddDate(-1, 0, 0).Unix(); back >= postCtime {
		return back
	}
	return postCtime
}

func absInt64(v int64) int64 {
	if v < 0 {
		return -v
	}
	return v
}

// RollDate performs chronological date inference for comment timestamps
func RollDate(dateStr string, currYear, prevMonth, prevDay int, lastTs int64) (int64, int, int, int) {
	parts := strings.Fields(dateStr)
	if len(parts) == 0 {
		return lastTs, currYear, prevMonth, prevDay
	}

	dateParts := strings.Split(parts[0], "/")
	if len(dateParts) < 2 {
		return lastTs, currYear, prevMonth, prevDay
	}

	month, errM := strconv.Atoi(dateParts[0])
	day, errD := strconv.Atoi(dateParts[1])
	if errM != nil || errD != nil {
		return lastTs, currYear, prevMonth, prevDay
	}

	hour, min, sec := 0, 0, 0
	if len(parts) > 1 {
		timeParts := strings.Split(parts[1], ":")
		if len(timeParts) >= 2 {
			hour, _ = strconv.Atoi(timeParts[0])
			min, _ = strconv.Atoi(timeParts[1])
			if len(timeParts) >= 3 {
				sec, _ = strconv.Atoi(timeParts[2])
			}
		}
	}

	// Chronological year rollover
	if month < prevMonth || (month == prevMonth && day < prevDay) {
		currYear++
	}

	t := time.Date(currYear, time.Month(month), day, hour, min, sec, 0, time.Local)
	ts := t.Unix()
	if ts < lastTs {
		ts = lastTs + 1
	}

	return ts, currYear, month, day
}

// MergeConsecutiveComments merges consecutive comments from the same author
// if they occur within 3 minutes (180 seconds), consistent with mbbsd merge comment.
func MergeConsecutiveComments(comments []*ParsedComment) []*ParsedComment {
	if len(comments) == 0 {
		return nil
	}
	var merged []*ParsedComment
	for _, c := range comments {
		if strings.TrimSpace(c.Content) == "" && !c.Floor {
			continue
		}
		if len(merged) > 0 && !c.Floor && !merged[len(merged)-1].Floor &&
			strings.EqualFold(merged[len(merged)-1].CreatorName, c.CreatorName) {
			prev := merged[len(merged)-1]
			lastTime := prev.LastCTime
			if lastTime == 0 {
				lastTime = prev.CTime
			}
			diff := c.CTime - lastTime
			if diff < 0 {
				diff = -diff
			}
			if diff <= 180 {
				canMerge := false
				if c.LegacyType == model.LegacyTypeArrow {
					// 箭頭 (3) merges into 推 (1), 噓 (2), or 箭頭 (3): 一推多箭=一推, 一噓多箭=一噓, 多箭=一箭
					canMerge = true
				} else if prev.LegacyType == model.LegacyTypeOldRecommend && c.LegacyType == model.LegacyTypeOldRecommend {
					// PTT2 OLD_RECOMMEND (0) merges into OLD_RECOMMEND (0)
					canMerge = true
				}

				if canMerge {
					prevLines := model.CountCommentLines(prev.Content)
					curLines := model.CountCommentLines(c.Content)
					if prevLines+curLines > model.MaxCommentLines {
						canMerge = false
					}
				}

				if canMerge {
					prev.Content += "\n" + c.Content
					if prev.IP == "" && c.IP != "" {
						prev.IP = c.IP
					}
					prev.LastCTime = c.CTime
					continue
				}
			}
		}
		merged = append(merged, c)
	}
	return merged
}

// ParseArticleText parses raw article bytes into body, comments, and crossposts
func ParseArticleText(rawBytes []byte, defaultAuthor string, postCtime int64, community, postFile string, isBig5 bool, noMerge ...bool) (*ParsedArticle, error) {
	var fullText string
	if isBig5 && !utf8.Valid(rawBytes) {
		fullText = big5uao.DecodeSGR66(rawBytes)
	} else {
		fullText = string(rawBytes)
	}

	rawLines := strings.Split(fullText, "\n")

	// 1. Locate the LAST end mark line ('--')
	lastEndIdx := -1
	for i := len(rawLines) - 1; i >= 0; i-- {
		clean := strings.TrimRight(model.StripANSI(rawLines[i]), "\r ")
		if clean == "--" {
			lastEndIdx = i
			break
		}
	}

	var bodyLines []string
	var candidateLines []string
	if lastEndIdx != -1 {
		bodyLines = append(bodyLines, rawLines[:lastEndIdx+1]...)
		candidateLines = append(candidateLines, rawLines[lastEndIdx+1:]...)
	} else {
		// Fallback: scan for first system signature or push line
		splitIdx := len(rawLines)
		for i, l := range rawLines {
			clean := strings.TrimSpace(model.StripANSI(l))
			if strings.HasPrefix(clean, "※ 發信站:") || isCommentLine(clean) {
				splitIdx = i
				break
			}
		}
		bodyLines = append(bodyLines, rawLines[:splitIdx]...)
		candidateLines = append(candidateLines, rawLines[splitIdx:]...)
	}

	var crossposts []*ParsedCrosspost
	var copiedFrom *ParsedCrosspost

	// The first copy header names the post this one was copied from;
	// those after it come with the copied text.
	isCopy := false
	for _, bl := range bodyLines {
		clean := strings.TrimSpace(model.StripANSI(bl))
		if !copyHeaderRegex.MatchString(clean) {
			continue
		}
		isCopy = true
		m := crosspostFromRegex.FindStringSubmatch(clean)
		srcFile := ""
		if m != nil {
			srcFile = m[3]
			if m[2] != "" {
				srcFile, _ = model.AIDToPostFile(m[2])
			}
		}
		if srcFile != "" {
			copiedFrom = &ParsedCrosspost{
				SrcBoard:    m[1],
				SrcFile:     srcFile,
				TargetBoard: community,
				TargetFile:  postFile,
				CreatorName: defaultAuthor,
				CTime:       postCtime,
			}
		}
		break
	}

	// A copy since 2011 ends with "※ 轉錄者:" (addforwardsignature) and no
	// "--", so the original's comments come after the last "--"; what is
	// before that line is quoted text, and only the comments after it are
	// the copy's own.
	if isCopy {
		for i := len(candidateLines) - 1; i >= 0; i-- {
			if copierRegex.MatchString(strings.TrimSpace(model.StripANSI(candidateLines[i]))) {
				bodyLines = append(bodyLines, candidateLines[:i]...)
				candidateLines = candidateLines[i:]
				break
			}
		}
	}

	// Separate system signatures and crosspost logs from comments
	// Find the first system origin line in candidateLines (e.g. ※ 發信站:, ※ 轉錄, ◆ From:, ◆ 來源:)
	firstSysIdx := -1
	for i, l := range candidateLines {
		clean := strings.TrimSpace(model.StripANSI(l))
		if (strings.HasPrefix(clean, "※") || strings.HasPrefix(clean, "◆")) && !strings.HasPrefix(clean, "※ 編輯:") {
			firstSysIdx = i
			break
		}
	}

	// Find the first genuine push comment line in candidateLines
	firstPushIdx := -1
	searchStart := 0
	if firstSysIdx != -1 {
		searchStart = firstSysIdx
	}
	for i := searchStart; i < len(candidateLines); i++ {
		clean := strings.TrimSpace(model.StripANSI(candidateLines[i]))
		if isCommentLine(clean) {
			firstPushIdx = i
			break
		}
	}

	var commentRawLines []string
	baseDt := time.Unix(postCtime, 0)
	currYear := baseDt.Year()
	prevMonth := int(baseDt.Month())
	prevDay := baseDt.Day()
	lastTs := postCtime

	for i, l := range candidateLines {
		clean := strings.TrimSpace(model.StripANSI(l))

		// Crosspost log: ※ operator:轉錄至看板 target_board [date]
		mCp := crosspostLogRegex.FindStringSubmatch(clean)
		if len(mCp) >= 3 {
			op := mCp[1]
			targetBrd := mCp[2]
			cpTime := postCtime
			tail := ""
			if len(mCp) >= 4 {
				tail = mCp[3]
			}
			tm := nonOldTailRegex.FindStringSubmatch(tail)
			hasDate := len(tm) >= 3 && tm[2] != ""
			// mbbsd copies the whole file, with the cplog lines of the
			// original; those are crossposts of the original.
			if isCopy && (!hasDate || cplogBeforeCopy(tm[2], postCtime)) {
				bodyLines = append(bodyLines, l)
				continue
			}
			if hasDate {
				cpTime, _, _, _ = RollDate(tm[2], currYear, prevMonth, prevDay, postCtime)
				cpTime = clampCrosspostTime(cpTime, postCtime, time.Now().Unix())
			}
			crossposts = append(crossposts, &ParsedCrosspost{
				SrcBoard:    community,
				SrcFile:     postFile,
				TargetBoard: targetBrd,
				TargetFile:  "-",
				CreatorName: op,
				CTime:       cpTime,
			})
			continue
		}

		// System info / signatures: any line starting with '※' or '◆'
		if strings.HasPrefix(clean, "※") || strings.HasPrefix(clean, "◆") {
			bodyLines = append(bodyLines, l)
			continue
		}

		// Before the first real push comment, all lines belong to post body & signature!
		if firstPushIdx == -1 || i < firstPushIdx {
			bodyLines = append(bodyLines, l)
			continue
		}

		// From firstPushIdx onward, we are in the comment section:
		// ignore empty lines between comments
		if clean == "" {
			continue
		}

		commentRawLines = append(commentRawLines, l)
	}

	var comments []*ParsedComment
	var floor *ParsedComment // the floor of nest.svc being read, for its other lines

	for _, l := range commentRawLines {
		cleanTrimmed := strings.TrimSpace(model.StripANSI(l))
		if cleanTrimmed == "" {
			continue
		}

		// A floor written by nest.svc, then its other lines.
		if m := modernHeadRegex.FindStringSubmatch(cleanTrimmed); m != nil {
			ts := lastTs
			if t, err := time.ParseInLocation("01/02/2006 15:04", m[5], time.Local); err == nil {
				ts = t.Unix()
			}
			floor = &ParsedComment{
				CreatorName: m[2],
				Content:     m[7],
				CTime:       ts,
				LastCTime:   ts,
				CommentType: "→",
				LegacyType:  model.LegacyTypeArrow,
				Floor:       true,
				Edited:      m[3] == "~",
			}
			if isValidIP(m[6]) {
				floor.IP = m[6]
			}
			switch m[1] {
			case "-":
				floor.Content = ""
				floor.Deleted = true
			case "!":
				// FormatCommentBlock: 留言已移除 [(reason)]
				floor.Content = ""
				floor.Removed = true
				r := strings.TrimSpace(strings.TrimPrefix(m[7], "留言已移除"))
				floor.Reason = strings.TrimSuffix(strings.TrimPrefix(r, "("), ")")
			}
			comments = append(comments, floor)
			if ts > lastTs {
				lastTs = ts
				t := time.Unix(ts, 0)
				currYear, prevMonth, prevDay = t.Year(), int(t.Month()), t.Day()
			}
			continue
		}
		if floor != nil && !floor.Deleted && !floor.Removed {
			if m := modernMoreRegex.FindStringSubmatch(cleanTrimmed); m != nil {
				floor.Content += "\n" + m[1]
				continue
			}
		}
		floor = nil

		// Safeguard: any system information line starting with '※' or '◆'
		if strings.HasPrefix(cleanTrimmed, "※") || strings.HasPrefix(cleanTrimmed, "◆") {
			bodyLines = append(bodyLines, l)
			continue
		}

		mPush := pushRegex.FindStringSubmatch(cleanTrimmed)
		if len(mPush) >= 4 {
			frontTag := mPush[1]
			author := mPush[2]
			rawContent := mPush[3]

			var tag, content, ip, dateStr string
			var legacyType uint8 = model.LegacyTypeArrow
			if frontTag == "→" {
				loc := oldRecommendTailRegex.FindStringSubmatchIndex(rawContent)
				if loc != nil {
					content = strings.TrimSpace(rawContent[:loc[0]])
					tag = "推"
					legacyType = model.LegacyTypeOldRecommend
					if loc[2] != -1 && loc[3] != -1 {
						cand := rawContent[loc[2]:loc[3]]
						if isValidIP(cand) {
							ip = cand
						}
					}
					if loc[4] != -1 && loc[5] != -1 {
						dateStr = rawContent[loc[4]:loc[5]]
					}
				}
			}

			if tag == "" {
				loc := nonOldTailRegex.FindStringSubmatchIndex(rawContent)
				if loc != nil {
					content = strings.TrimSpace(rawContent[:loc[0]])
					tag = frontTag
					if loc[2] != -1 && loc[3] != -1 {
						cand := rawContent[loc[2]:loc[3]]
						if isValidIP(cand) {
							ip = cand
						}
					}
					if loc[4] != -1 && loc[5] != -1 {
						dateStr = rawContent[loc[4]:loc[5]]
					}
				} else {
					content = strings.TrimSpace(rawContent)
					tag = frontTag
				}

				if frontTag == "推" {
					legacyType = model.LegacyTypePush
				} else if frontTag == "噓" {
					legacyType = model.LegacyTypeBoo
				} else {
					legacyType = model.LegacyTypeArrow
				}
			}

			commentTs := lastTs
			if dateStr != "" {
				commentTs, currYear, prevMonth, prevDay = RollDate(
					dateStr, currYear, prevMonth, prevDay, lastTs,
				)
			}
			if commentTs < lastTs {
				commentTs = lastTs + 1
			}
			lastTs = commentTs

			comments = append(comments, &ParsedComment{
				CreatorName: author,
				Content:     content,
				IP:          ip,
				CTime:       commentTs,
				LastCTime:   commentTs,
				CommentType: tag,
				LegacyType:  legacyType,
			})
			continue
		}

		// mbbsd's note where it dropped spam comments; not the OP's words.
		if cleanTrimmed == spamCommentsRemovedNote {
			continue
		}

		// Interspersed white text
		comments = append(comments, &ParsedComment{
			CreatorName: defaultAuthor,
			Content:     cleanTrimmed,
			IP:          "",
			CTime:       lastTs,
			LastCTime:   lastTs,
			CommentType: "white",
			LegacyType:  model.LegacyTypeArrow,
		})
	}

	var mergedComments []*ParsedComment
	if len(noMerge) > 0 && noMerge[0] {
		mergedComments = comments
	} else {
		mergedComments = MergeConsecutiveComments(comments)
	}

	// Deduplicate edit marks: keep ONLY the last "※ 編輯:" line, discard previous ones
	lastEditIdx := -1
	for i := len(bodyLines) - 1; i >= 0; i-- {
		clean := strings.TrimSpace(model.StripANSI(bodyLines[i]))
		if editMarkRegex.MatchString(clean) {
			lastEditIdx = i
			break
		}
	}
	if lastEditIdx != -1 {
		var filteredBody []string
		for i, line := range bodyLines {
			clean := strings.TrimSpace(model.StripANSI(line))
			if editMarkRegex.MatchString(clean) && i != lastEditIdx {
				continue
			}
			filteredBody = append(filteredBody, line)
		}
		bodyLines = filteredBody
	}

	bodyContent := strings.Join(bodyLines, "\n")
	if len(bodyContent) > 0 && !strings.HasSuffix(bodyContent, "\n") {
		bodyContent += "\n"
	}

	return &ParsedArticle{
		BodyContent: bodyContent,
		Comments:    mergedComments,
		Crossposts:  crossposts,
		CopiedFrom:  copiedFrom,
	}, nil
}

// LocateBoardDir locates a BBS board directory under $BBSHOME/boards
func LocateBoardDir(bbsHome, board string) (string, error) {
	candidates := []string{
		filepath.Join(bbsHome, "boards", board),
	}
	if len(board) > 0 {
		c := string(board[0])
		candidates = append([]string{
			filepath.Join(bbsHome, "boards", c, board),
			filepath.Join(bbsHome, "boards", strings.ToUpper(c), board),
			filepath.Join(bbsHome, "boards", strings.ToLower(c), board),
		}, candidates...)
	}

	for _, cand := range candidates {
		dirFile := filepath.Join(cand, ".DIR")
		if fi, err := os.Stat(dirFile); err == nil && !fi.IsDir() {
			return cand, nil
		}
	}
	// A board whose .DIR is lost (e.g. to rebuild it with sync-dir).
	for _, cand := range candidates {
		if fi, err := os.Stat(cand); err == nil && fi.IsDir() {
			return cand, nil
		}
	}
	return "", fmt.Errorf("board directory not found for '%s' under %s/boards", board, bbsHome)
}

// ImportStats tracks statistics for a board migration
type ImportStats struct {
	TotalRecords     int
	ValidPosts       int
	PostsImported    int
	CommentsImported int
	CrosspostsLogged int
	DeletedImported  int // safe-deleted .DIR records imported as deleted posts
	DeletedMarked    int // existing posts marked deleted as .DIR has them safe-deleted
	Errors           int
	ErrorDetails     []string
	Elapsed          time.Duration
}

// ImportBoardOptions specifies configuration for board migration
type ImportBoardOptions struct {
	Ctx          context.Context
	BBSHome      string
	Board        string
	RenderTarget string
	Overwrite    bool
	Limit        int
	Offset       int
	IsBig5       bool
	NoMerge      bool
	CommentdDB   string
	LegacyFormat bool
	DryRun       bool
	Workers      int
	// Users resolves the person IDs of the authors in .DIR and in the articles.
	// Without it, they are imported with unknown (0) person IDs.
	Users      UserResolver
	ProgressFn func(current, total int)
}

// QueryCommentDB queries legacy COMMENTD LevelDB database for authoritative (ctime, ip)
func QueryCommentDB(db *leveldb.DB, board, filename string, seq uint32) (int64, string, error) {
	if db == nil {
		return 0, "", nil
	}
	key := fmt.Sprintf("%s/%s#%08d", board, filename, seq+1)
	val, err := db.Get([]byte(key), nil)
	if err != nil {
		if err == leveldb.ErrNotFound {
			return 0, "", nil
		}
		return 0, "", err
	}
	if len(val) < 8 {
		return 0, "", nil
	}

	ctime := int64(binary.LittleEndian.Uint32(val[0:4]))
	if ctime == 0 {
		return 0, "", nil
	}
	ip := ""
	if val[4] != 0 || val[5] != 0 || val[6] != 0 || val[7] != 0 {
		ip = fmt.Sprintf("%d.%d.%d.%d", val[4], val[5], val[6], val[7])
	}
	return ctime, ip, nil
}

// ImportBoard performs in-process, high-speed import of an entire legacy board (.DIR and article files) into storage.Storage
func ImportBoard(engine storage.Storage, opts ImportBoardOptions) (*ImportStats, error) {
	boardDir, err := LocateBoardDir(opts.BBSHome, opts.Board)
	if err != nil {
		return nil, err
	}

	dirPath := filepath.Join(boardDir, ".DIR")
	fi, err := os.Stat(dirPath)
	if err != nil {
		return nil, fmt.Errorf(".DIR not found: %w", err)
	}

	totalRecords := int(fi.Size() / FileHeaderSize)
	if opts.Limit > 0 && opts.Limit < totalRecords {
		totalRecords = opts.Limit
	}

	// If overwrite is requested, clear the existing community first (takes ~0.3s)
	if opts.Overwrite && !opts.DryRun {
		_, _, _ = engine.ClearCommunity(opts.Board)
	}
	// Mirror the board header first, so the community has its fields.
	if !opts.DryRun {
		if err := importBoardHeader(engine, opts.BBSHome, opts.Board, opts.Users, opts.IsBig5); err != nil {
			return nil, fmt.Errorf("import board header failed: %w", err)
		}
	}

	dirFile, err := os.Open(dirPath)
	if err != nil {
		return nil, fmt.Errorf("open .DIR failed: %w", err)
	}
	defer dirFile.Close()

	targetDir := opts.RenderTarget
	if targetDir == "" || targetDir == "-" || targetDir == "none" || targetDir == "no" {
		targetDir = ""
	} else if targetDir == "boards" {
		targetDir = boardDir
	} else if targetDir == "cache" {
		targetDir = filepath.Join(opts.BBSHome, "cache", opts.Board)
	}

	var commentDB *leveldb.DB
	if opts.CommentdDB != "" && opts.CommentdDB != "-" {
		cdb, err := leveldb.OpenFile(opts.CommentdDB, &opt.Options{
			ReadOnly:       true,
			ErrorIfMissing: true,
		})
		if err != nil {
			return nil, fmt.Errorf("open commentd LevelDB (%s) failed: %w", opts.CommentdDB, err)
		}
		defer cdb.Close()
		commentDB = cdb
	}

	startTime := time.Now()
	stats := &ImportStats{
		TotalRecords: totalRecords,
	}

	ctx := opts.Ctx
	if ctx == nil {
		ctx = context.Background()
	}

	users := NewCachedResolver(opts.Users)
	numWorkers := opts.Workers
	if numWorkers <= 0 {
		numWorkers = 8
	}
	type parseJob struct {
		seq int
		*recordJob
	}
	type parseResult struct {
		seq      int
		filename string
		data     *storage.ImportedPostData
		marked   bool // an existing post was marked deleted
		err      error
	}

	jobs := make(chan *parseJob, 4000)
	results := make(chan *parseResult, 4000)

	var workerWg sync.WaitGroup
	for w := 0; w < numWorkers; w++ {
		workerWg.Add(1)
		go func() {
			defer workerWg.Done()
			for {
				select {
				case <-ctx.Done():
					return
				case job, ok := <-jobs:
					if !ok {
						return
					}

					func() {
						defer func() {
							if r := recover(); r != nil {
								select {
								case <-ctx.Done():
								case results <- &parseResult{
									seq:      job.seq,
									filename: job.fhdr.Filename,
									err:      fmt.Errorf("panic parsing %s: %v", job.fhdr.Filename, r),
								}:
								}
							}
						}()

						data, marked, err := buildRecord(engine, opts, users, commentDB, job.recordJob)
						select {
						case <-ctx.Done():
						case results <- &parseResult{seq: job.seq, filename: job.fhdr.Filename, data: data, marked: marked, err: err}:
						}
					}()
				}
			}
		}()
	}

	// Producer: reads .DIR sequentially and sends jobs to worker pool
	go func() {
		defer close(jobs)
		buf := make([]byte, FileHeaderSize)
		idx := 0
		validCount := 0
		seenFiles := make(map[string]bool)

		for {
			select {
			case <-ctx.Done():
				return
			default:
			}

			_, err := io.ReadFull(dirFile, buf)
			if err == io.EOF || err == io.ErrUnexpectedEOF {
				break
			}
			idx++

			if opts.Offset > 0 && idx <= opts.Offset {
				continue
			}
			if opts.Limit > 0 && validCount >= opts.Limit {
				break
			}

			job, ok := prepareRecord(buf, boardDir, false)
			if !ok {
				continue
			}
			if seenFiles[job.fhdr.Filename] {
				continue
			}
			seenFiles[job.fhdr.Filename] = true
			validCount++
			select {
			case <-ctx.Done():
				return
			case jobs <- &parseJob{seq: validCount, recordJob: job}:
			}
		}
	}()

	// Closer: closes results when all workers finish
	go func() {
		workerWg.Wait()
		close(results)
	}()

	// Consumer / Sequencer: collects results, preserves order, and commits in batches of 2000
	batchSize := 2000
	var batch []*storage.ImportedPostData
	pending := make(map[int]*storage.ImportedPostData)
	expectedSeq := 1

	flushBatch := func() error {
		if len(batch) == 0 {
			return nil
		}
		if err := ctx.Err(); err != nil {
			return err // cancelled: write nothing more
		}
		if !opts.DryRun {
			if err := engine.ImportPostBatch(batch, targetDir, opts.LegacyFormat); err != nil {
				stats.Errors += len(batch)
				batch = batch[:0]
				return err
			}
		}
		for _, item := range batch {
			stats.PostsImported++
			if item.Post.DeletedOrRemoved() {
				stats.DeletedImported++
			}
			stats.CommentsImported += len(item.Comments)
			stats.CrosspostsLogged += len(item.Crossposts)
			if item.Origin != nil {
				stats.CrosspostsLogged++
			}
		}
		batch = batch[:0]
		return nil
	}

	for {
		select {
		case <-ctx.Done():
			return stats, ctx.Err()
		case res, ok := <-results:
			if !ok {
				goto finished
			}
			stats.ValidPosts++
			if res.marked {
				stats.DeletedMarked++
			}
			if res.err != nil {
				stats.Errors++
				pending[res.seq] = nil
				if len(stats.ErrorDetails) < 20 {
					stats.ErrorDetails = append(stats.ErrorDetails, fmt.Sprintf("%s: %v", res.filename, res.err))
				}
			} else {
				pending[res.seq] = res.data
			}

			for {
				data, ok := pending[expectedSeq]
				if !ok {
					break
				}
				delete(pending, expectedSeq)
				expectedSeq++
				if data != nil {
					batch = append(batch, data)
					if len(batch) >= batchSize {
						if err := flushBatch(); err != nil {
							return stats, err
						}
					}
				}
			}

			if opts.ProgressFn != nil {
				opts.ProgressFn(stats.ValidPosts, totalRecords)
			}
		}
	}

finished:
	// Final flush
	if err := flushBatch(); err != nil {
		return stats, err
	}

	stats.Elapsed = time.Since(startTime)
	return stats, nil
}

// postModified returns the modification time of a post from its .DIR record.
func postModified(fhdr *FileHeader, ctime int64) int64 {
	if fhdr.Modified > 0 {
		return int64(fhdr.Modified)
	}
	return ctime
}

// parseDeletedTitle recovers who wrote and who deleted a post from the title
// that mbbsd gave its safe-deleted .DIR record (see DeletedTitle). A title
// in none of the standard forms was entered by the moderator who deleted the
// post, and is kept as the reason, so that DeletedTitle reproduces it.
func parseDeletedTitle(title string) (author, deletedBy, reason string) {
	switch title {
	case safeDelTitle:
		return "", "", ""
	case safeRemoveTitle:
		return "", safeDelOwner, ""
	}
	// Older mbbsd wrote safeDelTitle also for posts removed by others.
	for _, prefix := range []string{safeDelTitle, safeRemoveTitle} {
		rest, ok := strings.CutPrefix(title, prefix+" ")
		if !ok || len(rest) <= 2 {
			continue
		}
		inner := rest[1 : len(rest)-1]
		switch {
		case rest[0] == '[' && rest[len(rest)-1] == ']' && !strings.ContainsAny(inner, " []<>"):
			return inner, inner, ""
		case rest[0] == '<' && rest[len(rest)-1] == '>' && !strings.ContainsAny(inner, " []<>"):
			return inner, safeDelOwner, ""
		}
	}
	// Moderator's title, by default "(已被<moderator>移除) <author>" (or
	// "刪除" by older mbbsd).
	author, deletedBy = "", safeDelOwner
	if i := strings.LastIndex(title, "<"); i >= 0 && strings.HasSuffix(title, ">") {
		author = title[i+1 : len(title)-1]
	}
	if rest, ok := strings.CutPrefix(title, "(已被"); ok {
		for _, verb := range []string{"移除)", "刪除)"} {
			if i := strings.Index(rest, verb); i > 0 {
				deletedBy = rest[:i]
				break
			}
		}
	}
	return author, deletedBy, title
}

// resolveAuthor returns the person ID of the author of a post in .DIR. Anonymous
// posts keep the usernum of their author in multi.
func resolveAuthor(users UserResolver, fhdr *FileHeader, ctime int64) model.PersonID {
	if int(fhdr.Filemode)&model.FileAnonymous != 0 {
		return resolveAnonymous(users, int(int32(fhdr.Multi)), ctime)
	}
	return ResolvePersonID(users, fhdr.Owner, 0, ctime)
}

// resolveAnonymous returns the person behind an anonymous post, whose .DIR
// record keeps the usernum of the author (fileheader_t.multi.anon_uid). If
// the account cannot be told any more, the usernum is still kept (with
// firstlogin 0), so that .DIR can be rebuilt from nest.svc.
func resolveAnonymous(users UserResolver, usernum int, ctime int64) model.PersonID {
	if id := ResolvePersonID(users, "", usernum, ctime); id != 0 {
		return id
	}
	return model.MakePersonID(usernum, 0)
}

// moneyOf returns the money of a post in .DIR, whose multi holds something
// else for anonymous and petition posts.
func moneyOf(fhdr *FileHeader) int {
	if int(fhdr.Filemode)&(model.FileAnonymous|model.FilePetition) != 0 {
		return 0
	}
	return int(int32(fhdr.Multi))
}

// importSafeDeleted handles a record that mbbsd has safe-deleted: its
// article file is gone, so only what the record tells is kept. A post that
// nest.svc already has is marked deleted (unless it already is); other
// safe-deleted records are skipped.
func importSafeDeleted(engine storage.Storage, opts ImportBoardOptions, users UserResolver, fhdr *FileHeader) (*storage.ImportedPostData, bool, error) {
	p, err := engine.GetPostByCommunityFile(opts.Board, fhdr.Filename)
	if err != nil || p == nil {
		// Not imported: the article is gone, and RebuildSQLiteFromPebble
		// could not recount what the record has left.
		return nil, false, nil
	}
	if p.DeletedOrRemoved() || opts.DryRun {
		return nil, !p.DeletedOrRemoved(), nil
	}
	title := fhdr.Title
	if opts.IsBig5 && !utf8.Valid([]byte(title)) {
		title = big5uao.DecodeSGR66([]byte(title))
	}
	_, deletedByName, reason := parseDeletedTitle(title)
	deletedByID := p.CreatorID
	if deletedByName != "" && deletedByName != p.CreatorName {
		deletedByID = ResolvePersonID(users, deletedByName, 0, int64(fhdr.Modified))
	} else {
		deletedByName = p.CreatorName
	}
	if _, err := engine.DeletePostByCommunityFile(opts.Board, fhdr.Filename, deletedByName, deletedByID, reason); err != nil {
		return nil, false, err
	}
	return nil, true, nil
}
