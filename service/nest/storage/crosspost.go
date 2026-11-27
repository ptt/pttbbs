package storage

// Crossposts known from the copies. A copy (轉錄) names the post it was
// copied from in its header; its CrosspostOrigin is kept with the copy (a
// 'w' record and a bbs_crosspost_origins row), and the crosspost record
// itself, as everywhere, with the source post ('x' records), which may be
// in another engine or not imported yet. Linking adds that record once the
// source is there, or completes the record that the cplog line of the
// source made (target file "-", time to the minute) instead of adding a
// second one. Origins stay after linking, so that a source imported again
// (with new IDs) gets its records back; an origin whose source is not
// found (deleted) is pending.

import (
	"database/sql"
	"errors"
	"fmt"
	"log"
	"strings"

	"github.com/cockroachdb/pebble"

	"pttbbs/nest/model"
)

// crosspostMergeWindow is how far apart a cplog record and the copy it
// tells of may be dated: the year of a cplog line is guessed.
const crosspostMergeWindow = 86400

const insertOriginSQL = `INSERT OR REPLACE INTO bbs_crosspost_origins
	(target_post_id, source_community, source_post_file, creator_name, creator_id, created_at) VALUES (?, ?, ?, ?, ?, ?)`

func originArgs(o *model.CrosspostOrigin) []any {
	return []any{o.TargetPostID, o.SourceCommunity, o.SourcePostFile, o.CreatorName, o.CreatorID, o.CreatedAt}
}

// LinkStats reports what LinkCrossposts did.
type LinkStats struct {
	Origins int // origins checked
	Added   int // records added at their sources
	Merged  int // cplog records completed
	Linked  int // already linked
	Pending int // sources not found
}

func (s *LinkStats) count(r linkResult) {
	s.Origins++
	switch r {
	case linkAdded:
		s.Added++
	case linkMerged:
		s.Merged++
	case linkAlready:
		s.Linked++
	case linkPending:
		s.Pending++
	}
}

type linkResult int

const (
	linkPending linkResult = iota
	linkAdded
	linkMerged
	linkAlready
	linkGone // the copy is gone
)

// engineSet is the engines to look for source posts in.
type engineSet interface {
	engineOf(community string) *Engine
	allEngines() []*Engine
}

func (e *Engine) engineOf(string) *Engine { return e }
func (e *Engine) allEngines() []*Engine   { return []*Engine{e} }

func (s *ShardedEngine) engineOf(community string) *Engine { return s.GetEngineByCommunity(community) }
func (s *ShardedEngine) allEngines() []*Engine             { return s.engines }

type crosspostEntry struct {
	seq uint32
	rec *model.CrosspostRecord
}

// crosspostEntries returns the crosspost records of a source post.
func (e *Engine) crosspostEntries(sourcePostID uint64) ([]crosspostEntry, error) {
	prefix := model.EncodeCrosspostPrefix(sourcePostID)
	it, err := e.postsDB.NewIter(&pebble.IterOptions{LowerBound: prefix, UpperBound: append(prefix, 0xff)})
	if err != nil {
		return nil, err
	}
	defer it.Close()
	var entries []crosspostEntry
	for it.First(); it.Valid(); it.Next() {
		_, seq, err := model.DecodeCrosspostKey(it.Key())
		if err != nil {
			continue
		}
		rec, err := model.DecodeRFC822Crosspost(it.Value())
		if err != nil {
			continue
		}
		entries = append(entries, crosspostEntry{seq: seq, rec: rec})
	}
	return entries, it.Error()
}

func lastCrosspostSeq(entries []crosspostEntry) uint32 {
	var last uint32
	for _, x := range entries {
		last = max(last, x.seq)
	}
	return last
}

func absDiff(a, b int64) int64 {
	if a > b {
		return a - b
	}
	return b - a
}

// knownCrosspost tells whether entries already tell of cp, as for a post
// imported again: the same target post, or a cplog record on either side
// within crosspostMergeWindow.
func knownCrosspost(entries []crosspostEntry, cp *model.CrosspostRecord) bool {
	for _, x := range entries {
		if x.rec.TargetCommunityID != cp.TargetCommunityID {
			continue
		}
		if x.rec.TargetPostFile == cp.TargetPostFile && cp.TargetPostFile != "-" {
			return true
		}
		if (x.rec.TargetPostFile == "-" || cp.TargetPostFile == "-") && absDiff(x.rec.CreatedAt, cp.CreatedAt) <= crosspostMergeWindow {
			return true
		}
	}
	return false
}

// crosspostOrigin returns the origin of post id, or nil if it is no copy.
func (e *Engine) crosspostOrigin(id uint64) (*model.CrosspostOrigin, error) {
	val, closer, err := e.postsDB.Get(model.EncodeCrosspostOriginKey(id))
	if errors.Is(err, pebble.ErrNotFound) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	defer closer.Close()
	return model.DecodeRFC822CrosspostOrigin(val)
}

// deleteCrosspostOrigin forgets the origin of post id.
func (e *Engine) deleteCrosspostOrigin(id uint64) error {
	e.metaWriteMu.Lock()
	defer e.metaWriteMu.Unlock()
	if err := e.postsDB.Delete(model.EncodeCrosspostOriginKey(id), pebble.NoSync); err != nil {
		return err
	}
	_, err := e.metaDB.Exec(`DELETE FROM bbs_crosspost_origins WHERE target_post_id = ?`, id)
	return err
}

// sourcePostID finds the post file of community. An article ID of a post
// file without a hex suffix ("M.<stamp>.A") gives ".A.000".
func (e *Engine) sourcePostID(community, file string) (uint64, bool) {
	cid, ok := e.reg.Lookup(community)
	if !ok {
		return 0, false
	}
	for _, f := range []string{file, strings.TrimSuffix(file, ".000")} {
		var id uint64
		if e.metaDB.QueryRow(`SELECT id FROM posts WHERE community_id = ? AND post_file = ?`, cid, f).Scan(&id) == nil {
			return id, true
		}
		if !strings.HasSuffix(file, ".A.000") {
			break
		}
	}
	return 0, false
}

// linkOrigin makes sure the source of the copy o (a post of et) has the
// crosspost record of it.
func linkOrigin(set engineSet, et *Engine, o *model.CrosspostOrigin) (linkResult, error) {
	var tcid int64
	var tfile string
	err := et.metaDB.QueryRow(`SELECT community_id, post_file FROM posts WHERE id = ?`, o.TargetPostID).Scan(&tcid, &tfile)
	if errors.Is(err, sql.ErrNoRows) {
		return linkGone, et.deleteCrosspostOrigin(o.TargetPostID)
	}
	if err != nil {
		return linkPending, err
	}
	es := set.engineOf(o.SourceCommunity)
	srcID, ok := es.sourcePostID(o.SourceCommunity, o.SourcePostFile)
	if !ok || srcID == o.TargetPostID {
		return linkPending, nil
	}
	return es.addCopyCrosspost(srcID, tcid, tfile, o)
}

// addCopyCrosspost records that post srcID was copied to the post file
// tfile of community tcid as o tells: unless it is recorded already, it
// completes the nearest cplog record of the same target community within
// crosspostMergeWindow, or else adds a record.
func (e *Engine) addCopyCrosspost(srcID uint64, tcid int64, tfile string, o *model.CrosspostOrigin) (linkResult, error) {
	e.metaWriteMu.Lock()
	defer e.metaWriteMu.Unlock()
	entries, err := e.crosspostEntries(srcID)
	if err != nil {
		return linkPending, err
	}
	best := -1
	for i, x := range entries {
		if x.rec.TargetCommunityID != tcid {
			continue
		}
		if x.rec.TargetPostFile == tfile {
			// SQLite may have lost it (clearing the target community).
			return linkAlready, e.ensureCrosspostRow(x.rec, len(entries))
		}
		d := absDiff(x.rec.CreatedAt, o.CreatedAt)
		if x.rec.TargetPostFile == "-" && d <= crosspostMergeWindow &&
			(best < 0 || d < absDiff(entries[best].rec.CreatedAt, o.CreatedAt)) {
			best = i
		}
	}

	if best >= 0 {
		rec := entries[best].rec
		oldCreatedAt := rec.CreatedAt
		rec.SourcePostID = srcID
		rec.TargetPostFile = tfile
		rec.CreatedAt = o.CreatedAt
		if rec.CreatorName == "" {
			rec.CreatorName, rec.CreatorID = o.CreatorName, o.CreatorID
		}
		if err := e.postsDB.Set(model.EncodeCrosspostKey(srcID, entries[best].seq), model.EncodeRFC822Crosspost(rec), pebble.Sync); err != nil {
			return linkPending, err
		}
		res, err := e.metaDB.Exec(`UPDATE crossposts SET target_post_file = ?, created_at = ?, creator_name = ?, creator_id = ?
			WHERE id = (SELECT id FROM crossposts WHERE source_post_id = ? AND target_community_id = ? AND target_post_file = '-' AND created_at = ? LIMIT 1)`,
			tfile, rec.CreatedAt, rec.CreatorName, rec.CreatorID, srcID, tcid, oldCreatedAt)
		if err != nil {
			return linkPending, err
		}
		if n, _ := res.RowsAffected(); n == 0 {
			if err := e.ensureCrosspostRow(rec, len(entries)); err != nil {
				return linkPending, err
			}
		}
		return linkMerged, nil
	}

	rec := &model.CrosspostRecord{
		SourcePostID:      srcID,
		TargetCommunityID: tcid,
		TargetCommunity:   e.reg.Name(tcid),
		TargetPostFile:    tfile,
		CreatorName:       o.CreatorName,
		CreatorID:         o.CreatorID,
		CreatedAt:         o.CreatedAt,
	}
	if err := e.postsDB.Set(model.EncodeCrosspostKey(srcID, lastCrosspostSeq(entries)+1), model.EncodeRFC822Crosspost(rec), pebble.Sync); err != nil {
		return linkPending, err
	}
	return linkAdded, e.ensureCrosspostRow(rec, len(entries)+1)
}

// ensureCrosspostRow adds the SQLite row of rec if it is missing, and sets
// the crosspost count of its source post to count. metaWriteMu is held.
func (e *Engine) ensureCrosspostRow(rec *model.CrosspostRecord, count int) error {
	var one int
	err := e.metaDB.QueryRow(`SELECT 1 FROM crossposts WHERE source_post_id = ? AND target_community_id = ? AND target_post_file = ? LIMIT 1`,
		rec.SourcePostID, rec.TargetCommunityID, rec.TargetPostFile).Scan(&one)
	if errors.Is(err, sql.ErrNoRows) {
		_, err = e.metaDB.Exec(`INSERT INTO crossposts (source_post_id, target_community_id, target_post_file, creator_name, creator_id, created_at, bbs_exempt)
			VALUES (?, ?, ?, ?, ?, ?, ?)`, rec.SourcePostID, rec.TargetCommunityID, rec.TargetPostFile, rec.CreatorName, rec.CreatorID, rec.CreatedAt, boolToInt(rec.BBSExempt))
	}
	if err != nil {
		return err
	}
	_, err = e.metaDB.Exec(`UPDATE posts SET num_crossposts = ? WHERE id = ? AND num_crossposts != ?`, count, rec.SourcePostID, count)
	return err
}

// originsBySource returns the origins in e of copies of the given post
// files of community.
func (e *Engine) originsBySource(community string, files []string) ([]*model.CrosspostOrigin, error) {
	var names []any
	for _, f := range files {
		names = append(names, f)
		if strings.HasSuffix(f, ".A") {
			names = append(names, f+".000")
		}
	}
	var origins []*model.CrosspostOrigin
	const chunk = 500
	for off := 0; off < len(names); off += chunk {
		part := names[off:min(off+chunk, len(names))]
		rows, err := e.metaDB.Query(`SELECT target_post_id, source_community, source_post_file, creator_name, creator_id, created_at
			FROM bbs_crosspost_origins WHERE source_community = ? COLLATE NOCASE AND source_post_file IN (`+
			strings.TrimSuffix(strings.Repeat("?, ", len(part)), ", ")+`)`, append([]any{community}, part...)...)
		if err != nil {
			return nil, err
		}
		got, err := scanOrigins(rows)
		if err != nil {
			return nil, err
		}
		origins = append(origins, got...)
	}
	return origins, nil
}

func scanOrigins(rows *sql.Rows) ([]*model.CrosspostOrigin, error) {
	defer rows.Close()
	var origins []*model.CrosspostOrigin
	for rows.Next() {
		o := &model.CrosspostOrigin{}
		if err := rows.Scan(&o.TargetPostID, &o.SourceCommunity, &o.SourcePostFile, &o.CreatorName, &o.CreatorID, &o.CreatedAt); err != nil {
			return nil, err
		}
		origins = append(origins, o)
	}
	return origins, rows.Err()
}

// linkImported links what a batch just imported into et brings: the
// copies in it, and the copies (in any engine) of the posts in it. Errors
// are logged, as the posts are imported already; LinkCrossposts retries.
func linkImported(set engineSet, et *Engine, batch []*ImportedPostData) LinkStats {
	var st LinkStats
	link := func(eng *Engine, o *model.CrosspostOrigin) {
		r, err := linkOrigin(set, eng, o)
		if err != nil {
			log.Printf("[nest.svc] link crosspost of post %d: %v", o.TargetPostID, err)
		}
		st.count(r)
	}
	files := make(map[string][]string)
	for _, item := range batch {
		if o := item.Origin; o != nil && o.TargetPostID != 0 {
			link(et, o)
		}
		if p := item.Post; p.ID != 0 && p.Community != "" {
			files[p.Community] = append(files[p.Community], p.PostFile)
		}
	}
	for community, fs := range files {
		for _, eng := range set.allEngines() {
			origins, err := eng.originsBySource(community, fs)
			if err != nil {
				log.Printf("[nest.svc] find copies of %s: %v", community, err)
				continue
			}
			for _, o := range origins {
				link(eng, o)
			}
		}
	}
	return st
}

// linkAll links every origin of the engines in set.
func linkAll(set engineSet) (LinkStats, error) {
	var st LinkStats
	for _, et := range set.allEngines() {
		var last uint64
		for {
			rows, err := et.metaDB.Query(`SELECT target_post_id, source_community, source_post_file, creator_name, creator_id, created_at
				FROM bbs_crosspost_origins WHERE target_post_id > ? ORDER BY target_post_id LIMIT 5000`, last)
			if err != nil {
				return st, err
			}
			origins, err := scanOrigins(rows)
			if err != nil {
				return st, err
			}
			if len(origins) == 0 {
				break
			}
			for _, o := range origins {
				r, err := linkOrigin(set, et, o)
				if err != nil {
					return st, fmt.Errorf("link crosspost of post %d: %w", o.TargetPostID, err)
				}
				st.count(r)
			}
			last = origins[len(origins)-1].TargetPostID
		}
	}
	return st, nil
}

// LinkCrossposts adds the crosspost records of every copy whose source is
// there now (see linkOrigin), e.g. after importing all boards.
func (e *Engine) LinkCrossposts() (LinkStats, error) { return linkAll(e) }

// LinkCrossposts links the copies of every engine to their sources in any.
func (s *ShardedEngine) LinkCrossposts() (LinkStats, error) { return linkAll(s) }
