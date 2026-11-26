package storage

import (
	"errors"
	"fmt"
	"strings"
	"testing"
	"time"
)

func names(t *testing.T, s *Store, root string, path ...string) string {
	t.Helper()
	l, err := s.List(root, path)
	if err != nil {
		t.Fatal(err)
	}
	var ns []string
	for i := range l.Entries {
		ns = append(ns, l.Entries[i].Name())
	}
	return strings.Join(ns, " ")
}

func TestTrash(t *testing.T) {
	s := openTest(t, nil)
	if err := s.CreateRoot("board:Test"); err != nil {
		t.Fatal(err)
	}
	must2(s.AddDir("board:Test", nil, -1, hdr("D1", "dir")))
	must2(s.AddFile("board:Test", p("D1"), -1, hdr("M.1.A", "in D1"), []byte("one")))
	must2(s.AddFile("board:Test", p("D1"), -1, hdr("M.2.A", "also in D1"), []byte("two")))
	must2(s.AddFile("board:Test", nil, -1, hdr("M.3.A", "top"), []byte("three")))
	must2(s.AddDir("board:Test", nil, -1, hdr("D2", "another")))
	must2(s.AddFile("board:Test", p("D2"), -1, hdr("M.4.A", "in D2"), []byte("four")))

	if tr, infos, err := s.Trash("board:Test"); err != nil || len(tr.Entries) != 0 || infos != nil {
		t.Fatalf("empty trash: %v %v", tr, err)
	}
	before := time.Now().Unix()
	fileName := must(s.Delete(Ref{"board:Test", p("D1"), 0, "M.1.A"}, "alice"))
	dirName := must(s.Delete(Ref{"board:Test", nil, -1, "D2"}, "bob"))
	if got := names(t, s, "board:Test"); got != "D1 M.3.A" {
		t.Fatalf("top after delete: %s", got)
	}
	if got := names(t, s, "board:Test", "D1"); got != "M.2.A" {
		t.Fatalf("D1 after delete: %s", got)
	}
	// Totals are those of what is not deleted.
	if tot := must(s.RootTotals("board:Test")); tot.Files != 2 || tot.Dirs != 1 {
		t.Fatalf("totals %+v", tot)
	}
	// The last deleted first, with what was kept.
	tr, infos, err := s.Trash("board:Test")
	if err != nil || len(tr.Entries) != 2 || tr.Entries[0].Name() != dirName || tr.Entries[1].Name() != fileName {
		t.Fatalf("trash: %+v %v", tr, err)
	}
	if ti := infos[1]; ti.Name != "M.1.A" || strings.Join(ti.Path, "/") != "D1" || ti.Index != 0 || ti.By != "alice" || ti.At < before {
		t.Fatalf("info %+v", ti)
	}
	if ti := infos[0]; ti.Name != "D2" || len(ti.Path) != 0 || ti.Index != 2 || ti.By != "bob" {
		t.Fatalf("info %+v", ti)
	}
	// Deleted directories are read as they were.
	if got := names(t, s, TrashRoot("board:Test"), dirName); got != "M.4.A" {
		t.Fatalf("in the deleted D2: %s", got)
	}
	data, _, err := s.ReadFile(TrashRoot("board:Test"), nil, -1, fileName)
	if err != nil || string(data) != "one" {
		t.Fatalf("deleted file: %q %v", data, err)
	}
	// The trash is not an area, and cannot be changed as one.
	if roots := must(s.Roots()); len(roots) != 1 {
		t.Fatalf("roots %v", roots)
	}
	if _, _, err := s.AddFile(TrashRoot("board:Test"), nil, -1, hdr("", "x"), nil); err == nil {
		t.Fatal("added to the trash")
	}

	// What a deleted directory has may be copied out of it.
	if _, _, err := s.Copy(Ref{TrashRoot("board:Test"), []string{dirName}, -1, "M.4.A"}, "board:Test", nil, -1, nil); err != nil {
		t.Fatal(err)
	}
	if _, err := s.Remove(Ref{"board:Test", nil, -1, "M.4.A"}); err != nil {
		t.Fatal(err)
	}
	if _, _, err := s.Move(Ref{TrashRoot("board:Test"), nil, -1, fileName}, "board:Test", nil, -1, nil); err == nil {
		t.Fatal("moved out of the trash")
	}

	// Back where it was.
	r, err := s.Restore("board:Test", fileName)
	if err != nil || r.Index != 0 || r.Name != "M.1.A" || strings.Join(r.Path, "/") != "D1" {
		t.Fatalf("restore: %+v %v", r, err)
	}
	if got := names(t, s, "board:Test", "D1"); got != "M.1.A M.2.A" {
		t.Fatalf("D1 after restore: %s", got)
	}
	if _, err := s.Restore("board:Test", fileName); !errors.Is(err, ErrNotFound) {
		t.Fatalf("restored twice: %v", err)
	}
	// Its name taken, and its index past the end: renamed, at the end.
	must2(s.AddDir("board:Test", nil, 0, hdr("D2", "a new D2")))
	r, err = s.Restore("board:Test", dirName)
	if err != nil || r.Name == "D2" || !strings.HasPrefix(r.Name, "D") || r.Index != 2 {
		t.Fatalf("restore dir: %+v %v", r, err)
	}
	if got := names(t, s, "board:Test", r.Name); got != "M.4.A" {
		t.Fatalf("restored D2: %s", got)
	}

	// Its directory gone: at the end of the top.
	must(s.Delete(Ref{"board:Test", p("D1"), -1, "M.2.A"}, "alice"))
	d1 := must(s.Delete(Ref{"board:Test", nil, -1, "D1"}, "alice"))
	tr, _, _ = s.Trash("board:Test")
	m2 := tr.Entries[1].Name()
	r, err = s.Restore("board:Test", m2)
	if err != nil || len(r.Path) != 0 || r.Name != "M.2.A" {
		t.Fatalf("restore without its directory: %+v %v", r, err)
	}

	// Renaming the area moves its trash.
	if err := s.RenameRoot("board:Test", "board:Renamed"); err != nil {
		t.Fatal(err)
	}
	if _, ti, err := s.TrashEntry("board:Renamed", d1); err != nil || ti.Name != "D1" {
		t.Fatalf("trash after rename: %+v %v", ti, err)
	}
	if _, _, err := s.Trash("board:Test"); !errors.Is(err, ErrNoRoot) {
		t.Fatalf("old trash: %v", err)
	}

	// Purge what was deleted before a time.
	if n, err := s.Purge(before); err != nil || n != 0 {
		t.Fatalf("purge nothing: %d %v", n, err)
	}
	if n, err := s.Purge(time.Now().Unix() + 1); err != nil || n != 1 {
		t.Fatalf("purge: %d %v", n, err)
	}
	if tr, _, _ := s.Trash("board:Renamed"); len(tr.Entries) != 0 {
		t.Fatalf("left %d", len(tr.Entries))
	}
	x := must(s.Delete(Ref{"board:Renamed", nil, -1, "M.3.A"}, "carol"))
	if err := s.PurgeEntry("board:Renamed", x); err != nil {
		t.Fatal(err)
	}
	must(s.Delete(Ref{"board:Renamed", nil, -1, "M.2.A"}, "carol"))

	// Deleting the area drops its trash.
	if err := s.DeleteRoot("board:Renamed"); err != nil {
		t.Fatal(err)
	}
	if _, err := s.FreeAll(); err != nil {
		t.Fatal(err)
	}
	rep := verifyOK(t, s)
	if rep.Listings != 0 || rep.Blobs != 0 {
		t.Fatalf("left %+v", rep)
	}
	if roots, _ := s.trashRoots(); len(roots) != 0 {
		t.Fatalf("trash roots %v", roots)
	}
}

func TestExpire(t *testing.T) {
	s := openTest(t, nil)
	if err := s.CreateRoot("board:Note"); err != nil {
		t.Fatal(err)
	}
	must2(s.AddDir("board:Note", nil, -1, hdr("SONGO", "songs")))
	now := time.Now().Unix()
	if err := s.Update(func(tx *Tx) error {
		for i, age := range []int64{10, 6, 4, 0} {
			e, err := tx.FileEntry(hdr(fmt.Sprintf("M.%d.A", i), "song"), []byte(fmt.Sprint("song ", i)))
			if err != nil {
				return err
			}
			if err := tx.incEntry(&e); err != nil {
				return err
			}
			e.Mtime = now - age*86400
			if _, _, err := tx.add("board:Note", p("SONGO"), -1, e); err != nil {
				return err
			}
		}
		// An old directory is left.
		id, tot, err := tx.NewListing(nil)
		if err != nil {
			return err
		}
		tx.refs[id]++
		_, _, err = tx.add("board:Note", p("SONGO"), -1, Entry{Header: hdr("D1", "dir"), Kind: KindDir, Child: id, Sub: tot, Mtime: 1})
		return err
	}); err != nil {
		t.Fatal(err)
	}
	if n, err := s.Expire("board:Note", p("SONGO"), now-5*86400); err != nil || n != 2 {
		t.Fatalf("expire: %d %v", n, err)
	}
	if got := names(t, s, "board:Note", "SONGO"); got != "M.2.A M.3.A D1" {
		t.Fatalf("left %s", got)
	}
	if n, err := s.Expire("board:Note", p("SONGO"), now-5*86400); err != nil || n != 0 {
		t.Fatalf("expire again: %d %v", n, err)
	}
	if _, err := s.FreeAll(); err != nil {
		t.Fatal(err)
	}
	if rep := verifyOK(t, s); rep.Blobs != 2 {
		t.Fatalf("blobs %d", rep.Blobs)
	}
}

func TestSharing(t *testing.T) {
	s := openTest(t, nil)
	if err := s.CreateRoot("board:A"); err != nil {
		t.Fatal(err)
	}
	must2(s.AddDir("board:A", nil, -1, hdr("D1", "dir")))
	must2(s.AddFile("board:A", p("D1"), -1, hdr("M.1.A", "one"), []byte("same")))
	must2(s.AddFile("board:A", nil, -1, hdr("M.2.A", "two"), []byte("same")))
	h := hdr("Dcopy", "copy")
	must2(s.Copy(Ref{"board:A", nil, -1, "D1"}, "board:A", nil, -1, &h)) // shared
	must2(s.AddDir("board:A", nil, -1, hdr("D2", "alike")))
	must2(s.AddFile("board:A", p("D2"), -1, hdr("M.1.A", "one"), []byte("same")))
	must2(s.AddDir("board:A", nil, -1, hdr("D3", "alike")))
	must2(s.AddFile("board:A", p("D3"), -1, hdr("M.1.A", "one"), []byte("same")))
	must2(s.AddDir("board:A", nil, -1, hdr("E", "empty")))
	r, err := s.Sharing()
	if err != nil {
		t.Fatal(err)
	}
	// Seen: D1, its copy, D2, D3, E; M.1.A four times and M.2.A.
	if r.Dirs != 5 || r.Files != 5 || r.Bytes != 20 || r.Blobs != 1 || r.StoredBytes != 4 {
		t.Fatalf("%+v", r)
	}
	if r.SharedListings != 1 || r.SharedListingRefs != 1 || r.SharedBlobs != 1 || r.SharedBlobRefs != 3 || r.SavedBytes != 12 {
		t.Fatalf("shared %+v", r)
	}
	// D1 (with its copy), D2 and D3 are alike, kept as 3 listings.
	if r.SameDirs != 2 || r.SameDirEntries != 2 || r.EmptyDirs != 1 {
		t.Fatalf("alike %+v", r)
	}
}
