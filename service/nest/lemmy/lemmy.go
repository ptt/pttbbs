// Package lemmy serves nest.svc in the shapes of Lemmy's API (v3): the
// request and response types of its endpoints (GetPosts, GetPost,
// GetComments, GetComment, LockPost), and the views (PostView,
// CommentView) they return, built from nest.svc's storage.
//
// nest.svc speaks it over its socket as
//
//	API <method> <json_bytes>\n<request json>
//
// replying "OK <json_bytes>\n<response json>", or "ERR <error>" where error
// is a Lemmy error name (such as couldnt_find_post). An HTTP gateway for
// Lemmy clients maps /api/v3/post/list to API GetPosts, and so on.
//
// There is no user (no auth) yet, so it only serves what every visitor of
// the BBS may read: communities that are hidden (BRD_HIDE) or need a
// permission to read are treated as not existing, and so are their posts
// and comments. Lemmy fields about the user (my_vote, saved, read, ...)
// have their default values.
package lemmy

import (
	"encoding/json"
	"errors"
	"fmt"
	"time"

	"pttbbs/nest/model"
	"pttbbs/nest/storage"
)

// Error is a Lemmy error name, such as couldnt_find_post.
type Error string

func (e Error) Error() string { return string(e) }

const (
	ErrNotFound          Error = "not_found"
	ErrCouldntFindPost   Error = "couldnt_find_post"
	ErrCouldntFindCommun Error = "couldnt_find_community"
	ErrCouldntFindCommen Error = "couldnt_find_comment"
	ErrInvalidSort       Error = "invalid_default_post_listing_type"
	ErrBadRequest        Error = "invalid_body_field"
	ErrUnknownMethod     Error = "unknown_method"
)

// Lemmy limits listings to 50 items; PTT comment floors come in thousands.
const (
	maxPostLimit    = 50
	maxCommentLimit = 1000
)

// Service serves the API from a storage.
type Service struct {
	Storage storage.Storage
	// SiteURL (e.g. https://www.ptt.cc) makes the ActivityPub IDs (ap_id,
	// actor_id) of what is served; empty leaves them empty.
	SiteURL string
}

// Handle runs one API method on a JSON request and returns the JSON
// response.
func (s *Service) Handle(method string, req []byte) ([]byte, error) {
	var resp any
	var err error
	switch method {
	case "GetPosts":
		var r GetPosts
		if err = decode(req, &r); err == nil {
			resp, err = s.GetPosts(r)
		}
	case "GetPost":
		var r GetPost
		if err = decode(req, &r); err == nil {
			resp, err = s.GetPost(r)
		}
	case "GetComments":
		var r GetComments
		if err = decode(req, &r); err == nil {
			resp, err = s.GetComments(r)
		}
	case "GetComment":
		var r GetComment
		if err = decode(req, &r); err == nil {
			resp, err = s.GetComment(r)
		}
	case "LockPost":
		var r LockPost
		if err = decode(req, &r); err == nil {
			resp, err = s.LockPost(r)
		}
	default:
		return nil, ErrUnknownMethod
	}
	if err != nil {
		return nil, err
	}
	return json.Marshal(resp)
}

func decode(req []byte, v any) error {
	if len(req) == 0 {
		return nil
	}
	if err := json.Unmarshal(req, v); err != nil {
		return ErrBadRequest
	}
	return nil
}

// Timestamp formats a Unix time as Lemmy does.
func Timestamp(t int64) string {
	return time.Unix(t, 0).UTC().Format("2006-01-02T15:04:05.000000Z")
}

func optTimestamp(t, published int64) *string {
	if t == 0 || t == published {
		return nil
	}
	s := Timestamp(t)
	return &s
}

// ---------------------------------------------------------------------------
// Views
// ---------------------------------------------------------------------------

// Person is Lemmy's Person.
type Person struct {
	ID          int64   `json:"id"`
	Name        string  `json:"name"`
	DisplayName *string `json:"display_name,omitempty"`
	Banned      bool    `json:"banned"`
	Published   string  `json:"published"`
	Updated     *string `json:"updated,omitempty"`
	ActorID     string  `json:"actor_id"`
	Local       bool    `json:"local"`
	Deleted     bool    `json:"deleted"`
	BotAccount  bool    `json:"bot_account"`
	InstanceID  int     `json:"instance_id"`
}

// Community is Lemmy's Community.
type Community struct {
	ID                      int64   `json:"id"`
	Name                    string  `json:"name"`
	Title                   string  `json:"title"`
	Description             *string `json:"description,omitempty"`
	Removed                 bool    `json:"removed"`
	Published               string  `json:"published"`
	Updated                 *string `json:"updated,omitempty"`
	Deleted                 bool    `json:"deleted"`
	NSFW                    bool    `json:"nsfw"`
	ActorID                 string  `json:"actor_id"`
	Local                   bool    `json:"local"`
	Hidden                  bool    `json:"hidden"`
	PostingRestrictedToMods bool    `json:"posting_restricted_to_mods"`
	InstanceID              int     `json:"instance_id"`
	Visibility              string  `json:"visibility"`
}

// Post is Lemmy's Post.
type Post struct {
	ID                int64   `json:"id"`
	Name              string  `json:"name"`
	Body              *string `json:"body,omitempty"`
	CreatorID         int64   `json:"creator_id"`
	CommunityID       int64   `json:"community_id"`
	Removed           bool    `json:"removed"`
	Locked            bool    `json:"locked"`
	Published         string  `json:"published"`
	Updated           *string `json:"updated,omitempty"`
	Deleted           bool    `json:"deleted"`
	NSFW              bool    `json:"nsfw"`
	APID              string  `json:"ap_id"`
	Local             bool    `json:"local"`
	LanguageID        int     `json:"language_id"`
	FeaturedCommunity bool    `json:"featured_community"`
	FeaturedLocal     bool    `json:"featured_local"`

	// PTT extensions (ignored by Lemmy clients).
	BBSPostFile string `json:"bbs_post_file,omitempty"` // M.<stamp>.A.<xxx>
	BBSParentID int64  `json:"bbs_parent_id,omitempty"` // the post replied to
	BBSMarked   bool   `json:"bbs_marked,omitempty"`    // FILE_MARKED
}

// PostAggregates is Lemmy's PostAggregates.
type PostAggregates struct {
	PostID            int64  `json:"post_id"`
	Comments          int    `json:"comments"`
	Score             int    `json:"score"`
	Upvotes           int    `json:"upvotes"`
	Downvotes         int    `json:"downvotes"`
	Published         string `json:"published"`
	NewestCommentTime string `json:"newest_comment_time"`
}

// PostView is Lemmy's PostView.
type PostView struct {
	Post                       Post           `json:"post"`
	Creator                    Person         `json:"creator"`
	Community                  Community      `json:"community"`
	CreatorBannedFromCommunity bool           `json:"creator_banned_from_community"`
	Counts                     PostAggregates `json:"counts"`
	Subscribed                 string         `json:"subscribed"`
	Saved                      bool           `json:"saved"`
	Read                       bool           `json:"read"`
	CreatorBlocked             bool           `json:"creator_blocked"`
	MyVote                     *int           `json:"my_vote,omitempty"`
	UnreadComments             int            `json:"unread_comments"`
}

// Comment is Lemmy's Comment. Its ID is post_id << 16 | floor (see
// model.MakeCommentID): beyond int32, unlike Lemmy's, but exact in
// JavaScript.
type Comment struct {
	ID            int64   `json:"id"`
	CreatorID     int64   `json:"creator_id"`
	PostID        int64   `json:"post_id"`
	Content       string  `json:"content"`
	Removed       bool    `json:"removed"`
	Published     string  `json:"published"`
	Updated       *string `json:"updated,omitempty"`
	Deleted       bool    `json:"deleted"`
	APID          string  `json:"ap_id"`
	Local         bool    `json:"local"`
	Path          string  `json:"path"`
	Distinguished bool    `json:"distinguished"`
	LanguageID    int     `json:"language_id"`

	// PTT extensions (ignored by Lemmy clients).
	BBSFloor int    `json:"bbs_floor"` // the floor shown by mbbsd
	BBSType  string `json:"bbs_type"`  // push, boo, arrow or old
}

// CommentAggregates is Lemmy's CommentAggregates.
type CommentAggregates struct {
	CommentID  int64  `json:"comment_id"`
	Score      int    `json:"score"`
	Upvotes    int    `json:"upvotes"`
	Downvotes  int    `json:"downvotes"`
	Published  string `json:"published"`
	ChildCount int    `json:"child_count"`
}

// CommentView is Lemmy's CommentView.
type CommentView struct {
	Comment                    Comment           `json:"comment"`
	Creator                    Person            `json:"creator"`
	Post                       Post              `json:"post"`
	Community                  Community         `json:"community"`
	Counts                     CommentAggregates `json:"counts"`
	CreatorBannedFromCommunity bool              `json:"creator_banned_from_community"`
	Subscribed                 string            `json:"subscribed"`
	Saved                      bool              `json:"saved"`
	CreatorBlocked             bool              `json:"creator_blocked"`
	MyVote                     *int              `json:"my_vote,omitempty"`
}

// ---------------------------------------------------------------------------
// Building views
// ---------------------------------------------------------------------------

func (s *Service) url(kind string, id int64) string {
	if s.SiteURL == "" {
		return ""
	}
	return fmt.Sprintf("%s/%s/%d", s.SiteURL, kind, id)
}

// community returns the community id if it may be served.
func (s *Service) community(id int64) (model.Community, bool) {
	c, ok := s.Storage.Communities().Get(id)
	if !ok || c.DeletedOrRemoved() || c.Hidden || c.Level != 0 {
		return model.Community{}, false
	}
	return c, true
}

func (s *Service) communityView(c model.Community) Community {
	v := Community{
		ID:         c.ID,
		Name:       c.Name,
		Title:      c.Title,
		Removed:    c.Removed,
		Published:  Timestamp(c.Published),
		Updated:    optTimestamp(c.Updated, c.Published),
		Deleted:    c.Deleted,
		NSFW:       c.NSFW,
		ActorID:    s.url("c", c.ID),
		Local:      true,
		Hidden:     c.Hidden,
		InstanceID: 1,
		Visibility: "Public",
	}
	if c.Description != "" {
		v.Description = &c.Description
	}
	return v
}

// person returns the person shown as the creator of a post or comment.
// On anonymous boards (and for anonymous posts) only the shown name is
// known: the PersonID behind it must not be revealed.
func (s *Service) person(key model.PersonID, name string, anonymous bool) Person {
	reg := s.Storage.Communities()
	var p model.Person
	var err error
	if anonymous {
		p, err = reg.AnonymousPerson(name)
	} else {
		p, err = reg.Person(key, name)
	}
	if err != nil {
		return Person{Name: name, Local: true, InstanceID: 1, Published: Timestamp(0)}
	}
	v := Person{
		ID:         p.ID,
		Name:       p.Name,
		Published:  Timestamp(p.Published),
		Updated:    optTimestamp(p.Updated, p.Published),
		ActorID:    s.url("u", p.ID),
		Local:      true,
		Deleted:    p.Deleted,
		InstanceID: 1,
	}
	if p.DisplayName != "" {
		v.DisplayName = &p.DisplayName
	}
	return v
}

func (s *Service) postView(p *model.Post, c model.Community, withBody bool) PostView {
	anonymous := c.Anonymous || p.Filemode&model.FileAnonymous != 0
	creator := s.person(p.CreatorID, p.CreatorName, anonymous)
	post := Post{
		ID:                int64(p.ID),
		Name:              p.Title,
		CreatorID:         creator.ID,
		CommunityID:       c.ID,
		Removed:           p.Removed,
		Locked:            p.Locked,
		Published:         Timestamp(p.CreatedAt),
		Updated:           optTimestamp(p.Modified, p.CreatedAt),
		Deleted:           p.Deleted,
		NSFW:              c.NSFW,
		APID:              s.url("post", int64(p.ID)),
		Local:             true,
		FeaturedCommunity: p.Filemode&model.FileBottom != 0,
		BBSPostFile:       p.PostFile,
		BBSParentID:       int64(p.ParentID),
		BBSMarked:         p.Filemode&model.FileMarked != 0,
	}
	// The content of locked (PTT hides it), deleted and removed posts is
	// not shown.
	if withBody && !p.Locked && !p.DeletedOrRemoved() {
		body := p.Content
		post.Body = &body
	}
	return PostView{
		Post:      post,
		Creator:   creator,
		Community: s.communityView(c),
		Counts: PostAggregates{
			PostID:    int64(p.ID),
			Comments:  p.NumComments,
			Score:     p.NetScore(),
			Upvotes:   p.Upvotes,
			Downvotes: p.Downvotes,
			Published: Timestamp(p.CreatedAt),
			// nest.svc does not keep the time of the newest comment yet.
			NewestCommentTime: Timestamp(max(p.Modified, p.CreatedAt)),
		},
		Subscribed: "NotSubscribed",
	}
}

var bbsTypes = map[uint8]string{
	model.LegacyTypeOldRecommend: "old",
	model.LegacyTypePush:         "push",
	model.LegacyTypeBoo:          "boo",
	model.LegacyTypeArrow:        "arrow",
}

func (s *Service) commentView(cm *model.Comment, pv PostView, c model.Community) CommentView {
	creator := s.person(cm.CreatorID, cm.CreatorName, c.Anonymous)
	content := cm.Content
	if cm.DeletedOrRemoved() {
		content = ""
	}
	published := Timestamp(cm.CreatedAt)
	return CommentView{
		Comment: Comment{
			ID:        int64(cm.ID),
			CreatorID: creator.ID,
			PostID:    int64(cm.PostID),
			Content:   content,
			Removed:   cm.Removed,
			Published: published,
			Updated:   optTimestamp(cm.ModifiedAt, cm.CreatedAt),
			Deleted:   cm.Deleted,
			APID:      s.url("comment", int64(cm.ID)),
			Local:     true,
			// PTT comments are flat: every comment is a top-level one.
			Path:     fmt.Sprintf("0.%d", cm.ID),
			BBSFloor: int(cm.Sequence),
			BBSType:  bbsTypes[cm.LegacyType],
		},
		Creator:    creator,
		Post:       pv.Post,
		Community:  pv.Community,
		Counts:     CommentAggregates{CommentID: int64(cm.ID), Published: published},
		Subscribed: "NotSubscribed",
	}
}

// post returns the post id with its community, if it may be served.
func (s *Service) post(id int64) (*model.Post, model.Community, error) {
	if id <= 0 {
		return nil, model.Community{}, ErrCouldntFindPost
	}
	p, err := s.Storage.GetPost(uint64(id))
	if err != nil || p == nil {
		return nil, model.Community{}, ErrCouldntFindPost
	}
	c, ok := s.community(p.CommunityID)
	if !ok {
		return nil, model.Community{}, ErrCouldntFindPost
	}
	return p, c, nil
}

// ---------------------------------------------------------------------------
// Methods
// ---------------------------------------------------------------------------

// GetPosts is Lemmy's GetPosts (GET /api/v3/post/list).
type GetPosts struct {
	Sort          string `json:"sort,omitempty"` // New (default), Old, TopAll, MostComments
	Page          int    `json:"page,omitempty"` // from 1
	Limit         int    `json:"limit,omitempty"`
	CommunityID   int64  `json:"community_id,omitempty"`
	CommunityName string `json:"community_name,omitempty"`
}

// GetPostsResponse is Lemmy's GetPostsResponse.
type GetPostsResponse struct {
	Posts []PostView `json:"posts"`
}

// Lemmy sorts that need what nest.svc does not keep yet (the time of the
// newest comment, hot ranks) list the newest posts instead.
var postSorts = map[string]storage.PostSort{
	"": storage.SortNew, "New": storage.SortNew, "Old": storage.SortOld,
	"TopAll": storage.SortTopAll, "MostComments": storage.SortMostComments,
	"Active": storage.SortNew, "Hot": storage.SortNew, "Scaled": storage.SortNew,
	"NewComments": storage.SortNew,
}

func pageOffset(page, limit, maxLimit int) (int, int) {
	if limit <= 0 {
		limit = 10
	}
	limit = min(limit, maxLimit)
	return max(page-1, 0) * limit, limit
}

func (s *Service) GetPosts(r GetPosts) (*GetPostsResponse, error) {
	sortType, ok := postSorts[r.Sort]
	if !ok {
		return nil, ErrInvalidSort
	}
	offset, limit := pageOffset(r.Page, r.Limit, maxPostLimit)
	q := storage.PostQuery{Sort: sortType, Limit: limit, Offset: offset}

	reg := s.Storage.Communities()
	communities := map[int64]model.Community{}
	switch {
	case r.CommunityID != 0 || r.CommunityName != "":
		id := r.CommunityID
		if id == 0 {
			if id, ok = reg.Lookup(r.CommunityName); !ok {
				return nil, ErrCouldntFindCommun
			}
		}
		c, ok := s.community(id)
		if !ok {
			return nil, ErrCouldntFindCommun
		}
		communities[id] = c
		q.CommunityIDs = []int64{id}
		q.FeaturedFirst = true
	default:
		// Every community that may be served.
		for _, c := range reg.List() {
			if c, ok := s.community(c.ID); ok {
				communities[c.ID] = c
				q.CommunityIDs = append(q.CommunityIDs, c.ID)
			}
		}
		if len(q.CommunityIDs) == 0 {
			return &GetPostsResponse{Posts: []PostView{}}, nil
		}
	}

	posts, err := s.Storage.QueryPosts(q)
	if err != nil {
		return nil, err
	}
	resp := &GetPostsResponse{Posts: make([]PostView, 0, len(posts))}
	for _, p := range posts {
		resp.Posts = append(resp.Posts, s.postView(p, communities[p.CommunityID], false))
	}
	return resp, nil
}

// GetPost is Lemmy's GetPost (GET /api/v3/post).
type GetPost struct {
	ID        int64 `json:"id,omitempty"`
	CommentID int64 `json:"comment_id,omitempty"`
}

// GetPostResponse is Lemmy's GetPostResponse (without community_view and
// moderators yet).
type GetPostResponse struct {
	PostView   PostView   `json:"post_view"`
	CrossPosts []PostView `json:"cross_posts"`
}

func (s *Service) GetPost(r GetPost) (*GetPostResponse, error) {
	id := r.ID
	if id == 0 && r.CommentID != 0 {
		cm, err := s.Storage.GetComment(uint64(r.CommentID))
		if err != nil {
			return nil, ErrCouldntFindPost
		}
		id = int64(cm.PostID)
	}
	p, c, err := s.post(id)
	if err != nil {
		return nil, err
	}
	return &GetPostResponse{PostView: s.postView(p, c, true), CrossPosts: []PostView{}}, nil
}

// GetComments is Lemmy's GetComments (GET /api/v3/comment/list). PTT
// comments are listed by floor (Old, the default) or newest first (New).
type GetComments struct {
	PostID int64  `json:"post_id"`
	Sort   string `json:"sort,omitempty"`
	Page   int    `json:"page,omitempty"`
	Limit  int    `json:"limit,omitempty"`
}

// GetCommentsResponse is Lemmy's GetCommentsResponse.
type GetCommentsResponse struct {
	Comments []CommentView `json:"comments"`
}

func (s *Service) GetComments(r GetComments) (*GetCommentsResponse, error) {
	p, c, err := s.post(r.PostID)
	if err != nil {
		return nil, err
	}
	offset, limit := pageOffset(r.Page, r.Limit, maxCommentLimit)
	var comments []*model.Comment
	switch r.Sort {
	case "", "Old":
		comments, err = s.Storage.GetComments(p.ID, uint32(offset+1), uint32(limit))
	case "New":
		// Floors offset+1.. counted from the last one.
		total, cerr := s.Storage.GetCommentCount(p.ID)
		if cerr != nil {
			return nil, cerr
		}
		last := total - offset
		first := max(last-limit+1, 1)
		if last >= 1 {
			comments, err = s.Storage.GetComments(p.ID, uint32(first), uint32(last-first+1))
		}
		for i, j := 0, len(comments)-1; i < j; i, j = i+1, j-1 {
			comments[i], comments[j] = comments[j], comments[i]
		}
	default:
		return nil, ErrInvalidSort
	}
	if err != nil {
		return nil, err
	}
	pv := s.postView(p, c, false)
	resp := &GetCommentsResponse{Comments: make([]CommentView, 0, len(comments))}
	for _, cm := range comments {
		resp.Comments = append(resp.Comments, s.commentView(cm, pv, c))
	}
	return resp, nil
}

// GetComment is Lemmy's GetComment (GET /api/v3/comment).
type GetComment struct {
	ID int64 `json:"id"`
}

// CommentResponse is Lemmy's CommentResponse.
type CommentResponse struct {
	CommentView CommentView `json:"comment_view"`
}

func (s *Service) GetComment(r GetComment) (*CommentResponse, error) {
	if r.ID <= 0 {
		return nil, ErrCouldntFindCommen
	}
	cm, err := s.Storage.GetComment(uint64(r.ID))
	if err != nil {
		if errors.Is(err, storage.ErrCommentNotFound) {
			return nil, ErrCouldntFindCommen
		}
		return nil, err
	}
	p, c, err := s.post(int64(cm.PostID))
	if err != nil {
		return nil, ErrCouldntFindCommen
	}
	return &CommentResponse{CommentView: s.commentView(cm, s.postView(p, c, false), c)}, nil
}

// LockPost is Lemmy's LockPost (POST /api/v3/post/lock). The caller is
// trusted to have checked that the user may lock the post.
type LockPost struct {
	PostID int64 `json:"post_id"`
	Locked bool  `json:"locked"`
}

// PostResponse is Lemmy's PostResponse.
type PostResponse struct {
	PostView PostView `json:"post_view"`
}

func (s *Service) LockPost(r LockPost) (*PostResponse, error) {
	if _, _, err := s.post(r.PostID); err != nil {
		return nil, err
	}
	if err := s.Storage.LockPost(uint64(r.PostID), r.Locked); err != nil {
		return nil, err
	}
	p, c, err := s.post(r.PostID)
	if err != nil {
		return nil, err
	}
	return &PostResponse{PostView: s.postView(p, c, true)}, nil
}
