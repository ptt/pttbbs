package archive

import (
	"bytes"
	"compress/gzip"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/klauspost/compress/zstd"
)

func day(s string) time.Time {
	t, err := time.ParseInLocation("2006-01-02", s, time.Local)
	if err != nil {
		panic(err)
	}
	return t
}

func touch(t *testing.T, home, rel string, data []byte) {
	t.Helper()
	p := filepath.Join(home, rel)
	os.MkdirAll(filepath.Dir(p), 0755)
	if err := os.WriteFile(p, data, 0644); err != nil {
		t.Fatal(err)
	}
}

func TestPaths(t *testing.T) {
	home := t.TempDir()
	d := day("2026-11-24")
	if got := Dir("usies", 2026); got != "adm/2026/usies" {
		t.Errorf("Dir = %s", got)
	}
	if got := NewPath(home, "usies", d, ".zst"); got != "adm/2026/usies/usies-20261124.zst" {
		t.Errorf("NewPath = %s", got)
	}
	// Taken with another extension: -2.
	touch(t, home, "adm/2026/usies/usies-20261124.gz", nil)
	if got := NewPath(home, "usies", d, ".zst"); got != "adm/2026/usies/usies-20261124-2.zst" {
		t.Errorf("NewPath = %s", got)
	}
	touch(t, home, "adm/2026/usies/usies-20261124-2.zst", nil)
	touch(t, home, "adm/2026/usies/usies-20261124-10.zst", nil)
	touch(t, home, "adm/2026/usies/usies-20261124.zst.tmp", nil)
	touch(t, home, "adm/2026/usies/usies-20261123", nil)
	touch(t, home, "adm/2026/usies/usies-2026112", nil)
	touch(t, home, "adm/2026/usies/other-20261124.zst", nil)
	got := Of(home, "usies", d)
	var rel []string
	for _, p := range got {
		r, _ := filepath.Rel(home, p)
		rel = append(rel, r)
	}
	want := "adm/2026/usies/usies-20261124.gz adm/2026/usies/usies-20261124-2.zst adm/2026/usies/usies-20261124-10.zst"
	if strings.Join(rel, " ") != want {
		t.Errorf("Of = %v", rel)
	}
	if got := Newest(home, "usies"); got != filepath.Join(home, "adm/2026/usies/usies-20261124-10.zst") {
		t.Errorf("Newest = %s", got)
	}
	// A newer year, and a missing log.
	touch(t, home, "adm/2027/usies/usies-20270101.zst", nil)
	if got := Newest(home, "usies"); got != filepath.Join(home, "adm/2027/usies/usies-20270101.zst") {
		t.Errorf("Newest = %s", got)
	}
	if got := Newest(home, "none"); got != "" {
		t.Errorf("Newest(none) = %s", got)
	}
}

func TestOpen(t *testing.T) {
	home := t.TempDir()
	const text = "11/24/2026 00:01:01 Tue ENTER a 1.2.3.4\n"
	var zb, gb bytes.Buffer
	zw, _ := zstd.NewWriter(&zb)
	zw.Write([]byte(text))
	zw.Close()
	gw := gzip.NewWriter(&gb)
	gw.Write([]byte(text))
	gw.Close()
	touch(t, home, "a.zst", zb.Bytes())
	touch(t, home, "a.gz", gb.Bytes())
	touch(t, home, "a", []byte(text))
	touch(t, home, "bad.zst", zb.Bytes()[:zb.Len()-3])
	for _, f := range []string{"a.zst", "a.gz", "a"} {
		r, err := Open(filepath.Join(home, f))
		if err != nil {
			t.Fatal(err)
		}
		b, err := io.ReadAll(r)
		r.Close()
		if err != nil || string(b) != text {
			t.Errorf("%s = %q %v", f, b, err)
		}
	}
	r, err := Open(filepath.Join(home, "bad.zst"))
	if err == nil {
		_, err = io.ReadAll(r)
		r.Close()
	}
	if err == nil {
		t.Errorf("truncated .zst: no error")
	}
}
