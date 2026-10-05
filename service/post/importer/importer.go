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

	"pttbbs/big5uao"
	"pttbbs/post/model"
	"pttbbs/post/storage"
)

// MaxArticleFileSize is the absolute upper limit for article file size,
// aligned with mbbsd/edit.c EDIT_SIZE_LIMIT (32MB).
const (
	FileHeaderSize     = 128
	MaxArticleFileSize = 32 * 1024 * 1024 // 32MB
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
	Downvote  uint8
	Upvote    uint8
	Recommend int8
	Owner     string
	Date      string
	Title     string
	Filemode  uint16
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
	fn := cString(buf[0:28])
	mod := binary.LittleEndian.Uint32(buf[28:32])
	downvote := buf[32]
	upvote := buf[33]
	rec := int8(buf[33])
	owner := cString(buf[34:48])
	date := cString(buf[48:54])
	title := cString(buf[54:119])
	fmode := binary.LittleEndian.Uint16(buf[124:126])
	comments := binary.LittleEndian.Uint16(buf[126:128])

	return &FileHeader{
		Filename:  fn,
		Modified:  mod,
		Downvote:  downvote,
		Upvote:    upvote,
		Recommend: rec,
		Owner:     owner,
		Date:      date,
		Title:     title,
		Filemode:  fmode,
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

	crosspostLogRegex      = regexp.MustCompile(`^※\s*([A-Za-z0-9_]+)\s*:\s*轉錄至看板\s+([A-Za-z0-9_\-\.]+)(.*)$`)
	crosspostFromRegex     = regexp.MustCompile(`^※\s*\[本文轉錄自\s+([A-Za-z0-9_\-\.]+)\s+看板\s*(?:([A-Za-z0-9_\-\.]+))?\]`)
	postFilenameCtimeRegex = regexp.MustCompile(`^[A-Z]\.(\d+)\.`)
	editMarkRegex          = regexp.MustCompile(`^※\s*編輯:`)
)

// isPushLine checks if a line is a genuine push comment line.
func isPushLine(clean string) bool {
	if !pushRegex.MatchString(clean) {
		return false
	}
	return nonOldTailRegex.MatchString(clean) || oldRecommendTailRegex.MatchString(clean)
}

// ParsedComment represents an extracted comment
type ParsedComment struct {
	Author      string
	Content     string
	IP          string
	CTime       int64
	CommentType string // "推", "噓", "→", "white"
	LegacyType  uint8  // 0: OLD_RECOMMEND, 1: 推, 2: 噓, 3: 箭頭
}

// ParsedCrosspost represents an extracted crosspost record
type ParsedCrosspost struct {
	SrcBoard    string
	SrcFile     string
	TargetBoard string
	TargetFile  string
	Operator    string
	CTime       int64
}

// ParsedArticle contains extracted post body, comments, and crossposts
type ParsedArticle struct {
	BodyContent string
	Comments    []*ParsedComment
	Crossposts  []*ParsedCrosspost
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
// if they occur on the same day OR within 60 seconds
func MergeConsecutiveComments(comments []*ParsedComment) []*ParsedComment {
	if len(comments) == 0 {
		return nil
	}
	var merged []*ParsedComment
	for _, c := range comments {
		if strings.TrimSpace(c.Content) == "" {
			continue
		}
		if len(merged) > 0 && strings.EqualFold(merged[len(merged)-1].Author, c.Author) {
			prev := merged[len(merged)-1]
			tPrev := time.Unix(prev.CTime, 0)
			tCurr := time.Unix(c.CTime, 0)
			sameDay := tPrev.Year() == tCurr.Year() && tPrev.Month() == tCurr.Month() && tPrev.Day() == tCurr.Day()
			diff := c.CTime - prev.CTime
			if diff < 0 {
				diff = -diff
			}
			within1Min := diff <= 60
			if sameDay || within1Min {
				canMerge := false
				if c.LegacyType == model.LegacyTypeArrow {
					// 箭頭 (3) merges into 推 (1), 噓 (2), or 箭頭 (3): 一推多箭=一推, 一噓多箭=一噓, 多箭=一箭
					canMerge = true
				} else if prev.LegacyType == model.LegacyTypeOldRecommend && c.LegacyType == model.LegacyTypeOldRecommend {
					// PTT2 OLD_RECOMMEND (0) merges into OLD_RECOMMEND (0)
					canMerge = true
				}

				if canMerge {
					prev.Content += "\n" + c.Content
					if prev.IP == "" && c.IP != "" {
						prev.IP = c.IP
					}
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
	if isBig5 {
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
			if strings.HasPrefix(clean, "※ 發信站:") || isPushLine(clean) {
				splitIdx = i
				break
			}
		}
		bodyLines = append(bodyLines, rawLines[:splitIdx]...)
		candidateLines = append(candidateLines, rawLines[splitIdx:]...)
	}

	var crossposts []*ParsedCrosspost

	// Check body for origin crosspost note
	for _, bl := range bodyLines {
		clean := strings.TrimSpace(model.StripANSI(bl))
		m := crosspostFromRegex.FindStringSubmatch(clean)
		if len(m) >= 2 {
			srcFile := ""
			if len(m) >= 3 {
				srcFile = m[2]
			}
			crossposts = append(crossposts, &ParsedCrosspost{
				SrcBoard:    m[1],
				SrcFile:     srcFile,
				TargetBoard: community,
				TargetFile:  postFile,
				Operator:    defaultAuthor,
				CTime:       postCtime,
			})
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
		if isPushLine(clean) {
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
			if len(tm) >= 3 && tm[2] != "" {
				cpTime, _, _, _ = RollDate(tm[2], currYear, prevMonth, prevDay, postCtime)
			}
			crossposts = append(crossposts, &ParsedCrosspost{
				SrcBoard:    community,
				SrcFile:     postFile,
				TargetBoard: targetBrd,
				TargetFile:  "-",
				Operator:    op,
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

	for _, l := range commentRawLines {
		cleanTrimmed := strings.TrimSpace(model.StripANSI(l))
		if cleanTrimmed == "" {
			continue
		}

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
				Author:      author,
				Content:     content,
				IP:          ip,
				CTime:       commentTs,
				CommentType: tag,
				LegacyType:  legacyType,
			})
			continue
		}

		// Interspersed white text
		comments = append(comments, &ParsedComment{
			Author:      defaultAuthor,
			Content:     cleanTrimmed,
			IP:          "",
			CTime:       lastTs,
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
	return "", fmt.Errorf("board directory not found for '%s' under %s/boards", board, bbsHome)
}

// ImportStats tracks statistics for a board migration
type ImportStats struct {
	TotalRecords     int
	ValidPosts       int
	PostsImported    int
	CommentsImported int
	CrosspostsLogged int
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
	CommentdAddr string
	LegacyFormat bool
	DryRun       bool
	Workers      int
	ProgressFn   func(current, total int)
}

// QueryCommentd queries legacy COMMENTD daemon over TCP port 5134 for authoritative (ctime, ip)
func QueryCommentd(addr, board, filename string, seq uint32) (int64, string, error) {
	if addr == "" {
		return 0, "", nil
	}
	conn, err := net.DialTimeout("tcp", addr, 100*time.Millisecond)
	if err != nil {
		return 0, "", err
	}
	defer conn.Close()
	_ = conn.SetDeadline(time.Now().Add(200 * time.Millisecond))

	payload := make([]byte, 4+14+30)
	binary.LittleEndian.PutUint32(payload[0:4], seq)
	copy(payload[4:18], board)
	copy(payload[18:48], filename)

	hdr := make([]byte, 4)
	binary.LittleEndian.PutUint16(hdr[0:2], uint16(len(payload)))
	binary.LittleEndian.PutUint16(hdr[2:4], 3) // COMMENTD_REQ_QUERY_BODY

	if _, err := conn.Write(append(hdr, payload...)); err != nil {
		return 0, "", err
	}

	resp := make([]byte, 8)
	if _, err := io.ReadFull(conn, resp); err != nil {
		return 0, "", err
	}
	ctime := binary.LittleEndian.Uint32(resp[0:4])
	if ctime == 0 {
		return 0, "", nil
	}
	ipv4 := binary.BigEndian.Uint32(resp[4:8])
	ip := fmt.Sprintf("%d.%d.%d.%d", byte(ipv4>>24), byte(ipv4>>16), byte(ipv4>>8), byte(ipv4))
	return int64(ctime), ip, nil
}

// MigrateBoard performs in-process, high-speed migration of an entire board into storage.Storage
func MigrateBoard(engine storage.Storage, opts ImportBoardOptions) (*ImportStats, error) {
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
		_, _, _ = engine.DeleteCommunity(opts.Board)
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

	startTime := time.Now()
	stats := &ImportStats{
		TotalRecords: totalRecords,
	}

	ctx := opts.Ctx
	if ctx == nil {
		ctx = context.Background()
	}

	numWorkers := opts.Workers
	if numWorkers <= 0 {
		numWorkers = 8
	}
	type parseJob struct {
		seq         int
		fhdr        *FileHeader
		articlePath string
	}
	type parseResult struct {
		seq      int
		filename string
		data     *storage.ImportedPostData
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

						rawBytes, err := ReadArticleFile(job.articlePath)
						if err != nil {
							select {
							case <-ctx.Done():
							case results <- &parseResult{seq: job.seq, filename: job.fhdr.Filename, err: err}:
							}
							return
						}

						postCtime := int64(job.fhdr.Modified)
						mFn := postFilenameCtimeRegex.FindStringSubmatch(job.fhdr.Filename)
						if len(mFn) >= 2 {
							if v, err := strconv.ParseInt(mFn[1], 10, 64); err == nil {
								postCtime = v
							}
						}
						if postCtime == 0 {
							if afi, err := os.Stat(job.articlePath); err == nil {
								postCtime = afi.ModTime().Unix()
							}
						}

						parsed, err := ParseArticleText(rawBytes, job.fhdr.Owner, postCtime, opts.Board, job.fhdr.Filename, opts.IsBig5, opts.NoMerge)
						if err != nil {
							select {
							case <-ctx.Done():
							case results <- &parseResult{seq: job.seq, filename: job.fhdr.Filename, err: err}:
							}
							return
						}

						if opts.CommentdAddr != "" {
							for idx, pc := range parsed.Comments {
								if ts, ip, err := QueryCommentd(opts.CommentdAddr, opts.Board, job.fhdr.Filename, uint32(idx)); err == nil && ts > 0 {
									pc.CTime = ts
									if ip != "" && pc.IP == "" {
										pc.IP = ip
									}
								}
							}
						}

						title := job.fhdr.Title
						if opts.IsBig5 {
							title = big5uao.DecodeSGR66([]byte(title))
						}

						upvotes := 0
						downvotes := 0
						var comments []*model.Comment
						for _, pc := range parsed.Comments {
							if pc.CommentType == "推" {
								upvotes++
							} else if pc.CommentType == "噓" {
								downvotes++
							}
							comments = append(comments, &model.Comment{
								Author:     pc.Author,
								Content:    pc.Content,
								IP:         pc.IP,
								CreatedAt:  pc.CTime,
								LegacyType: pc.LegacyType,
							})
						}

						var crossposts []*model.CrosspostRecord
						for _, cp := range parsed.Crossposts {
							crossposts = append(crossposts, &model.CrosspostRecord{
								TargetCommunity: cp.TargetBoard,
								TargetPostFile:  cp.TargetFile,
								Operator:        cp.Operator,
								CreatedAt:       cp.CTime,
							})
						}

						post := &model.Post{
							ParentID:  0,
							Community: opts.Board,
							PostFile:  job.fhdr.Filename,
							Title:     title,
							Author:    job.fhdr.Owner,
							CreatedAt: postCtime,
							Modified:  postCtime,
							Filemode:  int(job.fhdr.Filemode),
							Upvotes:   upvotes,
							Downvotes: downvotes,
							Content:   parsed.BodyContent,
							Encoding:  "utf-8",
						}

						select {
						case <-ctx.Done():
							return
						case results <- &parseResult{
							seq:      job.seq,
							filename: job.fhdr.Filename,
							data: &storage.ImportedPostData{
								Post:       post,
								Comments:   comments,
								Crossposts: crossposts,
							},
						}:
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

			fhdr, err := UnpackFileHeader(buf)
			if err != nil || fhdr.Filename == "" || strings.HasPrefix(fhdr.Filename, ".") || (fhdr.Filemode&0x0020 != 0) {
				continue
			}

			if seenFiles[fhdr.Filename] {
				continue
			}
			seenFiles[fhdr.Filename] = true

			articlePath := filepath.Join(boardDir, fhdr.Filename)
			validCount++
			select {
			case <-ctx.Done():
				return
			case jobs <- &parseJob{
				seq:         validCount,
				fhdr:        fhdr,
				articlePath: articlePath,
			}:
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
		if !opts.DryRun {
			if err := engine.ImportPostBatch(batch, targetDir, opts.LegacyFormat); err != nil {
				stats.Errors += len(batch)
				batch = batch[:0]
				return err
			}
		}
		for _, item := range batch {
			stats.PostsImported++
			stats.CommentsImported += len(item.Comments)
			stats.CrosspostsLogged += len(item.Crossposts)
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
