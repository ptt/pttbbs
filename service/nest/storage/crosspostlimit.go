package storage

// Crosspost limits (PTT's rules against crosspost spam; Lemmy, Reddit and
// Discourse have none like them, so the names are bbs_). Copies of copies
// count as copies of the original, found through the records and the
// origins (up to maxCrosspostHops), and the family of an original is every
// copy under it:
//  1. one copy per board, for good, and none back to the original's board;
//  2. a person crossposts an original to at most PersonMax boards, and
//  3. an original is crossposted at most PostMax times, within Window.
// Exempt crossposts (by a moderator of the target board, of a post by the
// site staff, or of an original in an exempt board) skip 2 and 3 and are
// not counted by them.

import (
	"database/sql"
	"errors"
	"fmt"
	"strings"
	"time"

	"pttbbs/nest/model"
)

// maxCrosspostHops bounds how far copies of copies are followed.
const maxCrosspostHops = 8

// Names of the errors of CheckCrosspost.
const (
	ErrNameAlreadyCrossposted = "bbs_already_crossposted"
	ErrNameCrosspostBack      = "bbs_crosspost_back_to_origin"
	ErrNameCrosspostPerson    = "bbs_crosspost_person_limit"
	ErrNameCrosspostPost      = "bbs_crosspost_post_limit"
)

// CrosspostLimitError is why a crosspost may not be made; Limit is the
// number that was reached, for the message (0 for the per-board rules).
type CrosspostLimitError struct {
	Name  string
	Limit int
}

func (e *CrosspostLimitError) Error() string {
	if e.Limit > 0 {
		return fmt.Sprintf("%s %d", e.Name, e.Limit)
	}
	return e.Name
}

// CrosspostLimits are the limits of rules 2 and 3 (0: none).
type CrosspostLimits struct {
	Window       int64 // seconds
	PersonMax    int
	PostMax      int
	ExemptBoards []string // originals there skip rules 2 and 3
}

// ExemptBoard tells whether originals in board skip rules 2 and 3.
func (l CrosspostLimits) ExemptBoard(board string) bool {
	for _, b := range l.ExemptBoards {
		if strings.EqualFold(b, board) {
			return true
		}
	}
	return false
}

// CrosspostCheck is a crosspost about to be made.
type CrosspostCheck struct {
	SourceCommunity, SourcePostFile string
	TargetCommunity                 string
	CreatorName                     string
	CreatorID                       model.PersonID
	Exempt                          bool
	Now                             int64 // 0: now
}

// postAt is a post and the engine that has it.
type postAt struct {
	eng       *Engine
	id        uint64
	cid       int64
	community string
	file      string
}

func findPostAt(set engineSet, community, file string) (postAt, bool) {
	eng := set.engineOf(community)
	file = model.CanonicalPostFile(file)
	id, ok := eng.sourcePostID(community, file)
	if !ok {
		return postAt{}, false
	}
	return postAtID(eng, id)
}

func postAtID(eng *Engine, id uint64) (postAt, bool) {
	p := postAt{eng: eng, id: id}
	if eng.metaDB.QueryRow(`SELECT community_id, post_file FROM posts WHERE id = ?`, id).Scan(&p.cid, &p.file) != nil {
		return postAt{}, false
	}
	p.community = eng.reg.Name(p.cid)
	return p, true
}

// crosspostParent returns the post p was copied from: the source of a
// record naming p, or else the source its origin names.
func crosspostParent(set engineSet, p postAt) (postAt, bool) {
	for _, eng := range set.allEngines() {
		var src uint64
		err := eng.metaDB.QueryRow(`SELECT source_post_id FROM crossposts WHERE target_community_id = ? AND target_post_file = ? LIMIT 1`,
			p.cid, p.file).Scan(&src)
		if err == nil {
			return postAtID(eng, src)
		}
	}
	if o, err := p.eng.crosspostOrigin(p.id); err == nil && o != nil {
		return findPostAt(set, o.SourceCommunity, o.SourcePostFile)
	}
	return postAt{}, false
}

// resolveCrosspostRoot returns the original of the post file of community.
func resolveCrosspostRoot(set engineSet, community, file string) (postAt, error) {
	p, ok := findPostAt(set, community, file)
	if !ok {
		return postAt{}, fmt.Errorf("source post not found: %s/%s: %w", community, file, sql.ErrNoRows)
	}
	seen := map[uint64]bool{p.id: true}
	for i := 0; i < maxCrosspostHops; i++ {
		q, ok := crosspostParent(set, p)
		if !ok || seen[q.id] {
			break
		}
		seen[q.id] = true
		p = q
	}
	return p, nil
}

func crosspostRoot(set engineSet, community, file string) (string, string, error) {
	p, err := resolveCrosspostRoot(set, community, file)
	if err != nil {
		return "", "", err
	}
	return p.community, p.file, nil
}

func addCrosspost(set engineSet, srcCommunity, srcPostFile, targetCommunity, targetPostFile, creatorName string, creatorID model.PersonID, createdAt int64, exempt bool) (*model.CrosspostRecord, int, error) {
	root, err := resolveCrosspostRoot(set, srcCommunity, srcPostFile)
	if err != nil {
		return nil, 0, err
	}
	return root.eng.addCrosspostAt(root.id, targetCommunity, targetPostFile, creatorName, creatorID, createdAt, exempt)
}

// crosspostFamily returns the boards that the copies under root are in,
// and the records made since since.
func crosspostFamily(set engineSet, root postAt, since int64) (map[int64]bool, []*model.CrosspostRecord, error) {
	boards := make(map[int64]bool)
	var recent []*model.CrosspostRecord
	seen := map[uint64]bool{root.id: true}
	level := []postAt{root}
	for depth := 0; depth < maxCrosspostHops && len(level) > 0; depth++ {
		var next []postAt
		for _, p := range level {
			rows, err := p.eng.metaDB.Query(`SELECT target_community_id, target_post_file, creator_name, creator_id, created_at, bbs_exempt
				FROM crossposts WHERE source_post_id = ?`, p.id)
			if err != nil {
				return nil, nil, err
			}
			var recs []*model.CrosspostRecord
			for rows.Next() {
				r := &model.CrosspostRecord{SourcePostID: p.id}
				if err := rows.Scan(&r.TargetCommunityID, &r.TargetPostFile, &r.CreatorName, &r.CreatorID, &r.CreatedAt, &r.BBSExempt); err != nil {
					rows.Close()
					return nil, nil, err
				}
				recs = append(recs, r)
			}
			rows.Close()
			if err := rows.Err(); err != nil {
				return nil, nil, err
			}
			for _, r := range recs {
				boards[r.TargetCommunityID] = true
				if r.CreatedAt >= since {
					recent = append(recent, r)
				}
				if r.TargetPostFile == "-" {
					continue
				}
				if c, ok := findPostAt(set, p.eng.reg.Name(r.TargetCommunityID), r.TargetPostFile); ok && !seen[c.id] {
					seen[c.id] = true
					next = append(next, c)
				}
			}
		}
		level = next
	}
	return boards, recent, nil
}

// checkCrosspost applies the rules above to req.
func checkCrosspost(set engineSet, req CrosspostCheck, lim CrosspostLimits) error {
	root, err := resolveCrosspostRoot(set, req.SourceCommunity, req.SourcePostFile)
	if err != nil {
		return err
	}
	if strings.EqualFold(root.community, req.TargetCommunity) {
		return &CrosspostLimitError{Name: ErrNameCrosspostBack}
	}
	now := req.Now
	if now == 0 {
		now = time.Now().Unix()
	}
	boards, recent, err := crosspostFamily(set, root, now-lim.Window)
	if err != nil {
		return err
	}
	if tcid, ok := root.eng.reg.Lookup(req.TargetCommunity); ok && boards[tcid] {
		return &CrosspostLimitError{Name: ErrNameAlreadyCrossposted}
	}
	if req.Exempt || lim.ExemptBoard(root.community) {
		return nil
	}
	var person, total int
	for _, r := range recent {
		if r.BBSExempt {
			continue
		}
		total++
		if req.CreatorID != 0 && r.CreatorID != 0 {
			if r.CreatorID == req.CreatorID {
				person++
			}
		} else if strings.EqualFold(r.CreatorName, req.CreatorName) {
			person++
		}
	}
	if lim.PersonMax > 0 && person >= lim.PersonMax {
		return &CrosspostLimitError{Name: ErrNameCrosspostPerson, Limit: lim.PersonMax}
	}
	if lim.PostMax > 0 && total >= lim.PostMax {
		return &CrosspostLimitError{Name: ErrNameCrosspostPost, Limit: lim.PostMax}
	}
	return nil
}

// IsCrosspostLimit tells whether err is a CrosspostLimitError.
func IsCrosspostLimit(err error) bool {
	var le *CrosspostLimitError
	return errors.As(err, &le)
}

// CheckCrosspost tells whether a crosspost may be made (see checkCrosspost).
func (e *Engine) CheckCrosspost(req CrosspostCheck, lim CrosspostLimits) error {
	return checkCrosspost(e, req, lim)
}

// CrosspostRoot returns the original of a post (itself if it is none).
func (e *Engine) CrosspostRoot(community, postFile string) (string, string, error) {
	return crosspostRoot(e, community, postFile)
}
