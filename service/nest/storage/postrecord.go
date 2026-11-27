package storage

import (
	"errors"
	"fmt"

	"github.com/cockroachdb/pebble"

	"pttbbs/nest/model"
)

// The records of a post in posts.pebble (see model.EncodePostMetaKey): the
// metadata, the body and the counters. Writers that change only the metadata
// leave the body alone, and the counters follow meta.sqlite3 (syncCounts).

// putPost writes the metadata and the body of post p.
func putPost(w pebble.Writer, p *model.Post) error {
	if err := putPostMeta(w, p); err != nil {
		return err
	}
	return putPostBody(w, p)
}

// putPostMeta writes the metadata of post p.
func putPostMeta(w pebble.Writer, p *model.Post) error {
	return w.Set(model.EncodePostMetaKey(p.ID), model.EncodeRFC822PostMeta(p), pebble.NoSync)
}

// putPostBody writes the body of post p.
func putPostBody(w pebble.Writer, p *model.Post) error {
	return w.Set(model.EncodePostKey(p.ID), []byte(p.Content), pebble.NoSync)
}

// putPostCounts writes the counters of post id; all zero deletes them.
func putPostCounts(w pebble.Writer, id uint64, c model.PostCounts) error {
	if c == (model.PostCounts{}) {
		return w.Delete(model.EncodePostCountsKey(id), pebble.NoSync)
	}
	return w.Set(model.EncodePostCountsKey(id), model.EncodePostCounts(c), pebble.NoSync)
}

// deletePostRecords deletes the metadata, body and counters of post id.
func deletePostRecords(w pebble.Writer, id uint64) error {
	return errors.Join(
		w.Delete(model.EncodePostMetaKey(id), pebble.NoSync),
		w.Delete(model.EncodePostKey(id), pebble.NoSync),
		w.Delete(model.EncodePostCountsKey(id), pebble.NoSync))
}

// readPostRecord returns the metadata headers and the body of post id, or
// pebble.ErrNotFound.
func (e *Engine) readPostRecord(id uint64) (map[string]string, string, error) {
	val, closer, err := e.postsDB.Get(model.EncodePostMetaKey(id))
	if err != nil {
		return nil, "", err
	}
	meta := model.DecodeRFC822PostMeta(val)
	closer.Close()
	val, closer, err = e.postsDB.Get(model.EncodePostKey(id))
	if err != nil {
		return nil, "", err
	}
	body := string(val)
	closer.Close()
	return meta, body, nil
}

// syncCounts copies the counters of posts ids from meta.sqlite3 into their
// 'n' records. The caller holds metaWriteMu, so no flush can change them in
// between; a counter record is thus as current as SQLite after each flush.
func (e *Engine) syncCounts(ids ...uint64) error {
	if len(ids) == 0 {
		return nil
	}
	stmt, err := e.metaDB.Prepare("SELECT upvotes, downvotes, num_comments, modified FROM posts WHERE id = ?")
	if err != nil {
		return err
	}
	defer stmt.Close()
	batch := e.postsDB.NewBatch()
	defer batch.Close()
	for _, id := range ids {
		var c model.PostCounts
		if err := stmt.QueryRow(id).Scan(&c.Upvotes, &c.Downvotes, &c.NumComments, &c.Modified); err != nil {
			continue // Purged meanwhile.
		}
		if err := putPostCounts(batch, id, c); err != nil {
			return err
		}
	}
	if err := batch.Commit(pebble.NoSync); err != nil {
		return fmt.Errorf("write post counts: %w", err)
	}
	return nil
}
