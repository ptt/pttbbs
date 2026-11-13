package storage

import (
	"fmt"
	"os"
	"path/filepath"
	"testing"
	"time"

	"pttbbs/post/config"
	"pttbbs/post/model"
)

func TestShardedEngineRoutingAndID(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "post_sharded_test_*")
	if err != nil {
		t.Fatalf("create tmp dir failed: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	numShards := 4
	locations := make([]config.EngineLocation, numShards)
	for i := 0; i < numShards; i++ {
		locations[i] = config.EngineLocation{
			Index:    i,
			DataDir:  filepath.Join(tmpDir, fmt.Sprintf("disk%d", i)),
			CacheDir: filepath.Join(tmpDir, fmt.Sprintf("cache%d", i)),
		}
	}

	pins := map[string]int{
		"Gossiping":   0,
		"Marginalman": 1,
	}

	baseCfg := Config{
		BBSHome:        tmpDir,
		CacheSizeMB:    128,
		FlushSeconds:   1,
		MinReplyRunes:  20,
		FilterEncoding: "utf-8",
	}

	sharded, err := OpenShardedEngine(baseCfg, locations, pins)
	if err != nil {
		t.Fatalf("OpenShardedEngine failed: %v", err)
	}
	defer sharded.Close()

	if sharded.Shards() != numShards {
		t.Fatalf("expected %d shards, got %d", numShards, sharded.Shards())
	}

	// 1. Verify community routing and pinning (pins choose the shard of
	// new communities)
	for _, name := range []string{"Gossiping", "Marginalman"} {
		if _, err := sharded.Communities().Ensure(name); err != nil {
			t.Fatal(err)
		}
	}
	e0 := sharded.GetEngineByCommunity("Gossiping")
	if e0 != sharded.engines[0] {
		t.Errorf("Gossiping not pinned to engine 0")
	}

	e1 := sharded.GetEngineByCommunity("Marginalman")
	if e1 != sharded.engines[1] {
		t.Errorf("Marginalman not pinned to engine 1")
	}

	// 2. Create posts on different shards: the IDs come from one global
	// sequence and say nothing about the shard
	p0 := &model.Post{
		Community:   "Gossiping",
		PostFile:    "M.1700000000.A.001",
		Title:       "八卦板測試文",
		CreatorName: "user1",
		CreatedAt:   time.Now().Unix(),
		Content:     "八卦板首篇分片文章",
	}
	c0, err := sharded.CreatePost(p0)
	if err != nil {
		t.Fatalf("CreatePost on Gossiping failed: %v", err)
	}

	p1 := &model.Post{
		Community:   "Marginalman",
		PostFile:    "M.1700000000.A.002",
		Title:       "邊緣人板測試文",
		CreatorName: "user2",
		CreatedAt:   time.Now().Unix(),
		Content:     "邊緣人板首篇分片文章",
	}
	c1, err := sharded.CreatePost(p1)
	if err != nil {
		t.Fatalf("CreatePost on Marginalman failed: %v", err)
	}
	if c0.ID != 1 || c1.ID != 2 {
		t.Errorf("post IDs on shards 0 and 1 = %d, %d; want 1, 2", c0.ID, c1.ID)
	}
	if sharded.GetEngineByPostID(c0.ID) != e0 || sharded.GetEngineByPostID(c1.ID) != e1 {
		t.Errorf("posts not found on the shards of their communities")
	}

	// 3. Verify retrieval by Post ID (asking every shard)
	gotP0, err := sharded.GetPost(c0.ID)
	if err != nil || gotP0 == nil {
		t.Fatalf("GetPost(%d) failed: %v", c0.ID, err)
	}
	if gotP0.Title != "八卦板測試文" {
		t.Errorf("title mismatch: %s", gotP0.Title)
	}

	gotP1, err := sharded.GetPost(c1.ID)
	if err != nil || gotP1 == nil {
		t.Fatalf("GetPost(%d) failed: %v", c1.ID, err)
	}
	if gotP1.Title != "邊緣人板測試文" {
		t.Errorf("title mismatch: %s", gotP1.Title)
	}

	// 4. Verify Comments across shards
	cmt0 := &model.Comment{
		PostID:      c0.ID,
		CreatorName: "commenter_gossip",
		Content:     "推八卦",
		CreatedAt:   time.Now().Unix(),
	}
	addedCmt, err := sharded.AddComment(cmt0)
	if err != nil {
		t.Fatalf("AddComment on post %d failed: %v", c0.ID, err)
	}
	if addedCmt.Sequence != 1 {
		t.Errorf("expected sequence 1, got %d", addedCmt.Sequence)
	}

	cmts, err := sharded.GetComments(c0.ID, 1, 10)
	if err != nil || len(cmts) != 1 {
		t.Fatalf("GetComments failed: len=%d, err=%v", len(cmts), err)
	}
	if cmts[0].Content != "推八卦" {
		t.Errorf("comment content mismatch: %s", cmts[0].Content)
	}

	// 5. Verify Votes across shards
	up, down, err := sharded.VotePost(c1.ID, "voter1", 1234, model.VoteUp, 1792778690, "127.0.0.1")
	if err != nil {
		t.Fatalf("VotePost on post %d failed: %v", c1.ID, err)
	}
	if up != 1 || down != 0 {
		t.Errorf("expected up=1 down=0, got up=%d down=%d", up, down)
	}
	votes, err := sharded.GetVotes(c1.ID)
	if err != nil || len(votes) != 1 {
		t.Fatalf("GetVotes failed: len=%d, err=%v", len(votes), err)
	}
	if votes[0].PersonName != "voter1" || votes[0].PersonID != 1234 || votes[0].Vote != model.VoteUp || votes[0].CreatedAt != 1792778690 || votes[0].IP != "127.0.0.1" {
		t.Errorf("VoteRecord mismatch: %+v", votes[0])
	}

	// 6. Verify cross-shard PurgeUserPosts and PurgeUserComments
	purgedPosts, err := sharded.PurgeUserPosts("user1", 24*time.Hour)
	if err != nil {
		t.Fatalf("PurgeUserPosts failed: %v", err)
	}
	if purgedPosts != 1 {
		t.Errorf("expected 1 purged post for user1, got %d", purgedPosts)
	}
}

// A single engine becomes the first of several: its posts keep their IDs
// (and are still found by them), and new posts continue the sequence.
func TestSingleEngineBecomesSharded(t *testing.T) {
	dir := t.TempDir()
	disk0 := filepath.Join(dir, "disk0")
	single, err := OpenEngine(Config{DataDir: disk0, CacheDir: filepath.Join(dir, "cache0"), FilterEncoding: "utf-8"})
	if err != nil {
		t.Fatal(err)
	}
	var old []*model.Post
	for i := 0; i < 3; i++ {
		p, err := single.CreatePost(&model.Post{Community: fmt.Sprintf("Old%d", i), PostFile: "M.1.A.001", Title: "old", CreatorName: "author", Content: "body"})
		if err != nil {
			t.Fatal(err)
		}
		old = append(old, p)
	}
	// A purged post's ID is never used again.
	purged, err := single.CreatePost(&model.Post{Community: "Old0", PostFile: "M.2.A.001", Title: "purged", CreatorName: "author", Content: "body"})
	if err != nil {
		t.Fatal(err)
	}
	if err := single.PurgePost(purged.ID); err != nil {
		t.Fatal(err)
	}
	single.Close()

	locations := []config.EngineLocation{
		{Index: 0, DataDir: disk0, CacheDir: filepath.Join(dir, "cache0")},
		{Index: 1, DataDir: filepath.Join(dir, "disk1"), CacheDir: filepath.Join(dir, "cache1")},
	}
	s, err := OpenShardedEngine(Config{FilterEncoding: "utf-8"}, locations, map[string]int{"New": 1})
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	for _, p := range old {
		got, err := s.GetPost(p.ID)
		if err != nil || got.Community != p.Community {
			t.Fatalf("GetPost(%d) after sharding = %+v, %v", p.ID, got, err)
		}
	}
	fresh, err := s.CreatePost(&model.Post{Community: "New", PostFile: "M.3.A.001", Title: "new", CreatorName: "author", Content: "body"})
	if err != nil {
		t.Fatal(err)
	}
	if fresh.ID != purged.ID+1 || s.GetEngineByPostID(fresh.ID) != s.engines[1] {
		t.Fatalf("new post %d on shard 1: want ID %d", fresh.ID, purged.ID+1)
	}
}
