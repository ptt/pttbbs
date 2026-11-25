package rotate

import (
	"archive/tar"
	"bytes"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"
	"time"
)

func mustParse(t *testing.T, s string) *Config {
	t.Helper()
	cfg, err := ParseConfig(strings.NewReader(s))
	if err != nil {
		t.Fatal(err)
	}
	return cfg
}

func TestParseConfig(t *testing.T) {
	cfg := mustParse(t, `
# comment
compress gzip
log/push     daily       archive
weekday sat
log/post     monthly     archive:posts compress=zstd:19
log/regmaild.log weekly  archive   copytruncate
log/x        weekly:mon  archive
usies        daily       archive compress=none
tmp/         weekly      snapshot
`)
	if len(cfg.Entries) != 6 {
		t.Fatalf("entries = %d", len(cfg.Entries))
	}
	e := cfg.Entries
	if e[0].Compression.String() != "gzip" || e[0].Name != "push" || e[0].Weekday != time.Sunday {
		t.Errorf("push = %+v", e[0])
	}
	if e[1].Period != Monthly || e[1].Compression.String() != "zstd:19" || e[1].Name != "posts" {
		t.Errorf("post = %+v", e[1])
	}
	if !e[2].CopyTruncate || e[2].Weekday != time.Saturday || e[2].Name != "regmaild.log" {
		t.Errorf("regmaild = %+v", e[2])
	}
	if e[3].Weekday != time.Monday || e[3].When() != "weekly:mon" {
		t.Errorf("x = %+v", e[3])
	}
	if e[4].Kind != Archive || e[4].Name != "usies" || e[4].Compression.Ext() != "" {
		t.Errorf("usies = %+v", e[4])
	}
	if e[5].Kind != Snapshot || e[5].Path != "tmp" || e[5].Name != "tmp" {
		t.Errorf("tmp = %+v", e[5])
	}
	if DefaultCompression.String() != "zstd:9" {
		t.Errorf("default = %s", DefaultCompression)
	}

	for _, bad := range []string{
		"compress bzip2",
		"compress zstd:23",
		"compress none:3",
		"weekday someday",
		"/etc/passwd daily archive",
		"../x daily archive",
		"a/../../x daily archive",
		"x hourly archive",
		"x daily:mon archive",
		"x weekly:xyz archive",
		"x daily keep",
		"x daily keep:0",
		"x daily rename",
		"x daily archive sometimes",
		"x daily archive\nx weekly archive",
		"a daily archive:n\nb daily archive:n",
		"tmp daily snapshot copytruncate",
		"x daily",
	} {
		if _, err := ParseConfig(strings.NewReader(bad)); err == nil {
			t.Errorf("ParseConfig(%q): no error", bad)
		}
	}
	if _, err := ParseConfig(strings.NewReader(DefaultConfig)); err != nil {
		t.Errorf("DefaultConfig: %v", err)
	}
}

func at(s string) time.Time {
	t, err := time.ParseInLocation("2006-01-02T15:04", s, time.Local)
	if err != nil {
		panic(err)
	}
	return t
}

func TestDue(t *testing.T) {
	cfg := mustParse(t, "d daily archive\nw weekly:sat archive\nm monthly archive\n")
	d, w, m := cfg.Entries[0], cfg.Entries[1], cfg.Entries[2]
	r := &Rotator{Config: cfg, State: State{}, Now: at("2026-11-25T00:01")} // a Wednesday
	// Never done: only on their day.
	if !r.Due(d) || r.Due(w) || r.Due(m) {
		t.Errorf("new: due %v %v %v", r.Due(d), r.Due(w), r.Due(m))
	}
	if got := Next(w, r.Now).Format(dateLayout); got != "2026-11-28" {
		t.Errorf("next weekly = %s", got)
	}
	if got := Next(m, r.Now).Format(dateLayout); got != "2026-12-01" {
		t.Errorf("next monthly = %s", got)
	}
	r.State["d"] = &EntryState{Last: "2026-11-25"}
	r.State["w"] = &EntryState{Last: "2026-11-14"} // missed the 21st
	r.State["m"] = &EntryState{Last: "2026-11-01"}
	if r.Due(d) || !r.Due(w) || r.Due(m) {
		t.Errorf("done: due %v %v %v", r.Due(d), r.Due(w), r.Due(m))
	}
	r.Now = at("2026-12-01T00:01")
	if !r.Due(d) || !r.Due(m) {
		t.Errorf("next month: due %v %v", r.Due(d), r.Due(m))
	}
}

func write(t *testing.T, path, s string) {
	t.Helper()
	os.MkdirAll(filepath.Dir(path), 0755)
	if err := os.WriteFile(path, []byte(s), 0644); err != nil {
		t.Fatal(err)
	}
}

// writeAt writes path, last written at when (as "2006-01-02T15:04").
func writeAt(t *testing.T, path, s, when string) {
	t.Helper()
	write(t, path, s)
	mt := at(when)
	os.Chtimes(path, mt, mt)
}

func read(t *testing.T, path string, c Compression) string {
	t.Helper()
	f, err := os.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	r, err := decompressor(f, c)
	if err != nil {
		t.Fatal(err)
	}
	b, err := io.ReadAll(r)
	if err != nil {
		t.Fatal(err)
	}
	return string(b)
}

func files(t *testing.T, home string) []string {
	var out []string
	filepath.Walk(home, func(p string, info os.FileInfo, err error) error {
		if err == nil && !info.IsDir() {
			rel, _ := filepath.Rel(home, p)
			if !strings.HasPrefix(rel, StateDir) {
				out = append(out, rel)
			}
		}
		return nil
	})
	sort.Strings(out)
	return out
}

func run(t *testing.T, home, conf, now string) []error {
	t.Helper()
	state, err := LoadState(home)
	if err != nil {
		t.Fatal(err)
	}
	r := &Rotator{Home: home, Config: mustParse(t, conf), State: state, Now: at(now),
		Out: io.Discard}
	errs := r.Run(nil)
	if err := SaveState(home, state); err != nil {
		t.Fatal(err)
	}
	return errs
}

func TestArchive(t *testing.T) {
	for _, c := range []string{"zstd", "zstd:19", "gzip", "none"} {
		comp, _ := ParseCompression(c)
		home := t.TempDir()
		conf := "compress " + c + "\nlog/usies daily archive:usies\netc/mailog daily archive:mailog\n"
		// bin/account moved usies there at 0:01.
		writeAt(t, home+"/log/usies", "day 1\n", "2026-11-25T00:01")
		if errs := run(t, home, conf, "2026-11-25T00:20"); errs != nil {
			t.Fatal(errs)
		}
		zst := "adm/2026/usies/usies-20261124" + comp.Ext()
		if got := read(t, home+"/"+zst, comp); got != "day 1\n" {
			t.Errorf("%s: %s = %q", c, zst, got)
		}
		if exists(home + "/log/usies") {
			t.Errorf("%s: log/usies left", c)
		}
		// Again the same day: nothing.
		writeAt(t, home+"/log/usies", "more\n", "2026-11-25T03:00")
		run(t, home, conf, "2026-11-25T03:00")
		if b, _ := os.ReadFile(home + "/log/usies"); string(b) != "more\n" {
			t.Errorf("%s: rotated twice a day", c)
		}
		run(t, home, conf, "2026-11-26T00:20")
		want := []string{"adm/2026/usies/usies-20261124" + comp.Ext(),
			"adm/2026/usies/usies-20261125" + comp.Ext()}
		if got := files(t, home); strings.Join(got, " ") != strings.Join(want, " ") {
			t.Errorf("%s: files = %v, want %v", c, got, want)
		}
	}
}

func TestArchiveDay(t *testing.T) {
	home := t.TempDir()
	conf := "etc/mailog daily archive:mailog\n"
	// Last written two days before (nothing since): named after that day.
	writeAt(t, home+"/etc/mailog", "old\n", "2026-11-23T15:00")
	if errs := run(t, home, conf, "2026-11-25T00:20"); errs != nil {
		t.Fatal(errs)
	}
	if got := read(t, home+"/adm/2026/mailog/mailog-20261123.zst", DefaultCompression); got != "old\n" {
		t.Errorf("archive = %q", got)
	}
}

func TestArchiveRetry(t *testing.T) {
	home := t.TempDir()
	conf := "log/usies daily archive:usies\n"
	writeAt(t, home+"/log/usies", "day 1\n", "2026-11-25T00:01")
	// adm is a file: archiving fails, the log is kept for next time.
	write(t, home+"/adm", "")
	if errs := run(t, home, conf, "2026-11-25T00:20"); len(errs) != 1 {
		t.Fatalf("errs = %v", errs)
	}
	if b, _ := os.ReadFile(home + "/log/usies.20261124.rotating"); string(b) != "day 1\n" {
		t.Fatalf("rotating = %q", b)
	}
	writeAt(t, home+"/log/usies", "day 2\n", "2026-11-26T00:01")
	if errs := run(t, home, conf, "2026-11-26T00:20"); len(errs) != 1 {
		t.Fatalf("still failing: errs = %v", errs)
	}
	os.Remove(home + "/adm")
	if errs := run(t, home, conf, "2026-11-26T00:21"); errs != nil {
		t.Fatal(errs)
	}
	for f, want := range map[string]string{
		"adm/2026/usies/usies-20261124.zst": "day 1\n",
		"adm/2026/usies/usies-20261125.zst": "day 2\n",
	} {
		if got := read(t, home+"/"+f, DefaultCompression); got != want {
			t.Errorf("%s = %q", f, got)
		}
	}
	if got := files(t, home); len(got) != 2 {
		t.Errorf("files = %v", got)
	}
}

func TestArchiveTaken(t *testing.T) {
	home := t.TempDir()
	writeAt(t, home+"/usies", "new\n", "2026-11-25T00:01")
	write(t, home+"/adm/2026/usies/usies-20261124.zst", "old")
	if errs := run(t, home, "usies daily archive:usies\n", "2026-11-25T00:20"); errs != nil {
		t.Fatal(errs)
	}
	if got := read(t, home+"/adm/2026/usies/usies-20261124-2.zst", DefaultCompression); got != "new\n" {
		t.Errorf("-2 = %q", got)
	}
}

func TestArchiveCopyTruncate(t *testing.T) {
	home := t.TempDir()
	conf := "log/held weekly archive copytruncate compress=gzip\n"
	writeAt(t, home+"/log/held", "held\n", "2026-11-28T23:00")
	if errs := run(t, home, conf, "2026-11-29T00:20"); errs != nil { // a Sunday
		t.Fatal(errs)
	}
	if got := read(t, home+"/adm/2026/held/held-20261128.gz", Compression{Algo: "gzip"}); got != "held\n" {
		t.Errorf("archive = %q", got)
	}
	if b, err := os.ReadFile(home + "/log/held"); err != nil || len(b) != 0 {
		t.Errorf("not truncated in place: %q %v", b, err)
	}
}

func TestSnapshot(t *testing.T) {
	home := t.TempDir()
	write(t, home+"/tmp/a", "A")
	write(t, home+"/tmp/sub/b", "B")
	os.Symlink("a", home+"/tmp/link")
	os.Chmod(home+"/tmp", 0700)
	conf := "tmp weekly:sat snapshot\n"
	if errs := run(t, home, conf, "2026-11-28T00:01"); errs != nil {
		t.Fatal(errs)
	}
	st, err := os.Stat(home + "/tmp")
	if err != nil || !st.IsDir() || st.Mode().Perm() != 0700 {
		t.Fatalf("tmp = %v %v", st, err)
	}
	if ents, _ := os.ReadDir(home + "/tmp"); len(ents) != 0 || exists(home+"/tmp.rotating") {
		t.Errorf("tmp not emptied: %v", ents)
	}
	tarball := read(t, home+"/adm/2026/tmp/tmp-20261128.tar.zst", DefaultCompression)
	tr := tar.NewReader(bytes.NewReader([]byte(tarball)))
	got := map[string]string{}
	for {
		h, err := tr.Next()
		if err == io.EOF {
			break
		} else if err != nil {
			t.Fatal(err)
		}
		b, _ := io.ReadAll(tr)
		got[h.Name] = string(b) + h.Linkname
	}
	want := map[string]string{"tmp/": "", "tmp/a": "A", "tmp/sub/": "",
		"tmp/sub/b": "B", "tmp/link": "a"}
	if len(got) != len(want) {
		t.Errorf("tar = %v", got)
	}
	for k, v := range want {
		if got[k] != v {
			t.Errorf("tar[%s] = %q, want %q (%v)", k, got[k], v, got)
		}
	}
	// Not its day: nothing.
	write(t, home+"/tmp/c", "C")
	run(t, home, conf, "2026-11-29T00:01")
	if !exists(home + "/tmp/c") {
		t.Errorf("packed on another day")
	}
}

func TestDryRun(t *testing.T) {
	home := t.TempDir()
	write(t, home+"/usies", "x\n")
	write(t, home+"/log/push", "y\n")
	write(t, home+"/tmp/a", "z")
	var out bytes.Buffer
	r := &Rotator{Home: home, State: State{}, Now: at("2026-11-28T00:01"), DryRun: true,
		Out: &out, Config: mustParse(t, "usies daily archive\nlog/push daily archive\ntmp weekly:sat snapshot\n")}
	if errs := r.Run(nil); errs != nil {
		t.Fatal(errs)
	}
	if len(r.State) != 0 {
		t.Errorf("state changed: %v", r.State)
	}
	want := []string{"log/push", "tmp/a", "usies"}
	if got := files(t, home); strings.Join(got, " ") != strings.Join(want, " ") {
		t.Errorf("files = %v", got)
	}
	if !strings.Contains(out.String(), "would") {
		t.Errorf("out = %s", out.String())
	}
}
