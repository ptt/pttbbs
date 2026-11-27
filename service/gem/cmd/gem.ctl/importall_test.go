package main

import (
	"os"
	"path/filepath"
	"testing"
	"time"
)

// Of directories differing in case, the one named as in .BRD is the area,
// else the latest; the others go into it.
func TestGroupBoards(t *testing.T) {
	home := t.TempDir()
	mk := func(letter, name string, age time.Duration) string {
		p := filepath.Join(home, "man", "boards", letter, name)
		if err := os.MkdirAll(p, 0755); err != nil {
			t.Fatal(err)
		}
		at := time.Now().Add(-age)
		os.Chtimes(p, at, at)
		return p
	}
	upper := mk("E", "Emergency", time.Hour)
	lower := mk("E", "emergency", 2*time.Hour)
	madOld := mk("M", "MAD", 2*time.Hour)
	madNew := mk("M", "mad", time.Hour)
	solo := mk("S", "Solo", time.Hour)
	emptyOld := mk("S", "solo", 2*time.Hour)
	for d, age := range map[string]time.Duration{upper: time.Hour, madOld: 2 * time.Hour} {
		if err := os.WriteFile(filepath.Join(d, ".DIR"), make([]byte, 128), 0644); err != nil {
			t.Fatal(err)
		}
		at := time.Now().Add(-age)
		os.Chtimes(d, at, at)
	}
	brd := make([]byte, 2*256)
	copy(brd, "emergency")
	copy(brd[256:], "Solo")
	if err := os.WriteFile(filepath.Join(home, ".BRD"), brd, 0644); err != nil {
		t.Fatal(err)
	}
	got := groupBoards(home, []string{upper, lower, madOld, madNew, solo, emptyOld})
	if len(got) != 3 {
		t.Fatalf("got %+v", got)
	}
	// .BRD names it emergency, though Emergency is newer.
	if g := got[0]; g.root != "board:emergency" || g.dir != lower || len(g.others) != 1 || g.others[0] != upper {
		t.Errorf("emergency: %+v", g)
	}
	// Not in .BRD: the latest.
	if g := got[1]; g.root != "board:mad" || g.dir != madNew || len(g.others) != 1 || g.others[0] != madOld {
		t.Errorf("mad: %+v", g)
	}
	// solo lists nothing: left out.
	if g := got[2]; g.root != "board:Solo" || g.dir != solo || len(g.others) != 0 {
		t.Errorf("Solo: %+v", g)
	}
}
