package storage

import (
	"fmt"
	"sort"

	"pttbbs/nest/model"
)

// PostSort orders a post listing, named after Lemmy's SortType.
type PostSort string

const (
	SortNew          PostSort = "New"          // newest first
	SortOld          PostSort = "Old"          // oldest first
	SortTopAll       PostSort = "TopAll"       // highest score first
	SortMostComments PostSort = "MostComments" // most comments first
)

// PostQuery selects posts to list, as Lemmy's GetPosts does.
type PostQuery struct {
	// CommunityIDs limits the listing to these communities; empty means
	// every community.
	CommunityIDs []int64
	Sort         PostSort
	// FeaturedFirst lists the posts pinned to the community
	// (model.FileBottom, Lemmy's featured_community) first.
	FeaturedFirst bool
	Limit, Offset int
}

// postOrder returns the ORDER BY clause of q and a function that compares
// two posts in that order (for merging the listings of several engines).
func (q PostQuery) postOrder() (string, func(a, b *model.Post) bool, error) {
	var clause string
	var less func(a, b *model.Post) bool
	switch q.Sort {
	case SortNew, "":
		clause = "created_at DESC, id DESC"
		less = func(a, b *model.Post) bool {
			if a.CreatedAt != b.CreatedAt {
				return a.CreatedAt > b.CreatedAt
			}
			return a.ID > b.ID
		}
	case SortOld:
		clause = "created_at, id"
		less = func(a, b *model.Post) bool {
			if a.CreatedAt != b.CreatedAt {
				return a.CreatedAt < b.CreatedAt
			}
			return a.ID < b.ID
		}
	case SortTopAll:
		clause = "upvotes - downvotes DESC, created_at DESC, id DESC"
		less = func(a, b *model.Post) bool {
			if a.NetScore() != b.NetScore() {
				return a.NetScore() > b.NetScore()
			}
			if a.CreatedAt != b.CreatedAt {
				return a.CreatedAt > b.CreatedAt
			}
			return a.ID > b.ID
		}
	case SortMostComments:
		clause = "num_comments DESC, created_at DESC, id DESC"
		less = func(a, b *model.Post) bool {
			if a.NumComments != b.NumComments {
				return a.NumComments > b.NumComments
			}
			if a.CreatedAt != b.CreatedAt {
				return a.CreatedAt > b.CreatedAt
			}
			return a.ID > b.ID
		}
	default:
		return "", nil, fmt.Errorf("unsupported sort: %s", q.Sort)
	}
	if q.FeaturedFirst {
		clause = fmt.Sprintf("(filemode & %d) != 0 DESC, ", model.FileBottom) + clause
		inner := less
		less = func(a, b *model.Post) bool {
			fa, fb := a.Filemode&model.FileBottom != 0, b.Filemode&model.FileBottom != 0
			if fa != fb {
				return fa
			}
			return inner(a, b)
		}
	}
	return clause, less, nil
}

// QueryPosts lists the posts (not deleted or removed) that q selects.
func (e *Engine) QueryPosts(q PostQuery) ([]*model.Post, error) {
	order, _, err := q.postOrder()
	if err != nil {
		return nil, err
	}
	if q.Limit <= 0 {
		q.Limit = 20
	}
	where := "deleted = 0 AND removed = 0"
	var args []any
	if len(q.CommunityIDs) > 0 {
		where += " AND community_id IN (?"
		args = append(args, q.CommunityIDs[0])
		for _, id := range q.CommunityIDs[1:] {
			where += ", ?"
			args = append(args, id)
		}
		where += ")"
	}
	args = append(args, q.Limit, max(q.Offset, 0))

	e.FlushDirtyDeltas()
	rows, err := e.metaDB.Query(`
		SELECT id, parent_id, community_id, post_file, title, creator_name, creator_id, created_at, modified, filemode, upvotes, downvotes, num_comments, num_crossposts, encoding, deleted, removed, deleted_at, mod_person_name, mod_person_id, reason, money, locked
		FROM posts WHERE `+where+` ORDER BY `+order+` LIMIT ? OFFSET ?`, args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var posts []*model.Post
	for rows.Next() {
		var p model.Post
		if err := rows.Scan(&p.ID, &p.ParentID, &p.CommunityID, &p.PostFile, &p.Title, &p.CreatorName, &p.CreatorID, &p.CreatedAt, &p.Modified, &p.Filemode, &p.Upvotes, &p.Downvotes, &p.NumComments, &p.NumCrossposts, &p.Encoding, &p.Deleted, &p.Removed, &p.DeletedAt, &p.ModPersonName, &p.ModPersonID, &p.Reason, &p.Money, &p.Locked); err != nil {
			return nil, err
		}
		if p.Modified == 0 {
			p.Modified = p.CreatedAt
		}
		e.fillCommunity(&p)
		posts = append(posts, &p)
	}
	return posts, rows.Err()
}

// QueryPosts lists the posts that q selects. A listing of one community is
// read from its engine; otherwise every engine lists its first
// Offset + Limit posts, which are merged.
func (s *ShardedEngine) QueryPosts(q PostQuery) ([]*model.Post, error) {
	if len(q.CommunityIDs) == 1 {
		return s.GetEngineByCommunityID(q.CommunityIDs[0]).QueryPosts(q)
	}
	_, less, err := q.postOrder()
	if err != nil {
		return nil, err
	}
	if q.Limit <= 0 {
		q.Limit = 20
	}
	offset := max(q.Offset, 0)
	sub := q
	sub.Offset, sub.Limit = 0, offset+q.Limit
	var all []*model.Post
	for _, e := range s.engines {
		posts, err := e.QueryPosts(sub)
		if err != nil {
			return nil, err
		}
		all = append(all, posts...)
	}
	sort.SliceStable(all, func(i, j int) bool { return less(all[i], all[j]) })
	if offset >= len(all) {
		return nil, nil
	}
	return all[offset:min(offset+q.Limit, len(all))], nil
}
