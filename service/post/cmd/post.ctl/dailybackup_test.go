package main

import (
	"os"
	"path/filepath"
	"testing"
	"time"
)

func writeFile(t *testing.T, path, content string, mtime time.Time) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte(content), 0644); err != nil {
		t.Fatal(err)
	}
	if err := os.Chtimes(path, mtime, mtime); err != nil {
		t.Fatal(err)
	}
}

func sameInode(t *testing.T, a, b string) bool {
	t.Helper()
	ai, err1 := os.Stat(a)
	bi, err2 := os.Stat(b)
	if err1 != nil || err2 != nil {
		t.Fatalf("stat: %v %v", err1, err2)
	}
	return os.SameFile(ai, bi)
}

func TestCopyIncremental(t *testing.T) {
	root := t.TempDir()
	old := time.Unix(1700000000, 0)
	src := filepath.Join(root, "checkpoint")
	writeFile(t, filepath.Join(src, "posts.pebble", "000005.sst"), "old sst", old)
	writeFile(t, filepath.Join(src, "posts.pebble", "MANIFEST-000001"), "manifest v1", old)
	writeFile(t, filepath.Join(src, "meta.sqlite3"), "sqlite v1", old)

	day1 := filepath.Join(root, "copies", "daily-1")
	linked, copied, err := copyIncremental(src, day1, "")
	if err != nil || linked != 0 || copied != int64(len("old sst")+len("manifest v1")+len("sqlite v1")) {
		t.Fatalf("first copy: linked=%d copied=%d err=%v", linked, copied, err)
	}

	// Next day: a new SST appears, the old one is unchanged, and the
	// MANIFEST and SQLite file change (with the same size and mtime, to show
	// that only SSTs are ever linked).
	writeFile(t, filepath.Join(src, "posts.pebble", "000009.sst"), "new sst", time.Now())
	writeFile(t, filepath.Join(src, "posts.pebble", "MANIFEST-000001"), "manifest v2", old)
	writeFile(t, filepath.Join(src, "meta.sqlite3"), "sqlite v2", old)
	day2 := filepath.Join(root, "copies", "daily-2")
	linked, copied, err = copyIncremental(src, day2, day1)
	if err != nil {
		t.Fatal(err)
	}
	if linked != int64(len("old sst")) || copied != int64(len("new sst")+len("manifest v2")+len("sqlite v2")) {
		t.Errorf("second copy: linked=%d copied=%d", linked, copied)
	}
	if !sameInode(t, filepath.Join(day1, "posts.pebble", "000005.sst"), filepath.Join(day2, "posts.pebble", "000005.sst")) {
		t.Errorf("unchanged SST was not hard-linked")
	}
	if b, _ := os.ReadFile(filepath.Join(day2, "posts.pebble", "MANIFEST-000001")); string(b) != "manifest v2" {
		t.Errorf("MANIFEST = %q, want the new one", b)
	}
	if b, _ := os.ReadFile(filepath.Join(day1, "meta.sqlite3")); string(b) != "sqlite v1" {
		t.Errorf("previous copy was modified: %q", b)
	}
	if fi, _ := os.Stat(filepath.Join(day2, "posts.pebble", "000009.sst")); fi == nil || !fi.ModTime().Equal(
		mustStat(t, filepath.Join(src, "posts.pebble", "000009.sst")).ModTime()) {
		t.Errorf("mtime of copied SST not preserved")
	}
	if _, _, err := copyIncremental(src, day2, day1); err == nil {
		t.Errorf("copying onto an existing backup should fail")
	}

	if got := latestBackupDir(filepath.Join(root, "copies")); got != day2 {
		t.Errorf("latestBackupDir = %s, want %s", got, day2)
	}
}

func mustStat(t *testing.T, path string) os.FileInfo {
	t.Helper()
	fi, err := os.Stat(path)
	if err != nil {
		t.Fatal(err)
	}
	return fi
}

func TestPruneBackups(t *testing.T) {
	root := t.TempDir()
	for _, name := range []string{"daily-20261101-0400", "daily-20261102-0400", "daily-20261103-0400", "manual", "daily-20261104-0400.tmp-123", "shard-1"} {
		if err := os.MkdirAll(filepath.Join(root, name), 0755); err != nil {
			t.Fatal(err)
		}
	}
	n, err := pruneBackups(root, 2)
	if err != nil || n != 1 {
		t.Fatalf("pruneBackups = %d, %v; want 1", n, err)
	}
	for name, want := range map[string]bool{
		"daily-20261101-0400": false, "daily-20261102-0400": true, "daily-20261103-0400": true,
		"manual": true, "shard-1": true, "daily-20261104-0400.tmp-123": true,
	} {
		if _, err := os.Stat(filepath.Join(root, name)); (err == nil) != want {
			t.Errorf("%s exists = %v, want %v", name, err == nil, want)
		}
	}
	removeStaleTmpDirs(root)
	if _, err := os.Stat(filepath.Join(root, "daily-20261104-0400.tmp-123")); err == nil {
		t.Errorf("stale tmp dir not removed")
	}
}
