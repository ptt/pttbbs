package storage

// Disaster recovery: meta.sqlite3 is an index of posts.pebble. A rebuild
// reads only the small records of each post (metadata, counters and
// crossposts), not the bodies, comments or votes, so it takes minutes even
// for tens of millions of posts; RecountPosts checks the counters against
// the comments and votes when that is worth the hours it takes.

import (
	"database/sql"
	"encoding/binary"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/cockroachdb/pebble"

	"pttbbs/nest/model"
)

// sqlIndexes are the indexes initSQLiteSchema creates. A rebuild drops them
// while it fills the tables, as building them at the end is much faster.
var sqlIndexes = []string{
	"idx_posts_comm_id", "idx_posts_comm_del", "idx_posts_comm_file", "idx_posts_creator", "idx_posts_parent_id",
	"idx_crossposts_source", "idx_crossposts_target", "idx_bbs_crosspost_origins_source",
}

const rebuildPostColumns = `id, parent_id, community_id, post_file, title, creator_name, creator_id, created_at, modified, filemode,
	upvotes, downvotes, num_comments, num_crossposts, encoding, deleted, removed, deleted_at, mod_person_name, mod_person_id, reason, money, locked`

// RebuildSQLiteFromPebble replaces meta.sqlite3 with one built from what
// posts.pebble holds. It returns the number of posts (not deleted or
// removed) and of comments (tombstones included, per the counters).
//
// The new database is written to a file of its own without a journal, its
// indexes built at the end, and then put in place of the old one, which is
// several times faster than refilling the live one through its WAL. Nothing
// else may use the engine meanwhile (nest.svc -rebuild runs it alone).
func (e *Engine) RebuildSQLiteFromPebble() (postCount int, commentCount int, err error) {
	e.FlushDirtyDeltas()
	e.metaWriteMu.Lock()
	defer e.metaWriteMu.Unlock()

	path := filepath.Join(e.cfg.DataDir, "meta.sqlite3")
	tmp := path + ".rebuild"
	for _, f := range []string{tmp, tmp + "-journal"} {
		if err := os.Remove(f); err != nil && !os.IsNotExist(err) {
			return 0, 0, err
		}
	}
	db, err := sql.Open("sqlite", tmp+"?_pragma=journal_mode(OFF)&_pragma=synchronous(OFF)&_pragma=locking_mode(EXCLUSIVE)"+
		"&_pragma=temp_store(MEMORY)&_pragma=cache_size(-1048576)")
	if err != nil {
		return 0, 0, err
	}
	db.SetMaxOpenConns(1)
	postCount, commentCount, err = e.fillMetaDB(db)
	if cerr := db.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		os.Remove(tmp)
		return 0, 0, err
	}

	// Closing the old database checkpoints and removes its WAL; any WAL
	// left must still go, or it would be applied to the new file.
	if err := e.metaDB.Close(); err != nil {
		return 0, 0, fmt.Errorf("close meta.sqlite3: %w", err)
	}
	for _, f := range []string{path + "-wal", path + "-shm"} {
		if err := os.Remove(f); err != nil && !os.IsNotExist(err) {
			return 0, 0, err
		}
	}
	if err := os.Rename(tmp, path); err != nil {
		return 0, 0, err
	}
	if e.metaDB, err = openMetaDB(path); err != nil {
		return 0, 0, err
	}
	return postCount, commentCount, nil
}

// fillMetaDB fills the empty database db from posts.pebble.
func (e *Engine) fillMetaDB(db *sql.DB) (postCount int, commentCount int, err error) {
	start := time.Now()
	if err := initSQLiteSchema(db); err != nil {
		return 0, 0, err
	}
	for _, idx := range sqlIndexes {
		if _, err := db.Exec("DROP INDEX IF EXISTS " + idx); err != nil {
			return 0, 0, err
		}
	}
	tx, err := db.Begin()
	if err != nil {
		return 0, 0, err
	}
	defer tx.Rollback()

	crossposts, err := e.rebuildCrossposts(tx)
	if err != nil {
		return 0, 0, err
	}
	if err := e.rebuildCrosspostOrigins(tx); err != nil {
		return 0, 0, err
	}

	// One row per statement: binding costs this driver more than parsing,
	// and more per parameter the more a statement has.
	ins, err := tx.Prepare("INSERT INTO posts (" + rebuildPostColumns + ") VALUES (" +
		strings.TrimSuffix(strings.Repeat("?, ", strings.Count(rebuildPostColumns, ",")+1), ", ") + ")")
	if err != nil {
		return 0, 0, err
	}
	defer ins.Close()
	posts, err := e.postsDB.NewIter(&pebble.IterOptions{LowerBound: []byte{'m'}, UpperBound: []byte{'n'}})
	if err != nil {
		return 0, 0, err
	}
	defer posts.Close()
	counts, err := e.postsDB.NewIter(&pebble.IterOptions{LowerBound: []byte{'n'}, UpperBound: []byte{'o'}})
	if err != nil {
		return 0, 0, err
	}
	defer counts.Close()
	counts.First()

	for posts.First(); posts.Valid(); posts.Next() {
		id, err := model.DecodePostIDKey(posts.Key())
		if err != nil {
			continue
		}
		var c model.PostCounts
		for ; counts.Valid(); counts.Next() {
			cid, err := model.DecodePostIDKey(counts.Key())
			if err != nil || cid < id {
				continue // Malformed, or the counters of a purged post.
			}
			if cid == id {
				c, _ = model.DecodePostCounts(counts.Value())
			}
			break
		}
		meta := model.DecodeRFC822PostMeta(posts.Value())
		del := model.DeletionFromRFC822(meta)
		parentID, _ := strconv.ParseUint(meta["ParentID"], 10, 64)
		ctime, _ := strconv.ParseInt(meta["CreatedAt"], 10, 64)
		mtime, _ := strconv.ParseInt(meta["Modified"], 10, 64)
		if mtime == 0 {
			mtime = ctime
		}
		mtime = max(mtime, c.Modified) // Not ctime: .DIR dates may be a second older.
		filemode, _ := strconv.Atoi(meta["Filemode"])
		money, _ := strconv.Atoi(meta["Money"])
		communityID, _ := strconv.ParseInt(meta["CommunityID"], 10, 64)
		if _, err := ins.Exec(id, parentID, communityID, meta["PostFile"], meta["Title"], meta["CreatorName"], model.ParsePersonID(meta["CreatorID"]),
			ctime, mtime, filemode, c.Upvotes, c.Downvotes, c.NumComments, crossposts[id], "utf-8",
			boolToInt(del.Deleted), boolToInt(del.Removed), del.DeletedAt, del.ModPersonName, del.ModPersonID, del.Reason,
			money, boolToInt(meta["Locked"] == "true")); err != nil {
			return 0, 0, fmt.Errorf("insert post %d: %w", id, err)
		}
		if !del.DeletedOrRemoved() {
			postCount++
		}
		commentCount += c.NumComments
	}
	if err := posts.Error(); err != nil {
		return 0, 0, err
	}
	if err := counts.Error(); err != nil {
		return 0, 0, err
	}
	if err := tx.Commit(); err != nil {
		return 0, 0, err
	}
	log.Printf("[nest.svc] rebuild: %d posts inserted in %s", postCount, time.Since(start).Round(time.Second))
	if err := initSQLiteSchema(db); err != nil {
		return 0, 0, fmt.Errorf("create indexes: %w", err)
	}
	log.Printf("[nest.svc] rebuild: indexes created after %s", time.Since(start).Round(time.Second))
	return postCount, commentCount, nil
}

// rebuildCrossposts fills the crossposts table from the 'x' records and
// returns the number of crossposts of each source post.
func (e *Engine) rebuildCrossposts(tx *sql.Tx) (map[uint64]int, error) {
	stmt, err := tx.Prepare(`INSERT INTO crossposts (source_post_id, target_community_id, target_post_file, creator_name, creator_id, created_at)
		VALUES (?, ?, ?, ?, ?, ?)`)
	if err != nil {
		return nil, err
	}
	defer stmt.Close()
	it, err := e.postsDB.NewIter(&pebble.IterOptions{LowerBound: []byte{'x'}, UpperBound: []byte{'y'}})
	if err != nil {
		return nil, err
	}
	defer it.Close()
	counts := map[uint64]int{}
	for it.First(); it.Valid(); it.Next() {
		x, err := model.DecodeRFC822Crosspost(it.Value())
		if err != nil {
			continue
		}
		if _, err := stmt.Exec(x.SourcePostID, x.TargetCommunityID, x.TargetPostFile, x.CreatorName, x.CreatorID, x.CreatedAt); err != nil {
			return nil, err
		}
		counts[x.SourcePostID]++
	}
	return counts, it.Error()
}

// rebuildCrosspostOrigins fills bbs_crosspost_origins from the 'w' records.
func (e *Engine) rebuildCrosspostOrigins(tx *sql.Tx) error {
	stmt, err := tx.Prepare(insertOriginSQL)
	if err != nil {
		return err
	}
	defer stmt.Close()
	it, err := e.postsDB.NewIter(&pebble.IterOptions{LowerBound: []byte{'w'}, UpperBound: []byte{'x'}})
	if err != nil {
		return err
	}
	defer it.Close()
	for it.First(); it.Valid(); it.Next() {
		o, err := model.DecodeRFC822CrosspostOrigin(it.Value())
		if err != nil {
			continue
		}
		if _, err := stmt.Exec(originArgs(o)...); err != nil {
			return err
		}
	}
	return it.Error()
}

// RecountPosts counts the comments (tombstones included) and votes of every
// post again from comments.pebble and votes.pebble, and corrects the
// counters (and the modified time, for edited comments) in meta.sqlite3 and
// in the counter records where they differ. It reads every comment and
// vote, so it is slow; it returns how many posts it corrected.
func (e *Engine) RecountPosts() (int, error) {
	e.FlushDirtyDeltas()
	e.metaWriteMu.Lock()
	defer e.metaWriteMu.Unlock()

	type fix struct {
		id                 uint64
		up, down, comments int
		modified           int64
	}
	var fixes []fix
	err := func() error {
		comments, err := newIDCursor(e.commentsDB, 'c')
		if err != nil {
			return err
		}
		defer comments.close()
		votes, err := newIDCursor(e.votesDB, 'v')
		if err != nil {
			return err
		}
		defer votes.close()
		rows, err := e.metaDB.Query("SELECT id, upvotes, downvotes, num_comments, modified FROM posts ORDER BY id")
		if err != nil {
			return err
		}
		defer rows.Close()
		for rows.Next() {
			var f, have fix
			if err := rows.Scan(&have.id, &have.up, &have.down, &have.comments, &have.modified); err != nil {
				return err
			}
			f.id, f.modified = have.id, have.modified
			for cid, ok := comments.id(); ok && cid <= f.id; cid, ok = comments.id() {
				if k := comments.it.Key(); cid == f.id && len(k) == 13 {
					f.comments++
					if c, err := model.DecodeRFC822Comment(cid, binary.BigEndian.Uint32(k[9:13]), comments.it.Value()); err == nil {
						f.modified = max(f.modified, c.ModifiedAt)
					}
				}
				comments.it.Next()
			}
			for vid, ok := votes.id(); ok && vid <= f.id; vid, ok = votes.id() {
				if v := votes.it.Value(); vid == f.id && len(v) > 0 {
					switch v[0] {
					case 'U':
						f.up++
					case 'D':
						f.down++
					}
				}
				votes.it.Next()
			}
			if f != have {
				fixes = append(fixes, f)
			}
		}
		return rows.Err()
	}()
	if err != nil {
		return 0, err
	}

	tx, err := e.metaDB.Begin()
	if err != nil {
		return 0, err
	}
	defer tx.Rollback()
	stmt, err := tx.Prepare("UPDATE posts SET upvotes = ?, downvotes = ?, num_comments = ?, modified = ? WHERE id = ?")
	if err != nil {
		return 0, err
	}
	defer stmt.Close()
	ids := make([]uint64, len(fixes))
	for i, f := range fixes {
		if _, err := stmt.Exec(f.up, f.down, f.comments, f.modified, f.id); err != nil {
			return 0, err
		}
		ids[i] = f.id
	}
	if err := tx.Commit(); err != nil {
		return 0, err
	}
	return len(fixes), e.syncCounts(ids...)
}
