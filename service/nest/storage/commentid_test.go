package storage

import (
	"errors"
	"fmt"
	"path/filepath"
	"testing"

	"pttbbs/nest/config"
	"pttbbs/nest/model"
)

func TestCommentIDs(t *testing.T) {
	dir := t.TempDir()
	cfg := Config{
		BBSHome:        filepath.Join(dir, "bbshome"),
		DataDir:        filepath.Join(dir, "data"),
		CacheDir:       filepath.Join(dir, "cache"),
		FilterEncoding: "utf-8",
	}
	e, err := OpenEngine(cfg)
	if err != nil {
		t.Fatal(err)
	}
	defer func() { e.Close() }()

	post := mustCreatePost(t, e, "Board", "M.1.A.001", "body")
	add := func(author, content string, at int64) *model.Comment {
		t.Helper()
		c, err := e.AddComment(&model.Comment{PostID: post.ID, CreatorName: author, Content: content, CreatedAt: at})
		if err != nil {
			t.Fatal(err)
		}
		return c
	}
	c1 := add("alice", "one", 1000)
	c2 := add("bob", "two", 1001)
	merged := add("bob", "two, more", 1002)
	// A comment ID is its post ID and floor.
	if c1.ID != post.ID<<16|1 || c2.ID != post.ID<<16|2 {
		t.Fatalf("comment IDs %d, %d of post %d", c1.ID, c2.ID, post.ID)
	}
	if !merged.IsMerged || merged.ID != c2.ID || merged.Sequence != c2.Sequence {
		t.Fatalf("merged comment %+v, want the ID and floor of #%d", merged, c2.ID)
	}
	got, err := e.GetComment(c2.ID)
	if err != nil || got.ID != c2.ID || got.PostID != post.ID || got.Sequence != 2 || got.Content != "two\ntwo, more" {
		t.Fatalf("GetComment(%d) = %+v, %v", c2.ID, got, err)
	}
	for _, id := range []uint64{99, post.ID << 16, post.ID<<16 | 3} {
		if _, err := e.GetComment(id); !errors.Is(err, ErrCommentNotFound) {
			t.Errorf("GetComment(%d) = %v, want ErrCommentNotFound", id, err)
		}
	}
	// Editing and deleting keep the ID.
	if _, err := e.UpdateComment(post.ID, 1, "one, edited", "alice", 0); err != nil {
		t.Fatal(err)
	}
	if _, err := e.DeleteComment(post.ID, 1, "alice", 0, ""); err != nil {
		t.Fatal(err)
	}
	if got, err := e.GetComment(c1.ID); err != nil || got.Content != "one, edited" || !got.Deleted {
		t.Fatalf("GetComment(%d) after edit and delete = %+v, %v", c1.ID, got, err)
	}

	// Imported comments get the IDs of their floors.
	p := &model.Post{Community: "Board", PostFile: "M.2.A.001", Title: "imported", CreatorName: "carol", Content: "body"}
	if err := e.ImportPostBatch([]*ImportedPostData{{Post: p, Comments: []*model.Comment{
		{CreatorName: "carol", Content: "imported one", CreatedAt: 900},
		{CreatorName: "dave", Content: "imported two", CreatedAt: 901},
	}}}, ""); err != nil {
		t.Fatal(err)
	}
	cmts, _ := e.GetComments(p.ID, 1, 10)
	if len(cmts) != 2 || cmts[0].ID != p.ID<<16|1 || cmts[1].ID != p.ID<<16|2 {
		t.Fatalf("imported comments: %+v", cmts)
	}

	// The IDs of a purged post's comments are not used again: neither is
	// the post ID, even after a restart.
	if err := e.PurgePost(p.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := e.GetComment(p.ID<<16 | 1); err == nil {
		t.Fatalf("comment of a purged post still found")
	}
	e.Close()
	if e, err = OpenEngine(cfg); err != nil {
		t.Fatal(err)
	}
	if np := mustCreatePost(t, e, "Board", "M.3.A.001", "body"); np.ID == p.ID {
		t.Fatalf("post ID %d used again", np.ID)
	}
}

// A post has at most MaxFloor floors: new comments are refused, and
// imported ones beyond are folded into the last floor.
func TestFloorLimit(t *testing.T) {
	e, _ := openTestEngine(t)
	post := mustCreatePost(t, e, "Board", "M.1.A.001", "body")
	last := &model.Comment{PostID: post.ID, Sequence: model.MaxFloor, CreatorName: "alice", Content: "last"}
	if err := e.commentsDB.Set(model.EncodeCommentKey(post.ID, model.MaxFloor), model.EncodeRFC822Comment(last), nil); err != nil {
		t.Fatal(err)
	}
	if _, err := e.AddComment(&model.Comment{PostID: post.ID, CreatorName: "bob", Content: "one too many"}); !errors.Is(err, ErrTooManyComments) {
		t.Fatalf("AddComment beyond the last floor = %v", err)
	}

	comments := make([]*model.Comment, model.MaxFloor+2)
	for i := range comments {
		comments[i] = &model.Comment{CreatorName: fmt.Sprintf("u%d", i), Content: fmt.Sprintf("c%d", i), CreatedAt: int64(i)}
	}
	folded := foldComments(comments)
	if len(folded) != model.MaxFloor {
		t.Fatalf("foldComments kept %d comments", len(folded))
	}
	want := fmt.Sprintf("c%d\nu%d: c%d\nu%d: c%d", model.MaxFloor-1, model.MaxFloor, model.MaxFloor, model.MaxFloor+1, model.MaxFloor+1)
	if f := folded[model.MaxFloor-1]; f.Content != want || f.LastCommentAt != int64(model.MaxFloor+1) {
		t.Fatalf("last floor: %+v", f)
	}
}

func TestPostIDRange(t *testing.T) {
	for _, id := range []uint64{0, model.MaxPostID + 1} {
		if checkPostID(id) == nil {
			t.Errorf("checkPostID(%d) accepted", id)
		}
	}
	if err := checkPostID(model.MaxPostID); err != nil {
		t.Errorf("checkPostID(MaxPostID) = %v", err)
	}
	if _, _, ok := model.SplitCommentID(uint64(model.MaxPostID+1)<<16 | 1); ok {
		t.Errorf("SplitCommentID accepted a post ID beyond MaxPostID")
	}
}

func TestShardedCommentIDs(t *testing.T) {
	s, _ := openTestSharded(t, 2, map[string]int{"A": 0, "B": 1})
	pa := mustCreatePost(t, s, "A", "M.1.A.001", "a")
	pb := mustCreatePost(t, s, "B", "M.1.A.001", "b")
	var ids []uint64
	for i, p := range []*model.Post{pa, pb, pa} {
		c, err := s.AddComment(&model.Comment{PostID: p.ID, CreatorName: fmt.Sprintf("u%d", i), Content: "push"})
		if err != nil {
			t.Fatal(err)
		}
		ids = append(ids, c.ID)
	}
	if ids[0] != pa.ID<<16|1 || ids[1] != pb.ID<<16|1 || ids[2] != pa.ID<<16|2 {
		t.Fatalf("comment IDs across engines = %v", ids)
	}
	if got, err := s.GetComment(ids[1]); err != nil || got.PostID != pb.ID {
		t.Fatalf("GetComment(%d) = %+v, %v", ids[1], got, err)
	}
	// Moving the community keeps the comment IDs.
	if _, err := s.MoveCommunity("A", 1); err != nil {
		t.Fatal(err)
	}
	if got, err := s.GetComment(ids[2]); err != nil || got.PostID != pa.ID || got.Sequence != 2 {
		t.Fatalf("GetComment(%d) after the move = %+v, %v", ids[2], got, err)
	}
}

// A single engine's comments are found by their IDs when it becomes
// sharded.
func TestCommentIDsSurviveSharding(t *testing.T) {
	dir := t.TempDir()
	disk0 := filepath.Join(dir, "disk0")
	single, err := OpenEngine(Config{DataDir: disk0, CacheDir: filepath.Join(dir, "cache0"), FilterEncoding: "utf-8"})
	if err != nil {
		t.Fatal(err)
	}
	p := mustCreatePost(t, single, "Old", "M.1.A.001", "body")
	c, err := single.AddComment(&model.Comment{PostID: p.ID, CreatorName: "alice", Content: "push"})
	if err != nil {
		t.Fatal(err)
	}
	single.Close()

	s, err := OpenShardedEngine(Config{FilterEncoding: "utf-8"}, []config.EngineLocation{
		{Index: 0, DataDir: disk0, CacheDir: filepath.Join(dir, "cache0")},
		{Index: 1, DataDir: filepath.Join(dir, "disk1"), CacheDir: filepath.Join(dir, "cache1")},
	}, map[string]int{"New": 1})
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	if got, err := s.GetComment(c.ID); err != nil || got.Content != "push" {
		t.Fatalf("GetComment(%d) after sharding = %+v, %v", c.ID, got, err)
	}
}
