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

	// 1. Verify community routing and pinning
	e0 := sharded.GetEngineByCommunity("Gossiping")
	if e0 != sharded.engines[0] {
		t.Errorf("Gossiping not pinned to engine 0")
	}

	e1 := sharded.GetEngineByCommunity("Marginalman")
	if e1 != sharded.engines[1] {
		t.Errorf("Marginalman not pinned to engine 1")
	}

	// 2. Create posts on different shards and verify ID modulo
	p0 := &model.Post{
		Community: "Gossiping",
		PostFile:  "M.1700000000.A.001",
		Title:     "八卦板測試文",
		Author:    "user1",
		CreatedAt: time.Now().Unix(),
		Content:   "八卦板首篇分片文章",
	}
	c0, err := sharded.CreatePost(p0)
	if err != nil {
		t.Fatalf("CreatePost on Gossiping failed: %v", err)
	}
	if c0.ID%uint64(numShards) != 0 {
		t.Errorf("expected post ID %% %d == 0 for Shard 0, got ID %d", numShards, c0.ID)
	}

	p1 := &model.Post{
		Community: "Marginalman",
		PostFile:  "M.1700000000.A.002",
		Title:     "邊緣人板測試文",
		Author:    "user2",
		CreatedAt: time.Now().Unix(),
		Content:   "邊緣人板首篇分片文章",
	}
	c1, err := sharded.CreatePost(p1)
	if err != nil {
		t.Fatalf("CreatePost on Marginalman failed: %v", err)
	}
	if c1.ID%uint64(numShards) != 1 {
		t.Errorf("expected post ID %% %d == 1 for Shard 1, got ID %d", numShards, c1.ID)
	}

	// 3. Verify retrieval by Post ID (O(1) modulo routing)
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
		PostID:    c0.ID,
		Author:    "commenter_gossip",
		Content:   "推八卦",
		CreatedAt: time.Now().Unix(),
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
	up, down, err := sharded.VotePost(c1.ID, "voter1", 1234, model.VoteUp)
	if err != nil {
		t.Fatalf("VotePost on post %d failed: %v", c1.ID, err)
	}
	if up != 1 || down != 0 {
		t.Errorf("expected up=1 down=0, got up=%d down=%d", up, down)
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
