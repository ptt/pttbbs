package importer

import (
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
	"unicode/utf8"

	"github.com/syndtr/goleveldb/leveldb"

	"pttbbs/big5uao"
	"pttbbs/nest/model"
	"pttbbs/nest/storage"
)

// ErrPostNotFound is returned by ImportPost for a post that is neither in
// the board's .DIR nor on disk (or that .DIR has safe-deleted).
var ErrPostNotFound = errors.New("post not found")

// ErrMissingFile is a .DIR record whose article file is gone: skipped.
var ErrMissingFile = errors.New("article file missing")

// recordJob is a .DIR record to import. fhdr.Filename is the name nest.svc
// keeps the post under (M.<stamp>, see model.CanonicalPostFile).
type recordJob struct {
	fhdr        *FileHeader
	articlePath string
	deleted     bool // safe-deleted record; fhdr.Filename is the original name
	locked      bool // locked (L.<stamp> in .DIR)
}

// prepareRecord reads a .DIR record of the board in boardDir for import.
// It returns false for records that are not posts to import: empty ones,
// other dot files, and FILE_BID (0x0020) records unless withBid.
func prepareRecord(buf []byte, boardDir string, withBid bool) (*recordJob, bool) {
	fhdr, err := UnpackFileHeader(buf)
	if err != nil || fhdr.Filename == "" || (!withBid && fhdr.Filemode&0x0020 != 0) {
		return nil, false
	}
	job := &recordJob{fhdr: fhdr}
	if orig, ok := IsSafeDeletedFilename(fhdr.Filename); ok {
		fhdr.Filename = orig
		job.deleted = true
	} else if strings.HasPrefix(fhdr.Filename, ".") {
		return nil, false
	}
	if canon := model.CanonicalPostFile(fhdr.Filename); canon != fhdr.Filename {
		job.locked = !job.deleted
		fhdr.Filename = canon
	}
	job.articlePath = filepath.Join(boardDir, fhdr.Filename)
	if job.locked {
		// mbbsd keeps the file of a locked post as X.<stamp>
		// (older locks left it as L.<stamp>).
		for _, name := range []string{model.LockedFileName(fhdr.Filename), model.LockedListName(fhdr.Filename)} {
			if _, err := os.Stat(filepath.Join(boardDir, name)); err == nil {
				job.articlePath = filepath.Join(boardDir, name)
				break
			}
		}
	}
	return job, true
}

// postCtimeOf returns the creation time of a post: the stamp in its file
// name, else the time in its record, else the time of its file.
//
// A stamp later than now comes from a clock gone wrong when the post was
// written (an NTP rollover set some hosts to 2036-02-07 06:28:16 UTC for a
// while), so the creation time of the file is taken instead: the file name
// stays, as it names the post, but the post is not dated in the future
// (where it would top every list by time).
func postCtimeOf(fhdr *FileHeader, articlePath string) int64 {
	postCtime := int64(fhdr.Modified)
	if m := postFilenameCtimeRegex.FindStringSubmatch(fhdr.Filename); len(m) >= 2 {
		if v, err := strconv.ParseInt(m[1], 10, 64); err == nil {
			postCtime = v
		}
	}
	if postCtime == 0 || postCtime > time.Now().Unix() {
		if t := fileCreationTime(articlePath); t > 0 {
			postCtime = t
		}
	}
	return postCtime
}

// fileCreationTime returns when the file at path was created, as well as
// the file system tells: its birth time where there is one (statx), unless
// its modification time is earlier, as for a file copied or restored with
// its times kept (rsync -a, tar), whose birth time is the copy. 0 if the
// file cannot be read.
func fileCreationTime(path string) int64 {
	fi, err := os.Stat(path)
	if err != nil {
		return 0
	}
	t := fi.ModTime().Unix()
	if b := fileBirthTime(path); b > 0 && b < t {
		t = b
	}
	return t
}

// decodeHeaderText converts a text field of a .DIR record (Big5-UAO on a
// Big5 site) to UTF-8, as the articles are; a lead byte left at the end by
// a cut in the middle of a character is dropped first.
func decodeHeaderText(s string, isBig5 bool) string {
	if !isBig5 || utf8.ValidString(s) {
		return s
	}
	return strings.ToValidUTF8(big5uao.DecodeSGR66(trimDanglingLead([]byte(s))), "?")
}

// trimDanglingLead drops a Big5 lead byte without its trail byte at the
// end of b, stepping over the characters as the decoder does.
func trimDanglingLead(b []byte) []byte {
	for i := 0; i < len(b); {
		switch {
		case b[i] < 0x80:
			i++
		case i+1 == len(b):
			return b[:i]
		case b[i+1] >= 0x40:
			i += 2
		default:
			i++
		}
	}
	return b
}

// decodeRecord converts the owner and title of fhdr to UTF-8 (see
// decodeHeaderText); anonymous owners may have spaces around.
func decodeRecord(fhdr *FileHeader, isBig5 bool) {
	fhdr.Owner = strings.TrimSpace(decodeHeaderText(fhdr.Owner, isBig5))
	fhdr.Title = decodeHeaderText(fhdr.Title, isBig5)
}

// buildRecord returns what to import for one .DIR record, the way
// ImportBoard imports it: title, owner, filemode, money, modified and
// locked from the record, created_at from the file name, the author's
// person ID, and the comments, votes and crossposts parsed from the
// article. For a safe-deleted record see importSafeDeleted (marked tells
// that an existing post was marked deleted instead).
func buildRecord(engine storage.Storage, opts ImportBoardOptions, users UserResolver, commentDB *leveldb.DB, job *recordJob) (data *storage.ImportedPostData, marked bool, err error) {
	fhdr := job.fhdr
	decodeRecord(fhdr, opts.IsBig5)
	// Also records that old mbbsd marked deleted but left the file of.
	if job.deleted || isDeletedTitle(fhdr.Title) {
		return importSafeDeleted(engine, opts, users, fhdr)
	}
	rawBytes, err := ReadArticleFile(job.articlePath)
	if errors.Is(err, fs.ErrNotExist) {
		return nil, false, fmt.Errorf("%w: %s", ErrMissingFile, job.articlePath)
	}
	if err != nil {
		return nil, false, err
	}
	postCtime := postCtimeOf(fhdr, job.articlePath)
	if repairRecord(fhdr, postCtime, time.Now().Unix()) {
		fhdr.Date = ""
	}
	if fhdr.Owner == "" || fhdr.Title == "" {
		// e.g. PttDigest: from the headers of the article.
		owner, title := ArticleHeader(rawBytes, opts.IsBig5)
		if fhdr.Owner == "" {
			fhdr.Owner = owner
		}
		if fhdr.Title == "" {
			fhdr.Title = title
		}
	}

	parsed, err := parseArticle(rawBytes, articleInfo{
		defaultAuthor: fhdr.Owner,
		postCtime:     postCtime,
		commentBase:   commentBase(fhdr.Date, postCtime),
		community:     opts.Board,
		postFile:      fhdr.Filename,
		isBig5:        opts.IsBig5,
		noMerge:       opts.NoMerge,
	})
	if err != nil {
		return nil, false, err
	}
	if commentDB != nil {
		for idx, pc := range parsed.Comments {
			if ts, ip, err := QueryCommentDB(commentDB, opts.Board, fhdr.Filename, uint32(idx)); err == nil && ts > 0 {
				pc.CTime = ts
				if ip != "" && pc.IP == "" {
					pc.IP = ip
				}
			}
		}
	}

	title := fhdr.Title

	comments, votes, upvotes, downvotes := ConvertParsedComments(parsed.Comments, users)

	var crossposts []*model.CrosspostRecord
	for _, cp := range parsed.Crossposts {
		if !boardExists(engine, opts.BBSHome, cp.TargetBoard) {
			continue
		}
		crossposts = append(crossposts, &model.CrosspostRecord{
			TargetCommunity: cp.TargetBoard,
			TargetPostFile:  cp.TargetFile,
			CreatorName:     cp.CreatorName,
			CreatorID:       ResolvePersonID(users, cp.CreatorName, 0, cp.CTime),
			CreatedAt:       cp.CTime,
		})
	}

	post := &model.Post{
		ParentID:    0,
		Community:   opts.Board,
		PostFile:    fhdr.Filename,
		Title:       title,
		CreatorName: fhdr.Owner,
		CreatorID:   resolveAuthor(users, fhdr, postCtime),
		CreatedAt:   postCtime,
		Modified:    postModified(fhdr, postCtime),
		Filemode:    int(fhdr.Filemode),
		Money:       moneyOf(fhdr),
		Upvotes:     upvotes,
		Downvotes:   downvotes,
		NumComments: len(comments),
		Content:     parsed.BodyContent,
		Locked:      job.locked,
		Encoding:    "utf-8",
	}
	data = &storage.ImportedPostData{
		Post:       post,
		Comments:   comments,
		Crossposts: crossposts,
		Votes:      votes,
	}
	if cf := parsed.CopiedFrom; cf != nil {
		data.Origin = &model.CrosspostOrigin{
			SourceCommunity: cf.SrcBoard,
			SourcePostFile:  cf.SrcFile,
			CreatorName:     post.CreatorName,
			CreatorID:       post.CreatorID,
			CreatedAt:       postCtime,
		}
	}
	return data, false, nil
}

// findRecord looks for the .DIR record of the post canon (an M. name) in
// boardDir, from the end of .DIR (where recent posts are). It returns nil
// if .DIR has no such record.
func findRecord(boardDir, canon string) (*recordJob, error) {
	f, err := os.Open(filepath.Join(boardDir, ".DIR"))
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil
		}
		return nil, err
	}
	defer f.Close()
	fi, err := f.Stat()
	if err != nil {
		return nil, err
	}
	const chunkRecords = 256
	n := fi.Size() / FileHeaderSize
	buf := make([]byte, chunkRecords*FileHeaderSize)
	for end := n; end > 0; {
		start := max(end-chunkRecords, 0)
		chunk := buf[:(end-start)*FileHeaderSize]
		if _, err := f.ReadAt(chunk, start*FileHeaderSize); err != nil && err != io.EOF {
			return nil, err
		}
		for i := end - start - 1; i >= 0; i-- {
			rec := chunk[i*FileHeaderSize : (i+1)*FileHeaderSize]
			name, _ := IsSafeDeletedFilename(cString(rec[fhdrOffFilename : fhdrOffFilename+fhdrLenFilename]))
			if model.CanonicalPostFile(name) != canon {
				continue
			}
			if job, ok := prepareRecord(rec, boardDir, true); ok {
				return job, nil
			}
		}
		end = start
	}
	return nil, nil
}

// headerField returns the value of a header line of an article (作者 or
// 標題), with or without the colon and the ANSI colors mbbsd gives them.
func headerField(lines []string, names ...string) string {
	for _, l := range lines {
		clean := strings.TrimSpace(model.StripANSI(l))
		for _, name := range names {
			rest, ok := strings.CutPrefix(clean, name)
			if !ok {
				continue
			}
			if r, ok := strings.CutPrefix(rest, ":"); ok {
				rest = r
			} else if !strings.HasPrefix(rest, " ") {
				continue
			}
			return strings.TrimSpace(rest)
		}
	}
	return ""
}

// ArticleHeader returns the author (the user ID only) and the title in the
// headers of an article, in UTF-8.
func ArticleHeader(raw []byte, isBig5 bool) (owner, title string) {
	head := raw
	if len(head) > 2048 {
		head = head[:2048]
	}
	text := string(head)
	if isBig5 && !utf8.Valid(head) {
		text = big5uao.DecodeSGR66(head)
	}
	lines := strings.Split(text, "\n")
	if len(lines) > 10 {
		lines = lines[:10]
	}
	owner = headerField(lines, "作者", "Author")
	if i := strings.IndexAny(owner, " ("); i >= 0 {
		owner = owner[:i]
	}
	// "標題: <title>"; the line has nothing else.
	title = headerField(lines, "標題", "Title")
	return owner, title
}

// recordFromFile makes up the record of a post that is on disk but not in
// .DIR (e.g. not yet added to it) from the headers of its article: owner,
// title, and the time of the file as modified. The file may be the M.,
// X. or L. one (the latter two of a locked post).
func recordFromFile(boardDir, canon string, isBig5 bool) (*recordJob, error) {
	for _, name := range []string{canon, model.LockedFileName(canon), model.LockedListName(canon)} {
		path := filepath.Join(boardDir, name)
		fi, err := os.Stat(path)
		if err != nil || !fi.Mode().IsRegular() {
			continue
		}
		raw, err := ReadArticleFile(path)
		if err != nil {
			return nil, err
		}
		owner, title := ArticleHeader(raw, isBig5)
		return &recordJob{
			fhdr: &FileHeader{
				Filename: canon,
				Modified: uint32(fi.ModTime().Unix()),
				Owner:    owner,
				Title:    title,
			},
			articlePath: path,
			locked:      name != canon,
		}, nil
	}
	return nil, nil
}

// ImportPost imports the post postFile (its M., L. or X. name) of board
// opts.Board the way ImportBoard imports it, from its .DIR record (or, if
// .DIR does not have it, from the headers of its article), and returns it.
// A post that nest.svc already has is returned as it is. The article file
// is left as it is (opts.RenderTarget is not used). A post that .DIR has
// safe-deleted is not imported (ErrPostNotFound).
func ImportPost(engine storage.Storage, opts ImportBoardOptions, postFile string) (*model.Post, error) {
	return importPost(engine, opts, postFile, false)
}

// ImportRestoredPost is ImportPost for a post deleted before the switch to
// nest.svc whose article has been put back (e.g. from the time capsule)
// to be restored: as its .DIR record is safe-deleted (or gone), the post
// is imported from the headers of its article.
func ImportRestoredPost(engine storage.Storage, opts ImportBoardOptions, postFile string) (*model.Post, error) {
	return importPost(engine, opts, postFile, true)
}

func importPost(engine storage.Storage, opts ImportBoardOptions, postFile string, restored bool) (*model.Post, error) {
	canon := model.CanonicalPostFile(postFile)
	if canon == "" || strings.ContainsAny(canon, "/\x00") || strings.HasPrefix(canon, ".") {
		return nil, fmt.Errorf("%w: invalid post file %q", ErrPostNotFound, postFile)
	}
	if p, err := engine.GetPostByCommunityFile(opts.Board, canon); err == nil && p != nil {
		return p, nil
	}
	boardDir, err := LocateBoardDir(opts.BBSHome, opts.Board)
	if err != nil {
		return nil, err
	}
	job, err := findRecord(boardDir, canon)
	if err != nil {
		return nil, err
	}
	if job == nil || (restored && job.deleted) {
		if job, err = recordFromFile(boardDir, canon, opts.IsBig5); err != nil {
			return nil, err
		}
	}
	if job == nil {
		return nil, fmt.Errorf("%w: %s/%s", ErrPostNotFound, opts.Board, postFile)
	}
	if job.deleted {
		return nil, fmt.Errorf("%w: %s/%s is deleted", ErrPostNotFound, opts.Board, postFile)
	}
	data, _, err := buildRecord(engine, opts, NewCachedResolver(opts.Users), nil, job)
	if errors.Is(err, ErrMissingFile) {
		return nil, fmt.Errorf("%w: %s/%s has no file", ErrPostNotFound, opts.Board, postFile)
	}
	if err != nil {
		return nil, err
	}
	if data == nil {
		return nil, fmt.Errorf("%w: %s/%s is deleted", ErrPostNotFound, opts.Board, postFile)
	}
	if opts.DryRun {
		return data.Post, nil
	}
	if err := engine.ImportPostBatch([]*storage.ImportedPostData{data}, ""); err != nil {
		return nil, err
	}
	return engine.GetPostByCommunityFile(opts.Board, canon)
}
