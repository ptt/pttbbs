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

	"pttbbs/nest/model"
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
// and older data whose titles or deleted-post counters are only in SQLite)
// are warnings.

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

// FindCommunitiesDB returns the communities.db among dirs (it is in the
// backup of one engine only), or "".
func FindCommunitiesDB(dirs []string) string {
	for _, d := range dirs {
		if p := filepath.Join(d, CommunitiesDBName); fileExists(p) {
			return p
		}
	}
	return ""
}

func fileExists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

// openReadOnlyPebble opens a Pebble DB read-only. Even a read-only open
// creates an empty LOCK file; the returned close function removes it again
// so that verification leaves the backup exactly as it was.
func openReadOnlyPebble(path string) (*pebble.DB, func(), error) {
	lock := filepath.Join(path, "LOCK")
	_, err := os.Lstat(lock)
	hadLock := !errors.Is(err, os.ErrNotExist)
	db, err := pebble.Open(path, &pebble.Options{ReadOnly: true, Logger: quietLogger{}})
	if err != nil {
		if !hadLock {
			os.Remove(lock)
		}
		return nil, nil, err
	}
	return db, func() {
		db.Close()
		if !hadLock {
			os.Remove(lock)
		}
	}, nil
}

func openReadOnlySQLite(path string) (*sql.DB, error) {
	return sql.Open("sqlite", "file:"+(&url.URL{Path: path}).EscapedPath()+"?mode=ro&immutable=1")
}

// loadCommunityIDs verifies communities.db and returns its community IDs.
func loadCommunityIDs(r *VerifyReport, path string) map[int64]bool {
	db, err := openReadOnlySQLite(path)
	if err != nil {
		r.errorf("open %s: %v", path, err)
		return nil
	}
	defer db.Close()
	var check string
	if err := db.QueryRow("PRAGMA quick_check").Scan(&check); err != nil || check != "ok" {
		r.errorf("%s quick_check: %v %s", path, err, check)
		return nil
	}
	rows, err := db.Query("SELECT id FROM community")
	if err != nil {
		r.errorf("read %s: %v", path, err)
		return nil
	}
	defer rows.Close()
	ids := map[int64]bool{}
	for rows.Next() {
		var id int64
		if err := rows.Scan(&id); err != nil {
			r.errorf("read %s: %v", path, err)
			return nil
		}
		ids[id] = true
	}
	if err := rows.Err(); err != nil {
		r.errorf("read %s: %v", path, err)
		return nil
	}
	return ids
}

// DefaultQuickVerifyTail is how many newest posts VerifyDataDirQuick checks.
const DefaultQuickVerifyTail = 10000

// VerifyDataDir verifies the engine DBs in dir without modifying them.
// communitiesDB is the communities.db of the same backup (see
// FindCommunitiesDB); every post must belong to a community it knows. With
// "", that check is skipped with a warning.
func VerifyDataDir(dir, communitiesDB string) *VerifyReport {
	return verifyDataDir(dir, communitiesDB, 0)
}

// VerifyDataDirQuick is VerifyDataDir limited to DB integrity checks and the
// newest tail posts, for fast checks after a crash or reboot.
func VerifyDataDirQuick(dir, communitiesDB string, tail int) *VerifyReport {
	if tail <= 0 {
		tail = DefaultQuickVerifyTail
	}
	return verifyDataDir(dir, communitiesDB, tail)
}

func verifyDataDir(dir, communitiesDB string, tail int) *VerifyReport {
	quick := tail > 0
	r := &VerifyReport{Dir: dir, Warnings: map[string]int{}}
	var communities map[int64]bool
	if communitiesDB == "" {
		r.warn("no communities.db to check community IDs against", 0, dir)
	} else if communities = loadCommunityIDs(r, communitiesDB); communities == nil {
		return r
	}

	dbs := map[string]*pebble.DB{}
	for _, name := range []string{"posts.pebble", "comments.pebble", "votes.pebble", "history.pebble"} {
		db, closeDB, err := openReadOnlyPebble(filepath.Join(dir, name))
		if err != nil {
			r.errorf("open %s: %v", name, err)
			continue
		}
		defer closeDB()
		dbs[name] = db
	}
	if !r.OK() {
		return r
	}
	// History is not cross-checked against SQLite; reading it all verifies it.
	if !quick {
		if _, err := scanAll(dbs["history.pebble"], nil); err != nil {
			r.errorf("read history.pebble: %v", err)
		}
	}

	sqlitePath := filepath.Join(dir, "meta.sqlite3")
	if _, err := os.Stat(sqlitePath); os.IsNotExist(err) {
		var minID uint64
		if quick {
			minID, _ = pebbleTailMinID(dbs["posts.pebble"], tail)
		}
		verifyPebbleOnly(r, dbs["posts.pebble"], dbs["comments.pebble"], dbs["votes.pebble"], communities, minID, quick)
		return r
	} else if err != nil {
		r.errorf("meta.sqlite3: %v", err)
		return r
	}
	meta, err := openReadOnlySQLite(sqlitePath)
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

	var minID uint64
	if quick {
		pMin, pOK := pebbleTailMinID(dbs["posts.pebble"], tail)
		mMin, mOK := metaTailMinID(meta, tail)
		switch {
		case pOK && mOK:
			minID = min(pMin, mMin)
		case pOK:
			minID = pMin
		case mOK:
			minID = mMin
		}
	}
	compareMetaWithPebble(r, meta, dbs["posts.pebble"], dbs["comments.pebble"], dbs["votes.pebble"], communities, minID, quick)
	if !quick && communities != nil {
		checkCommunityRefs(r, meta, communities)
	}
	return r
}

// loadCommunityShards returns the shard of every community in communities.db.
func loadCommunityShards(path string) (map[int64]int, error) {
	db, err := openReadOnlySQLite(path)
	if err != nil {
		return nil, err
	}
	defer db.Close()
	rows, err := db.Query("SELECT id, shard FROM community")
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	shards := map[int64]int{}
	for rows.Next() {
		var id int64
		var shard int
		if err := rows.Scan(&id, &shard); err != nil {
			return nil, err
		}
		shards[id] = shard
	}
	return shards, rows.Err()
}

// shardCursor walks the posts (metadata records) of one engine data dir in
// post ID order.
type shardCursor struct {
	it  *pebble.Iterator
	id  uint64
	cid int64
	ok  bool
}

func (c *shardCursor) next(r *VerifyReport, dir string) {
	for c.ok = c.it.Valid(); c.ok; c.ok = c.it.Valid() {
		id, err := model.DecodePostIDKey(c.it.Key())
		if err == nil {
			c.id = id
			c.cid, _ = strconv.ParseInt(model.DecodeRFC822PostMeta(c.it.Value())["CommunityID"], 10, 64)
			c.it.Next()
			return
		}
		c.it.Next()
	}
	if err := c.it.Error(); err != nil {
		r.errorf("%s: read posts.pebble: %v", dir, err)
	}
}

// VerifyShards checks the engine data dirs of one sharded backup (or of
// stopped engines) against each other, in O(communities) memory:
//   - a post ID is used by one post in one engine only (post IDs are
//     global, so the service can find a post by its ID alone; a comment
//     ID is its post ID and floor),
//   - all posts of a community are in the same engine, and
//   - with communitiesDB, each engine holds the communities of one shard,
//     and no two engines the same shard.
//
// Each dir must pass VerifyDataDir first.
func VerifyShards(dirs []string, communitiesDB string) *VerifyReport {
	r := &VerifyReport{Dir: "shards", Warnings: map[string]int{}}
	var registry map[int64]int
	if communitiesDB != "" {
		var err error
		if registry, err = loadCommunityShards(communitiesDB); err != nil {
			r.errorf("read %s: %v", communitiesDB, err)
			return r
		}
	}

	cursors := make([]*shardCursor, len(dirs))
	for i, dir := range dirs {
		db, closeDB, err := openReadOnlyPebble(filepath.Join(dir, "posts.pebble"))
		if err != nil {
			r.errorf("%s: open posts.pebble: %v", dir, err)
			return r
		}
		defer closeDB()
		it, err := db.NewIter(&pebble.IterOptions{LowerBound: []byte{'m'}, UpperBound: []byte{'n'}})
		if err != nil {
			r.errorf("%s: read posts.pebble: %v", dir, err)
			return r
		}
		defer it.Close()
		it.First()
		cursors[i] = &shardCursor{it: it}
		cursors[i].next(r, dir)
	}

	communityDir := map[int64]int{} // community ID -> index of its dir
	dirShard := map[int]int{}       // index of a dir -> its shard in the registry
	duplicates := 0
	for {
		// Pick the smallest post ID among the dirs (there are only a few).
		lo := -1
		for i, c := range cursors {
			if c.ok && (lo < 0 || c.id < cursors[lo].id) {
				lo = i
			}
		}
		if lo < 0 {
			break
		}
		id := cursors[lo].id
		for i, c := range cursors {
			if !c.ok || c.id != id {
				continue
			}
			if i != lo {
				if duplicates < maxVerifyExamples {
					r.errorf("post %d is in both %s and %s", id, dirs[lo], dirs[i])
				}
				duplicates++
			}
			r.Posts++
			if d, seen := communityDir[c.cid]; !seen {
				communityDir[c.cid] = i
			} else if d != i && len(r.Errors) < maxVerifyExamples {
				r.errorf("community #%d has posts in both %s and %s (e.g. post %d)", c.cid, dirs[d], dirs[i], id)
			}
			if shard, known := registry[c.cid]; known {
				if s, seen := dirShard[i]; !seen {
					dirShard[i] = shard
				} else if s != shard && len(r.Errors) < maxVerifyExamples {
					r.errorf("%s holds communities of shards %d and %d (e.g. community #%d)", dirs[i], s, shard, c.cid)
				}
			}
			c.next(r, dirs[i])
		}
	}
	if duplicates > maxVerifyExamples {
		r.errorf("... %d post IDs used more than once in total", duplicates)
	}
	byShard := map[int]int{}
	for i := range dirs {
		s, ok := dirShard[i]
		if !ok {
			continue
		}
		if other, taken := byShard[s]; taken {
			r.errorf("%s and %s both hold communities of shard %d", dirs[other], dirs[i], s)
		}
		byShard[s] = i
	}
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

func idIterBounds(prefix byte, minID uint64) *pebble.IterOptions {
	lower := []byte{prefix}
	if minID > 0 {
		lower = make([]byte, 9)
		lower[0] = prefix
		binary.BigEndian.PutUint64(lower[1:9], minID)
	}
	return &pebble.IterOptions{LowerBound: lower, UpperBound: []byte{prefix + 1}}
}

func newIDCursor(db *pebble.DB, prefix byte) (*idCursor, error) {
	return newIDCursorFrom(db, prefix, 0)
}

func newIDCursorFrom(db *pebble.DB, prefix byte, minID uint64) (*idCursor, error) {
	it, err := db.NewIter(idIterBounds(prefix, minID))
	if err != nil {
		return nil, err
	}
	it.First()
	return &idCursor{it: it}, nil
}

func pebbleTailMinID(postsDB *pebble.DB, tail int) (uint64, bool) {
	it, err := postsDB.NewIter(&pebble.IterOptions{LowerBound: []byte{'m'}, UpperBound: []byte{'n'}})
	if err != nil {
		return 0, false
	}
	defer it.Close()
	var minID uint64
	found := false
	for ok, n := it.Last(), 0; ok && n < tail; ok = it.Prev() {
		id, err := model.DecodePostIDKey(it.Key())
		if err != nil {
			continue
		}
		minID = id
		found = true
		n++
	}
	return minID, found
}

func metaTailMinID(meta *sql.DB, tail int) (uint64, bool) {
	var id uint64
	err := meta.QueryRow("SELECT id FROM posts ORDER BY id DESC LIMIT 1 OFFSET ?", tail-1).Scan(&id)
	if err == nil {
		return id, true
	}
	var minID sql.NullInt64
	if err := meta.QueryRow("SELECT MIN(id) FROM posts").Scan(&minID); err == nil && minID.Valid {
		return uint64(minID.Int64), true
	}
	return 0, false
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
	stored                                   model.PostCounts // the 'n' record
}

type metaRow struct {
	id                                          uint64
	parentID                                    uint64
	communityID                                 int64
	postFile, title, creatorName                string
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

// aggregator derives what a rebuild needs of each post, in post ID order,
// from cursors over the records keyed by post ID.
type aggregator struct {
	r                                   *VerifyReport
	comments, votes, crossposts, counts *idCursor
}

func newAggregator(r *VerifyReport, postsDB, commentsDB, votesDB *pebble.DB, minID uint64) (*aggregator, error) {
	comments, err1 := newIDCursorFrom(commentsDB, 'c', minID)
	votes, err2 := newIDCursorFrom(votesDB, 'v', minID)
	crossposts, err3 := newIDCursorFrom(postsDB, 'x', minID)
	counts, err4 := newIDCursorFrom(postsDB, 'n', minID)
	if err := errors.Join(err1, err2, err3, err4); err != nil {
		return nil, err
	}
	return &aggregator{r: r, comments: comments, votes: votes, crossposts: crossposts, counts: counts}, nil
}

func (g *aggregator) close() {
	for name, c := range map[string]*idCursor{"comments.pebble": g.comments, "votes.pebble": g.votes, "crossposts": g.crossposts, "post counters": g.counts} {
		if err := c.close(); err != nil {
			g.r.errorf("read %s: %v", name, err)
		}
	}
}

// of consumes the records of post id from all cursors; records of smaller
// IDs belong to posts that no longer exist.
func (g *aggregator) of(id uint64) postAggregate {
	r, comments, votes, crossposts, counts := g.r, g.comments, g.votes, g.crossposts, g.counts
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
	for nid, ok := counts.id(); ok && nid <= id; nid, ok = counts.id() {
		if nid == id {
			if c, err := model.DecodePostCounts(counts.it.Value()); err == nil {
				a.stored = c
			} else {
				r.warn("malformed counter record", nid, err.Error())
			}
		}
		counts.it.Next()
	}
	return a
}

// checkPostBodies reads every post body (which checks their blocks) and
// compares their number with the metadata records.
func checkPostBodies(r *VerifyReport, postsDB *pebble.DB) {
	metas, _ := scanAll(postsDB, &pebble.IterOptions{LowerBound: []byte{'m'}, UpperBound: []byte{'n'}})
	if bodies, err := scanAll(postsDB, &pebble.IterOptions{LowerBound: []byte{'p'}, UpperBound: []byte{'q'}}); err != nil {
		r.errorf("read post bodies: %v", err)
	} else if bodies != metas {
		r.warn("post bodies and metadata records differ in number", 0, fmt.Sprintf("bodies=%d metadata=%d", bodies, metas))
	}
}

// checkPostBody verifies that the 'p' body record for id exists in posts.pebble.
func checkPostBody(r *VerifyReport, bodies *idCursor, id uint64) {
	for bid, ok := bodies.id(); ok && bid < id; bid, ok = bodies.id() {
		r.warn("post body of a missing post", bid, "orphan body record")
		bodies.it.Next()
	}
	if bid, ok := bodies.id(); ok && bid == id {
		_ = bodies.it.Value()
		bodies.it.Next()
		return
	}
	r.warn("post body missing in posts.pebble", id, "")
}

// verifyPebbleOnly checks a data dir without meta.sqlite3 (a backup, from
// which a restore rebuilds it): the counter records against the comments
// and votes, and the communities of the posts.
func verifyPebbleOnly(r *VerifyReport, postsDB, commentsDB, votesDB *pebble.DB, communities map[int64]bool, minID uint64, quick bool) {
	g, err := newAggregator(r, postsDB, commentsDB, votesDB, minID)
	if err != nil {
		r.errorf("open iterators: %v", err)
		return
	}
	defer g.close()
	posts, err := postsDB.NewIter(idIterBounds('m', minID))
	if err != nil {
		r.errorf("posts.pebble: %v", err)
		return
	}
	defer posts.Close()
	var bodies *idCursor
	if quick {
		if bodies, err = newIDCursorFrom(postsDB, 'p', minID); err != nil {
			r.errorf("posts.pebble bodies: %v", err)
			return
		}
		defer func() {
			if err := bodies.close(); err != nil {
				r.errorf("read post bodies: %v", err)
			}
		}()
	}
	missing := 0
	for posts.First(); posts.Valid(); posts.Next() {
		id, err := model.DecodePostIDKey(posts.Key())
		if err != nil {
			continue
		}
		r.Posts++
		a := g.of(id)
		if bodies != nil {
			checkPostBody(r, bodies, id)
		}
		if s := a.stored; s.Upvotes != a.upvotes || s.Downvotes != a.downvotes || s.NumComments != a.comments {
			r.warn("counter record differs from comments/votes", id, fmt.Sprintf("record=%d/%d/%d pebble=%d/%d/%d", s.Upvotes, s.Downvotes, s.NumComments, a.upvotes, a.downvotes, a.comments))
		}
		if communities != nil {
			cid, _ := strconv.ParseInt(model.DecodeRFC822PostMeta(posts.Value())["CommunityID"], 10, 64)
			if !communities[cid] {
				if missing < maxVerifyExamples {
					r.errorf("post %d refers to community #%d, which is not in communities.db", id, cid)
				}
				missing++
			}
		}
	}
	if err := posts.Error(); err != nil {
		r.errorf("read posts: %v", err)
	}
	if missing > maxVerifyExamples {
		r.errorf("... %d references to unknown communities in total", missing)
	}
	g.of(^uint64(0))
	if !quick {
		checkPostBodies(r, postsDB)
	}
}

func compareMetaWithPebble(r *VerifyReport, meta *sql.DB, postsDB, commentsDB, votesDB *pebble.DB, communities map[int64]bool, minID uint64, quick bool) {
	posts, err := postsDB.NewIter(idIterBounds('m', minID))
	if err != nil {
		r.errorf("posts.pebble: %v", err)
		return
	}
	defer posts.Close()
	g, err := newAggregator(r, postsDB, commentsDB, votesDB, minID)
	if err != nil {
		r.errorf("open iterators: %v", err)
		return
	}
	defer g.close()
	aggregate := g.of
	var bodies *idCursor
	if quick {
		if bodies, err = newIDCursorFrom(postsDB, 'p', minID); err != nil {
			r.errorf("posts.pebble bodies: %v", err)
			return
		}
		defer func() {
			if err := bodies.close(); err != nil {
				r.errorf("read post bodies: %v", err)
			}
		}()
	}

	rows, err := meta.Query(`SELECT id, parent_id, community_id, post_file, title, creator_name, creator_id, created_at, modified,
		filemode, money, upvotes, downvotes, num_comments, num_crossposts, deleted, removed, deleted_at, mod_person_name, mod_person_id, reason
		FROM posts WHERE id >= ? ORDER BY id`, minID)
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
			if err := rows.Scan(&row.id, &row.parentID, &row.communityID, &row.postFile, &row.title, &row.creatorName, &row.creatorID,
				&row.createdAt, &row.modified, &row.filemode, &row.money, &row.upvotes, &row.downvotes, &row.numComments, &row.crossposts,
				&row.deleted, &row.removed, &row.deletedAt, &row.modPersonName, &row.modPersonID, &row.reason); err != nil {
				r.errorf("scan meta.sqlite3: %v", err)
				haveRow = false
			}
		}
	}
	nextRow()
	posts.First()
	missingComm := 0
	for posts.Valid() || haveRow {
		var pid uint64
		havePost := false
		if posts.Valid() {
			if id, err := model.DecodePostIDKey(posts.Key()); err == nil {
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
			r.warn("post only in meta.sqlite3", row.id, fmt.Sprintf("#%d/%s", row.communityID, row.postFile))
			nextRow()
		default:
			r.Posts++
			if bodies != nil {
				checkPostBody(r, bodies, pid)
			}
			if quick && communities != nil && !communities[row.communityID] {
				if missingComm < maxVerifyExamples {
					r.errorf("post %d refers to community #%d, which is not in communities.db", row.id, row.communityID)
				}
				missingComm++
			}
			comparePost(r, &row, posts.Value(), aggregate(pid))
			posts.Next()
			nextRow()
		}
	}
	if missingComm > maxVerifyExamples {
		r.errorf("... %d references to unknown communities in total", missingComm)
	}
	if err := errors.Join(posts.Error(), rows.Err()); err != nil {
		r.errorf("read posts: %v", err)
	}
	// Drain the remaining records so their checksums are verified as well.
	aggregate(^uint64(0))
	if quick {
		return
	}
	checkPostBodies(r, postsDB)

	var metaCrossposts, pebbleCrossposts int
	_ = meta.QueryRow("SELECT COUNT(*) FROM crossposts").Scan(&metaCrossposts)
	if n, err := scanAll(postsDB, &pebble.IterOptions{LowerBound: []byte{'x'}, UpperBound: []byte{'y'}}); err == nil {
		pebbleCrossposts = n
	}
	if metaCrossposts != pebbleCrossposts {
		r.warn("crossposts table size differs", 0, fmt.Sprintf("meta.sqlite3=%d pebble=%d", metaCrossposts, pebbleCrossposts))
	}
}

// checkCommunityRefs reports posts and crossposts whose community is not in
// the registry. The registry is copied after the posts, so this means a
// restored service could not name, route or list them.
func checkCommunityRefs(r *VerifyReport, meta *sql.DB, communities map[int64]bool) {
	for _, q := range []struct{ what, query string }{
		{"post", "SELECT id, community_id FROM posts"},
		{"crosspost of post", "SELECT source_post_id, target_community_id FROM crossposts"},
	} {
		rows, err := meta.Query(q.query)
		if err != nil {
			r.errorf("query meta.sqlite3: %v", err)
			return
		}
		missing := 0
		for rows.Next() {
			var id, cid int64
			if err := rows.Scan(&id, &cid); err != nil {
				r.errorf("scan meta.sqlite3: %v", err)
				break
			}
			if !communities[cid] {
				if missing < maxVerifyExamples {
					r.errorf("%s %d refers to community #%d, which is not in communities.db", q.what, id, cid)
				}
				missing++
			}
		}
		rows.Close()
		if missing > maxVerifyExamples {
			r.errorf("... %d references to unknown communities in total", missing)
		}
	}
}

func comparePost(r *VerifyReport, row *metaRow, val []byte, a postAggregate) {
	h := model.DecodeRFC822PostMeta(val)
	del := model.DeletionFromRFC822(h)
	num := func(k string) int64 { v, _ := strconv.ParseInt(h[k], 10, 64); return v }
	createdAt, modified := num("CreatedAt"), num("Modified")
	if modified == 0 {
		modified = createdAt
	}
	modified = max(modified, a.commentModified, a.stored.Modified)

	diff := func(kind string, sqlite, pebble any) {
		r.warn(kind, row.id, fmt.Sprintf("meta.sqlite3=%v pebble=%v", sqlite, pebble))
	}
	if row.title != h["Title"] {
		// Older versions changed titles (UpdatePostTitle) only in SQLite.
		diff("title", row.title, h["Title"])
	} else if row.modified != modified {
		diff("modified", row.modified, modified)
	}
	if row.communityID != num("CommunityID") || row.postFile != h["PostFile"] || row.creatorName != h["CreatorName"] ||
		row.parentID != uint64(num("ParentID")) || row.creatorID != int64(model.ParsePersonID(h["CreatorID"])) || row.createdAt != createdAt {
		diff("identity fields", fmt.Sprintf("#%d/%s by %s(%d) at %d parent %d", row.communityID, row.postFile, row.creatorName, row.creatorID, row.createdAt, row.parentID),
			fmt.Sprintf("#%s/%s by %s(%s) at %d parent %s", h["CommunityID"], h["PostFile"], h["CreatorName"], h["CreatorID"], createdAt, h["ParentID"]))
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
	if s := a.stored; s.Upvotes != row.upvotes || s.Downvotes != row.downvotes || s.NumComments != row.numComments {
		// What a rebuild takes; flushed with SQLite, so equal in a backup.
		diff("counter record", fmt.Sprintf("%d/%d/%d", row.upvotes, row.downvotes, row.numComments), fmt.Sprintf("%d/%d/%d", s.Upvotes, s.Downvotes, s.NumComments))
	}
	if row.crossposts != a.crossposts {
		diff("num_crossposts", row.crossposts, a.crossposts)
	}
}
