package importer

import (
	"os"
	"path/filepath"
	"testing"

	"pttbbs/post/model"
	"pttbbs/post/storage"
)

func exists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

// A locked post keeps its M. name in post.svc and is found by its L. name
// (as in .DIR); its board file is X.<stamp>, and .DIR gets it back as L.
func TestLockPost(t *testing.T) {
	tempDir := t.TempDir()
	boardDir := filepath.Join(tempDir, "boards", "L", "LockBoard")
	if err := os.MkdirAll(boardDir, 0755); err != nil {
		t.Fatal(err)
	}
	cfg := storage.Config{
		BBSHome:        tempDir,
		DataDir:        filepath.Join(tempDir, "engine"),
		CacheDir:       "boards",
		FilterEncoding: "utf-8",
	}
	engine, err := storage.OpenEngine(cfg)
	if err != nil {
		t.Fatal(err)
	}
	defer func() { engine.Close() }()

	const (
		mName = "M.1728000001.A.001"
		lName = "L.1728000001.A.001"
		xName = "X.1728000001.A.001"
	)
	p, err := engine.CreatePost(&model.Post{Community: "LockBoard", PostFile: mName, Title: "t", CreatorName: "userA", Content: "body\n"})
	if err != nil {
		t.Fatal(err)
	}
	if !exists(filepath.Join(boardDir, mName)) {
		t.Fatalf("board file not rendered")
	}

	if err := engine.LockPostByCommunityFile("LockBoard", lName, true); err != nil {
		t.Fatalf("LockPostByCommunityFile by the L. name: %v", err)
	}
	got, err := engine.GetPostByCommunityFile("LockBoard", lName)
	if err != nil || got.ID != p.ID || !got.Locked || got.PostFile != mName {
		t.Fatalf("locked post by its L. name = %+v, %v", got, err)
	}
	if exists(filepath.Join(boardDir, mName)) || !exists(filepath.Join(boardDir, xName)) {
		t.Fatalf("board file of a locked post should be renamed to %s", xName)
	}
	// Rendering a locked post (e.g. after a comment) keeps it hidden.
	if _, err := engine.AddComment(&model.Comment{PostID: p.ID, CreatorName: "userB", Content: "push"}); err != nil {
		t.Fatal(err)
	}
	_ = engine.RenderAndSave(got)
	if exists(filepath.Join(boardDir, mName)) {
		t.Fatalf("rendering a locked post wrote its M. file")
	}
	// mbbsd syncs filemode by the name in .DIR.
	if err := engine.SetPostMeta("LockBoard", lName, model.FileMarked, 0); err != nil {
		t.Fatalf("SetPostMeta by the L. name: %v", err)
	}

	stats, err := SyncDir(engine, SyncDirOptions{BBSHome: tempDir, Board: "LockBoard", Rebuild: true})
	if err != nil {
		t.Fatal(err)
	}
	recs := readDirRecords(t, filepath.Join(boardDir, ".DIR"))
	if len(recs) != 1 || recFilename(recs[0]) != lName || stats.Rendered != 0 {
		t.Fatalf("rebuilt .DIR: %d records, first %q, %d rendered", len(recs), recFilename(recs[0]), stats.Rendered)
	}

	// The lock survives rebuilding SQLite from Pebble.
	engine.FlushDirtyDeltas()
	if _, _, err := engine.RebuildSQLiteFromPebble(); err != nil {
		t.Fatal(err)
	}
	if got, _ := engine.GetPost(p.ID); !got.Locked || got.Filemode != model.FileMarked {
		t.Fatalf("post after rebuild: %+v", got)
	}

	if err := engine.LockPost(p.ID, false); err != nil {
		t.Fatal(err)
	}
	if !exists(filepath.Join(boardDir, mName)) || exists(filepath.Join(boardDir, xName)) {
		t.Fatalf("board file of an unlocked post should be %s again", mName)
	}

	// Importing a board with a locked post (L. in .DIR, X. file).
	importDir := filepath.Join(tempDir, "boards", "I", "ImportLock")
	if err := os.MkdirAll(importDir, 0755); err != nil {
		t.Fatal(err)
	}
	article := "作者: userA (A)\n看板: ImportLock\n標題: locked\n時間: Thu Oct  3 00:00:00 2024\n\nhidden body\n"
	if err := os.WriteFile(filepath.Join(importDir, "X.1728000002.A.002"), []byte(article), 0644); err != nil {
		t.Fatal(err)
	}
	dir := dirFixture{filename: "L.1728000002.A.002", owner: "userA", title: "locked"}.record()
	if err := os.WriteFile(filepath.Join(importDir, ".DIR"), dir, 0644); err != nil {
		t.Fatal(err)
	}
	if _, err := ImportBoard(engine, ImportBoardOptions{BBSHome: tempDir, Board: "ImportLock", Overwrite: true}); err != nil {
		t.Fatal(err)
	}
	ip, err := engine.GetPostByCommunityFile("ImportLock", "M.1728000002.A.002")
	if err != nil || !ip.Locked || ip.PostFile != "M.1728000002.A.002" {
		t.Fatalf("imported locked post = %+v, %v", ip, err)
	}
	if exists(filepath.Join(importDir, "M.1728000002.A.002")) {
		t.Fatalf("importing a locked post wrote its M. file")
	}
}
