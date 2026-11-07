package storage

import (
	"database/sql"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"net/url"
	"os"
	"path/filepath"
	"sort"
	"strconv"

	"github.com/cockroachdb/pebble"

	"pttbbs/post/model"
)

// Verification of a backup (or any stopped engine data dir).
//
// VerifyDataDir opens the DBs read-only, so it never changes the backup. It
//   - reads every key and value of the four Pebble DBs, which also checks the
//     block checksums,
//   - runs SQLite's quick_check,
//   - streams SQLite posts and the Pebble records side by side in post ID order
//     (O(1) memory) and compares SQLite with what a rebuild would produce.
//
// Problems that make the backup unusable are errors. Differences that a live
// backup can legitimately contain (writes in flight between the checkpoints,
// titles changed by UpdatePostTitle, counters of deleted posts imported from
// .DIR) are warnings.

// VerifyReport is the result of VerifyDataDir.
type VerifyReport struct {
	Dir      string
	Posts    int            // posts in Pebble
	Comments int            // comment records in Pebble, tombstones included
	Votes    int            // vote records in Pebble
	Errors   []string       // the backup is not usable as is
	Warnings map[string]int // kind of difference -> count
	Examples []string       // a few examples of the warnings
}

// OK reports whether the backup has no errors.
func (r *VerifyReport) OK() bool { return len(r.Errors) == 0 }

const maxVerifyExamples = 20

func (r *VerifyReport) errorf(format string, args ...any) {
	r.Errors = append(r.Errors, fmt.Sprintf(format, args...))
}

func (r *VerifyReport) warn(kind string, postID uint64, detail string) {
	r.Warnings[kind]++
	if len(r.Examples) < maxVerifyExamples {
		r.Examples = append(r.Examples, fmt.Sprintf("post %d: %s: %s", postID, kind, detail))
	}
}

// Print writes a human readable summary of the report.
func (r *VerifyReport) Print(w io.Writer) {
	status := "OK"
	if !r.OK() {
		status = "FAILED"
	}
	fmt.Fprintf(w, "%s: %s (%d posts, %d comments, %d votes)\n", r.Dir, status, r.Posts, r.Comments, r.Votes)
	for _, e := range r.Errors {
		fmt.Fprintf(w, "  ERROR: %s\n", e)
	}
	kinds := make([]string, 0, len(r.Warnings))
	for k := range r.Warnings {
		kinds = append(kinds, k)
	}
	sort.Strings(kinds)
	for _, k := range kinds {
		fmt.Fprintf(w, "  warning: %d x %s\n", r.Warnings[k], k)
	}
	for _, ex := range r.Examples {
		fmt.Fprintf(w, "    e.g. %s\n", ex)
	}
}

// ExpandVerifyDirs turns each argument into the engine data dirs it holds: the
// directory itself when it has posts.pebble, otherwise its shard-<N> subdirs.
func ExpandVerifyDirs(args []string) ([]string, error) {
	var dirs []string
	for _, arg := range args {
		if _, err := os.Stat(filepath.Join(arg, "posts.pebble")); err == nil {
			dirs = append(dirs, arg)
			continue
		}
		shards, _ := filepath.Glob(filepath.Join(arg, "shard-*", "posts.pebble"))
		if len(shards) == 0 {
			return nil, fmt.Errorf("%s: no posts.pebble or shard-*/posts.pebble found", arg)
		}
		sort.Strings(shards)
		for _, s := range shards {
			dirs = append(dirs, filepath.Dir(s))
		}
	}
	return dirs, nil
}

// VerifyDataDir verifies the engine DBs in dir without modifying them.
func VerifyDataDir(dir string) *VerifyReport {
	r := &VerifyReport{Dir: dir, Warnings: map[string]int{}}

	dbs := map[string]*pebble.DB{}
	for _, name := range []string{"posts.pebble", "comments.pebble", "votes.pebble", "history.pebble"} {
		path := filepath.Join(dir, name)
		// Even a read-only open creates an empty LOCK file; remove it again
		// so that verification leaves the backup exactly as it was.
		lock := filepath.Join(path, "LOCK")
		if _, err := os.Lstat(lock); errors.Is(err, os.ErrNotExist) {
			defer os.Remove(lock)
		}
		db, err := pebble.Open(path, &pebble.Options{ReadOnly: true, Logger: quietLogger{}})
		if err != nil {
			r.errorf("open %s: %v", name, err)
			continue
		}
		defer db.Close()
		dbs[name] = db
	}
	if !r.OK() {
		return r
	}
	// History is not cross-checked against SQLite; reading it all verifies it.
	if _, err := scanAll(dbs["history.pebble"], nil); err != nil {
		r.errorf("read history.pebble: %v", err)
	}

	sqlitePath := filepath.Join(dir, "meta.sqlite3")
	if _, err := os.Stat(sqlitePath); err != nil {
		r.errorf("meta.sqlite3: %v", err)
		return r
	}
	meta, err := sql.Open("sqlite", "file:"+(&url.URL{Path: sqlitePath}).EscapedPath()+"?mode=ro&immutable=1")
	if err != nil {
		r.errorf("open meta.sqlite3: %v", err)
		return r
	}
	defer meta.Close()
	var check string
	if err := meta.QueryRow("PRAGMA quick_check").Scan(&check); err != nil || check != "ok" {
		r.errorf("meta.sqlite3 quick_check: %v %s", err, check)
		return r
	}

	compareMetaWithPebble(r, meta, dbs["posts.pebble"], dbs["comments.pebble"], dbs["votes.pebble"])
	return r
}

// quietLogger drops pebble's informational messages (such as WAL replay)
// that would otherwise drown the verification report.
type quietLogger struct{}

func (quietLogger) Infof(string, ...any) {}
func (quietLogger) Fatalf(format string, args ...any) {
	panic(fmt.Sprintf(format, args...))
}

// scanAll iterates the whole DB (or [lower, upper)) and returns the number of
// keys, reading every value so that every block checksum is verified.
func scanAll(db *pebble.DB, bounds *pebble.IterOptions) (int, error) {
	it, err := db.NewIter(bounds)
	if err != nil {
		return 0, err
	}
	n := 0
	for it.First(); it.Valid(); it.Next() {
		_ = it.Value()
		n++
	}
	if err := it.Error(); err != nil {
		it.Close()
		return n, err
	}
	return n, it.Close()
}

// idCursor walks keys of the form <prefix byte><8-byte big-endian post ID>...
// in post ID order.
type idCursor struct {
	it  *pebble.Iterator
	err error
}

func newIDCursor(db *pebble.DB, prefix byte) (*idCursor, error) {
	it, err := db.NewIter(&pebble.IterOptions{LowerBound: []byte{prefix}, UpperBound: []byte{prefix + 1}})
	if err != nil {
		return nil, err
	}
	it.First()
	return &idCursor{it: it}, nil
}

// id returns the post ID of the current key, or false at the end.
func (c *idCursor) id() (uint64, bool) {
	for c.it.Valid() {
		if k := c.it.Key(); len(k) >= 9 {
			return binary.BigEndian.Uint64(k[1:9]), true
		}
		c.it.Next() // Malformed key; skip.
	}
	return 0, false
}

func (c *idCursor) close() error {
	err := c.it.Error()
	if cerr := c.it.Close(); err == nil {
		err = cerr
	}
	return err
}

// postAggregate is what a rebuild derives for one post from Pebble.
type postAggregate struct {
	comments, upvotes, downvotes, crossposts int
	commentModified                          int64
}

type metaRow struct {
	id                                          uint64
	parentID                                    uint64
	community, postFile, title, creatorName     string
	creatorID                                   int64
	createdAt, modified                         int64
	filemode, money                             int
	upvotes, downvotes, numComments, crossposts int
	deleted, removed                            bool
	deletedAt                                   int64
	modPersonName                               string
	modPersonID                                 int64
	reason                                      string
}

func compareMetaWithPebble(r *VerifyReport, meta *sql.DB, postsDB, commentsDB, votesDB *pebble.DB) {
	posts, err := postsDB.NewIter(&pebble.IterOptions{LowerBound: []byte{'p'}, UpperBound: []byte{'q'}})
	if err != nil {
		r.errorf("posts.pebble: %v", err)
		return
	}
	defer posts.Close()
	comments, err1 := newIDCursor(commentsDB, 'c')
	votes, err2 := newIDCursor(votesDB, 'v')
	crossposts, err3 := newIDCursor(postsDB, 'x')
	if err := errors.Join(err1, err2, err3); err != nil {
		r.errorf("open iterators: %v", err)
		return
	}
	defer func() {
		for name, c := range map[string]*idCursor{"comments.pebble": comments, "votes.pebble": votes, "crossposts": crossposts} {
			if err := c.close(); err != nil {
				r.errorf("read %s: %v", name, err)
			}
		}
	}()

	// aggregate consumes the records of post id from all cursors; records of
	// smaller IDs belong to posts that no longer exist.
	aggregate := func(id uint64) postAggregate {
		var a postAggregate
		for cid, ok := comments.id(); ok && cid <= id; cid, ok = comments.id() {
			if k := comments.it.Key(); len(k) == 13 {
				if cid < id {
					r.warn("comments of a missing post", cid, "orphan comment record")
				} else {
					a.comments++
					r.Comments++
					if c, err := model.DecodeRFC822Comment(cid, binary.BigEndian.Uint32(k[9:13]), comments.it.Value()); err == nil && c.ModifiedAt > a.commentModified {
						a.commentModified = c.ModifiedAt
					}
				}
			}
			comments.it.Next()
		}
		for vid, ok := votes.id(); ok && vid <= id; vid, ok = votes.id() {
			if vid < id {
				r.warn("votes of a missing post", vid, "orphan vote record")
			} else if v := votes.it.Value(); len(v) > 0 {
				r.Votes++
				switch v[0] {
				case 'U':
					a.upvotes++
				case 'D':
					a.downvotes++
				}
			}
			votes.it.Next()
		}
		for xid, ok := crossposts.id(); ok && xid <= id; xid, ok = crossposts.id() {
			if xid == id {
				a.crossposts++
			}
			crossposts.it.Next()
		}
		return a
	}

	rows, err := meta.Query(`SELECT id, parent_id, community, post_file, title, creator_name, creator_id, created_at, modified,
		filemode, money, upvotes, downvotes, num_comments, num_crossposts, deleted, removed, deleted_at, mod_person_name, mod_person_id, reason
		FROM posts ORDER BY id`)
	if err != nil {
		r.errorf("query meta.sqlite3: %v", err)
		return
	}
	defer rows.Close()

	var row metaRow
	haveRow := false
	nextRow := func() {
		haveRow = rows.Next()
		if haveRow {
			if err := rows.Scan(&row.id, &row.parentID, &row.community, &row.postFile, &row.title, &row.creatorName, &row.creatorID,
				&row.createdAt, &row.modified, &row.filemode, &row.money, &row.upvotes, &row.downvotes, &row.numComments, &row.crossposts,
				&row.deleted, &row.removed, &row.deletedAt, &row.modPersonName, &row.modPersonID, &row.reason); err != nil {
				r.errorf("scan meta.sqlite3: %v", err)
				haveRow = false
			}
		}
	}
	nextRow()
	posts.First()
	for posts.Valid() || haveRow {
		var pid uint64
		havePost := false
		if posts.Valid() {
			if id, err := model.DecodePostKey(posts.Key()); err == nil {
				pid, havePost = id, true
			} else {
				posts.Next() // Not a post key (e.g. history-style); skip.
				continue
			}
		}
		switch {
		case havePost && (!haveRow || pid < row.id):
			// SQLite is copied after Pebble, so it should know every post;
			// a restored service could otherwise reuse this ID.
			r.Posts++
			aggregate(pid)
			r.errorf("post %d is in posts.pebble but not in meta.sqlite3", pid)
			if len(r.Errors) > maxVerifyExamples {
				r.errorf("too many errors; giving up")
				return
			}
			posts.Next()
		case haveRow && (!havePost || row.id < pid):
			// Written to SQLite after the Pebble checkpoint, or a post whose
			// Pebble write failed; GetPost falls back to the board file.
			r.warn("post only in meta.sqlite3", row.id, row.community+"/"+row.postFile)
			nextRow()
		default:
			r.Posts++
			comparePost(r, &row, posts.Value(), aggregate(pid))
			posts.Next()
			nextRow()
		}
	}
	if err := errors.Join(posts.Error(), rows.Err()); err != nil {
		r.errorf("read posts: %v", err)
	}
	// Drain the remaining records so their checksums are verified as well.
	aggregate(^uint64(0))

	var metaCrossposts, pebbleCrossposts int
	_ = meta.QueryRow("SELECT COUNT(*) FROM crossposts").Scan(&metaCrossposts)
	if n, err := scanAll(postsDB, &pebble.IterOptions{LowerBound: []byte{'x'}, UpperBound: []byte{'y'}}); err == nil {
		pebbleCrossposts = n
	}
	if metaCrossposts != pebbleCrossposts {
		r.warn("crossposts table size differs", 0, fmt.Sprintf("meta.sqlite3=%d pebble=%d", metaCrossposts, pebbleCrossposts))
	}
}

func comparePost(r *VerifyReport, row *metaRow, val []byte, a postAggregate) {
	h, _ := model.DecodeRFC822Post(val)
	del := model.DeletionFromRFC822(h)
	num := func(k string) int64 { v, _ := strconv.ParseInt(h[k], 10, 64); return v }
	createdAt, modified := num("CreatedAt"), num("Modified")
	if modified == 0 {
		modified = createdAt
	}
	if modified < a.commentModified {
		modified = a.commentModified
	}

	diff := func(kind string, sqlite, pebble any) {
		r.warn(kind, row.id, fmt.Sprintf("meta.sqlite3=%v pebble=%v", sqlite, pebble))
	}
	if row.title != h["Title"] {
		// UpdatePostTitle changes the title and modified time only in SQLite.
		diff("title (UpdatePostTitle is not written to Pebble)", row.title, h["Title"])
	} else if row.modified != modified {
		diff("modified", row.modified, modified)
	}
	if row.community != h["Community"] || row.postFile != h["PostFile"] || row.creatorName != h["CreatorName"] ||
		row.parentID != uint64(num("ParentID")) || row.creatorID != int64(model.ParsePersonID(h["CreatorID"])) || row.createdAt != createdAt {
		diff("identity fields", fmt.Sprintf("%s/%s by %s(%d) at %d parent %d", row.community, row.postFile, row.creatorName, row.creatorID, row.createdAt, row.parentID),
			fmt.Sprintf("%s/%s by %s(%s) at %d parent %s", h["Community"], h["PostFile"], h["CreatorName"], h["CreatorID"], createdAt, h["ParentID"]))
	}
	if row.filemode != int(num("Filemode")) || row.money != int(num("Money")) {
		diff("filemode/money", fmt.Sprint(row.filemode, "/", row.money), fmt.Sprint(num("Filemode"), "/", num("Money")))
	}
	if row.deleted != del.Deleted || row.removed != del.Removed || row.deletedAt != del.DeletedAt ||
		row.modPersonName != del.ModPersonName || row.modPersonID != int64(del.ModPersonID) || row.reason != del.Reason {
		diff("deletion", fmt.Sprint(row.deleted, row.removed, row.deletedAt, row.modPersonName, row.reason),
			fmt.Sprint(del.Deleted, del.Removed, del.DeletedAt, del.ModPersonName, del.Reason))
	}
	if row.upvotes != a.upvotes || row.downvotes != a.downvotes || row.numComments != a.comments {
		kind := "counters"
		if (row.deleted || row.removed) && a.comments == 0 && a.upvotes == 0 && a.downvotes == 0 {
			kind = "counters of a deleted post imported from .DIR"
		}
		diff(kind, fmt.Sprintf("%d/%d/%d", row.upvotes, row.downvotes, row.numComments), fmt.Sprintf("%d/%d/%d", a.upvotes, a.downvotes, a.comments))
	}
	if row.crossposts != a.crossposts {
		diff("num_crossposts", row.crossposts, a.crossposts)
	}
}
