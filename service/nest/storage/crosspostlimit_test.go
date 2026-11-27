package storage

import (
	"database/sql"
	"errors"
	"path/filepath"
	"testing"

	"pttbbs/nest/model"
)

func limitName(err error) string {
	var le *CrosspostLimitError
	if errors.As(err, &le) {
		return le.Error()
	}
	if err != nil {
		return "error: " + err.Error()
	}
	return "OK"
}

func checkCP(t *testing.T, st Storage, srcComm, srcFile, target, who string, id model.PersonID, exempt bool, lim CrosspostLimits, now int64) string {
	t.Helper()
	return limitName(st.CheckCrosspost(CrosspostCheck{
		SourceCommunity: srcComm, SourcePostFile: srcFile, TargetCommunity: target,
		CreatorName: who, CreatorID: id, Exempt: exempt, Now: now,
	}, lim))
}

func mustCrosspost(t *testing.T, st Storage, srcComm, srcFile, target, targetFile, who string, id model.PersonID, at int64, exempt bool) {
	t.Helper()
	if _, _, err := st.AddCrosspost(srcComm, srcFile, target, targetFile, who, id, at, exempt); err != nil {
		t.Fatalf("AddCrosspost %s/%s -> %s: %v", srcComm, srcFile, target, err)
	}
}

// One copy per board, none back to the original's board; copies of copies
// (live ones, kept with the original, and imported ones, linked to their
// direct source) count as copies of the original.
func TestCrosspostOnePerBoard(t *testing.T) {
	s, _ := openTestSharded(t, 2, map[string]int{"Src": 0, "A": 1, "B": 0, "C": 1})
	const now = int64(1800000000)
	var lim CrosspostLimits
	mustCreatePost(t, s, "Src", "M.1.A.001", "original")
	if got := checkCP(t, s, "Src", "M.1.A.001", "src", "alice", 0, false, lim, now); got != ErrNameCrosspostBack {
		t.Fatalf("to its own board: %s", got)
	}
	if got := checkCP(t, s, "Src", "M.1.A.001", "A", "alice", 0, false, lim, now); got != "OK" {
		t.Fatalf("first copy: %s", got)
	}
	mustCreatePost(t, s, "A", "M.2.A.001", "copy")
	mustCrosspost(t, s, "Src", "M.1.A.001", "A", "M.2.A.001", "alice", 0, now, false)
	if got := checkCP(t, s, "Src", "M.1.A.001", "a", "bob", 0, true, lim, now); got != ErrNameAlreadyCrossposted {
		t.Fatalf("second copy to A (even exempt): %s", got)
	}

	// A crosspost of the copy is one of the original.
	if got := checkCP(t, s, "A", "M.2.A.001", "Src", "bob", 0, false, lim, now); got != ErrNameCrosspostBack {
		t.Fatalf("copy back to the original's board: %s", got)
	}
	mustCreatePost(t, s, "B", "M.3.A.001", "copy of copy")
	mustCrosspost(t, s, "A", "M.2.A.001", "B", "M.3.A.001", "bob", 0, now, false)
	if cps := crosspostsOf(t, s, "Src", "M.1.A.001"); len(cps) != 2 {
		t.Fatalf("records of the original: %+v", cps)
	}
	if cps := crosspostsOf(t, s, "A", "M.2.A.001"); len(cps) != 0 {
		t.Fatalf("records kept with the copy: %+v", cps)
	}
	if got := checkCP(t, s, "B", "M.3.A.001", "A", "carol", 0, false, lim, now); got != ErrNameAlreadyCrossposted {
		t.Fatalf("copy of copy to A: %s", got)
	}

	// An imported copy is linked to its direct source (B); it still counts.
	importPost(t, s, "C", "M.4.A.001", now, nil, &model.CrosspostOrigin{
		SourceCommunity: "B", SourcePostFile: "M.3.A.001", CreatorName: "dave", CreatedAt: now,
	})
	if cps := crosspostsOf(t, s, "B", "M.3.A.001"); len(cps) != 1 {
		t.Fatalf("imported copy not linked to its source: %+v", cps)
	}
	if got := checkCP(t, s, "Src", "M.1.A.001", "C", "erin", 0, false, lim, now); got != ErrNameAlreadyCrossposted {
		t.Fatalf("original to C: %s", got)
	}
	if got := checkCP(t, s, "C", "M.4.A.001", "Src", "erin", 0, false, lim, now); got != ErrNameCrosspostBack {
		t.Fatalf("imported copy back to the original's board: %s", got)
	}
	if root, file, err := s.CrosspostRoot("C", "M.4.A.001"); err != nil || root != "Src" || file != "M.1.A.001" {
		t.Fatalf("CrosspostRoot = %s, %s, %v", root, file, err)
	}
	if got := checkCP(t, s, "Src", "M.9.A.009", "C", "erin", 0, false, lim, now); got == "OK" {
		t.Fatalf("a post nest.svc does not have: %s", got)
	}
}

// Per person and per original within the window; exempt ones are neither
// limited nor counted.
func TestCrosspostRateLimits(t *testing.T) {
	e, _ := openTestEngine(t)
	const now = int64(1800000000)
	lim := CrosspostLimits{Window: 86400, PersonMax: 2, PostMax: 4}
	mustCreatePost(t, e, "Src", "M.1.A.001", "original")
	cp := func(target, who string, id model.PersonID, at int64, exempt bool) {
		t.Helper()
		mustCrosspost(t, e, "Src", "M.1.A.001", target, "M.5.A.001", who, id, at, exempt)
	}
	check := func(target, who string, id model.PersonID, exempt bool, l CrosspostLimits, at int64) string {
		t.Helper()
		return checkCP(t, e, "Src", "M.1.A.001", target, who, id, exempt, l, at)
	}

	cp("Old", "alice", 7, now-2*86400, false) // out of the window
	cp("A", "alice", 7, now-100, false)
	cp("B", "Alice", 7, now-90, false)
	if got := check("X", "ALICE", 0, false, lim, now); got != ErrNameCrosspostPerson+" 2" {
		t.Fatalf("alice by name: %s", got)
	}
	if got := check("X", "renamed", 7, false, lim, now); got != ErrNameCrosspostPerson+" 2" {
		t.Fatalf("alice by person ID: %s", got)
	}
	if got := check("X", "alice", 8, false, lim, now); got != "OK" {
		t.Fatalf("another person of the same name: %s", got)
	}
	if got := check("X", "alice", 7, true, lim, now); got != "OK" {
		t.Fatalf("alice exempt: %s", got)
	}
	if got := check("X", "alice", 7, false, lim, now+86400-80); got != "OK" {
		t.Fatalf("alice after the window: %s", got)
	}

	// Exempt crossposts do not count.
	cp("E1", "mod", 0, now-50, true)
	cp("E2", "mod", 0, now-40, true)
	cp("E3", "mod", 0, now-30, true)
	if got := check("X", "mod", 0, false, lim, now); got != "OK" {
		t.Fatalf("after exempt ones: %s", got)
	}
	cp("C", "bob", 0, now-20, false)
	cp("D", "carol", 0, now-10, false)
	if got := check("X", "dave", 0, false, lim, now); got != ErrNameCrosspostPost+" 4" {
		t.Fatalf("post limit: %s", got)
	}
	if got := check("X", "dave", 0, true, lim, now); got != "OK" {
		t.Fatalf("post limit, exempt: %s", got)
	}
	exemptBoard := lim
	exemptBoard.ExemptBoards = []string{"other", "src"}
	if got := check("X", "dave", 0, false, exemptBoard, now); got != "OK" {
		t.Fatalf("original in an exempt board: %s", got)
	}

	// The flag survives a rebuild.
	if _, _, err := e.RebuildSQLiteFromPebble(); err != nil {
		t.Fatal(err)
	}
	var exempt int
	for _, r := range crosspostsOf(t, e, "Src", "M.1.A.001") {
		if r.BBSExempt {
			exempt++
		}
	}
	if exempt != 3 {
		t.Fatalf("%d exempt records after the rebuild, want 3", exempt)
	}
	if got := check("X", "dave", 0, false, lim, now); got != ErrNameCrosspostPost+" 4" {
		t.Fatalf("post limit after the rebuild: %s", got)
	}
}

// An older meta.sqlite3 gets the bbs_exempt column.
func TestCrosspostExemptColumnAdded(t *testing.T) {
	path := filepath.Join(t.TempDir(), "meta.sqlite3")
	old, err := sql.Open("sqlite", path)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := old.Exec(`CREATE TABLE crossposts (id INTEGER PRIMARY KEY AUTOINCREMENT, source_post_id INTEGER NOT NULL,
		target_community_id INTEGER NOT NULL, target_post_file TEXT NOT NULL, creator_name TEXT NOT NULL,
		creator_id INTEGER NOT NULL DEFAULT 0, created_at INTEGER NOT NULL);
		INSERT INTO crossposts (source_post_id, target_community_id, target_post_file, creator_name, created_at) VALUES (1, 2, '-', 'x', 3)`); err != nil {
		t.Fatal(err)
	}
	old.Close()
	for i := 0; i < 2; i++ {
		db, err := openMetaDB(path)
		if err != nil {
			t.Fatalf("open %d: %v", i, err)
		}
		var exempt int
		if err := db.QueryRow(`SELECT bbs_exempt FROM crossposts`).Scan(&exempt); err != nil || exempt != 0 {
			t.Fatalf("bbs_exempt = %d, %v", exempt, err)
		}
		db.Close()
	}
}
