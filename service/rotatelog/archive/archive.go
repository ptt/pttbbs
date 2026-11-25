// Package archive is where and how bin/rotatelog keeps the archives of the
// logs, for it and the programs reading them (uquery.ctl import,
// activity.ctl diag): adm/YYYY/<name>/<name>-YYYYMMDD[-N]<ext>, one
// directory per year and in it one per log, named after the last day the
// log covers; -2, -3... when there is more than one for a day; <ext> is
// .zst, .gz or none, as configured.
package archive

import (
	"compress/gzip"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/klauspost/compress/zstd"
)

// Root is the directory of the archives, relative to BBSHOME.
const Root = "adm"

// Exts are the extensions of the archives of logs (compressed with zstd,
// gzip, or not).
var Exts = []string{".zst", ".gz", ""}

// Dir is the directory of the archives of name for year, relative to
// BBSHOME: adm/YYYY/<name>.
func Dir(name string, year int) string {
	return filepath.Join(Root, strconv.Itoa(year), name)
}

// FileName is <name>-YYYYMMDD<suffix>, or <name>-YYYYMMDD-<n><suffix> for
// n > 1.
func FileName(name string, day time.Time, n int, suffix string) string {
	base := name + "-" + day.Format("20060102")
	if n > 1 {
		base += "-" + strconv.Itoa(n)
	}
	return base + suffix
}

// NewPath picks the path (relative to BBSHOME) for a new archive of name
// for day: the first of <name>-YYYYMMDD<suffix>, -2, -3... not taken (with
// any extension).
func NewPath(home, name string, day time.Time, suffix string) string {
	dir := Dir(name, day.Year())
	for n := 1; ; n++ {
		base := filepath.Join(dir, FileName(name, day, n, ""))
		taken := false
		for _, ext := range append([]string{suffix}, Exts...) {
			if _, err := os.Lstat(filepath.Join(home, base+ext)); err == nil {
				taken = true
			}
		}
		if !taken {
			return base + suffix
		}
	}
}

// entry is an archive found, by its day and number.
type entry struct {
	path string
	day  string // YYYYMMDD
	n    int
}

// parse tells the day and number of an archive of name, by its file name;
// ok is false for other files (e.g. .tmp files being written).
func parse(name, file string) (day string, n int, ok bool) {
	rest, found := strings.CutPrefix(file, name+"-")
	if !found || len(rest) < 8 {
		return "", 0, false
	}
	day, rest = rest[:8], rest[8:]
	if _, err := time.Parse("20060102", day); err != nil {
		return "", 0, false
	}
	n = 1
	if strings.HasPrefix(rest, "-") {
		i := 1
		for i < len(rest) && rest[i] >= '0' && rest[i] <= '9' {
			i++
		}
		v, err := strconv.Atoi(rest[1:i])
		if err != nil || v < 2 {
			return "", 0, false
		}
		n, rest = v, rest[i:]
	}
	for _, ext := range Exts {
		if rest == ext {
			return day, n, true
		}
	}
	return "", 0, false
}

func list(home, name string, year int) []entry {
	dir := filepath.Join(home, Dir(name, year))
	ents, err := os.ReadDir(dir)
	if err != nil {
		return nil
	}
	var out []entry
	for _, e := range ents {
		if e.IsDir() {
			continue
		}
		if day, n, ok := parse(name, e.Name()); ok {
			out = append(out, entry{filepath.Join(dir, e.Name()), day, n})
		}
	}
	sort.Slice(out, func(i, j int) bool {
		if out[i].day != out[j].day {
			return out[i].day < out[j].day
		}
		return out[i].n < out[j].n
	})
	return out
}

// Of gives the archives of name for day (full paths), in order: the first,
// then -2, -3...
func Of(home, name string, day time.Time) []string {
	ymd := day.Format("20060102")
	var out []string
	for _, e := range list(home, name, day.Year()) {
		if e.day == ymd {
			out = append(out, e.path)
		}
	}
	return out
}

// Newest gives the newest archive of name (full path): of the latest day,
// the last one; "" if there is none.
func Newest(home, name string) string {
	ents, err := os.ReadDir(filepath.Join(home, Root))
	if err != nil {
		return ""
	}
	var years []int
	for _, e := range ents {
		if y, err := strconv.Atoi(e.Name()); err == nil && e.IsDir() {
			years = append(years, y)
		}
	}
	sort.Sort(sort.Reverse(sort.IntSlice(years)))
	for _, y := range years {
		if l := list(home, name, y); len(l) > 0 {
			return l[len(l)-1].path
		}
	}
	return ""
}

// Open opens a log or an archive, decompressing it if it ends with .zst or
// .gz.
func Open(path string) (io.ReadCloser, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	switch {
	case strings.HasSuffix(path, ".zst"):
		d, err := zstd.NewReader(f)
		if err != nil {
			f.Close()
			return nil, err
		}
		return readCloser{d.IOReadCloser(), f}, nil
	case strings.HasSuffix(path, ".gz"):
		z, err := gzip.NewReader(f)
		if err != nil {
			f.Close()
			return nil, err
		}
		return readCloser{z, f}, nil
	}
	return f, nil
}

// readCloser reads r, and closes it and then f.
type readCloser struct {
	r io.ReadCloser
	f *os.File
}

func (rc readCloser) Read(p []byte) (int, error) { return rc.r.Read(p) }

func (rc readCloser) Close() error {
	rc.r.Close()
	return rc.f.Close()
}
