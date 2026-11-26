package legacy

import (
	"fmt"
	"path/filepath"
	"testing"

	"pttbbs/gem/storage"
)

// bigArea stores an area of dirs directories of files files each (the files
// share one content) as board:Big.
func bigArea(tb testing.TB, dirs, files int) *storage.Store {
	tb.Helper()
	st, err := storage.Open(storage.Options{Dir: filepath.Join(tb.TempDir(), "gem.pebble"), NoSync: true, NoReaper: true})
	if err != nil {
		tb.Fatal(err)
	}
	tb.Cleanup(func() { st.Close() })
	err = st.Update(func(t *storage.Tx) error {
		var top []storage.Entry
		for d := 0; d < dirs; d++ {
			var ents []storage.Entry
			for f := 0; f < files; f++ {
				e, err := t.FileEntry(hdr(fmt.Sprintf("M.%d.A", f), fmt.Sprintf("article %d of %d", f, d)), []byte("body"))
				if err != nil {
					return err
				}
				ents = append(ents, e)
			}
			id, tot, err := t.NewListing(ents)
			if err != nil {
				return err
			}
			h := hdr(fmt.Sprintf("D%X", d), fmt.Sprintf("directory %d", d))
			h.SetFilemode(0)
			top = append(top, storage.Entry{Header: h, Kind: storage.KindDir, Child: id, Sub: tot})
		}
		id, _, err := t.NewListing(top)
		if err != nil {
			return err
		}
		return t.SetRoot("board:Big", id)
	})
	if err != nil {
		tb.Fatal(err)
	}
	return st
}

func TestIndexBig(t *testing.T) {
	st := bigArea(t, 100, 100)
	text, err := Index(st, "board:Big")
	if err != nil {
		t.Fatal(err)
	}
	// The header, and a line for each of the 100 directories and 10000
	// files.
	if n := countLines(text); n != 2+100+100*100 {
		t.Fatalf("%d lines", n)
	}
}

func countLines(b []byte) int {
	n := 0
	for _, c := range b {
		if c == '\n' {
			n++
		}
	}
	return n
}

// 1000 directories of 1000 files: a million entries, past IndexMaxBytes.
func BenchmarkIndex(b *testing.B) {
	st := bigArea(b, 1000, 1000)
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		text, err := Index(st, "board:Big")
		if err != nil {
			b.Fatal(err)
		}
		b.SetBytes(int64(len(text)))
	}
}
