package storage

// Export of the posts of one community straight from a backup (or any
// engine data dir nest.svc is not using), read only and without
// meta.sqlite3: posts.pebble is keyed by post ID, so the metadata records of
// every post are scanned for the community ID, and only the bodies and
// comments of the matches are read. communities.db maps the name to the ID.

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"

	"github.com/cockroachdb/pebble"

	"pttbbs/nest/model"
)

// ExportCommunityOptions are the options of ExportCommunity.
type ExportCommunityOptions struct {
	Dirs          []string // engine data dirs, see ExpandVerifyDirs
	CommunitiesDB string   // communities.db; may be "" when CommunityID is set
	Community     string   // name; ignored when CommunityID is set
	CommunityID   int64
	TargetDir     string
	LegacyFormat  bool
	Big5          bool
	// Deleted also exports deleted and removed posts, with their content.
	Deleted    bool
	ProgressFn func(dir string, scanned, exported int)
}

// ExportCommunityResult tells what ExportCommunity did.
type ExportCommunityResult struct {
	CommunityID int64
	Name        string
	Dir         string // the engine data dir that holds the community
	Scanned     int
	Exported    int
	Skipped     int // deleted or removed posts left out
}

// exportIndexName is the file, next to the posts, with one JSON line per
// exported post: its metadata (model.Post without content) and file name.
const exportIndexName = "index.jsonl"

var shardDirRE = regexp.MustCompile(`(^|/)shard-([0-9]+)(/|$)`)

// ExportCommunity writes each post of a community to TargetDir/<post file>,
// rendered with its comments as RenderCommunity does, plus index.jsonl.
func ExportCommunity(opts ExportCommunityOptions) (*ExportCommunityResult, error) {
	if len(opts.Dirs) == 0 || opts.TargetDir == "" {
		return nil, fmt.Errorf("export: no data dirs or target dir")
	}
	res := &ExportCommunityResult{CommunityID: opts.CommunityID}
	shard := -1
	if opts.CommunitiesDB != "" {
		c, err := lookupBackupCommunity(opts.CommunitiesDB, opts.Community, opts.CommunityID)
		if err != nil {
			return nil, err
		}
		res.CommunityID, res.Name, shard = c.ID, c.Name, c.Shard
	} else if opts.CommunityID == 0 {
		return nil, fmt.Errorf("export: no communities.db; give the community ID")
	}
	if res.Name == "" {
		res.Name = opts.Community
	}
	if err := os.MkdirAll(opts.TargetDir, 0755); err != nil {
		return nil, err
	}

	// All posts of a community are in one engine: try the dir of its
	// shard first, and stop at the first dir that has any.
	dirs := append([]string(nil), opts.Dirs...)
	if shard >= 0 && len(dirs) > 1 {
		for i, d := range dirs {
			if m := shardDirRE.FindStringSubmatch(filepath.ToSlash(d)); m != nil && m[2] == strconv.Itoa(shard) {
				dirs[0], dirs[i] = dirs[i], dirs[0]
				break
			}
		}
	}
	index, err := os.Create(filepath.Join(opts.TargetDir, exportIndexName))
	if err != nil {
		return nil, err
	}
	defer index.Close()
	for _, d := range dirs {
		found, err := exportFromDir(d, res, opts, index)
		if err != nil {
			return res, fmt.Errorf("%s: %w", d, err)
		}
		if found {
			res.Dir = d
			break
		}
	}
	return res, index.Close()
}

// BackupCommunity is a row of communities.db.
type BackupCommunity struct {
	ID        int64
	Name      string
	Shard     int
	TakenDown bool
}

// ListBackupCommunities returns the communities in communities.db at path,
// by name.
func ListBackupCommunities(path string) ([]BackupCommunity, error) {
	db, err := openReadOnlySQLite(path)
	if err != nil {
		return nil, err
	}
	defer db.Close()
	rows, err := db.Query("SELECT id, name, shard, deleted_at != 0 FROM community ORDER BY name COLLATE NOCASE, id")
	if err != nil {
		return nil, fmt.Errorf("read %s: %w", path, err)
	}
	defer rows.Close()
	var cs []BackupCommunity
	for rows.Next() {
		var c BackupCommunity
		if err := rows.Scan(&c.ID, &c.Name, &c.Shard, &c.TakenDown); err != nil {
			return nil, err
		}
		cs = append(cs, c)
	}
	return cs, rows.Err()
}

// lookupBackupCommunity finds the community by ID, or else by name: the
// active one, or the only taken-down one (board names are reused).
func lookupBackupCommunity(path, name string, id int64) (*BackupCommunity, error) {
	db, err := openReadOnlySQLite(path)
	if err != nil {
		return nil, err
	}
	defer db.Close()
	q := "SELECT id, name, shard, deleted_at != 0 FROM community WHERE "
	var arg any = id
	if id != 0 {
		q += "id = ?"
	} else {
		q += "name = ? COLLATE NOCASE"
		arg = strings.TrimSpace(name)
	}
	rows, err := db.Query(q+" ORDER BY id", arg)
	if err != nil {
		return nil, fmt.Errorf("read %s: %w", path, err)
	}
	defer rows.Close()
	var active *BackupCommunity
	var gone []*BackupCommunity
	for rows.Next() {
		c := &BackupCommunity{}
		if err := rows.Scan(&c.ID, &c.Name, &c.Shard, &c.TakenDown); err != nil {
			return nil, err
		}
		if c.TakenDown {
			gone = append(gone, c)
		} else {
			active = c
		}
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}
	switch {
	case active != nil:
		return active, nil
	case len(gone) == 1:
		return gone[0], nil
	case len(gone) > 1:
		var ids []string
		for _, c := range gone {
			ids = append(ids, strconv.FormatInt(c.ID, 10))
		}
		return nil, fmt.Errorf("%q names several taken-down communities (IDs %s); give the ID", name, strings.Join(ids, ", "))
	}
	if id != 0 {
		return nil, fmt.Errorf("community #%d: %w", id, ErrCommunityNotFound)
	}
	return nil, fmt.Errorf("%s: %w", name, ErrCommunityNotFound)
}

// exportFromDir exports the posts of res.CommunityID in engine data dir
// dir, and reports whether it has any (exported or not).
func exportFromDir(dir string, res *ExportCommunityResult, opts ExportCommunityOptions, index *os.File) (bool, error) {
	postsDB, closePosts, err := openReadOnlyPebble(filepath.Join(dir, "posts.pebble"))
	if err != nil {
		return false, fmt.Errorf("open posts.pebble: %w", err)
	}
	defer closePosts()
	commentsDB, closeComments, err := openReadOnlyPebble(filepath.Join(dir, "comments.pebble"))
	if err != nil {
		return false, fmt.Errorf("open comments.pebble: %w", err)
	}
	defer closeComments()

	posts, err := postsDB.NewIter(&pebble.IterOptions{LowerBound: []byte{'m'}, UpperBound: []byte{'n'}})
	if err != nil {
		return false, err
	}
	defer posts.Close()
	comments, err := commentsDB.NewIter(&pebble.IterOptions{LowerBound: []byte{'c'}, UpperBound: []byte{'d'}})
	if err != nil {
		return false, err
	}
	defer comments.Close()

	// EncodeRFC822PostMeta writes CommunityID first; older records have an
	// Encoding header before it.
	want := []byte("CommunityID: " + strconv.FormatInt(res.CommunityID, 10) + "\n")
	legacy := []byte("Encoding: ")
	found := false
	enc := json.NewEncoder(index)
	enc.SetEscapeHTML(false)
	for posts.First(); posts.Valid(); posts.Next() {
		res.Scanned++
		if opts.ProgressFn != nil && res.Scanned%100000 == 0 {
			opts.ProgressFn(dir, res.Scanned, res.Exported)
		}
		// Most posts are of other communities: skip them unparsed.
		v := posts.Value()
		if bytes.HasPrefix(v, legacy) {
			if i := bytes.IndexByte(v, '\n'); i >= 0 {
				v = v[i+1:]
			}
		}
		if !bytes.HasPrefix(v, want) {
			continue
		}
		id, err := model.DecodePostIDKey(posts.Key())
		if err != nil {
			continue
		}
		meta := model.DecodeRFC822PostMeta(posts.Value())
		if cid, _ := strconv.ParseInt(meta["CommunityID"], 10, 64); cid != res.CommunityID {
			continue
		}
		found = true
		p := postFromMeta(id, meta)
		p.Community = res.Name
		if p.DeletedOrRemoved() && !opts.Deleted {
			res.Skipped++
			continue
		}
		if err := readPostExtras(postsDB, p); err != nil {
			return found, err
		}
		cs, err := readAllComments(comments, id)
		if err != nil {
			return found, err
		}
		var data []byte
		if opts.LegacyFormat {
			data = RenderLegacyPostWithComments(p, cs, opts.Big5)
		} else {
			data = RenderPostWithComments(p, cs, opts.Big5)
		}
		file := p.PostFile
		if file == "" || file == "-" || strings.ContainsAny(file, "/\\") || file == "." || file == ".." {
			file = fmt.Sprintf("post-%d", id)
		}
		if err := os.WriteFile(filepath.Join(opts.TargetDir, file), data, 0644); err != nil {
			return found, err
		}
		if err := enc.Encode(struct {
			*model.Post
			Content string `json:"content,omitempty"`
			File    string `json:"bbs_export_file"`
		}{Post: p, File: file}); err != nil {
			return found, err
		}
		res.Exported++
	}
	if opts.ProgressFn != nil {
		opts.ProgressFn(dir, res.Scanned, res.Exported)
	}
	return found, posts.Error()
}

// postFromMeta builds a post from its metadata record, as fillMetaDB does.
func postFromMeta(id uint64, meta map[string]string) *model.Post {
	p := &model.Post{ID: id, PostFile: meta["PostFile"], Title: meta["Title"], CreatorName: meta["CreatorName"]}
	p.ParentID, _ = strconv.ParseUint(meta["ParentID"], 10, 64)
	p.CommunityID, _ = strconv.ParseInt(meta["CommunityID"], 10, 64)
	p.CreatorID = model.ParsePersonID(meta["CreatorID"])
	p.CreatedAt, _ = strconv.ParseInt(meta["CreatedAt"], 10, 64)
	p.Modified, _ = strconv.ParseInt(meta["Modified"], 10, 64)
	if p.Modified == 0 {
		p.Modified = p.CreatedAt
	}
	p.Filemode, _ = strconv.Atoi(meta["Filemode"])
	p.Money, _ = strconv.Atoi(meta["Money"])
	p.Locked = meta["Locked"] == "true"
	p.BBSEditMark = meta["BBSEditMark"]
	p.Deletion = model.DeletionFromRFC822(meta)
	return p
}

// readPostExtras reads the body and the counters of p.
func readPostExtras(db *pebble.DB, p *model.Post) error {
	val, closer, err := db.Get(model.EncodePostKey(p.ID))
	switch {
	case err == nil:
		p.Content = string(val)
		closer.Close()
	case err != pebble.ErrNotFound:
		return err
	}
	val, closer, err = db.Get(model.EncodePostCountsKey(p.ID))
	switch {
	case err == nil:
		c, _ := model.DecodePostCounts(val)
		closer.Close()
		p.Upvotes, p.Downvotes, p.NumComments = c.Upvotes, c.Downvotes, c.NumComments
		p.Modified = max(p.Modified, c.Modified)
	case err != pebble.ErrNotFound:
		return err
	}
	return nil
}

// readAllComments returns the comments of post id, tombstones included.
func readAllComments(it *pebble.Iterator, id uint64) ([]*model.Comment, error) {
	prefix := model.EncodeCommentPrefix(id)
	var out []*model.Comment
	for it.SeekGE(prefix); it.Valid() && bytes.HasPrefix(it.Key(), prefix); it.Next() {
		_, seq, err := model.DecodeCommentKey(it.Key())
		if err != nil {
			continue
		}
		if c, err := model.DecodeRFC822Comment(id, seq, it.Value()); err == nil {
			out = append(out, c)
		}
	}
	return out, it.Error()
}
