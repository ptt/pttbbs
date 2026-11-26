package storage

import (
	"bytes"
	"errors"
	"fmt"
	"math/rand/v2"
	"slices"
	"strings"
	"testing"
)

func openTest(t *testing.T, lim *Limits) *Store {
	t.Helper()
	opts := Options{Dir: t.TempDir() + "/gem.pebble", NoSync: true, NoReaper: true, CacheMB: 8}
	if lim != nil {
		l := *lim
		opts.Limits = func(string) Limits { return l }
	}
	s, err := Open(opts)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { s.Close() })
	return s
}

func hdr(name, title string) Header {
	var h Header
	h.SetName(name)
	h.SetTitle([]byte(title))
	h.SetOwner("SYSOP")
	return h
}

func p(s string) []string {
	ps, err := SplitPath(s)
	if err != nil {
		panic(err)
	}
	return ps
}

func must[T any](v T, err error) T {
	if err != nil {
		panic(err)
	}
	return v
}

func verifyOK(t *testing.T, s *Store) *VerifyReport {
	t.Helper()
	rep, err := s.Verify(false)
	if err != nil {
		t.Fatal(err)
	}
	if !rep.OK() {
		t.Fatalf("verify: %+v", rep)
	}
	return rep
}

// dump renders a board's tree: names, kinds and file contents.
func dump(t *testing.T, s *Store, board string) string {
	t.Helper()
	var b strings.Builder
	var rec func(path []string, depth int)
	rec = func(path []string, depth int) {
		l, err := s.List(board, path)
		if err != nil {
			t.Fatal(err)
		}
		for _, e := range l.Entries {
			fmt.Fprintf(&b, "%s%s %s %q", strings.Repeat("  ", depth), e.Kind, e.Name(), e.Header.Title())
			switch e.Kind {
			case KindFile:
				data, _, err := s.ReadFile(board, path, -1, e.Name())
				if err != nil {
					t.Fatal(err)
				}
				fmt.Fprintf(&b, " = %q", data)
			case KindLink:
				fmt.Fprintf(&b, " -> %s", e.Target)
			}
			b.WriteString("\n")
			if e.Kind == KindDir {
				rec(append(slices.Clone(path), e.Name()), depth+1)
			}
		}
	}
	rec(nil, 0)
	return b.String()
}

func TestBasic(t *testing.T) {
	s := openTest(t, nil)
	if err := s.CreateRoot("board:Test"); err != nil {
		t.Fatal(err)
	}
	if err := s.CreateRoot("board:test"); !errors.Is(err, ErrRootExists) {
		t.Fatalf("board names are case-insensitive: %v", err)
	}
	must2(s.AddDir("board:Test", nil, -1, hdr("D1", "dir one")))
	must2(s.AddFile("board:Test", p("D1"), -1, hdr("M.1.A", "hello"), []byte("hello world")))
	must2(s.AddFile("board:Test", p("D1"), 0, hdr("", "made up name"), []byte("first")))
	// A link, as an import keeps it (there is no call to add one).
	if err := s.Update(func(tx *Tx) error {
		_, _, err := tx.add("board:Test", nil, -1, Entry{Header: hdr("L1", "link"), Kind: KindLink, Target: "/home/bbs/man/boards/N/Note"})
		return err
	}); err != nil {
		t.Fatal(err)
	}
	if _, _, err := s.AddFile("board:Test", p("D1"), -1, hdr("M.1.A", "dup"), nil); !errors.Is(err, ErrExists) {
		t.Fatalf("duplicate name: %v", err)
	}
	if _, _, err := s.AddFile("board:Test", p("D1"), -1, hdr("../x", "bad"), nil); !errors.Is(err, ErrInvalidName) {
		t.Fatalf("bad name: %v", err)
	}
	l := must(s.List("board:Test", p("D1")))
	if len(l.Entries) != 2 || l.Entries[1].Name() != "M.1.A" || !strings.HasPrefix(l.Entries[0].Name(), "M.") {
		t.Fatalf("listing: %v", dump(t, s, "board:Test"))
	}
	tot := must(s.RootTotals("board:test"))
	if tot.Files != 2 || tot.Dirs != 1 || tot.Links != 1 || tot.Bytes != 16 || tot.Depth != 1 {
		t.Fatalf("totals %+v", tot)
	}
	data, _ := must2(s.ReadFile("board:Test", p("D1"), 5, "M.1.A"))
	if string(data) != "hello world" {
		t.Fatalf("read %q", data)
	}
	if _, err := s.Reorder(Ref{"board:Test", p("D1"), 1, "M.1.A"}, 0); err != nil {
		t.Fatal(err)
	}
	h := hdr("", "renamed title")
	if _, err := s.SetHeader(Ref{"board:Test", p("D1"), 0, "M.1.A"}, h); err != nil {
		t.Fatal(err)
	}
	if _, err := s.PutFile(Ref{"board:Test", p("D1"), -1, "M.1.A"}, []byte("edited")); err != nil {
		t.Fatal(err)
	}
	e, _ := must2(s.Stat("board:Test", p("D1"), 0, "M.1.A"))
	if string(e.Header.Title()) != "renamed title" || e.Size != 6 {
		t.Fatalf("entry %+v", e)
	}
	verifyOK(t, s)
}

func must2[A, B any](a A, b B, err error) (A, B) {
	if err != nil {
		panic(err)
	}
	return a, b
}

func TestCopyOnWrite(t *testing.T) {
	s := openTest(t, nil)
	must0(s.CreateRoot("board:A"))
	must0(s.CreateRoot("board:B"))
	must2(s.AddDir("board:A", nil, -1, hdr("D1", "top")))
	must2(s.AddDir("board:A", p("D1"), -1, hdr("D2", "mid")))
	must2(s.AddDir("board:A", p("D1/D2"), -1, hdr("D3", "deep")))
	for i := 0; i < 5; i++ {
		must2(s.AddFile("board:A", p("D1/D2/D3"), -1, hdr(fmt.Sprintf("M.%d.A", i), "f"), []byte(fmt.Sprintf("body %d", i))))
	}
	must2(s.AddFile("board:A", p("D1"), -1, hdr("M.9.A", "f"), []byte("same")))
	before := dump(t, s, "board:A")
	stored0 := verifyOK(t, s).StoredBytes

	// Copy the tree to another board and within the board: O(1), no new
	// content.
	must2(s.Copy(Ref{"board:A", nil, -1, "D1"}, "board:B", nil, -1, nil))
	h := hdr("D1copy", "copy")
	must2(s.Copy(Ref{"board:A", nil, -1, "D1"}, "board:A", nil, -1, &h))
	rep := verifyOK(t, s)
	if rep.StoredBytes != stored0 {
		t.Fatalf("copies stored content again: %d != %d", rep.StoredBytes, stored0)
	}
	if got := must(s.RootTotals("board:A")); got.Files != 12 || got.Dirs != 6 {
		t.Fatalf("totals count copies: %+v", got)
	}

	// Change deep inside each copy: the others keep what they had.
	must(s.PutFile(Ref{"board:B", p("D1/D2/D3"), -1, "M.2.A"}, []byte("changed in B")))
	must(s.Remove(Ref{"board:A", p("D1copy/D2/D3"), -1, "M.3.A"}))
	must2(s.AddFile("board:A", p("D1copy/D2"), -1, hdr("M.10.A", "new"), []byte("new in copy")))
	if got := dump(t, s, "board:A"); !strings.HasPrefix(got, before) {
		t.Fatalf("original changed:\n%s\nwant prefix\n%s", got, before)
	}
	if got := dump(t, s, "board:B"); !strings.Contains(got, "changed in B") || strings.Contains(before, "changed in B") {
		t.Fatalf("B:\n%s", got)
	}
	if strings.Contains(dump(t, s, "board:B"), "new in copy") {
		t.Fatal("change in A's copy shows in B")
	}
	verifyOK(t, s)

	// Change the original: copies unchanged.
	bBefore := dump(t, s, "board:B")
	must(s.Remove(Ref{"board:A", p("D1/D2"), -1, "D3"}))
	if dump(t, s, "board:B") != bBefore {
		t.Fatal("B changed with A")
	}
	verifyOK(t, s)
}

func must0(err error) {
	if err != nil {
		panic(err)
	}
}

func TestCopyIntoItself(t *testing.T) {
	s := openTest(t, nil)
	must0(s.CreateRoot("board:A"))
	must2(s.AddDir("board:A", nil, -1, hdr("D1", "top")))
	must2(s.AddDir("board:A", p("D1"), -1, hdr("D2", "mid")))
	must2(s.AddFile("board:A", p("D1/D2"), -1, hdr("M.1.A", "f"), []byte("x")))
	h := hdr("Dself", "self")
	must2(s.Copy(Ref{"board:A", nil, -1, "D1"}, "board:A", p("D1/D2"), -1, &h))
	got := dump(t, s, "board:A")
	want := `dir D1 "top"
  dir D2 "mid"
    file M.1.A "f" = "x"
    dir Dself "self"
      dir D2 "mid"
        file M.1.A "f" = "x"
`
	if got != want {
		t.Fatalf("got\n%s\nwant\n%s", got, want)
	}
	verifyOK(t, s)
	if _, _, err := s.Move(Ref{"board:A", nil, -1, "D1"}, "board:A", p("D1/D2"), -1, nil); !errors.Is(err, ErrMoveIntoSelf) {
		t.Fatalf("move into itself: %v", err)
	}
	must2(s.Move(Ref{"board:A", p("D1/D2"), -1, "Dself"}, "board:A", nil, 0, nil))
	if l := must(s.List("board:A", nil)); l.Entries[0].Name() != "Dself" {
		t.Fatal("move")
	}
	verifyOK(t, s)
}

func TestFree(t *testing.T) {
	s := openTest(t, nil)
	must0(s.CreateRoot("board:A"))
	must2(s.AddDir("board:A", nil, -1, hdr("D1", "top")))
	for i := 0; i < 3; i++ {
		must2(s.AddDir("board:A", p("D1"), -1, hdr(fmt.Sprintf("D%d", i+10), "x")))
		must2(s.AddFile("board:A", p(fmt.Sprintf("D1/D%d", i+10)), -1, hdr("M.1.A", "f"), []byte(fmt.Sprint(i))))
	}
	must2(s.Copy(Ref{"board:A", nil, -1, "D1"}, "board:A", nil, -1, ptr(hdr("D2", "copy"))))
	must(s.Remove(Ref{"board:A", nil, -1, "D1"}))
	if n := must(s.FreeAll()); n != 0 {
		t.Fatalf("freed %d listings still shared with D2", n)
	}
	verifyOK(t, s)
	must0(s.DeleteRoot("board:A"))
	if n := must(s.FreeAll()); n != 5 {
		t.Fatalf("freed %d listings, want 5", n)
	}
	rep := verifyOK(t, s)
	if rep.Blobs != 0 || rep.StoredBytes != 0 || rep.Listings != 0 {
		t.Fatalf("left: %+v", rep)
	}
	if n := countPrefix(t, s, pChunk) + countPrefix(t, s, pBlob) + countPrefix(t, s, pList) + countPrefix(t, s, pRef); n != 0 {
		t.Fatalf("%d keys left", n)
	}
}

func ptr[T any](v T) *T { return &v }

func countPrefix(t *testing.T, s *Store, p byte) int {
	db := s.db
	if p == pChunk {
		db = s.data
	}
	it, err := db.NewIter(nil)
	if err != nil {
		t.Fatal(err)
	}
	defer it.Close()
	n := 0
	for it.First(); it.Valid(); it.Next() {
		if it.Key()[0] == p {
			n++
		}
	}
	return n
}

func TestQuota(t *testing.T) {
	s := openTest(t, &Limits{Nodes: 10, Dirs: 3, Width: 4, Depth: 2, Copy: 5, Bytes: 100})
	must0(s.CreateRoot("board:A"))
	must2(s.AddDir("board:A", nil, -1, hdr("D1", "")))
	must2(s.AddDir("board:A", p("D1"), -1, hdr("D2", "")))
	if _, _, err := s.AddDir("board:A", p("D1/D2"), -1, hdr("D3", "")); !errors.Is(err, ErrQuota) {
		t.Fatalf("depth: %v", err)
	}
	for i := 0; i < 4; i++ {
		must2(s.AddFile("board:A", p("D1/D2"), -1, hdr(fmt.Sprintf("M.%d.A", i), ""), []byte("x")))
	}
	if _, _, err := s.AddFile("board:A", p("D1/D2"), -1, hdr("M.9.A", ""), nil); !errors.Is(err, ErrQuota) {
		t.Fatalf("width: %v", err)
	}
	if _, _, err := s.AddFile("board:A", nil, -1, hdr("M.9.A", ""), make([]byte, 100)); !errors.Is(err, ErrQuota) {
		t.Fatalf("bytes: %v", err)
	}
	// D1 holds 5 entries below: a copy of it brings 6.
	if _, _, err := s.Copy(Ref{"board:A", nil, -1, "D1"}, "board:A", nil, -1, ptr(hdr("Dc", ""))); !errors.Is(err, ErrQuota) {
		t.Fatalf("copy: %v", err)
	}
	// A copy of D2 brings 5 (D2 and its 4 files), within the copy quota,
	// but makes 11 nodes > 10: refused, nothing changed.
	if _, _, err := s.Copy(Ref{"board:A", p("D1"), -1, "D2"}, "board:A", nil, -1, ptr(hdr("Dc", ""))); err == nil || !strings.Contains(err.Error(), "nodes") {
		t.Fatalf("nodes: %v", err)
	}
	verifyOK(t, s)
	if tot := must(s.RootTotals("board:A")); tot.Nodes() != 6 {
		t.Fatalf("totals after refused copy: %+v", tot)
	}
	// Moving within the board is not a copy: D2 (5 entries) may move
	// where a copy of it may not.
	s.opts.Limits = func(string) Limits { return Limits{Copy: 3} }
	if _, _, err := s.Copy(Ref{"board:A", p("D1"), -1, "D2"}, "board:A", nil, -1, ptr(hdr("Dc", ""))); err == nil || !strings.Contains(err.Error(), "copy") {
		t.Fatalf("copy: %v", err)
	}
	must2(s.Move(Ref{"board:A", p("D1"), -1, "D2"}, "board:A", nil, -1, nil))
	verifyOK(t, s)
}

// TestRandom runs random operations against a plain in-memory model.
func TestRandom(t *testing.T) {
	s := openTest(t, nil)
	r := rand.New(rand.NewPCG(1, 2))
	boards := []string{"board:A", "board:B"}
	for _, b := range boards {
		must0(s.CreateRoot(b))
	}
	type mnode struct {
		kind     Kind
		name     string
		data     string
		children []*mnode
	}
	model := map[string]*mnode{"board:A": {kind: KindDir}, "board:B": {kind: KindDir}}
	var deepCopy func(n *mnode) *mnode
	deepCopy = func(n *mnode) *mnode {
		c := *n
		c.children = nil
		for _, ch := range n.children {
			c.children = append(c.children, deepCopy(ch))
		}
		return &c
	}
	// randomDir picks a directory: its path and model node.
	randomDir := func(b string) ([]string, *mnode) {
		var path []string
		n := model[b]
		for r.IntN(3) != 0 {
			var dirs []*mnode
			for _, c := range n.children {
				if c.kind == KindDir {
					dirs = append(dirs, c)
				}
			}
			if len(dirs) == 0 {
				break
			}
			n = dirs[r.IntN(len(dirs))]
			path = append(path, n.name)
		}
		return path, n
	}
	render := func(b string) string {
		var sb strings.Builder
		var rec func(n *mnode, depth int)
		rec = func(n *mnode, depth int) {
			for _, c := range n.children {
				fmt.Fprintf(&sb, "%s%s %s %q", strings.Repeat("  ", depth), c.kind, c.name, "")
				if c.kind == KindFile {
					fmt.Fprintf(&sb, " = %q", c.data)
				}
				sb.WriteString("\n")
				if c.kind == KindDir {
					rec(c, depth+1)
				}
			}
		}
		rec(model[b], 0)
		return sb.String()
	}
	seq := 0
	for step := 0; step < 3000; step++ {
		b := boards[r.IntN(2)]
		path, dir := randomDir(b)
		seq++
		name := fmt.Sprintf("N%d", seq)
		switch op := r.IntN(10); {
		case op < 3:
			data := fmt.Sprintf("d%d", r.IntN(20)) // repeats: dedup
			must2(s.AddFile(b, path, -1, hdr(name, ""), []byte(data)))
			dir.children = append(dir.children, &mnode{kind: KindFile, name: name, data: data})
		case op < 5:
			must2(s.AddDir(b, path, -1, hdr(name, "")))
			dir.children = append(dir.children, &mnode{kind: KindDir, name: name})
		case op < 7 && len(dir.children) > 0:
			// Copy something to a random place, maybe on the other board.
			src := dir.children[r.IntN(len(dir.children))]
			db := boards[r.IntN(2)]
			dpath, ddir := randomDir(db)
			must2(s.Copy(Ref{b, path, -1, src.name}, db, dpath, -1, ptr(hdr(name, ""))))
			c := deepCopy(src)
			c.name = name
			ddir.children = append(ddir.children, c)
		case op < 8 && len(dir.children) > 0:
			i := r.IntN(len(dir.children))
			must(s.Remove(Ref{b, path, -1, dir.children[i].name}))
			dir.children = slices.Delete(dir.children, i, i+1)
		case op < 9 && len(dir.children) > 0:
			i := r.IntN(len(dir.children))
			c := dir.children[i]
			if c.kind == KindFile {
				c.data = fmt.Sprintf("e%d", step)
				must(s.PutFile(Ref{b, path, -1, c.name}, []byte(c.data)))
			}
		case len(dir.children) > 0:
			i := r.IntN(len(dir.children))
			c := dir.children[i]
			db := boards[r.IntN(2)]
			dpath, ddir := randomDir(db)
			_, _, err := s.Move(Ref{b, path, -1, c.name}, db, dpath, -1, nil)
			isSelf := false
			if c.kind == KindDir {
				// Into itself when ddir is c or below it.
				var within func(n *mnode) bool
				within = func(n *mnode) bool {
					if n == ddir {
						return true
					}
					for _, ch := range n.children {
						if within(ch) {
							return true
						}
					}
					return false
				}
				isSelf = within(c)
			}
			switch {
			case isSelf:
				if !errors.Is(err, ErrMoveIntoSelf) {
					t.Fatalf("step %d: move into itself: %v", step, err)
				}
			case db == b && slices.Equal(dpath, path):
				if !errors.Is(err, ErrInvalidPos) {
					t.Fatalf("step %d: move in place: %v", step, err)
				}
			default:
				if err != nil {
					t.Fatalf("step %d: move: %v", step, err)
				}
				dir.children = slices.Delete(dir.children, i, i+1)
				ddir.children = append(ddir.children, c)
			}
		}
		if step%100 == 99 {
			for _, b := range boards {
				if got, want := dump(t, s, b), render(b); got != want {
					t.Fatalf("step %d board %s:\n%s\nwant\n%s", step, b, got, want)
				}
			}
			if r.IntN(2) == 0 {
				must(s.FreeAll())
			}
			verifyOK(t, s)
		}
	}
	for _, b := range boards {
		must0(s.DeleteRoot(b))
	}
	must(s.FreeAll())
	if rep := verifyOK(t, s); rep.Listings != 0 || rep.Blobs != 0 {
		t.Fatalf("left %+v", rep)
	}
}

func TestEncoding(t *testing.T) {
	l := &Listing{Entries: []Entry{
		{Header: hdr("M.1.A", "t"), Kind: KindFile, Size: 3, Blob: HashContent([]byte("abc"))},
		{Header: hdr("D1", "d"), Kind: KindDir, Child: 77, Sub: Totals{Files: 3, Bytes: 9, Depth: 2}},
		{Header: hdr("L", "l"), Kind: KindLink, Target: "/x/y"},
		{Header: hdr("X", "m"), Kind: KindMissing},
		{Header: hdr("S.index", "i"), Kind: KindIndex},
	}}
	l.Recount()
	got, err := decodeListing(encodeListing(l))
	if err != nil {
		t.Fatal(err)
	}
	if fmt.Sprint(got) != fmt.Sprint(l) {
		t.Fatalf("roundtrip:\n%+v\n%+v", got, l)
	}
	if !bytes.Equal(encodeListing(got), encodeListing(l)) {
		t.Fatal("not stable")
	}
	// The table of contents counts nothing: it is made from the others.
	if got.Totals.Depth != 3 || got.Totals.Nodes() != 4+3 {
		t.Fatalf("totals %+v", got.Totals)
	}
}

func TestLargeBlob(t *testing.T) {
	s := openTest(t, nil)
	must0(s.CreateRoot("board:A"))
	data := bytes.Repeat([]byte("0123456789"), ChunkSize/4)
	must2(s.AddFile("board:A", nil, -1, hdr("M.1.A", ""), data))
	got, _ := must2(s.ReadFile("board:A", nil, -1, "M.1.A"))
	if !bytes.Equal(got, data) {
		t.Fatal("large blob")
	}
	must2(s.AddFile("board:A", nil, -1, hdr("M.2.A", ""), nil))
	got, _ = must2(s.ReadFile("board:A", nil, -1, "M.2.A"))
	if len(got) != 0 {
		t.Fatal("empty blob")
	}
	verifyOK(t, s)
}

func TestStaged(t *testing.T) {
	dir := t.TempDir() + "/man.pebble"
	s, err := Open(Options{Dir: dir, NoSync: true, NoReaper: true})
	if err != nil {
		t.Fatal(err)
	}
	must0(s.CreateRoot("board:A"))
	data := []byte("shared content")
	h := HashContent(data)
	add := func(st Staged, name string) {
		t.Helper()
		err := s.UpdateNoSync(func(tx *Tx) error {
			if _, err := tx.PutStagedBlob(st, data); err != nil {
				return err
			}
			e := Entry{Header: hdr(name, ""), Kind: KindFile, Size: uint64(len(data)), Blob: h}
			if err := tx.incEntry(&e); err != nil {
				return err
			}
			_, _, err := tx.add("board:A", nil, -1, e)
			return err
		})
		if err != nil {
			t.Fatal(err)
		}
	}
	// Two imports stage the same new content; the second finds the first
	// recorded and drops its copy.
	st1 := must(s.StageBlobs([]BlobID{h, h}, [][]byte{data, data}))
	st2 := must(s.StageBlobs([]BlobID{h}, [][]byte{data}))
	if st1[0].Serial == 0 || st1[1].Serial != st1[0].Serial || st2[0].Serial == st1[0].Serial || st1[0].Existed {
		t.Fatalf("staged %+v %+v", st1, st2)
	}
	add(st1[0], "M.1.A")
	add(st1[1], "M.2.A")
	add(st2[0], "M.3.A")
	if n := countPrefix(t, s, pChunk); n != 1 {
		t.Fatalf("%d chunks, want 1", n)
	}
	rep := verifyOK(t, s)
	if rep.Blobs != 1 {
		t.Fatalf("%+v", rep)
	}
	// Staged as stored, then gone: written by the Tx.
	st3 := must(s.StageBlobs([]BlobID{h}, [][]byte{data}))
	if !st3[0].Existed {
		t.Fatal("not found stored")
	}
	for _, n := range []string{"M.1.A", "M.2.A", "M.3.A"} {
		must(s.Remove(Ref{"board:A", nil, -1, n}))
	}
	add(st3[0], "M.4.A")
	got, _ := must2(s.ReadFile("board:A", nil, -1, "M.4.A"))
	if !bytes.Equal(got, data) {
		t.Fatal("content")
	}
	verifyOK(t, s)
	// A staged copy never recorded keeps its serial taken after a restart.
	other := []byte("never recorded")
	st4 := must(s.StageBlobs([]BlobID{HashContent(other)}, [][]byte{other}))
	s.Close()
	s, err = Open(Options{Dir: dir, NoSync: true, NoReaper: true})
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	if s.nextBlob.Load() <= st4[0].Serial {
		t.Fatalf("next serial %d reuses %d", s.nextBlob.Load(), st4[0].Serial)
	}
}
