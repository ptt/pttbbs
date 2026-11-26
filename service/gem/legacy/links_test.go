package legacy

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"pttbbs/gem/storage"
)

func TestLinkTargets(t *testing.T) {
	lt := LinkTargets("/srv/bbs", DefaultOldHomes)
	for _, c := range []struct {
		root   string
		dir    string
		target string
		want   string // root/path, "" for none
	}{
		{"site", "", "/home/bbs/man/boards/S/SYSOP/DE85", "board:SYSOP/DE85"},
		{"site", "", "/srv/bbs/man/boards/S/SYSOP", "board:SYSOP"},
		{"site", "", "/srv/bbs/man/D1/M.1.A", "site/D1/M.1.A"},
		{"board:Test", "D1", "M.1.A", "board:Test/D1/M.1.A"},
		{"board:Test", "D1", "../../../S/SYSOP/D2", "board:SYSOP/D2"},
		{"board:Test", "", "../../../../home/p/piaip/man/D3", "user:piaip/D3"},
		{"user:piaip", "", "/home/bbs/etc/SONGBOOK/M.2.A", "board:Note/SONGBOOK/M.2.A"},
		{"site", "", "/etc/passwd", ""},
		{"site", "", "/home/bbsx/man/D1", ""},
		{"site", "", "../../etc", ""},
		{"site", "", "/home/bbs/man/boards/S", ""},
		{"site", "", "/home/bbs/bin/mbbsd", ""},
	} {
		var dir []string
		if c.dir != "" {
			dir = strings.Split(c.dir, "/")
		}
		root, path, ok := lt(c.root, dir, c.target)
		got := ""
		if ok {
			got = strings.Join(append([]string{root}, path...), "/")
		}
		if got != c.want {
			t.Errorf("%s %s -> %s: %q, want %q", c.root, c.dir, c.target, got, c.want)
		}
	}
}

func symlink(t *testing.T, target, link string) {
	t.Helper()
	if err := os.Symlink(target, link); err != nil {
		t.Fatal(err)
	}
}

func TestResolveLinks(t *testing.T) {
	home := t.TempDir()
	site := RootDir(home, storage.RootSite)
	sysop := RootDir(home, "board:SYSOP")
	test := RootDir(home, "board:Test")

	writeDir(t, sysop, hdr("DE85", "rules"), hdr("M.1.A", "notice"))
	write(t, filepath.Join(sysop, "M.1.A"), "notice body")
	writeDir(t, filepath.Join(sysop, "DE85"), hdr("M.2.A", "rule 1"))
	write(t, filepath.Join(sysop, "DE85", "M.2.A"), "rule body")

	writeDir(t, site, hdr("S3E258147", "-> SYSOP rules"), hdr("S3E258148", "-> gone"))
	symlink(t, "/home/bbs/man/boards/S/SYSOP/DE85", filepath.Join(site, "S3E258147"))
	symlink(t, "/home/bbs/man/boards/S/SYSOP/D999", filepath.Join(site, "S3E258148"))

	writeDir(t, test,
		hdr("D1", "dir"),
		hdr("L2", "-> D1"),
		hdr("L3", "-> SYSOP notice"),
		hdr("L4", "-> L2"),
		hdr("Out", "-> out of man"),
	)
	symlink(t, "D1", filepath.Join(test, "L2"))
	symlink(t, "../../S/SYSOP/M.1.A", filepath.Join(test, "L3"))
	symlink(t, "L2", filepath.Join(test, "L4"))
	symlink(t, "/etc/passwd", filepath.Join(test, "Out"))
	writeDir(t, filepath.Join(test, "D1"), hdr("M.3.A", "inner"), hdr("Up", "-> the top"))
	write(t, filepath.Join(test, "D1", "M.3.A"), "inner body")
	symlink(t, filepath.Join(home, "man/boards/T/Test"), filepath.Join(test, "D1", "Up"))

	st, err := storage.Open(storage.Options{Dir: filepath.Join(t.TempDir(), "gem.pebble"), NoSync: true, NoReaper: true})
	if err != nil {
		t.Fatal(err)
	}
	defer st.Close()
	for _, root := range []string{storage.RootSite, "board:SYSOP", "board:Test"} {
		if _, err := ImportRoot(st, ImportOptions{BBSHome: home, Root: root}); err != nil {
			t.Fatal(err)
		}
	}

	var logged []string
	stats, err := st.ResolveLinks(LinkTargets(home, DefaultOldHomes), func(f string, args ...any) {
		logged = append(logged, f)
	})
	if err != nil {
		t.Fatal(err)
	}
	// S3E258147, L2, L4 (through L2) and the directory Up is in: D1 is
	// resolved first through L2, where Up (to the top, open) is a loop.
	want := storage.ResolveStats{Dirs: 3, Files: 1, Broken: 2, Loops: 1}
	if stats != want || len(logged) != 3 {
		t.Fatalf("stats %+v (%d logged), want %+v", stats, len(logged), want)
	}

	for _, root := range []string{storage.RootSite, "board:SYSOP", "board:Test"} {
		tot, err := st.RootTotals(root)
		if err != nil || tot.Links != 0 {
			t.Fatalf("%s: totals %+v %v", root, tot, err)
		}
	}
	kinds := func(root string, path ...string) string {
		t.Helper()
		l, err := st.List(root, path)
		if err != nil {
			t.Fatal(err)
		}
		var ks []string
		for i := range l.Entries {
			ks = append(ks, l.Entries[i].Name()+":"+l.Entries[i].Kind.String())
		}
		return strings.Join(ks, " ")
	}
	for _, c := range [][2]string{
		{kinds(storage.RootSite), "S3E258147:dir S3E258148:missing"},
		{kinds(storage.RootSite, "S3E258147"), "M.2.A:file"},
		{kinds("board:Test"), "D1:dir L2:dir L3:file L4:dir Out:missing"},
		{kinds("board:Test", "D1"), "M.3.A:file Up:missing"},
		{kinds("board:Test", "L4"), "M.3.A:file Up:missing"},
	} {
		if c[0] != c[1] {
			t.Errorf("got %q, want %q", c[0], c[1])
		}
	}
	data, _, err := st.ReadFile("board:Test", nil, -1, "L3")
	if err != nil || string(data) != "notice body" {
		t.Fatalf("L3: %q %v", data, err)
	}
	// The header of the link is kept.
	l, err := st.List("board:Test", nil)
	if err != nil || string(l.Entries[1].Header.Title()) != "-> D1" {
		t.Fatalf("L2 header: %v", err)
	}

	// The directory is shared: a change on one side copies it.
	if _, _, err := st.AddFile(storage.RootSite, []string{"S3E258147"}, -1, hdr("", "new"), []byte("x")); err != nil {
		t.Fatal(err)
	}
	if got := kinds("board:SYSOP", "DE85"); got != "M.2.A:file" {
		t.Fatalf("SYSOP/DE85 changed: %s", got)
	}
	if got := kinds(storage.RootSite, "S3E258147"); !strings.HasPrefix(got, "M.2.A:file M.") {
		t.Fatalf("site/S3E258147: %s", got)
	}

	if _, err := st.FreeAll(); err != nil {
		t.Fatal(err)
	}
	rep, err := st.Verify(false)
	if err != nil || !rep.OK() {
		t.Fatalf("verify %+v %v", rep, err)
	}

	// Nothing left to do.
	stats, err = st.ResolveLinks(LinkTargets(home, nil), nil)
	if err != nil || stats.Total() != 0 {
		t.Fatalf("again: %+v %v", stats, err)
	}
}

// Areas imported before the table of contents had its own kind keep links
// to .index: resolving makes them index entries, as an import now does.
func TestResolveOldIndexLinks(t *testing.T) {
	st, err := storage.Open(storage.Options{Dir: filepath.Join(t.TempDir(), "gem.pebble"), NoSync: true, NoReaper: true})
	if err != nil {
		t.Fatal(err)
	}
	defer st.Close()
	if err := st.Update(func(tx *storage.Tx) error {
		id, _, err := tx.NewListing([]storage.Entry{
			{Header: hdr("S3E258147", "table of contents (20.2k)"), Kind: storage.KindLink, Target: ".index"},
		})
		if err != nil {
			return err
		}
		return tx.SetRoot("board:Old", id)
	}); err != nil {
		t.Fatal(err)
	}
	stats, err := ResolveLinks(st, "/srv/bbs", nil, nil)
	if err != nil || stats != (storage.ResolveStats{Indexes: 1}) {
		t.Fatalf("%+v %v", stats, err)
	}
	l, _ := st.List("board:Old", nil)
	e := &l.Entries[0]
	if e.Kind != storage.KindIndex || e.Name() != IndexEntryName || string(e.Header.Title()) != string(indexTitle) {
		t.Fatalf("%s %q %q", e.Kind, e.Name(), e.Header.Title())
	}
}
