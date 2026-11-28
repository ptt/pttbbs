package storage

import (
	"bufio"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/cockroachdb/pebble"

	"pttbbs/nest/model"
)

func TestExportCommunity(t *testing.T) {
	tmpDir := t.TempDir()
	e, err := OpenEngine(testEngineConfig(tmpDir))
	if err != nil {
		t.Fatalf("OpenEngine: %v", err)
	}
	defer e.Close()

	mk := func(board, file, body string) *model.Post {
		p, err := e.CreatePost(&model.Post{Community: board, PostFile: file, Title: "t " + file,
			CreatorName: "author", CreatedAt: 1727800000, Content: body})
		if err != nil {
			t.Fatalf("CreatePost: %v", err)
		}
		return p
	}
	a1 := mk("BoardA", "M.1.A.001", "first body")
	mk("BoardB", "M.2.A.002", "other board")
	a2 := mk("BoardA", "M.3.A.003", "gone body")
	if _, err := e.AddComment(&model.Comment{PostID: a1.ID, CreatorName: "c1", Content: "nice", CreatedAt: 1727800001}); err != nil {
		t.Fatalf("AddComment: %v", err)
	}
	if _, err := e.DeletePost(a2.ID, "author", 0, ""); err != nil {
		t.Fatalf("DeletePost: %v", err)
	}
	// A record of before the Encoding header was dropped.
	meta := append([]byte("Encoding: utf-8\n"), model.EncodeRFC822PostMeta(a1)...)
	if err := e.postsDB.Set(model.EncodePostMetaKey(a1.ID), meta, pebble.Sync); err != nil {
		t.Fatalf("Set legacy meta: %v", err)
	}
	backup := filepath.Join(tmpDir, "backup")
	if _, err := e.Backup(backup); err != nil {
		t.Fatalf("Backup: %v", err)
	}

	export := func(deleted bool) (*ExportCommunityResult, string) {
		out := t.TempDir()
		dirs, err := ExpandVerifyDirs([]string{backup})
		if err != nil {
			t.Fatalf("ExpandVerifyDirs: %v", err)
		}
		res, err := ExportCommunity(ExportCommunityOptions{Dirs: dirs, CommunitiesDB: FindCommunitiesDB(dirs),
			Community: "boarda", TargetDir: out, Deleted: deleted})
		if err != nil {
			t.Fatalf("ExportCommunity: %v", err)
		}
		return res, out
	}

	dirs, _ := ExpandVerifyDirs([]string{backup})
	cs, err := ListBackupCommunities(FindCommunitiesDB(dirs))
	if err != nil || len(cs) != 2 || cs[0].Name != "BoardA" || cs[1].Name != "BoardB" {
		t.Fatalf("ListBackupCommunities = %+v, %v", cs, err)
	}

	res, out := export(false)
	if res.Exported != 1 || res.Skipped != 1 || res.Name != "BoardA" || res.Scanned != 3 {
		t.Fatalf("result = %+v", res)
	}
	got, err := os.ReadFile(filepath.Join(out, "M.1.A.001"))
	if err != nil || !strings.Contains(string(got), "first body") || !strings.Contains(string(got), "nice") {
		t.Fatalf("M.1.A.001 = %q, %v", got, err)
	}
	if _, err := os.Stat(filepath.Join(out, "M.2.A.002")); err == nil {
		t.Errorf("post of another board exported")
	}
	f, err := os.Open(filepath.Join(out, exportIndexName))
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	sc := bufio.NewScanner(f)
	var rows []map[string]any
	for sc.Scan() {
		var m map[string]any
		if err := json.Unmarshal(sc.Bytes(), &m); err != nil {
			t.Fatalf("index line %q: %v", sc.Text(), err)
		}
		rows = append(rows, m)
	}
	if len(rows) != 1 || rows[0]["bbs_export_file"] != "M.1.A.001" || rows[0]["num_comments"] != float64(1) || rows[0]["community"] != "BoardA" {
		t.Errorf("index = %v", rows)
	}
	if _, ok := rows[0]["content"]; ok {
		t.Errorf("index has the content: %v", rows[0])
	}

	res, out = export(true)
	if res.Exported != 2 || res.Skipped != 0 {
		t.Fatalf("with deleted: %+v", res)
	}
	if got, err := os.ReadFile(filepath.Join(out, "M.3.A.003")); err != nil || !strings.Contains(string(got), "gone body") {
		t.Errorf("deleted post = %q, %v", got, err)
	}
	if fi, err := os.Stat(filepath.Join(backup, "posts.pebble", "LOCK")); err == nil {
		t.Errorf("export left a LOCK file: %v", fi.Name())
	}
}
