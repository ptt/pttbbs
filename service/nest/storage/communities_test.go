package storage

import (
	"database/sql"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"

	"pttbbs/nest/config"
	"pttbbs/nest/model"
)

func openTestEngine(t *testing.T) (*Engine, string) {
	t.Helper()
	dir := t.TempDir()
	e, err := OpenEngine(Config{
		BBSHome:        filepath.Join(dir, "bbshome"),
		DataDir:        filepath.Join(dir, "data"),
		CacheDir:       filepath.Join(dir, "cache"),
		CacheSizeMB:    16,
		FlushSeconds:   1,
		FilterEncoding: "utf-8",
	})
	if err != nil {
		t.Fatalf("OpenEngine failed: %v", err)
	}
	t.Cleanup(func() { e.Close() })
	return e, dir
}

func mustCreatePost(t *testing.T, st Storage, community, postFile, content string) *model.Post {
	t.Helper()
	p, err := st.CreatePost(&model.Post{
		Community:   community,
		PostFile:    postFile,
		Title:       "title of " + postFile,
		CreatorName: "author",
		Content:     content,
	})
	if err != nil {
		t.Fatalf("CreatePost %s/%s failed: %v", community, postFile, err)
	}
	return p
}

func TestCommunityRegistry(t *testing.T) {
	dir := t.TempDir()
	reg, err := OpenCommunityRegistry(dir)
	if err != nil {
		t.Fatal(err)
	}

	id, err := reg.Ensure("SYSOP")
	if err != nil || id == 0 {
		t.Fatalf("Ensure failed: %d, %v", id, err)
	}
	// Names are case-insensitive but kept as given.
	if id2, _ := reg.Ensure("sysop"); id2 != id {
		t.Fatalf("Ensure(sysop) = %d, want %d", id2, id)
	}
	if got, ok := reg.Lookup("SysOp"); !ok || got != id {
		t.Fatalf("Lookup(SysOp) = %d %v, want %d", got, ok, id)
	}
	if name := reg.Name(id); name != "SYSOP" {
		t.Fatalf("Name = %q, want SYSOP", name)
	}

	other, _ := reg.Ensure("Test")
	if _, err := reg.Rename("SYSOP", "test"); !errors.Is(err, ErrCommunityExists) {
		t.Fatalf("Rename to a taken name: err = %v, want ErrCommunityExists", err)
	}
	if _, err := reg.Rename("nosuch", "x"); !errors.Is(err, ErrCommunityNotFound) {
		t.Fatalf("Rename of an unknown name: err = %v, want ErrCommunityNotFound", err)
	}
	// Changing only the case is fine.
	if got, err := reg.Rename("sysop", "SysOp"); err != nil || got != id || reg.Name(id) != "SysOp" {
		t.Fatalf("Rename case: %d %v name=%q", got, err, reg.Name(id))
	}

	// Removing frees the name; the old ID keeps its name, and the modlog
	// keeps who removed it and why.
	if got, err := reg.RemoveCommunity("TEST", 1000, "closed"); err != nil || got != other {
		t.Fatalf("RemoveCommunity = %d %v, want %d", got, err, other)
	}
	if c, ok := reg.Get(other); !ok || !c.Removed || c.Deleted || c.DeletedAt == 0 {
		t.Fatalf("removed #%d = %+v", other, c)
	}
	if log, err := reg.ModRemoveCommunities(other); err != nil || len(log) != 1 ||
		log[0].ModPersonID != 1000 || log[0].Reason != "closed" || !log[0].Removed {
		t.Fatalf("ModRemoveCommunities = %+v, %v", log, err)
	}
	if _, err := reg.RemoveCommunity("TEST", 0, ""); !errors.Is(err, ErrCommunityNotFound) {
		t.Fatalf("RemoveCommunity of a removed name: err = %v, want ErrCommunityNotFound", err)
	}
	if _, ok := reg.Lookup("Test"); ok {
		t.Fatalf("Lookup of a deleted name succeeded")
	}
	if reg.Name(other) != "Test" || reg.Active(other) {
		t.Fatalf("deleted #%d: name=%q active=%v", other, reg.Name(other), reg.Active(other))
	}
	newer, err := reg.Ensure("test")
	if err != nil || newer == other {
		t.Fatalf("re-created community got #%d (err %v), old #%d", newer, err, other)
	}
	reg.Close()

	// Everything survives a restart.
	reg, err = OpenCommunityRegistry(dir)
	if err != nil {
		t.Fatal(err)
	}
	defer reg.Close()
	if got, _ := reg.Lookup("test"); got != newer {
		t.Fatalf("after reopen Lookup(test) = %d, want %d", got, newer)
	}
	if c, ok := reg.Get(other); !ok || !c.Removed || c.Deleted || c.Name != "Test" {
		t.Fatalf("after reopen #%d = %+v", other, c)
	}
	if list := reg.List(); len(list) != 3 || list[0].ID != id || list[0].Name != "SysOp" {
		t.Fatalf("List = %+v", list)
	}
	if err := reg.Purge(other); err != nil {
		t.Fatal(err)
	}
	if _, ok := reg.Get(other); ok {
		t.Fatalf("#%d still registered after Purge", other)
	}
	if log, _ := reg.ModRemoveCommunities(other); len(log) != 0 {
		t.Fatalf("modlog of a purged community: %+v", log)
	}
}

func TestRenameCommunity(t *testing.T) {
	e, dir := openTestEngine(t)

	p := mustCreatePost(t, e, "OldName", "M.1.A.001", "content one")
	if _, err := e.AddComment(&model.Comment{PostID: p.ID, CreatorName: "c1", Content: "push"}); err != nil {
		t.Fatal(err)
	}
	if _, _, err := e.AddCrosspost("OldName", "M.1.A.001", "OldName", "M.1.A.002", "op", 0, 0); err != nil {
		t.Fatal(err)
	}
	cacheFile := filepath.Join(dir, "cache", strconv.FormatInt(p.CommunityID, 10), "M.1.A.001")
	if _, err := os.Stat(cacheFile); err != nil {
		t.Fatalf("cache file is not named after the community ID: %v", err)
	}

	id, err := e.RenameCommunity("oldname", "NewName")
	if err != nil || id != p.CommunityID {
		t.Fatalf("RenameCommunity = %d %v, want %d", id, err, p.CommunityID)
	}

	got, err := e.GetPostByCommunityFile("newname", "M.1.A.001")
	if err != nil || got.ID != p.ID || got.Community != "NewName" || got.CommunityID != id {
		t.Fatalf("post under the new name: %+v, err=%v", got, err)
	}
	if _, err := e.GetPostByCommunityFile("OldName", "M.1.A.001"); !errors.Is(err, sql.ErrNoRows) {
		t.Fatalf("post under the old name: err = %v, want sql.ErrNoRows", err)
	}
	if n, _ := e.CountPosts("OldName"); n != 0 {
		t.Fatalf("CountPosts(OldName) = %d", n)
	}
	if list, _ := e.ListPosts("NEWNAME", 10, 0); len(list) != 1 || list[0].Community != "NewName" {
		t.Fatalf("ListPosts(NEWNAME) = %+v", list)
	}
	if cmts, _ := e.GetCommentsByCommunityFile("NewName", "M.1.A.001", 1, 10); len(cmts) != 1 {
		t.Fatalf("comments under the new name: %d", len(cmts))
	}
	if cps, _ := e.GetCrossposts("NewName", "M.1.A.001"); len(cps) != 1 || cps[0].TargetCommunity != "NewName" {
		t.Fatalf("crossposts after rename: %+v", cps)
	}
	if _, rec, _ := e.GetOriginPost("NewName", "M.1.A.002"); rec == nil {
		t.Fatalf("origin of the crosspost not found under the new name")
	}
	if e.CacheFilePath(got) != cacheFile {
		t.Fatalf("cache path changed by the rename: %s", e.CacheFilePath(got))
	}

	// Pebble keeps the ID, so a rebuilt SQLite still knows the new name.
	if _, _, err := e.RebuildSQLiteFromPebble(); err != nil {
		t.Fatal(err)
	}
	if got, err := e.GetPostByCommunityFile("NewName", "M.1.A.001"); err != nil || got.ID != p.ID {
		t.Fatalf("post after rebuild: %+v, err=%v", got, err)
	}
	if cps, _ := e.GetCrossposts("NewName", "M.1.A.001"); len(cps) != 1 || cps[0].TargetCommunityID != id {
		t.Fatalf("crossposts after rebuild: %+v", cps)
	}
}

func TestRemoveCommunityKeepsBoardFiles(t *testing.T) {
	e, _ := openTestEngine(t)

	old := mustCreatePost(t, e, "Board", "M.1.A.001", "old board")
	oldID, err := e.RemoveCommunity("board", 0, "")
	if err != nil || oldID != old.CommunityID {
		t.Fatalf("RemoveCommunity = %d %v", oldID, err)
	}
	// The name may now belong to another board: the old posts must not
	// touch its files.
	if path := e.BoardFilePath(old); path != "" {
		t.Fatalf("BoardFilePath of a post of a deleted community = %q", path)
	}
	newer := mustCreatePost(t, e, "Board", "M.1.A.001", "new board")
	if newer.CommunityID == oldID {
		t.Fatalf("re-created board reused #%d", oldID)
	}
	if p, _ := e.GetPost(old.ID); p.Content != "old board" || p.Community != "Board" {
		t.Fatalf("old post: %+v", p)
	}
	if p, _ := e.GetPostByCommunityFile("Board", "M.1.A.001"); p.ID != newer.ID {
		t.Fatalf("GetPostByCommunityFile found post %d, want %d", p.ID, newer.ID)
	}
}

func TestMergeCommunity(t *testing.T) {
	e, dir := openTestEngine(t)

	moving := mustCreatePost(t, e, "From", "M.1.A.001", "moving post")
	if _, err := e.AddComment(&model.Comment{PostID: moving.ID, CreatorName: "c1", Content: "push one"}); err != nil {
		t.Fatal(err)
	}
	if _, _, err := e.VotePost(moving.ID, "voter", 0, model.VoteUp, 0, ""); err != nil {
		t.Fatal(err)
	}
	if _, _, err := e.UpdatePost(moving.ID, "", "moving post, edited", "author", 0, 0); err != nil {
		t.Fatal(err)
	}
	if _, err := e.DeletePost(mustCreatePost(t, e, "From", "M.1.A.003", "deleted").ID, "author", 0, ""); err != nil {
		t.Fatal(err)
	}
	colliding := mustCreatePost(t, e, "From", "M.1.A.002", "colliding post")
	kept := mustCreatePost(t, e, "To", "M.1.A.002", "target post")
	if _, _, err := e.AddCrosspost("To", "M.1.A.002", "From", "M.1.A.001", "op", 0, 0); err != nil {
		t.Fatal(err)
	}

	moved, collisions, err := e.MergeCommunity("from", "TO")
	if err != nil || moved != 2 || collisions != 1 {
		t.Fatalf("MergeCommunity = %d %d %v, want 2 1", moved, collisions, err)
	}

	p, err := e.GetPostByCommunityFile("To", "M.1.A.001")
	if err != nil || p.ID != moving.ID || p.Community != "To" || p.Content != "moving post, edited" {
		t.Fatalf("moved post: %+v, err=%v", p, err)
	}
	if cmts, _ := e.GetComments(p.ID, 1, 10); len(cmts) != 1 {
		t.Fatalf("comments of the moved post: %d", len(cmts))
	}
	if votes, _ := e.GetVotes(p.ID); len(votes) != 1 {
		t.Fatalf("votes of the moved post: %d", len(votes))
	}
	if revs, _ := e.ListPostHistory(p.ID); len(revs) != 1 || revs[0].Community != "To" {
		t.Fatalf("history of the moved post: %+v", revs)
	}
	if _, err := os.Stat(filepath.Join(dir, "cache", strconv.FormatInt(p.CommunityID, 10), "M.1.A.001")); err != nil {
		t.Fatalf("cache file not moved: %v", err)
	}
	if list, _ := e.ListDeletedPosts("To", 10, 0); len(list) != 1 {
		t.Fatalf("deleted posts moved: %d", len(list))
	}
	if _, rec, _ := e.GetOriginPost("To", "M.1.A.001"); rec == nil {
		t.Fatalf("crosspost to the moved post not retargeted")
	}

	// The colliding post stays in From, the target's post in To.
	if p, err := e.GetPostByCommunityFile("From", "M.1.A.002"); err != nil || p.ID != colliding.ID {
		t.Fatalf("colliding post: %+v, err=%v", p, err)
	}
	if p, err := e.GetPostByCommunityFile("To", "M.1.A.002"); err != nil || p.ID != kept.ID {
		t.Fatalf("target post: %+v, err=%v", p, err)
	}

	// The moved posts survive a rebuild under the target.
	if _, _, err := e.RebuildSQLiteFromPebble(); err != nil {
		t.Fatal(err)
	}
	if n, _ := e.CountPosts("To"); n != 2 {
		t.Fatalf("CountPosts(To) after rebuild = %d, want 2", n)
	}
	if moved, _, _ := e.MergeCommunity("NoSuchBoard", "To"); moved != 0 {
		t.Fatalf("merging an unknown community moved %d posts", moved)
	}
	if _, _, err := e.MergeCommunity("To", "to"); err == nil {
		t.Fatalf("merging a community into itself succeeded")
	}
}

func openTestSharded(t *testing.T, numShards int, pins map[string]int) (*ShardedEngine, string) {
	t.Helper()
	dir := t.TempDir()
	locations := make([]config.EngineLocation, numShards)
	for i := range locations {
		locations[i] = config.EngineLocation{
			Index:    i,
			DataDir:  filepath.Join(dir, fmt.Sprintf("disk%d", i)),
			CacheDir: filepath.Join(dir, fmt.Sprintf("cache%d", i)),
		}
	}
	s, err := OpenShardedEngine(Config{
		BBSHome:        filepath.Join(dir, "bbshome"),
		CacheSizeMB:    128,
		FlushSeconds:   1,
		FilterEncoding: "utf-8",
	}, locations, pins)
	if err != nil {
		t.Fatalf("OpenShardedEngine failed: %v", err)
	}
	t.Cleanup(func() { s.Close() })
	// The same disk usage everywhere, so new communities take turns.
	s.diskUsage = func(int) int64 { return 0 }
	return s, dir
}

func TestShardedCommunityRouting(t *testing.T) {
	const numShards = 4
	s, dir := openTestSharded(t, numShards, map[string]int{"pinned": 3})

	if _, err := os.Stat(filepath.Join(dir, "disk0", CommunitiesDBName)); err != nil {
		t.Fatalf("community registry not in the first DataDir: %v", err)
	}

	var posts []*model.Post
	for i := 0; i < 6; i++ {
		posts = append(posts, mustCreatePost(t, s, fmt.Sprintf("Board%d", i), "M.1.A.001", "content"))
	}
	// With the same disk usage, new communities take turns.
	for i, p := range posts {
		want := s.engines[i%numShards]
		if shard, _ := s.reg.Shard(p.CommunityID); shard != i%numShards {
			t.Errorf("%s (#%d) stored on shard %d, want %d", p.Community, p.CommunityID, shard, i%numShards)
		}
		if s.GetEngineByCommunity(p.Community) != want || s.GetEngineByCommunityID(p.CommunityID) != want {
			t.Errorf("%s (#%d) not routed to shard %d", p.Community, p.CommunityID, i%numShards)
		}
		if s.GetEngineByPostID(p.ID) != want {
			t.Errorf("post %d of %s not on the shard of its community", p.ID, p.Community)
		}
	}

	// Renaming keeps the community (and its posts) on its shard.
	p := posts[1]
	if _, err := s.RenameCommunity(p.Community, "Renamed"); err != nil {
		t.Fatal(err)
	}
	if s.GetEngineByCommunity("renamed") != s.engines[1] {
		t.Errorf("renamed community moved to another shard")
	}
	if got, err := s.GetPostByCommunityFile("Renamed", "M.1.A.001"); err != nil || got.ID != p.ID {
		t.Fatalf("post after rename: %+v, err=%v", got, err)
	}

	// Unknown names find nothing.
	if _, err := s.GetPostByCommunityFile("NoSuchBoard", "M.1.A.001"); err == nil {
		t.Fatalf("found a post of an unknown community")
	}
	if list, err := s.ListPosts("NoSuchBoard", 10, 0); err != nil || len(list) != 0 {
		t.Fatalf("ListPosts of an unknown community = %v, %v", list, err)
	}

	// Pins are case-insensitive, and a pinned community keeps its shard
	// when renamed (without changing the pins).
	pp := mustCreatePost(t, s, "PINNED", "M.1.A.001", "pinned")
	if s.GetEngineByPostID(pp.ID) != s.engines[3] {
		t.Fatalf("pinned community not on shard 3")
	}
	if _, err := s.RenameCommunity("Pinned", "StillPinned"); err != nil {
		t.Fatal(err)
	}
	if got, err := s.GetPostByCommunityFile("StillPinned", "M.1.A.001"); err != nil || got.ID != pp.ID {
		t.Fatalf("pinned post after rename: %+v, err=%v", got, err)
	}

	// Remove, re-create and purge by ID.
	oldID, err := s.RemoveCommunity(posts[2].Community, 0, "")
	if err != nil {
		t.Fatal(err)
	}
	re := mustCreatePost(t, s, posts[2].Community, "M.1.A.001", "re-created")
	if re.CommunityID == oldID {
		t.Fatalf("re-created community reused #%d", oldID)
	}
	if n, _, err := s.PurgeCommunity(oldID); err != nil || n != 1 {
		t.Fatalf("PurgeCommunity(#%d) = %d, %v", oldID, n, err)
	}
	if _, err := s.GetPost(posts[2].ID); err == nil {
		t.Fatalf("post of the purged community still there")
	}
	if got, err := s.GetPostByCommunityFile(posts[2].Community, "M.1.A.001"); err != nil || got.ID != re.ID {
		t.Fatalf("post of the re-created community: %+v, err=%v", got, err)
	}
}

func TestShardedMergeAcrossShards(t *testing.T) {
	const numShards = 2
	s, dir := openTestSharded(t, numShards, nil)

	// Consecutive new communities land on different shards.
	src := mustCreatePost(t, s, "Src", "M.1.A.001", "source post")
	dstPost := mustCreatePost(t, s, "Dst", "M.2.A.001", "target post")
	if s.GetEngineByCommunityID(src.CommunityID) == s.GetEngineByCommunityID(dstPost.CommunityID) {
		t.Fatalf("test communities are on the same shard")
	}
	reply, err := s.CreatePost(&model.Post{
		Community:   "Src",
		PostFile:    "M.1.A.002",
		ParentID:    src.ID,
		Title:       "Re: source",
		CreatorName: "replier",
		Content:     "a reply that is long enough not to be demoted to a comment",
	})
	if err != nil {
		t.Fatal(err)
	}
	if _, err := s.AddComment(&model.Comment{PostID: src.ID, CreatorName: "c1", Content: "push one"}); err != nil {
		t.Fatal(err)
	}
	if _, err := s.AddComment(&model.Comment{PostID: src.ID, CreatorName: "c2", Content: "push two"}); err != nil {
		t.Fatal(err)
	}
	if _, _, err := s.VotePost(src.ID, "voter", 0, model.VoteDown, 0, ""); err != nil {
		t.Fatal(err)
	}
	if _, _, err := s.UpdatePost(src.ID, "", "source post, edited", "author", 0, 0); err != nil {
		t.Fatal(err)
	}
	if _, err := s.UpdateComment(src.ID, 1, "push one, edited", "c1", 0); err != nil {
		t.Fatal(err)
	}

	moved, collisions, err := s.MergeCommunity("Src", "Dst")
	if err != nil || moved != 2 || collisions != 0 {
		t.Fatalf("MergeCommunity = %d %d %v, want 2 0", moved, collisions, err)
	}

	p, err := s.GetPostByCommunityFile("Dst", "M.1.A.001")
	if err != nil {
		t.Fatalf("moved post not found under the target: %v", err)
	}
	if p.ID != src.ID || s.GetEngineByPostID(p.ID) != s.GetEngineByCommunityID(dstPost.CommunityID) {
		t.Fatalf("moved post %d (was %d) not on the target shard with its ID", p.ID, src.ID)
	}
	if p.Content != "source post, edited" || p.Community != "Dst" {
		t.Fatalf("moved post: %+v", p)
	}
	cmts, _ := s.GetComments(p.ID, 1, 10)
	if len(cmts) != 2 || cmts[0].Content != "push one, edited" {
		t.Fatalf("comments of the moved post: %+v", cmts)
	}
	if votes, _ := s.GetVotes(p.ID); len(votes) != 1 || votes[0].Vote != model.VoteDown {
		t.Fatalf("votes of the moved post: %+v", votes)
	}
	if revs, _ := s.ListPostHistory(p.ID); len(revs) != 1 || revs[0].PostID != p.ID || revs[0].Community != "Dst" {
		t.Fatalf("history of the moved post: %+v", revs)
	}
	if revs, _ := s.ListCommentHistory(p.ID, 1); len(revs) != 1 || revs[0].PostID != p.ID {
		t.Fatalf("comment history of the moved post: %+v", revs)
	}
	if r, err := s.GetPostByCommunityFile("Dst", "M.1.A.002"); err != nil || r.ID != reply.ID || r.ParentID != p.ID {
		t.Fatalf("moved reply does not keep its ID and parent: %+v, err=%v", r, err)
	}
	srcEngine := s.GetEngineByCommunityID(src.CommunityID)
	if srcEngine.HasPost(src.ID) || srcEngine.HasPost(reply.ID) {
		t.Fatalf("moved posts still on the source shard")
	}
	if got, err := s.GetPost(reply.ID); err != nil || got.Community != "Dst" {
		t.Fatalf("GetPost(%d) after merge = %+v, %v", reply.ID, got, err)
	}
	if n, _ := s.CountPosts("Src"); n != 0 {
		t.Fatalf("CountPosts(Src) = %d after merge", n)
	}
	dstShard, _ := s.reg.Shard(dstPost.CommunityID)
	dstCache := filepath.Join(dir, fmt.Sprintf("cache%d", dstShard), strconv.FormatInt(dstPost.CommunityID, 10), "M.1.A.001")
	if _, err := os.Stat(dstCache); err != nil {
		t.Fatalf("cache of the moved post not written: %v", err)
	}
}

func TestCommunityUpsert(t *testing.T) {
	dir := t.TempDir()
	reg, err := OpenCommunityRegistry(dir)
	if err != nil {
		t.Fatal(err)
	}
	sysop := model.MakePersonID(1, 1600000000)
	other := model.MakePersonID(2, 1600000001)
	id, err := reg.Upsert(&model.Community{
		Name:        "Test",
		Title:       "測試",
		Description: "about",
		Category:    "站務",
		Flags:       model.BrdOver18,
		BID:         3,
		Moderators:  []model.CommunityModerator{{PersonID: sysop, PersonName: "SYSOP"}},
	})
	if err != nil || id == 0 {
		t.Fatalf("Upsert = %d, %v", id, err)
	}
	c, ok := reg.GetByName("test")
	if !ok || c.ID != id || c.Title != "測試" || !c.NSFW || c.Published == 0 || len(c.Moderators) != 1 || c.Moderators[0].CommunityID != id {
		t.Fatalf("after upsert: %+v", c)
	}
	firstSeen := c.Moderators[0].Published

	// A second upsert (by any case of the name) updates it in place and
	// replaces the moderators; the description is kept as .BRD has none.
	if id2, err := reg.Upsert(&model.Community{
		Name:       "TEST",
		Title:      "測試二",
		Flags:      model.BrdHide,
		Moderators: []model.CommunityModerator{{PersonID: other, PersonName: "other"}, {PersonID: sysop, PersonName: "SYSOP"}, {PersonID: other}},
	}); err != nil || id2 != id {
		t.Fatalf("second Upsert = %d, %v", id2, err)
	}
	reg.Close()

	reg, err = OpenCommunityRegistry(dir)
	if err != nil {
		t.Fatal(err)
	}
	defer reg.Close()
	c, _ = reg.Get(id)
	if c.Name != "Test" || c.Title != "測試二" || c.Description != "about" || c.NSFW || !c.Hidden || c.Category != "" {
		t.Fatalf("after second upsert and reopen: %+v", c)
	}
	if len(c.Moderators) != 2 || c.Moderators[0].PersonID != sysop || c.Moderators[0].Published != firstSeen || c.Moderators[1].PersonID != other {
		t.Fatalf("moderators: %+v", c.Moderators)
	}
	if list := reg.List(); len(list) != 1 || len(list[0].Moderators) != 2 {
		t.Fatalf("List: %+v", list)
	}

	// A board renamed in .BRD (same bid, new name) does not become a
	// second community.
	if _, err := reg.Upsert(&model.Community{Name: "Test", BID: 3, Moderators: c.Moderators}); err != nil {
		t.Fatal(err)
	}
	if _, err := reg.Upsert(&model.Community{Name: "Renamed", BID: 3}); !errors.Is(err, ErrCommunityExists) {
		t.Fatalf("upsert of a renamed board: err = %v, want ErrCommunityExists", err)
	}
	if _, ok := reg.Lookup("Renamed"); ok {
		t.Fatalf("upsert of a renamed board registered it")
	}
	if _, err := reg.Upsert(&model.Community{Name: "Test", BID: 3}); err != nil {
		t.Fatalf("upsert of the same board: %v", err)
	}

	// Upserting a removed name registers a new community.
	if _, err := reg.RemoveCommunity("Test", 0, ""); err != nil {
		t.Fatal(err)
	}
	if id3, _ := reg.Upsert(&model.Community{Name: "Test"}); id3 == id {
		t.Fatalf("upsert of a removed name reused #%d", id)
	}
	if err := reg.Purge(id); err != nil {
		t.Fatal(err)
	}
}

// TestCommunityBIDIndex checks that a new community is refused the board
// slot (BID) of an active community, through the index of the slots, as
// communities are taken down or move between slots; and that the counts of
// the communities per shard given to the assigner follow the registry.
func TestCommunityBIDIndex(t *testing.T) {
	dir := t.TempDir()
	reg, err := OpenCommunityRegistry(dir)
	if err != nil {
		t.Fatal(err)
	}
	var lastCounts map[int]int
	next := 0
	assign := func(_ string, _ int64, counts map[int]int) int {
		lastCounts = counts
		next = (next + 1) % 2
		return next
	}
	reg.SetShardAssigner(assign)
	upsert := func(name string, bid int) error {
		_, err := reg.Upsert(&model.Community{Name: name, BID: bid})
		return err
	}
	for i := 1; i <= 4; i++ {
		if err := upsert(fmt.Sprintf("B%d", i), i); err != nil {
			t.Fatal(err)
		}
	}
	if lastCounts[0]+lastCounts[1] != 3 {
		t.Fatalf("counts given for the 4th community: %v, want 3 in all", lastCounts)
	}
	if err := upsert("New", 2); !errors.Is(err, ErrCommunityExists) {
		t.Fatalf("new community on the slot of B2: err = %v, want ErrCommunityExists", err)
	}

	// A taken-down community frees its slot.
	if _, err := reg.RemoveCommunity("B2", 0, ""); err != nil {
		t.Fatal(err)
	}
	if err := upsert("New", 2); err != nil {
		t.Fatalf("new community on the slot of removed B2: %v", err)
	}

	// B3 moves to slot 5: slot 3 is free, slot 5 is not.
	if err := upsert("B3", 5); err != nil {
		t.Fatal(err)
	}
	if err := upsert("Other", 5); !errors.Is(err, ErrCommunityExists) {
		t.Fatalf("new community on the new slot of B3: err = %v, want ErrCommunityExists", err)
	}
	if err := upsert("Three", 3); err != nil {
		t.Fatalf("new community on the old slot of B3: %v", err)
	}

	// Two active communities on one slot (B4 moved onto it): taking one
	// down still leaves the slot to the other.
	if err := upsert("B4", 1); err != nil {
		t.Fatal(err)
	}
	if _, err := reg.RemoveCommunity("B4", 0, ""); err != nil {
		t.Fatal(err)
	}
	if err := upsert("Again", 1); !errors.Is(err, ErrCommunityExists) {
		t.Fatalf("new community on the slot of B1: err = %v, want ErrCommunityExists", err)
	}

	// The same after reopening, and the counts include the taken-down
	// communities and survive moves and purges.
	reg.Close()
	if reg, err = OpenCommunityRegistry(dir); err != nil {
		t.Fatal(err)
	}
	defer reg.Close()
	reg.SetShardAssigner(assign)
	if err := upsert("Again", 1); !errors.Is(err, ErrCommunityExists) {
		t.Fatalf("after reopen, new community on the slot of B1: err = %v, want ErrCommunityExists", err)
	}
	id, _ := reg.Lookup("B1")
	if err := reg.SetShard(id, 1); err != nil {
		t.Fatal(err)
	}
	b2 := int64(0)
	for _, c := range reg.List() {
		if c.Name == "B2" {
			b2 = c.ID
		}
	}
	if err := reg.Purge(b2); err != nil {
		t.Fatal(err)
	}
	want := map[int]int{}
	for _, c := range reg.List() {
		want[c.Shard]++
	}
	if err := upsert("Last", 0); err != nil {
		t.Fatal(err)
	}
	if lastCounts[0] != want[0] || lastCounts[1] != want[1] {
		t.Fatalf("counts given after reopen, move and purge: %v, want %v", lastCounts, want)
	}
}

func TestShardedRoutingSurvivesReopen(t *testing.T) {
	dir := t.TempDir()
	locations := make([]config.EngineLocation, 3)
	for i := range locations {
		locations[i] = config.EngineLocation{
			Index:    i,
			DataDir:  filepath.Join(dir, fmt.Sprintf("disk%d", i)),
			CacheDir: filepath.Join(dir, fmt.Sprintf("cache%d", i)),
		}
	}
	open := func(pins map[string]int) *ShardedEngine {
		t.Helper()
		s, err := OpenShardedEngine(Config{BBSHome: filepath.Join(dir, "bbshome"), FilterEncoding: "utf-8"}, locations, pins)
		if err != nil {
			t.Fatal(err)
		}
		return s
	}

	s := open(map[string]int{"Pinned": 2})
	pinned := mustCreatePost(t, s, "Pinned", "M.1.A.001", "pinned")
	plain := mustCreatePost(t, s, "Plain", "M.1.A.001", "plain")
	if _, err := s.RenameCommunity("Pinned", "Moved"); err != nil {
		t.Fatal(err)
	}
	s.Close()

	// Different pins after a restart: existing communities stay where
	// they are; only new ones follow the pins.
	s = open(map[string]int{"Plain": 0, "Fresh": 1})
	defer func() { s.Close() }()
	if got, err := s.GetPostByCommunityFile("Moved", "M.1.A.001"); err != nil || got.ID != pinned.ID {
		t.Fatalf("renamed pinned community after reopen: %+v, err=%v", got, err)
	}
	if got, err := s.GetPostByCommunityFile("Plain", "M.1.A.001"); err != nil || got.ID != plain.ID {
		t.Fatalf("unpinned community after re-pinning: %+v, err=%v", got, err)
	}
	fresh := mustCreatePost(t, s, "Fresh", "M.1.A.001", "fresh")
	if s.GetEngineByPostID(fresh.ID) != s.engines[1] {
		t.Fatalf("new community not on its pinned shard")
	}
	s.Close()

	// An engine added later takes the new communities until its disk
	// usage catches up; existing communities stay where they are.
	locations = append(locations, config.EngineLocation{
		Index:    3,
		DataDir:  filepath.Join(dir, "disk3"),
		CacheDir: filepath.Join(dir, "cache3"),
	})
	s = open(nil)
	if got, err := s.GetPostByCommunityFile("Plain", "M.1.A.001"); err != nil || got.ID != plain.ID {
		t.Fatalf("existing community after adding an engine: %+v, err=%v", got, err)
	}
	// 3 communities with 1200 bytes, 400 each on average.
	s.diskUsage = func(shard int) int64 { return []int64{1000, 100, 100, 0}[shard] }
	for i, want := range []int{3, 1, 2, 3, 1} {
		id, err := s.Communities().Ensure(fmt.Sprintf("Later%d", i))
		if err != nil {
			t.Fatal(err)
		}
		if shard, _ := s.reg.Shard(id); shard != want {
			t.Errorf("Later%d on shard %d, want %d", i, shard, want)
		}
	}
}

func TestShardedAssignByDiskUsage(t *testing.T) {
	s, _ := openTestSharded(t, 3, map[string]int{"Heavy": 0, "A1": 1, "A2": 1, "B1": 2, "B2": 2})
	for _, name := range []string{"Heavy", "A1", "A2", "B1", "B2"} {
		if _, err := s.Communities().Ensure(name); err != nil {
			t.Fatal(err)
		}
	}

	// Shard 0 has the fewest communities but the most data: 5
	// communities with 1500 bytes, 300 each on average.
	usage := []int64{900, 300, 300}
	s.diskUsage = func(shard int) int64 { return usage[shard] }
	s.usageAt = time.Time{} // measure again
	ensure := func(name string, want int) {
		t.Helper()
		id, err := s.Communities().Ensure(name)
		if err != nil {
			t.Fatal(err)
		}
		if shard, _ := s.reg.Shard(id); shard != want {
			t.Errorf("%s on shard %d, want %d", name, shard, want)
		}
	}
	// Boards registered before their posts arrive (no new usage) count
	// 300 each, so they are spread, and shard 0 gets none until the others
	// catch up.
	for i, want := range []int{1, 2, 1, 2, 0} {
		ensure(fmt.Sprintf("New%d", i), want)
	}

	// Usage is measured again after usageTTL.
	usage = []int64{900, 2000, 300}
	ensure("BeforeTTL", 1)
	s.usageAt = time.Now().Add(-usageTTL)
	ensure("AfterTTL", 2)

	// Engines measure the bytes of their databases.
	e := s.engines[0]
	before := e.DiskUsage()
	if before <= 0 {
		t.Fatalf("DiskUsage() = %d", before)
	}
	mustCreatePost(t, s, "Heavy", "M.1.A.001", strings.Repeat("content ", 1<<14))
	if after := e.DiskUsage(); after <= before {
		t.Errorf("DiskUsage() = %d after a post, was %d", after, before)
	}
}

func TestShardedMoveCommunity(t *testing.T) {
	const numShards = 2
	s, dir := openTestSharded(t, numShards, map[string]int{"Board": 0, "Other": 0})

	post := mustCreatePost(t, s, "Board", "M.1.A.001", "post to be moved")
	reply, err := s.CreatePost(&model.Post{
		Community:   "Board",
		PostFile:    "M.1.A.002",
		ParentID:    post.ID,
		Title:       "Re: post",
		CreatorName: "replier",
		Content:     "a reply that is long enough not to be demoted to a comment",
	})
	if err != nil {
		t.Fatal(err)
	}
	copied := mustCreatePost(t, s, "Board", "M.1.A.003", "crosspost of the post")
	if _, _, err := s.AddCrosspost("Board", "M.1.A.001", "Board", "M.1.A.003", "sharer", 0, 0); err != nil {
		t.Fatal(err)
	}
	other := mustCreatePost(t, s, "Other", "M.1.A.001", "stays")
	if _, err := s.AddComment(&model.Comment{PostID: post.ID, CreatorName: "c1", Content: "push one"}); err != nil {
		t.Fatal(err)
	}
	if _, _, err := s.VotePost(post.ID, "voter", 0, model.VoteUp, 0, ""); err != nil {
		t.Fatal(err)
	}
	if _, _, err := s.UpdatePost(post.ID, "", "post to be moved, edited", "author", 0, 0); err != nil {
		t.Fatal(err)
	}
	if _, err := s.UpdateComment(post.ID, 1, "push one, edited", "c1", 0); err != nil {
		t.Fatal(err)
	}

	if _, err := s.MoveCommunity("Board", numShards); err == nil {
		t.Fatalf("moved to an engine that does not exist")
	}
	if _, err := s.MoveCommunity("NoSuchBoard", 1); err == nil {
		t.Fatalf("moved an unknown community")
	}
	if n, err := s.MoveCommunity("Board", 0); err != nil || n != 0 {
		t.Fatalf("MoveCommunity to its own engine = %d, %v", n, err)
	}
	if n, err := s.MoveCommunity("board", 1); err != nil || n != 3 {
		t.Fatalf("MoveCommunity = %d, %v; want 3", n, err)
	}

	if shard, _ := s.reg.Shard(post.CommunityID); shard != 1 {
		t.Fatalf("Board on shard %d after the move", shard)
	}
	if s.engines[0].HasPost(post.ID) || s.engines[0].HasPost(reply.ID) || s.engines[0].HasPost(copied.ID) {
		t.Fatalf("moved posts still on the old engine")
	}
	if s.GetEngineByPostID(other.ID) != s.engines[0] {
		t.Fatalf("post of another community moved")
	}
	p, err := s.GetPostByCommunityFile("Board", "M.1.A.001")
	if err != nil || p.ID != post.ID || p.Content != "post to be moved, edited" || p.Upvotes != 1 {
		t.Fatalf("moved post: %+v, err=%v", p, err)
	}
	if got, err := s.GetPost(post.ID); err != nil || got.Community != "Board" {
		t.Fatalf("GetPost(%d) after the move = %+v, %v", post.ID, got, err)
	}
	if r, err := s.GetPostByCommunityFile("Board", "M.1.A.002"); err != nil || r.ID != reply.ID || r.ParentID != post.ID {
		t.Fatalf("moved reply: %+v, err=%v", r, err)
	}
	if cmts, _ := s.GetComments(post.ID, 1, 10); len(cmts) != 1 || cmts[0].Content != "push one, edited" {
		t.Fatalf("comments of the moved post: %+v", cmts)
	}
	if revs, _ := s.ListPostHistory(post.ID); len(revs) != 1 {
		t.Fatalf("history of the moved post: %+v", revs)
	}
	if revs, _ := s.ListCommentHistory(post.ID, 1); len(revs) != 1 {
		t.Fatalf("comment history of the moved post: %+v", revs)
	}
	if cps, err := s.GetCrossposts("Board", "M.1.A.001"); err != nil || len(cps) != 1 || cps[0].TargetPostFile != "M.1.A.003" {
		t.Fatalf("crossposts of the moved post: %+v, err=%v", cps, err)
	}
	if origin, _, err := s.GetOriginPost("Board", "M.1.A.003"); err != nil || origin == nil || origin.ID != post.ID {
		t.Fatalf("origin of the moved crosspost: %+v, err=%v", origin, err)
	}
	cache := filepath.Join(dir, "cache1", strconv.FormatInt(post.CommunityID, 10), "M.1.A.001")
	if _, err := os.Stat(cache); err != nil {
		t.Fatalf("cache of the moved post not written: %v", err)
	}

	// New posts of the community go to its new engine.
	fresh := mustCreatePost(t, s, "Board", "M.2.A.001", "after the move")
	if s.GetEngineByPostID(fresh.ID) != s.engines[1] {
		t.Fatalf("new post not on the new engine")
	}
}

func TestShardedCrosspostAcrossEngines(t *testing.T) {
	s, _ := openTestSharded(t, 2, map[string]int{"Src": 0, "Dst": 1})
	src := mustCreatePost(t, s, "Src", "M.1.A.001", "original")
	mustCreatePost(t, s, "Dst", "M.2.A.001", "copy")
	rec, n, err := s.AddCrosspost("Src", "M.1.A.001", "Dst", "M.2.A.001", "sharer", 0, 0)
	if err != nil || n != 1 || rec.SourcePostID != src.ID {
		t.Fatalf("AddCrosspost = %+v, %d, %v", rec, n, err)
	}
	if cps, err := s.GetCrossposts("Src", "M.1.A.001"); err != nil || len(cps) != 1 || cps[0].TargetCommunity != "Dst" {
		t.Fatalf("GetCrossposts = %+v, %v", cps, err)
	}
	if origin, _, err := s.GetOriginPost("Dst", "M.2.A.001"); err != nil || origin == nil || origin.ID != src.ID {
		t.Fatalf("GetOriginPost = %+v, %v", origin, err)
	}
	if p, _ := s.GetPost(src.ID); p.NumCrossposts != 1 {
		t.Fatalf("source post has %d crossposts, want 1", p.NumCrossposts)
	}
}

func TestShardedQueryPosts(t *testing.T) {
	s, _ := openTestSharded(t, 2, map[string]int{"A": 0, "B": 1})
	for i, c := range []string{"A", "B", "A", "B", "A"} {
		if _, err := s.CreatePost(&model.Post{Community: c, PostFile: fmt.Sprintf("M.%d.A.001", 1000+i), Title: "t", CreatorName: "u", CreatedAt: int64(1000 + i), Content: "x"}); err != nil {
			t.Fatal(err)
		}
	}
	posts, err := s.QueryPosts(PostQuery{Sort: SortNew, Limit: 2, Offset: 1})
	if err != nil || len(posts) != 2 || posts[0].CreatedAt != 1003 || posts[1].CreatedAt != 1002 {
		t.Fatalf("QueryPosts(all, New, 2 from 1) = %+v, %v", posts, err)
	}
	a, _ := s.Communities().Lookup("A")
	posts, err = s.QueryPosts(PostQuery{CommunityIDs: []int64{a}, Sort: SortOld})
	if err != nil || len(posts) != 3 || posts[0].CreatedAt != 1000 || posts[2].CreatedAt != 1004 {
		t.Fatalf("QueryPosts(A, Old) = %+v, %v", posts, err)
	}
	if _, err := s.QueryPosts(PostQuery{Sort: "Hot"}); err == nil {
		t.Fatalf("QueryPosts accepted an unsupported sort")
	}
}
