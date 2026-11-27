package legacy

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"

	"pttbbs/gem/storage"
)

func hdr(name, title string) storage.Header {
	var h storage.Header
	h.SetName(name)
	h.SetTitle([]byte(title))
	h.SetOwner("SYSOP")
	h.SetFilemode(storage.FileBM)
	return h
}

// writeDir writes a .DIR of headers into dir.
func writeDir(t *testing.T, dir string, hs ...storage.Header) {
	t.Helper()
	if err := os.MkdirAll(dir, 0755); err != nil {
		t.Fatal(err)
	}
	var b bytes.Buffer
	for _, h := range hs {
		b.Write(h[:])
	}
	if err := os.WriteFile(filepath.Join(dir, ".DIR"), b.Bytes(), 0644); err != nil {
		t.Fatal(err)
	}
}

func write(t *testing.T, path, data string) {
	t.Helper()
	if err := os.WriteFile(path, []byte(data), 0644); err != nil {
		t.Fatal(err)
	}
}

// snapshot renders a tree of files: every path with its content or link.
func snapshot(t *testing.T, root string, skip func(rel string) bool) string {
	t.Helper()
	var lines []string
	filepath.Walk(root, func(path string, fi os.FileInfo, err error) error {
		if err != nil {
			t.Fatal(err)
		}
		rel, _ := filepath.Rel(root, path)
		if skip != nil && skip(rel) {
			return nil
		}
		switch {
		case fi.Mode()&os.ModeSymlink != 0:
			target, _ := os.Readlink(path)
			lines = append(lines, rel+" -> "+target)
		case fi.IsDir():
			lines = append(lines, rel+"/")
		default:
			data, _ := os.ReadFile(path)
			lines = append(lines, fmt.Sprintf("%s = %q", rel, data))
		}
		return nil
	})
	sort.Strings(lines)
	return strings.Join(lines, "\n")
}

func TestImportExport(t *testing.T) {
	for _, flush := range []int64{64 << 20, 1} {
		t.Run(fmt.Sprint("flush", flush), func(t *testing.T) {
			old := flushBytes
			flushBytes = flush
			defer func() { flushBytes = old }()
			testImportExport(t)
		})
	}
}

func testImportExport(t *testing.T) {
	home := t.TempDir()
	root := RootDir(home, "board:Test")
	writeDir(t, root,
		hdr("M.1.A", "first"),
		hdr("D1", "dir"),
		hdr("M.2.A", "gone"),
		hdr("S3E258147", "table of contents (20.2k)"),
		hdr("L2", "link"),
		hdr("", "empty record"),
		hdr("M.3.A", "same content"),
		hdr(".index", "table of contents"),
	)
	write(t, filepath.Join(root, ".index"), "made by mandex")
	write(t, filepath.Join(root, "M.1.A"), "hello\n")
	write(t, filepath.Join(root, "M.3.A"), "shared body")
	write(t, filepath.Join(root, "M.99.A"), "unlisted")
	if err := os.Symlink(".index", filepath.Join(root, "S3E258147")); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink("/home/bbs/man/boards/T/Test/D1", filepath.Join(root, "L2")); err != nil {
		t.Fatal(err)
	}
	writeDir(t, filepath.Join(root, "D1"), hdr("M.4.A", "inner"), hdr("D2", "deeper"))
	write(t, filepath.Join(root, "D1", "M.4.A"), "shared body")
	// An empty directory without a .DIR (export writes none for it either).
	if err := os.MkdirAll(filepath.Join(root, "D1", "D2"), 0755); err != nil {
		t.Fatal(err)
	}

	st, err := storage.Open(storage.Options{Dir: filepath.Join(t.TempDir(), "gem.pebble"), NoSync: true, NoReaper: true})
	if err != nil {
		t.Fatal(err)
	}
	defer st.Close()
	stats, err := ImportRoot(st, ImportOptions{BBSHome: home, Root: "board:Test"})
	if err != nil {
		t.Fatal(err)
	}
	want := ImportStats{Dirs: 2, Files: 3, Links: 1, Indexes: 2, Missing: 2, Unlisted: 1, Bytes: 6 + 11 + 11}
	if stats != want {
		t.Fatalf("stats %v, want %v", stats, want)
	}
	if _, err := ImportRoot(st, ImportOptions{BBSHome: home, Root: "board:Test"}); err == nil {
		t.Fatal("imported twice")
	}
	tot, err := st.RootTotals("board:Test")
	if err != nil {
		t.Fatal(err)
	}
	if tot.Files != 3 || tot.Dirs != 2 || tot.Links != 1 || tot.Missing != 2 || tot.Depth != 2 {
		t.Fatalf("totals %+v", tot)
	}
	// The link mandex made to .index, and the record naming .index, are
	// the table of contents: the first renamed to S.index (the second
	// keeps its name, S.index being taken), both retitled.
	top, err := st.List("board:Test", nil)
	if err != nil {
		t.Fatal(err)
	}
	for _, i := range []int{3, 7} {
		e := &top.Entries[i]
		if e.Kind != storage.KindIndex || !bytes.Equal(e.Header.Title(), indexTitle) || e.Header.Owner() != indexOwner {
			t.Fatalf("entry %d: %s %q %q", i, e.Kind, e.Header.Title(), e.Header.Owner())
		}
	}
	if n := top.Entries[3].Name(); n != IndexEntryName {
		t.Fatalf("index entry named %q", n)
	}
	if n := top.Entries[7].Name(); n != IndexName {
		t.Fatalf("second index entry named %q", n)
	}
	if _, err := st.FreeAll(); err != nil {
		t.Fatal(err)
	}
	rep, err := st.Verify(false)
	if err != nil || !rep.OK() {
		t.Fatalf("verify %+v %v", rep, err)
	}
	if rep.Blobs != 2 {
		t.Fatalf("same content stored %d times", rep.Blobs)
	}

	out := filepath.Join(t.TempDir(), "out")
	if _, err := ExportRoot(st, "board:Test", out); err != nil {
		t.Fatal(err)
	}
	// The export has what .DIR lists (the empty record too, and the record
	// of the missing file), not the unlisted file.
	// The table of contents is written as it is now (a file), named as its
	// entry is; the top .DIR has the entries renamed and retitled.
	skip := func(rel string) bool {
		return rel == "M.99.A" || rel == ".index" || rel == "S3E258147" || rel == "S.index" || rel == ".DIR"
	}
	got := snapshot(t, out, skip)
	wantTree := snapshot(t, root, skip)
	if got != wantTree {
		t.Fatalf("export:\n%s\nwant\n%s", got, wantTree)
	}
	index, err := Index(st, "board:Test")
	if err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{IndexEntryName, IndexName} {
		if data, err := os.ReadFile(filepath.Join(out, name)); err != nil || !bytes.Equal(data, index) {
			t.Fatalf("%s: %q %v, want %q", name, data, err, index)
		}
	}
	hs, err := readDIR(filepath.Join(out, ".DIR"))
	if err != nil || len(hs) != len(top.Entries) {
		t.Fatalf(".DIR: %d records %v", len(hs), err)
	}
	for i := range hs {
		if hs[i] != top.Entries[i].Header {
			t.Fatalf(".DIR record %d: %q", i, hs[i].Name())
		}
	}

	// Replacing drops the old tree.
	if _, err := ImportRoot(st, ImportOptions{BBSHome: home, Root: "board:Test", Replace: true}); err != nil {
		t.Fatal(err)
	}
	// The 3 old listings, and the pins of the import when it stored in pieces.
	if n, err := st.FreeAll(); err != nil || n < 3 {
		t.Fatalf("freed %d listings (%v), want 3", n, err)
	}
	rep, err = st.Verify(false)
	if err != nil || !rep.OK() || rep.Listings != 3 {
		t.Fatalf("verify %+v %v", rep, err)
	}
}

func TestRootDir(t *testing.T) {
	for root, want := range map[string]string{
		"board:Test": "/bbs/man/boards/T/Test",
		"user:piaip": "/bbs/home/p/piaip/man",
		"site":       "/bbs/man",
		"Test":       "",
		"user:../x":  "",
	} {
		if got := RootDir("/bbs", root); got != want {
			t.Errorf("RootDir(%q) = %q, want %q", root, got, want)
		}
	}
}

// A directory attached to an area becomes its last directory, with the
// header given; an area is needed.
func TestImportAttach(t *testing.T) {
	home := t.TempDir()
	root := RootDir(home, "board:Test")
	writeDir(t, root, hdr("M.1.A", "first"))
	write(t, filepath.Join(root, "M.1.A"), "hello\n")
	old := filepath.Join(home, "old")
	writeDir(t, old, hdr("M.2.A", "older"))
	write(t, filepath.Join(old, "M.2.A"), "from before\n")

	st, err := storage.Open(storage.Options{Dir: filepath.Join(t.TempDir(), "gem.pebble"), NoSync: true, NoReaper: true})
	if err != nil {
		t.Fatal(err)
	}
	defer st.Close()
	var h storage.Header
	h.SetTitle([]byte("[old] old"))
	if _, err := ImportRoot(st, ImportOptions{BBSHome: home, Root: "board:Test", Dir: old, Attach: &h}); err == nil {
		t.Fatal("attached to an area that is not there")
	}
	if _, err := ImportRoot(st, ImportOptions{BBSHome: home, Root: "board:Test"}); err != nil {
		t.Fatal(err)
	}
	if _, err := ImportRoot(st, ImportOptions{BBSHome: home, Root: "board:Test", Dir: old, Attach: &h}); err != nil {
		t.Fatal(err)
	}
	l, err := st.List("board:Test", nil)
	if err != nil || len(l.Entries) != 2 {
		t.Fatalf("list: %v, %v", l, err)
	}
	d := l.Entries[1]
	if d.Kind != storage.KindDir || string(d.Header.Title()) != "[old] old" || d.Name() == "" {
		t.Fatalf("attached = %+v", d)
	}
	in, err := st.List("board:Test", []string{d.Name()})
	if err != nil || len(in.Entries) != 1 || string(in.Entries[0].Header.Title()) != "older" {
		t.Fatalf("inside: %v, %v", in, err)
	}
	if rep, err := st.Verify(false); err != nil || !rep.OK() {
		t.Fatalf("verify: %+v, %v", rep, err)
	}
}
