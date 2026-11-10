package importer

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"time"
	"unicode/utf8"

	"pttbbs/big5uao"
	"pttbbs/post/model"
	"pttbbs/post/storage"
)

// Layout of fileheader_t (include/pttstruct.h).
const (
	fhdrOffFilename = 0
	fhdrLenFilename = 28 // FNLEN
	fhdrOffModified = 28
	fhdrOffScore    = 32
	fhdrOffOwner    = 34
	fhdrLenOwner    = 14 // IDLEN + 2
	fhdrOffDate     = 48
	fhdrLenDate     = 6
	fhdrOffTitle    = 54
	fhdrLenTitle    = 65 // TTLEN + 1
	fhdrOffMulti    = 120
	fhdrOffFilemode = 124
	fhdrOffComments = 126
)

// What mbbsd writes to .DIR when it safe-deletes a post (set_safedel_fhdr
// in mbbsd/record.c, FN_SAFEDEL, STR_SAFEDEL_TITLE and STR_SAFEREMOVE_TITLE
// in include/common.h): deleted by the author, or removed by others.
const (
	safeDelPrefix   = ".d"
	safeDelTitle    = "(本文已被刪除)"
	safeRemoveTitle = "(本文已被移除)"
	safeDelOwner    = "-"
)

// internalDeleteReasons are the reasons that mbbsd and utilities send to
// post.svc which are codes rather than the title a moderator entered.
var internalDeleteReasons = map[string]bool{
	"expire":        true,
	"tag_delete":    true,
	"crosspost_del": true,
	"del_range":     true,
}

var stampFilenameRegex = regexp.MustCompile(`^[MG]\.(\d+)\.`)

// SyncDirOptions specifies what SyncDir writes.
type SyncDirOptions struct {
	BBSHome string
	Board   string
	IsBig5  bool // .DIR stores Big5-UAO titles
	DryRun  bool

	// Full rewrites every field of the records of the posts that post.svc
	// knows, instead of only score and comments. Deleted posts are written
	// the way mbbsd safe-deletes them.
	Full bool
	// Prune removes deleted entries: safe-deleted records, and records of
	// posts deleted in post.svc.
	Prune bool
	// AddMissing adds records for the (not deleted) posts that post.svc has
	// but .DIR does not, ordered by the time in their file names.
	AddMissing bool
}

// SyncDirStats reports what SyncDir did (or, in a dry run, would do).
type SyncDirStats struct {
	Records     int // records in .DIR
	Matched     int // records of posts known by post.svc
	Updated     int // records changed
	Pruned      int // records removed
	Added       int // records added
	AddedNoFile int // records added whose article file is not on the board
}

// ScoreFromVotes converts post.svc's votes to fileheader_t.score, which is
// the net score (upvotes - downvotes), clamped to fit in its int16.
func ScoreFromVotes(upvotes, downvotes int) int16 {
	v := upvotes - downvotes
	if v > math.MaxInt16 {
		return math.MaxInt16
	}
	if v < math.MinInt16 {
		return math.MinInt16
	}
	return int16(v)
}

// CommentsFromCount converts post.svc's number of comments to
// fileheader_t.comments, clamped to fit in its uint16.
func CommentsFromCount(n int) uint16 {
	if n > math.MaxUint16 {
		return math.MaxUint16
	}
	if n < 0 {
		return 0
	}
	return uint16(n)
}

// IsSafeDeletedFilename reports whether fn is the file name of a record that
// mbbsd has safe-deleted, and returns the original file name of the post.
func IsSafeDeletedFilename(fn string) (string, bool) {
	if len(fn) > len(safeDelPrefix) && fn[:len(safeDelPrefix)] == safeDelPrefix {
		return "M." + fn[len(safeDelPrefix):], true
	}
	return fn, false
}

// DeletedTitle returns the title that mbbsd gives a post when it is deleted.
func DeletedTitle(p *model.Post) string {
	if p.Removed && p.Reason != "" && !internalDeleteReasons[p.Reason] {
		// A title entered by the moderator who removed it.
		return p.Reason
	}
	title := safeDelTitle
	if p.Removed {
		title = safeRemoveTitle
	}
	if p.Filemode&model.FileAnonymous != 0 || p.CreatorName == "" || p.CreatorName == safeDelOwner {
		return title
	}
	if p.Deleted {
		return fmt.Sprintf("%s [%s]", title, p.CreatorName)
	}
	return fmt.Sprintf("%s <%s>", title, p.CreatorName)
}

// stampTime returns the creation time in the file name of a post, or 0.
func stampTime(fn string) int64 {
	m := stampFilenameRegex.FindStringSubmatch(fn)
	if len(m) < 2 {
		return 0
	}
	v, _ := strconv.ParseInt(m[1], 10, 64)
	return v
}

// fhdrDate formats the date field the way fhdr_set_date() in
// include/cmbbs.h does.
func fhdrDate(ts int64) string {
	if ts <= 740000000 {
		return "     "
	}
	t := time.Unix(ts, 0).In(time.Local)
	return fmt.Sprintf("%2d/%02d", int(t.Month())%100, t.Day()%100)
}

// truncateStorage cuts s to at most n bytes without splitting a character.
func truncateStorage(b []byte, n int, isBig5 bool) []byte {
	if len(b) <= n {
		return b
	}
	if isBig5 {
		i := 0
		for i < n {
			step := 1
			if b[i] >= 0x80 {
				step = 2
			}
			if i+step > n {
				break
			}
			i += step
		}
		return b[:i]
	}
	i := n
	for i > 0 && !utf8.RuneStart(b[i]) {
		i--
	}
	return b[:i]
}

// setString sets a NUL-terminated string field of size n, unless it already
// holds the same string (whatever follows its NUL).
func setString(rec []byte, off, n int, val []byte) {
	if len(val) > n-1 {
		val = val[:n-1]
	}
	if bytes.Equal([]byte(cString(rec[off:off+n])), val) {
		return
	}
	field := rec[off : off+n]
	for i := range field {
		field[i] = 0
	}
	copy(field, val)
}

func setCounts(rec []byte, p *model.Post) {
	binary.LittleEndian.PutUint16(rec[fhdrOffScore:], uint16(ScoreFromVotes(p.Upvotes, p.Downvotes)))
	binary.LittleEndian.PutUint16(rec[fhdrOffComments:], CommentsFromCount(p.NumComments))
}

// fillRecord writes every field of p into the fileheader_t rec. Bytes that
// post.svc does not know (pad2) are kept.
func fillRecord(rec []byte, p *model.Post, isBig5 bool) {
	filename, owner, title, modified := p.PostFile, p.CreatorName, p.Title, p.Modified
	if p.DeletedOrRemoved() {
		filename = safeDelPrefix + p.PostFile[min(2, len(p.PostFile)):]
		owner = safeDelOwner
		title = DeletedTitle(p)
		if p.DeletedAt > 0 {
			modified = p.DeletedAt
		}
	}
	var titleBytes []byte
	if isBig5 {
		titleBytes = big5uao.Encode(title)
	} else {
		titleBytes = []byte(title)
	}
	created := stampTime(p.PostFile)
	if created == 0 {
		created = p.CreatedAt
	}

	setString(rec, fhdrOffFilename, fhdrLenFilename, []byte(filename))
	binary.LittleEndian.PutUint32(rec[fhdrOffModified:], uint32(modified))
	setString(rec, fhdrOffOwner, fhdrLenOwner, []byte(owner))
	setString(rec, fhdrOffDate, fhdrLenDate, []byte(fhdrDate(created)))
	setString(rec, fhdrOffTitle, fhdrLenTitle, truncateStorage(titleBytes, fhdrLenTitle-1, isBig5))
	binary.LittleEndian.PutUint32(rec[fhdrOffMulti:], p.PackMulti())
	binary.LittleEndian.PutUint16(rec[fhdrOffFilemode:], uint16(p.Filemode))
	setCounts(rec, p)
}

type dirEntry struct {
	rec     []byte
	ts      int64
	changed bool
}

// SyncDir writes what post.svc knows about the posts of a board back to the
// board's .DIR, so that mbbsd and search.svc (which read .DIR) agree with
// post.svc.
//
// By default only score and comments are written, in place. Full rewrites
// whole records, still in place. Prune and AddMissing change the number of
// records, so .DIR is then rewritten to a new file that replaces it, keeping
// the old one as .DIR.bak-<time>; this must run while mbbsd is not using
// the board.
func SyncDir(st storage.Storage, opts SyncDirOptions) (*SyncDirStats, error) {
	boardDir, err := LocateBoardDir(opts.BBSHome, opts.Board)
	if err != nil {
		return nil, err
	}
	posts, err := st.ListPostMeta(opts.Board)
	if err != nil {
		return nil, fmt.Errorf("list posts: %w", err)
	}
	byFile := make(map[string]*model.Post, len(posts))
	for _, p := range posts {
		byFile[p.PostFile] = p
	}

	dirPath := filepath.Join(boardDir, ".DIR")
	fi, err := os.Stat(dirPath)
	if err != nil {
		return nil, fmt.Errorf("stat .DIR failed: %w", err)
	}
	data, err := os.ReadFile(dirPath)
	if err != nil {
		return nil, fmt.Errorf("read .DIR failed: %w", err)
	}
	if len(data)%FileHeaderSize != 0 {
		return nil, fmt.Errorf(".DIR size %d is not a multiple of %d", len(data), FileHeaderSize)
	}

	stats := &SyncDirStats{}
	seen := make(map[string]bool)
	var entries []dirEntry
	for off := 0; off < len(data); off += FileHeaderSize {
		stats.Records++
		old := data[off : off+FileHeaderSize]
		key, safeDeleted := IsSafeDeletedFilename(cString(old[fhdrOffFilename : fhdrOffFilename+fhdrLenFilename]))
		seen[key] = true
		p := byFile[key]

		rec := append([]byte(nil), old...)
		if p != nil {
			stats.Matched++
			if opts.Full {
				fillRecord(rec, p, opts.IsBig5)
			} else {
				setCounts(rec, p)
			}
		}
		if opts.Prune && (safeDeleted || (p != nil && p.DeletedOrRemoved())) {
			stats.Pruned++
			continue
		}
		e := dirEntry{rec: rec, ts: stampTime(key), changed: !bytes.Equal(rec, old)}
		if e.changed {
			stats.Updated++
		}
		entries = append(entries, e)
	}

	if opts.AddMissing {
		var missing []dirEntry
		var missingPosts []*model.Post
		for _, p := range posts {
			if seen[p.PostFile] || p.DeletedOrRemoved() {
				continue
			}
			missingPosts = append(missingPosts, p)
		}
		for _, p := range missingPosts {
			rec := make([]byte, FileHeaderSize)
			fillRecord(rec, p, opts.IsBig5)
			ts := stampTime(p.PostFile)
			if ts == 0 {
				ts = p.CreatedAt
			}
			missing = append(missing, dirEntry{rec: rec, ts: ts, changed: true})
			stats.Added++
			if _, err := os.Stat(filepath.Join(boardDir, p.PostFile)); err != nil {
				stats.AddedNoFile++
			}
		}
		sort.SliceStable(missing, func(i, j int) bool { return missing[i].ts < missing[j].ts })
		entries = mergeByTime(entries, missing)
	}

	if opts.DryRun || stats.Updated+stats.Pruned+stats.Added == 0 {
		return stats, nil
	}
	if !opts.Prune && !opts.AddMissing {
		return stats, writeInPlace(dirPath, entries)
	}
	return stats, replaceDir(dirPath, fi.Mode().Perm(), entries)
}

// mergeByTime inserts each of missing (sorted by time) before the first
// record of entries that is newer, keeping the order of entries.
func mergeByTime(entries, missing []dirEntry) []dirEntry {
	out := make([]dirEntry, 0, len(entries)+len(missing))
	m := 0
	for _, e := range entries {
		for e.ts > 0 && m < len(missing) && missing[m].ts < e.ts {
			out = append(out, missing[m])
			m++
		}
		out = append(out, e)
	}
	return append(out, missing[m:]...)
}

// writeInPlace writes the changed records back to where they are, which is
// possible as long as no record was removed or added.
func writeInPlace(dirPath string, entries []dirEntry) error {
	f, err := os.OpenFile(dirPath, os.O_RDWR, 0)
	if err != nil {
		return fmt.Errorf("open .DIR failed: %w", err)
	}
	defer f.Close()
	for i, e := range entries {
		if !e.changed {
			continue
		}
		if _, err := f.WriteAt(e.rec, int64(i)*FileHeaderSize); err != nil {
			return fmt.Errorf("write .DIR record %d: %w", i, err)
		}
	}
	return f.Close()
}

// replaceDir writes entries to a new file and puts it in place of .DIR,
// keeping the old .DIR as .DIR.bak-<time>.
func replaceDir(dirPath string, perm os.FileMode, entries []dirEntry) error {
	tmpPath := dirPath + ".syncdir.tmp"
	f, err := os.OpenFile(tmpPath, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, perm)
	if err != nil {
		return fmt.Errorf("create %s failed: %w", tmpPath, err)
	}
	buf := make([]byte, 0, len(entries)*FileHeaderSize)
	for _, e := range entries {
		buf = append(buf, e.rec...)
	}
	if _, err := f.Write(buf); err == nil {
		err = f.Sync()
	}
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		os.Remove(tmpPath)
		return fmt.Errorf("write %s failed: %w", tmpPath, err)
	}

	bakPath := fmt.Sprintf("%s.bak-%d", dirPath, time.Now().Unix())
	if err := os.Link(dirPath, bakPath); err != nil {
		os.Remove(tmpPath)
		return fmt.Errorf("back up .DIR to %s failed: %w", bakPath, err)
	}
	if err := os.Rename(tmpPath, dirPath); err != nil {
		os.Remove(tmpPath)
		return fmt.Errorf("replace .DIR failed: %w", err)
	}
	return nil
}
