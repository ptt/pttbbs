package lemmy

import (
	"encoding/json"
	"errors"
	"fmt"
	"path/filepath"
	"strings"
	"testing"

	"pttbbs/nest/model"
	"pttbbs/nest/storage"
)

func openTest(t *testing.T) (*storage.Engine, *Service) {
	t.Helper()
	dir := t.TempDir()
	e, err := storage.OpenEngine(storage.Config{
		BBSHome:        filepath.Join(dir, "bbshome"),
		DataDir:        filepath.Join(dir, "data"),
		CacheDir:       filepath.Join(dir, "cache"),
		FilterEncoding: "utf-8",
	})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { e.Close() })
	return e, &Service{Storage: e, SiteURL: "https://bbs.example"}
}

func mustPost(t *testing.T, e *storage.Engine, community, file, title string, at int64, creator model.PersonID) *model.Post {
	t.Helper()
	p, err := e.CreatePost(&model.Post{Community: community, PostFile: file, Title: title, CreatorName: "author", CreatorID: creator, CreatedAt: at, Content: "body of " + title})
	if err != nil {
		t.Fatal(err)
	}
	return p
}

func TestGetPostsAndComments(t *testing.T) {
	e, s := openTest(t)
	reg := e.Communities()
	for _, c := range []*model.Community{
		{Name: "Board", Title: "a board"},
		{Name: "Secret", Title: "hidden", Flags: model.BrdHide},
		{Name: "Anon", Title: "anonymous", Flags: model.BrdAnonymous},
	} {
		c.Derive()
		if _, err := reg.Upsert(c); err != nil {
			t.Fatal(err)
		}
	}
	alice := model.MakePersonID(7, 1600000000)
	old := mustPost(t, e, "Board", "M.1000.A.001", "old", 1000, alice)
	pinned := mustPost(t, e, "Board", "M.1500.A.001", "pinned", 1500, alice)
	newer := mustPost(t, e, "Board", "M.2000.A.001", "newer", 2000, alice)
	mustPost(t, e, "Secret", "M.3000.A.001", "secret", 3000, alice)
	anon := mustPost(t, e, "Anon", "M.2500.A.001", "anon", 2500, alice)
	if err := e.SetPostMeta("Board", pinned.PostFile, model.FileBottom, 0); err != nil {
		t.Fatal(err)
	}
	for i, name := range []string{"v1", "v2"} {
		if _, _, err := e.VotePost(old.ID, name, model.MakePersonID(10+i, 1600000000), model.VoteUp, 0, ""); err != nil {
			t.Fatal(err)
		}
	}
	for i, content := range []string{"first", "second", "third"} {
		if _, err := e.AddComment(&model.Comment{PostID: newer.ID, CreatorName: "c" + content, CreatedAt: int64(2100 + i*1000), Content: content, LegacyType: model.LegacyTypePush}); err != nil {
			t.Fatal(err)
		}
	}

	// One community: the pinned (featured) post first, then the newest.
	r, err := s.GetPosts(GetPosts{CommunityName: "board"})
	if err != nil {
		t.Fatal(err)
	}
	var ids []int64
	for _, pv := range r.Posts {
		ids = append(ids, pv.Post.ID)
		if pv.Post.Body != nil {
			t.Errorf("GetPosts returned the body of post %d", pv.Post.ID)
		}
	}
	if len(ids) != 3 || ids[0] != int64(pinned.ID) || ids[1] != int64(newer.ID) || ids[2] != int64(old.ID) {
		t.Fatalf("GetPosts(Board) = %v", ids)
	}
	if pv := r.Posts[0]; !pv.Post.FeaturedCommunity || pv.Community.Name != "Board" || pv.Creator.Name != "author" || pv.Creator.ID != 1 ||
		pv.Post.APID != "https://bbs.example/post/2" {
		t.Fatalf("featured post view: %+v", pv)
	}
	if c := r.Posts[2].Counts; c.Upvotes != 2 || c.Score != 2 {
		t.Fatalf("counts of the old post: %+v", c)
	}
	if r.Posts[1].Counts.Comments != 3 {
		t.Fatalf("counts of the newer post: %+v", r.Posts[1].Counts)
	}
	// Top, paging.
	if r, _ := s.GetPosts(GetPosts{CommunityID: old.CommunityID, Sort: "TopAll", Limit: 1, Page: 2}); len(r.Posts) != 1 || r.Posts[0].Post.ID != int64(old.ID) {
		t.Fatalf("GetPosts(TopAll, page 2) = %+v", r.Posts)
	}

	// Every community: not the hidden one; the anonymous author is shown by
	// name only.
	all, err := s.GetPosts(GetPosts{Sort: "New"})
	if err != nil {
		t.Fatal(err)
	}
	if len(all.Posts) != 4 || all.Posts[0].Post.ID != int64(anon.ID) {
		t.Fatalf("GetPosts(all) = %+v", all.Posts)
	}
	if all.Posts[0].Creator.ID == r.Posts[0].Creator.ID {
		t.Fatalf("anonymous post shown with its author's person")
	}
	if _, err := s.GetPosts(GetPosts{CommunityName: "Secret"}); !errors.Is(err, ErrCouldntFindCommun) {
		t.Fatalf("GetPosts(Secret) = %v", err)
	}
	if _, err := s.GetPosts(GetPosts{Sort: "Controversial"}); !errors.Is(err, ErrInvalidSort) {
		t.Fatalf("GetPosts(Controversial) = %v", err)
	}

	// GetPost has the body, unless the post is locked.
	gp, err := s.GetPost(GetPost{ID: int64(newer.ID)})
	if err != nil || gp.PostView.Post.Body == nil || *gp.PostView.Post.Body != "body of newer" {
		t.Fatalf("GetPost = %+v, %v", gp, err)
	}
	lp, err := s.LockPost(LockPost{PostID: int64(newer.ID), Locked: true})
	if err != nil || !lp.PostView.Post.Locked || lp.PostView.Post.Body != nil {
		t.Fatalf("LockPost = %+v, %v", lp, err)
	}

	// Comments by floor, or newest first.
	cr, err := s.GetComments(GetComments{PostID: int64(newer.ID), Limit: 2})
	if err != nil || len(cr.Comments) != 2 || cr.Comments[0].Comment.BBSFloor != 1 || cr.Comments[0].Comment.Content != "first" ||
		cr.Comments[0].Comment.BBSType != "push" || cr.Comments[0].Comment.ID != int64(newer.ID<<16|1) ||
		cr.Comments[0].Comment.Path != fmt.Sprintf("0.%d", newer.ID<<16|1) {
		t.Fatalf("GetComments = %+v, %v", cr, err)
	}
	cr, err = s.GetComments(GetComments{PostID: int64(newer.ID), Sort: "New", Limit: 2, Page: 1})
	if err != nil || len(cr.Comments) != 2 || cr.Comments[0].Comment.Content != "third" || cr.Comments[1].Comment.Content != "second" {
		t.Fatalf("GetComments(New) = %+v, %v", cr, err)
	}
	gc, err := s.GetComment(GetComment{ID: cr.Comments[0].Comment.ID})
	if err != nil || gc.CommentView.Comment.BBSFloor != 3 || gc.CommentView.Post.ID != int64(newer.ID) {
		t.Fatalf("GetComment = %+v, %v", gc, err)
	}
	if gp, err := s.GetPost(GetPost{CommentID: gc.CommentView.Comment.ID}); err != nil || gp.PostView.Post.ID != int64(newer.ID) {
		t.Fatalf("GetPost(comment_id) = %+v, %v", gp, err)
	}
}

func TestHandle(t *testing.T) {
	e, s := openTest(t)
	p := mustPost(t, e, "Board", "M.1000.A.001", "hello", 1000, 0)

	out, err := s.Handle("GetPost", []byte(`{"id":1}`))
	if err != nil {
		t.Fatal(err)
	}
	var resp struct {
		PostView struct {
			Post map[string]any `json:"post"`
		} `json:"post_view"`
	}
	if err := json.Unmarshal(out, &resp); err != nil {
		t.Fatal(err)
	}
	post := resp.PostView.Post
	if post["name"] != "hello" || post["id"] != float64(p.ID) || !strings.HasSuffix(post["published"].(string), "Z") {
		t.Fatalf("GetPost JSON: %s", out)
	}
	if _, err := s.Handle("GetPost", []byte(`{"id":99}`)); !errors.Is(err, ErrCouldntFindPost) {
		t.Fatalf("GetPost(99) = %v", err)
	}
	if _, err := s.Handle("GetPost", []byte(`{`)); !errors.Is(err, ErrBadRequest) {
		t.Fatalf("bad JSON = %v", err)
	}
	if _, err := s.Handle("Nope", nil); !errors.Is(err, ErrUnknownMethod) {
		t.Fatalf("unknown method = %v", err)
	}
}
