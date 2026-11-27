package storage

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"

	"github.com/cockroachdb/pebble"

	"pttbbs/nest/model"
)

// postRef identifies a post to be moved by MergeCommunity.
type postRef struct {
	id       uint64
	postFile string
}

// mergeCommunity moves the posts of community from to community to. route
// returns the engine of a community ID, and all lists every engine (which
// may keep crossposts to the moved posts).
//
// Posts whose post file the target already has are left behind (and
// counted as collisions). When both communities live on the same engine,
// the posts simply change their community ID; otherwise they are copied to
// the engine of the target and purged from the source. Either way they keep
// their (global) post IDs, so replies keep their parents, and clients the
// IDs they know.
func mergeCommunity(reg *CommunityRegistry, from, to string, route func(int64) *Engine, all []*Engine) (moved, collisions int, err error) {
	fromID, ok := reg.Lookup(from)
	if !ok {
		return 0, 0, nil // nothing to move
	}
	toID, err := reg.Ensure(to)
	if err != nil {
		return 0, 0, err
	}
	if fromID == toID {
		return 0, 0, fmt.Errorf("cannot merge community %s into itself", from)
	}

	src, dst := route(fromID), route(toID)
	refs, collisions, err := src.planMerge(fromID, toID, dst)
	if err != nil {
		return 0, 0, err
	}
	if len(refs) == 0 {
		return 0, collisions, nil
	}

	files := make(map[string]bool, len(refs))
	for _, r := range refs {
		files[r.postFile] = true
	}
	for _, e := range all {
		if err := e.retargetCrossposts(fromID, toID, files); err != nil {
			return 0, collisions, err
		}
	}

	if src == dst {
		moved, err = src.moveLocal(refs, fromID, toID)
		return moved, collisions, err
	}

	for _, r := range refs {
		if err := src.moveToEngine(r.id, toID, dst); err != nil {
			return moved, collisions, fmt.Errorf("move post %s/%s failed: %w", from, r.postFile, err)
		}
		moved++
	}
	return moved, collisions, nil
}

// MergeCommunity moves the posts of community from to community to; see
// mergeCommunity.
func (e *Engine) MergeCommunity(from, to string) (int, int, error) {
	return mergeCommunity(e.reg, from, to, func(int64) *Engine { return e }, []*Engine{e})
}

// planMerge lists the posts of fromID (in e) to be moved to toID (in dst),
// skipping those whose post file toID already has.
func (e *Engine) planMerge(fromID, toID int64, dst *Engine) ([]postRef, int, error) {
	e.FlushDirtyDeltas()
	if dst != e {
		dst.FlushDirtyDeltas()
	}

	existing := make(map[string]bool)
	rows, err := dst.metaDB.Query(`SELECT post_file FROM posts WHERE community_id = ?`, toID)
	if err != nil {
		return nil, 0, err
	}
	for rows.Next() {
		var f string
		if err := rows.Scan(&f); err != nil {
			rows.Close()
			return nil, 0, err
		}
		existing[f] = true
	}
	rows.Close()
	if err := rows.Err(); err != nil {
		return nil, 0, err
	}

	rows, err = e.metaDB.Query(`SELECT id, post_file FROM posts WHERE community_id = ? ORDER BY id`, fromID)
	if err != nil {
		return nil, 0, err
	}
	defer rows.Close()
	var refs []postRef
	collisions := 0
	for rows.Next() {
		var r postRef
		if err := rows.Scan(&r.id, &r.postFile); err != nil {
			return nil, 0, err
		}
		if existing[r.postFile] {
			collisions++
			continue
		}
		refs = append(refs, r)
	}
	return refs, collisions, rows.Err()
}

// retargetCrossposts makes the crossposts to the given post files of fromID
// point to toID.
func (e *Engine) retargetCrossposts(fromID, toID int64, files map[string]bool) error {
	e.metaWriteMu.Lock()
	defer e.metaWriteMu.Unlock()

	tx, err := e.metaDB.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	stmt, err := tx.Prepare(`UPDATE crossposts SET target_community_id = ? WHERE target_community_id = ? AND target_post_file = ?`)
	if err != nil {
		return err
	}
	defer stmt.Close()
	for f := range files {
		if _, err := stmt.Exec(toID, fromID, f); err != nil {
			return err
		}
	}

	// The crosspost records in Pebble are keyed by their source post.
	iter, err := e.postsDB.NewIter(&pebble.IterOptions{
		LowerBound: []byte{'x'},
		UpperBound: []byte{'x' + 1},
	})
	if err != nil {
		return err
	}
	batch := e.postsDB.NewBatch()
	defer batch.Close()
	for iter.First(); iter.Valid(); iter.Next() {
		rec, err := model.DecodeRFC822Crosspost(iter.Value())
		if err != nil || rec.TargetCommunityID != fromID || !files[rec.TargetPostFile] {
			continue
		}
		rec.TargetCommunityID = toID
		_ = batch.Set(bytes.Clone(iter.Key()), model.EncodeRFC822Crosspost(rec), nil)
	}
	if err := iter.Close(); err != nil {
		return err
	}
	if err := batch.Commit(pebble.NoSync); err != nil {
		return err
	}
	return tx.Commit()
}

// copyPostHistory copies the revisions of post oldID (and of its comments)
// in src to post newID in e, setting the community of the post revisions
// to communityID. src and e may be the same engine, and oldID and newID
// the same post. The revisions of the comments stay as they are when both
// are the same.
func (e *Engine) copyPostHistory(src *Engine, oldID, newID uint64, communityID int64) error {
	batch := e.historyDB.NewBatch()
	defer batch.Close()

	prefix := model.EncodePostHistoryPrefix(oldID)
	iter, err := src.historyDB.NewIter(&pebble.IterOptions{
		LowerBound: prefix,
		UpperBound: append(prefix, 0xff),
	})
	if err != nil {
		return err
	}
	for iter.First(); iter.Valid(); iter.Next() {
		r, err := model.DecodeRFC822PostRevision(iter.Value())
		if err != nil {
			continue
		}
		r.PostID, r.CommunityID = newID, communityID
		_ = batch.Set(model.EncodePostHistoryKey(newID, r.Revision), model.EncodeRFC822PostRevision(r), nil)
	}
	if err := iter.Close(); err != nil {
		return err
	}

	if src != e || newID != oldID {
		cPrefix := model.EncodeCommentPrefix(oldID)
		cIter, err := src.commentsDB.NewIter(&pebble.IterOptions{
			LowerBound: cPrefix,
			UpperBound: append(cPrefix, 0xff),
		})
		if err != nil {
			return err
		}
		for cIter.First(); cIter.Valid(); cIter.Next() {
			_, seq, err := model.DecodeCommentKey(cIter.Key())
			if err != nil {
				continue
			}
			hPrefix := model.EncodeCommentHistoryPrefix(oldID, seq)
			hIter, err := src.historyDB.NewIter(&pebble.IterOptions{
				LowerBound: hPrefix,
				UpperBound: append(hPrefix, 0xff),
			})
			if err != nil {
				continue
			}
			for hIter.First(); hIter.Valid(); hIter.Next() {
				r, err := model.DecodeRFC822CommentRevision(hIter.Value())
				if err != nil {
					continue
				}
				r.PostID = newID
				_ = batch.Set(model.EncodeCommentHistoryKey(newID, r.Sequence, r.Revision), model.EncodeRFC822CommentRevision(r), nil)
			}
			hIter.Close()
		}
		if err := cIter.Close(); err != nil {
			return err
		}
	}
	return batch.Commit(pebble.NoSync)
}

// moveLocal moves posts of this engine to community toID by changing their
// community ID.
func (e *Engine) moveLocal(refs []postRef, fromID, toID int64) (int, error) {
	e.metaWriteMu.Lock()
	tx, err := e.metaDB.Begin()
	if err != nil {
		e.metaWriteMu.Unlock()
		return 0, err
	}
	stmt, err := tx.Prepare(`UPDATE posts SET community_id = ? WHERE id = ?`)
	if err != nil {
		tx.Rollback()
		e.metaWriteMu.Unlock()
		return 0, err
	}
	for _, r := range refs {
		if _, err := stmt.Exec(toID, r.id); err != nil {
			stmt.Close()
			tx.Rollback()
			e.metaWriteMu.Unlock()
			return 0, err
		}
	}
	stmt.Close()
	err = tx.Commit()
	e.metaWriteMu.Unlock()
	if err != nil {
		return 0, err
	}

	moved := 0
	for _, r := range refs {
		p, err := e.GetPost(r.id)
		if err != nil {
			return moved, err
		}
		if err := putPostMeta(e.postsDB, p); err != nil {
			return moved, err
		}
		if err := e.copyPostHistory(e, p.ID, p.ID, toID); err != nil {
			return moved, err
		}
		if e.separateCache() {
			newPath := e.CacheFilePath(p)
			if err := os.MkdirAll(filepath.Dir(newPath), 0755); err == nil {
				_ = os.Rename(filepath.Join(e.communityCacheDir(fromID), p.PostFile), newPath)
			}
		}
		moved++
	}
	return moved, nil
}

// moveToEngine copies the post id of this engine (with its comments, votes,
// revisions and crossposts) to community toID in dst, keeping its ID, and
// then purges it here. toID may be the community of the post, which then
// only changes engine.
func (e *Engine) moveToEngine(id uint64, toID int64, dst *Engine) error {
	p, err := e.GetPost(id)
	if err != nil {
		return err
	}
	comments, err := e.GetComments(id, 1, 0xffffffff)
	if err != nil {
		return err
	}
	votes, err := e.GetVotes(id)
	if err != nil {
		return err
	}
	var crossposts []*model.CrosspostRecord
	cpPrefix := model.EncodeCrosspostPrefix(id)
	cpIter, err := e.postsDB.NewIter(&pebble.IterOptions{
		LowerBound: cpPrefix,
		UpperBound: append(cpPrefix, 0xff),
	})
	if err != nil {
		return err
	}
	for cpIter.First(); cpIter.Valid(); cpIter.Next() {
		if rec, err := model.DecodeRFC822Crosspost(cpIter.Value()); err == nil {
			crossposts = append(crossposts, rec)
		}
	}
	if err := cpIter.Close(); err != nil {
		return err
	}

	moved := *p
	moved.CommunityID = toID
	moved.Community = ""
	numComments := p.NumComments
	if err := dst.ImportPostBatch([]*ImportedPostData{{
		Post:       &moved,
		Comments:   comments,
		Crossposts: crossposts,
		Votes:      votes,
	}}, ""); err != nil {
		return err
	}

	// ImportPostBatch counts every comment; keep the count as it was.
	if moved.NumComments != numComments {
		dst.metaWriteMu.Lock()
		if _, err := dst.metaDB.Exec(`UPDATE posts SET num_comments = ? WHERE id = ?`, numComments, moved.ID); err == nil {
			_ = dst.syncCounts(moved.ID)
		}
		dst.metaWriteMu.Unlock()
	}
	if err := dst.copyPostHistory(e, id, moved.ID, toID); err != nil {
		return err
	}
	if dst.separateCache() && !moved.DeletedOrRemoved() {
		if rendered, err := dst.RenderFullPostText(moved.ID, dst.cfg.IsBig5()); err == nil {
			path := dst.CacheFilePath(&moved)
			if err := os.MkdirAll(filepath.Dir(path), 0755); err == nil {
				_ = os.WriteFile(path, rendered, 0644)
			}
		}
	}
	// Moving within the community keeps the crossposts to the post: their
	// records belong to the source posts (which stay here) and still name
	// the post by its community and post file.
	sameCommunity := p.CommunityID == toID
	return e.removePostRecord(p, sameCommunity, dst.CacheFilePath(&moved) == e.CacheFilePath(p))
}

// moveCommunityPosts moves all posts of community id from this engine to
// dst, keeping their IDs, and returns how many it moved.
func (e *Engine) moveCommunityPosts(id int64, dst *Engine) (int, error) {
	e.FlushDirtyDeltas()
	rows, err := e.metaDB.Query(`SELECT id FROM posts WHERE community_id = ? ORDER BY id`, id)
	if err != nil {
		return 0, err
	}
	var ids []uint64
	for rows.Next() {
		var pid uint64
		if err := rows.Scan(&pid); err != nil {
			rows.Close()
			return 0, err
		}
		ids = append(ids, pid)
	}
	rows.Close()
	if err := rows.Err(); err != nil {
		return 0, err
	}
	for i, pid := range ids {
		if err := e.moveToEngine(pid, id, dst); err != nil {
			return i, fmt.Errorf("move post %d failed: %w", pid, err)
		}
	}
	return len(ids), nil
}

// MoveCommunity of a single engine only accepts its one shard (0).
func (e *Engine) MoveCommunity(name string, shard int) (int, error) {
	if _, ok := e.reg.Lookup(name); !ok {
		return 0, fmt.Errorf("%w: %s", ErrCommunityNotFound, name)
	}
	if shard != 0 {
		return 0, fmt.Errorf("no engine %d: there is only one engine", shard)
	}
	return 0, nil
}
