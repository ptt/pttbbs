package storage

import (
	"database/sql"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"pttbbs/nest/config"
	"pttbbs/nest/model"
)

func testEngineConfig(dir string) Config {
	return Config{
		DataDir:        filepath.Join(dir, "db"),
		CacheDir:       filepath.Join(dir, "cache"),
		CacheSizeMB:    16,
		FlushSeconds:   3600, // Backup must flush pending counter deltas itself.
		FilterEncoding: "utf-8",
	}
}

func TestEngineBackup(t *testing.T) {
	tmpDir := t.TempDir()
	e, err := OpenEngine(testEngineConfig(tmpDir))
	if err != nil {
		t.Fatalf("OpenEngine: %v", err)
	}
	defer e.Close()

	p, err := e.CreatePost(&model.Post{
		Community: "testboard", PostFile: "M.1.A.001", Title: "backup me",
		CreatorName: "author", CreatedAt: 1727800000, Content: "post body",
	})
	if err != nil {
		t.Fatalf("CreatePost: %v", err)
	}
	if _, err := e.AddComment(&model.Comment{PostID: p.ID, CreatorName: "c1", Content: "hi", CreatedAt: 1727800001}); err != nil {
		t.Fatalf("AddComment: %v", err)
	}
	if _, _, err := e.VotePost(p.ID, "voter", 0, model.VoteUp, 1727800002, "127.0.0.1"); err != nil {
		t.Fatalf("VotePost: %v", err)
	}
	if _, _, err := e.UpdatePost(p.ID, "backup me v2", "edited body", "author", 0, 0); err != nil {
		t.Fatalf("UpdatePost: %v", err)
	}

	backupDir := filepath.Join(tmpDir, "backups", "b1")
	if dirs, err := e.Backup(backupDir); err != nil || len(dirs) != 1 || dirs[0] != backupDir {
		t.Fatalf("Backup = %v, %v; want [%s]", dirs, err, backupDir)
	}
	if _, err := e.Backup(backupDir); err == nil {
		t.Fatalf("Backup to an existing destination should fail")
	}
	if matches, _ := filepath.Glob(backupDir + ".tmp-*"); len(matches) != 0 {
		t.Fatalf("temporary backup dirs left behind: %v", matches)
	}

	// Writes after the backup must not show up in it.
	if _, err := e.AddComment(&model.Comment{PostID: p.ID, CreatorName: "c2", Content: "late", CreatedAt: 1727800003}); err != nil {
		t.Fatalf("AddComment: %v", err)
	}

	cfg := testEngineConfig(tmpDir)
	cfg.DataDir = backupDir
	cfg.CacheDir = filepath.Join(tmpDir, "restore-cache")
	r, err := OpenEngine(cfg)
	if err != nil {
		t.Fatalf("open backup: %v", err)
	}
	defer r.Close()

	got, err := r.GetPost(p.ID)
	if err != nil {
		t.Fatalf("GetPost from backup: %v", err)
	}
	if got.Title != "backup me v2" || got.Content != "edited body" {
		t.Errorf("backup post = %q / %q, want edited title and content", got.Title, got.Content)
	}
	if got.Upvotes != 1 || got.NumComments != 1 {
		t.Errorf("backup counters: upvotes=%d comments=%d, want 1 and 1", got.Upvotes, got.NumComments)
	}
	if n, _ := r.GetCommentCount(p.ID); n != 1 {
		t.Errorf("backup comment count = %d, want 1", n)
	}
	if hist, _ := r.ListPostHistory(p.ID); len(hist) == 0 {
		t.Errorf("backup lost post history")
	}
	if got.Community != "testboard" {
		t.Errorf("backup community = %q, want testboard (from communities.db)", got.Community)
	}

	// The backup must also survive the documented restore step.
	if posts, comments, err := r.RebuildSQLiteFromPebble(); err != nil || posts != 1 || comments != 1 {
		t.Errorf("rebuild on backup = %d posts, %d comments, %v; want 1, 1, nil", posts, comments, err)
	}
}

func TestShardedEngineBackup(t *testing.T) {
	tmpDir := t.TempDir()
	const numShards = 2
	locations := make([]config.EngineLocation, numShards)
	for i := range locations {
		locations[i] = config.EngineLocation{
			Index:    i,
			DataDir:  filepath.Join(tmpDir, fmt.Sprintf("disk%d", i)),
			CacheDir: filepath.Join(tmpDir, fmt.Sprintf("cache%d", i)),
		}
	}
	s, err := OpenShardedEngine(Config{CacheSizeMB: 64, FilterEncoding: "utf-8"}, locations, nil)
	if err != nil {
		t.Fatalf("OpenShardedEngine: %v", err)
	}
	defer s.Close()

	for i := 0; i < 8; i++ {
		if _, err := s.CreatePost(&model.Post{
			Community: fmt.Sprintf("board%d", i), PostFile: "M.1.A.001", Title: "t",
			CreatorName: "author", CreatedAt: 1727800000, Content: "body",
		}); err != nil {
			t.Fatalf("CreatePost: %v", err)
		}
	}

	if _, err := s.Backup("bad/name"); err == nil {
		t.Errorf("Backup with a relative path should fail")
	}

	// Named backup: every shard writes next to its own data dir (own disk).
	dirs, err := s.Backup("nightly")
	if err != nil {
		t.Fatalf("Backup(nightly): %v", err)
	}
	if _, err := s.Backup("nightly"); err == nil {
		t.Errorf("Backup to an existing name should fail")
	}
	// Absolute path: shards go to <path>/shard-<N>.
	absDir := filepath.Join(tmpDir, "offsite")
	absDirs, err := s.Backup(absDir)
	if err != nil {
		t.Fatalf("Backup(%s): %v", absDir, err)
	}

	for i := 0; i < numShards; i++ {
		if want := filepath.Join(locations[i].DataDir, BackupSubdir, "nightly"); dirs[i] != want {
			t.Errorf("named backup shard %d = %s, want %s", i, dirs[i], want)
		}
		if want := filepath.Join(absDir, fmt.Sprintf("shard-%d", i)); absDirs[i] != want {
			t.Errorf("absolute backup shard %d = %s, want %s", i, absDirs[i], want)
		}
	}

	// communities.db is shared and lives with shard 0 only.
	for i, set := range [][]string{dirs, absDirs} {
		for j, d := range set {
			_, err := os.Stat(filepath.Join(d, CommunitiesDBName))
			if (err == nil) != (j == 0) {
				t.Errorf("backup set %d shard %d: has communities.db = %v, want %v", i, j, err == nil, j == 0)
			}
		}
		communities := FindCommunitiesDB(set)
		for _, d := range set {
			if r := VerifyDataDir(d, communities); !r.OK() {
				var out strings.Builder
				r.Print(&out)
				t.Errorf("verify %s:\n%s", d, out.String())
			}
		}
		if r := VerifyShards(set, communities); !r.OK() || r.Posts != 8 {
			var out strings.Builder
			r.Print(&out)
			t.Errorf("verify shards of set %d (%d posts):\n%s", i, r.Posts, out.String())
		}
	}
	// The same engine twice: every post ID is used twice, and both copies
	// hold the communities of the same shard.
	if r := VerifyShards([]string{dirs[0], dirs[0]}, FindCommunitiesDB(dirs)); r.OK() {
		t.Errorf("VerifyShards passed with every post in two engines")
	}

	total := 0
	for i, shardDir := range dirs {
		for _, name := range []string{"posts.pebble", "comments.pebble", "votes.pebble", "history.pebble"} {
			if _, err := os.Stat(filepath.Join(shardDir, name)); err != nil {
				t.Fatalf("missing %s in shard %d backup: %v", name, i, err)
			}
		}
		cfg := Config{DataDir: shardDir, CacheDir: filepath.Join(tmpDir, fmt.Sprintf("rc%d", i)), CacheSizeMB: 16, FilterEncoding: "utf-8"}
		r, err := OpenEngine(cfg)
		if err != nil {
			t.Fatalf("open shard %d backup: %v", i, err)
		}
		posts, _, err := r.RebuildSQLiteFromPebble()
		r.Close()
		if err != nil {
			t.Fatalf("rebuild shard %d backup: %v", i, err)
		}
		total += posts
	}
	if total != 8 {
		t.Errorf("backup holds %d posts across shards, want 8", total)
	}
}

func dirSnapshot(t *testing.T, root string) map[string]string {
	t.Helper()
	out := map[string]string{}
	err := filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if !info.IsDir() {
			out[path] = fmt.Sprint(info.Size(), info.ModTime().UnixNano())
		}
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	return out
}

func TestVerifyDataDir(t *testing.T) {
	tmpDir := t.TempDir()
	e, err := OpenEngine(testEngineConfig(tmpDir))
	if err != nil {
		t.Fatalf("OpenEngine: %v", err)
	}
	defer e.Close()

	for i := 1; i <= 3; i++ {
		p, err := e.CreatePost(&model.Post{Community: "board", PostFile: fmt.Sprintf("M.%d.A.001", i), Title: "t",
			CreatorName: "author", CreatedAt: 1727800000, Content: "body"})
		if err != nil {
			t.Fatal(err)
		}
		e.AddComment(&model.Comment{PostID: p.ID, CreatorName: "a", Content: "c", CreatedAt: 1727800001})
		e.VotePost(p.ID, "a", 0, model.VoteUp, 1727800002, "")
	}
	e.AddCrosspost("board", "M.1.A.001", "other", "M.9.A.001", "op", 0, 1727800003, false)
	e.DeleteComment(2, 1, "mod", 0, "spam")
	// The new title goes to Pebble too.
	if _, err := e.UpdatePostTitle(3, "new title", "author", 0); err != nil {
		t.Fatal(err)
	}

	backupDir := filepath.Join(tmpDir, "backup")
	if _, err := e.Backup(backupDir); err != nil {
		t.Fatalf("Backup: %v", err)
	}
	before := dirSnapshot(t, backupDir)
	r := VerifyDataDir(backupDir, FindCommunitiesDB([]string{backupDir}))
	var out strings.Builder
	r.Print(&out)
	t.Log(out.String())
	if !r.OK() || r.Posts != 3 || r.Comments != 3 || r.Votes != 3 {
		t.Errorf("VerifyDataDir = ok %v, %d posts, %d comments, %d votes; want ok, 3, 3, 3", r.OK(), r.Posts, r.Comments, r.Votes)
	}
	if len(r.Warnings) != 0 {
		t.Errorf("warnings = %v, want none", r.Warnings)
	}
	if after := dirSnapshot(t, backupDir); fmt.Sprint(after) != fmt.Sprint(before) {
		t.Errorf("VerifyDataDir modified the backup:\nbefore %v\nafter  %v", before, after)
	}

	// Without communities.db the community check is skipped with a warning.
	if r := VerifyDataDir(backupDir, ""); !r.OK() || r.Warnings["no communities.db to check community IDs against"] != 1 {
		t.Errorf("verify without communities.db: ok %v, warnings %v", r.OK(), r.Warnings)
	}

	// A community missing from the registry is an error: a restored service
	// could not name or route its posts.
	regPath := filepath.Join(backupDir, CommunitiesDBName)
	regCopy := filepath.Join(tmpDir, "communities-copy.db")
	if b, err := os.ReadFile(regPath); err != nil {
		t.Fatal(err)
	} else if err := os.WriteFile(regCopy, b, 0644); err != nil {
		t.Fatal(err)
	}
	reg, err := sql.Open("sqlite", regCopy)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := reg.Exec("DELETE FROM community"); err != nil {
		t.Fatal(err)
	}
	reg.Close()
	if r := VerifyDataDir(backupDir, regCopy); r.OK() {
		t.Errorf("VerifyDataDir passed with posts of communities missing from communities.db")
	}

	// A backup has no meta.sqlite3, which a restore rebuilds.
	if fileExists(filepath.Join(backupDir, "meta.sqlite3")) {
		t.Errorf("backup holds meta.sqlite3")
	}

	// A stopped data dir with meta.sqlite3 is compared with it, and a post
	// that SQLite does not know about is an error.
	dataDir := testEngineConfig(tmpDir).DataDir
	e.Close()
	if r := VerifyDataDir(dataDir, FindCommunitiesDB([]string{dataDir})); !r.OK() || r.Posts != 3 || len(r.Warnings) != 0 {
		var out strings.Builder
		r.Print(&out)
		t.Errorf("verify of the data dir:\n%s", out.String())
	}
	db, err := sql.Open("sqlite", filepath.Join(dataDir, "meta.sqlite3"))
	if err != nil {
		t.Fatal(err)
	}
	if _, err := db.Exec("DELETE FROM posts WHERE id = 2"); err != nil {
		t.Fatal(err)
	}
	db.Close()
	if r := VerifyDataDir(dataDir, FindCommunitiesDB([]string{dataDir})); r.OK() {
		t.Errorf("VerifyDataDir passed with a post missing from meta.sqlite3")
	}

	if dirs, err := ExpandVerifyDirs([]string{backupDir}); err != nil || len(dirs) != 1 || dirs[0] != backupDir {
		t.Errorf("ExpandVerifyDirs = %v, %v", dirs, err)
	}
	if _, err := ExpandVerifyDirs([]string{tmpDir}); err == nil {
		t.Errorf("ExpandVerifyDirs accepted a dir without a backup")
	}
}
