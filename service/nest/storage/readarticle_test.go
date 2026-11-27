package storage

import (
	"bytes"
	"os"
	"path/filepath"
	"testing"
)

func TestReadArticleFileLimit(t *testing.T) {
	line := append(bytes.Repeat([]byte("\xa1@"), 99), '\n') // Big5 full width spaces

	for _, tc := range []struct {
		name string
		size int // bytes written (rounded up to whole lines)
		cut  bool
	}{
		{"under", MaxArticleFileSize - 1000, false},
		{"over", MaxArticleFileSize + 5<<20, true},
	} {
		t.Run(tc.name, func(t *testing.T) {
			data := bytes.Repeat(line, (tc.size+len(line)-1)/len(line))
			path := filepath.Join(t.TempDir(), "M.1.A.000")
			if err := os.WriteFile(path, data, 0644); err != nil {
				t.Fatal(err)
			}
			got, err := ReadArticleFile(path)
			if err != nil {
				t.Fatal(err)
			}
			if !tc.cut {
				if !bytes.Equal(got, data) {
					t.Fatalf("read %d bytes, want all %d", len(got), len(data))
				}
				return
			}
			if len(got) > MaxArticleFileSize || len(got) <= MaxArticleFileSize-len(line) {
				t.Fatalf("read %d bytes, want just under %d", len(got), MaxArticleFileSize)
			}
			if !bytes.HasPrefix(data, got) || got[len(got)-1] != '\n' {
				t.Fatalf("not cut at the end of a line")
			}
		})
	}
}
