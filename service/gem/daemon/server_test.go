package daemon

import (
	"bytes"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"pttbbs/gem/client"
	"pttbbs/gem/storage"
)

func start(t *testing.T, lim storage.Limits) (*client.Client, string) {
	t.Helper()
	home := t.TempDir()
	st, err := storage.Open(storage.Options{Dir: filepath.Join(home, "db", "gem.db", "gem.pebble"), NoSync: true,
		Limits: func(string) storage.Limits { return lim }})
	if err != nil {
		t.Fatal(err)
	}
	// UNIX socket paths are short: not under the (long) test directory.
	sock := filepath.Join(os.TempDir(), fmt.Sprintf("gem-test-%d.sock", os.Getpid()))
	srv := New(Config{BBSHome: home, UnixSocket: sock, DataDir: filepath.Join(home, "db", "gem.db"),
		Limits: func(string) storage.Limits { return lim }}, st)
	if err := srv.Start(); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		srv.Stop()
		st.Close()
		os.Remove(sock)
	})
	return &client.Client{Socket: sock}, home
}

func hdr(name, title string) storage.Header {
	var h storage.Header
	h.SetName(name)
	h.SetTitle([]byte(title))
	return h
}

func code(err error) string {
	var ce *client.Error
	if errors.As(err, &ce) {
		return ce.Code()
	}
	return fmt.Sprint(err)
}

func TestProtocol(t *testing.T) {
	c, home := start(t, storage.Limits{Width: 3})
	if err := c.Ping(); err != nil {
		t.Fatal(err)
	}
	// A symbolic link comes only from an import (gem.svc makes none).
	in := filepath.Join(home, "in")
	l1 := hdr("L1", "link")
	if err := os.MkdirAll(in, 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(in, ".DIR"), l1[:], 0644); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink("/x/y", filepath.Join(in, "L1")); err != nil {
		t.Fatal(err)
	}
	if _, err := c.Simple("IMPORT Test " + in); err != nil {
		t.Fatal(err)
	}
	if _, err := c.List("Nope", nil, 0, -1); code(err) != storage.ErrNoRoot.Code {
		t.Fatalf("no board: %v", err)
	}
	rec, at, err := c.Add("dir", "Test", nil, -1, hdr("", "a dir"), nil)
	if err != nil || at != 1 || rec.Kind != storage.KindDir || !strings.HasPrefix(rec.Header.Name(), "D") {
		t.Fatalf("add dir: %+v %d %v", rec, at, err)
	}
	dname := rec.Header.Name()
	body := bytes.Repeat([]byte("x"), 1000)
	if _, _, err := c.Add("file", "Test", []string{dname}, -1, hdr("M.1.A", "file"), body); err != nil {
		t.Fatal(err)
	}
	if _, _, err := c.Add("link", "Test", nil, 0, hdr("L2", "link"), []byte("/x/y")); code(err) != "bbs_gem_bad_request" {
		t.Fatalf("add a link: %v", err)
	}
	l, err := c.List("Test", nil, 0, -1)
	if err != nil || l.Count != 2 || l.Records[0].Kind != storage.KindLink || l.Records[1].Nodes != 1 || l.Records[1].Bytes != 1000 {
		t.Fatalf("list: %+v %v", l, err)
	}
	if l, err := c.List("Test", nil, 1, 5); err != nil || len(l.Records) != 1 || l.Count != 2 {
		t.Fatalf("list from 1: %+v %v", l, err)
	}
	if got, err := c.Get("Test", []string{dname}, 0, "M.1.A"); err != nil || !bytes.Equal(got, body) {
		t.Fatalf("get: %v", err)
	}
	if _, _, target, err := c.Entry("Test", nil, 0, "L1"); err != nil || target != "/x/y" {
		t.Fatalf("entry: %q %v", target, err)
	}
	if _, err := c.Get("Test", nil, 0, "L1"); code(err) != storage.ErrNotFile.Code {
		t.Fatalf("get link: %v", err)
	}

	// Copy the directory twice (shared), then change one copy.
	h := hdr("Dcopy", "copy")
	if _, _, err := c.CopyOrMove("COPY", storage.Ref{Root: "Test", Index: 1, Name: dname}, "Test", nil, -1, &h); err != nil {
		t.Fatal(err)
	}
	if _, _, err := c.CopyOrMove("COPY", storage.Ref{Root: "Test", Index: 1, Name: dname}, "Test", nil, -1, nil); code(err) != storage.ErrExists.Code {
		t.Fatalf("copy over the same name: %v", err)
	}
	if _, err := c.Simple("PUT Test Dcopy 0 M.1.A 3", []byte("new")); err != nil {
		t.Fatal(err)
	}
	if got, _ := c.Get("Test", []string{dname}, 0, "M.1.A"); !bytes.Equal(got, body) {
		t.Fatal("original changed with its copy")
	}
	if _, _, err := c.Add("file", "Test", nil, -1, hdr("M.2.A", ""), nil); code(err) != storage.ErrQuota.Code {
		t.Fatalf("width quota: %v", err)
	}
	if w, err := c.Simple("STAT Test"); err != nil || w[0] != "2" || w[1] != "2" || w[2] != "1" {
		t.Fatalf("stat: %v %v", w, err)
	}
	if _, err := c.Simple("REORDER Test . 2 Dcopy 0"); err != nil {
		t.Fatal(err)
	}
	if _, err := c.Simple("REMOVE Test . 0 Dcopy"); err != nil {
		t.Fatal(err)
	}
	if _, err := c.Simple("SET_HEADER Test . -1 L1", func() []byte { h := hdr("", "new title"); return h[:] }()); err != nil {
		t.Fatal(err)
	}
	if rec, _, _, _ := c.Entry("Test", nil, -1, "L1"); string(rec.Header.Title()) != "new title" {
		t.Fatalf("title %q", rec.Header.Title())
	}

	// Export, import back as another board, compare.
	out := filepath.Join(home, "out")
	if _, err := c.Simple("EXPORT Test " + out); err != nil {
		t.Fatal(err)
	}
	if _, err := c.Simple("IMPORT Copy " + out); err != nil {
		t.Fatal(err)
	}
	a, _ := c.List("Test", nil, 0, -1)
	b, _ := c.List("Copy", nil, 0, -1)
	if fmt.Sprint(a) != fmt.Sprint(b) {
		t.Fatalf("roundtrip:\n%+v\n%+v", a, b)
	}
	r, err := c.Do("VERIFY")
	if err != nil {
		t.Fatal(err)
	}
	problems, _ := r.Lines(int(r.Int(6)))
	r.Close()
	if r.Int(0) != 1 {
		t.Fatalf("verify: %v %v", r.Words, problems)
	}
	// L1 (in Test and in Copy) points out of the gem areas: missing.
	if _, err := c.Simple("RESOLVE_LINKS x"); code(err) != "bbs_gem_bad_request" {
		t.Fatalf("relative old home: %v", err)
	}
	r, err = c.Do("RESOLVE_LINKS")
	if err != nil {
		t.Fatal(err)
	}
	problems, _ = r.Lines(int(r.Int(5)))
	r.Close()
	if strings.Join(r.Words, " ") != "0 0 0 2 0 2" || len(problems) != 2 {
		t.Fatalf("resolve links: %v %q", r.Words, problems)
	}
	if l, _ := c.List("Test", nil, 0, -1); l.Records[0].Kind != storage.KindMissing {
		t.Fatalf("L1 is a %s", l.Records[0].Kind)
	}
	if _, err := c.Simple("BACKUP snap"); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(home, "db", "gem.db", "backup", "snap")); err != nil {
		t.Fatal(err)
	}
	if _, err := c.Simple("FROB"); code(err) != "bbs_gem_bad_request" {
		t.Fatalf("unknown command: %v", err)
	}
}

func TestTrashProtocol(t *testing.T) {
	c, _ := start(t, storage.Limits{})
	if _, err := c.Simple("CREATE_ROOT Test"); err != nil {
		t.Fatal(err)
	}
	if _, _, err := c.Add("file", "Test", nil, -1, hdr("M.1.A", "first"), []byte("body")); err != nil {
		t.Fatal(err)
	}
	w, err := c.Simple("DELETE Test . 0 M.1.A alice")
	if err != nil {
		t.Fatal(err)
	}
	name := w[0]
	if l, _ := c.List("Test", nil, 0, -1); l.Count != 0 {
		t.Fatalf("still listed: %+v", l)
	}
	// The top of the trash: who deleted it in place of the owner.
	l, err := c.List("trash:board:Test", nil, 0, -1)
	if err != nil || l.Count != 1 || l.Records[0].Header.Name() != name || l.Records[0].Header.Owner() != "alice" {
		t.Fatalf("trash list: %+v %v", l, err)
	}
	if got, err := c.Get("trash:board:Test", nil, 0, name); err != nil || string(got) != "body" {
		t.Fatalf("get from the trash: %q %v", got, err)
	}
	r, err := c.Do("TRASH Test")
	if err != nil {
		t.Fatal(err)
	}
	lines, _ := r.Lines(int(r.Int(0)))
	r.Close()
	if len(lines) != 1 || !strings.HasPrefix(lines[0], name+" file ") || !strings.HasSuffix(lines[0], " alice 0 M.1.A .\tfirst") {
		t.Fatalf("trash: %q", lines)
	}
	if _, err := c.Simple("ADD file trash:board:Test . -1 0", func() []byte { h := hdr("", "x"); return h[:] }()); code(err) != "bbs_gem_bad_request" {
		t.Fatalf("add to the trash: %v", err)
	}
	if _, err := c.Simple("MOVE trash:board:Test . 0 " + name + " Test . -1 0"); code(err) != "bbs_gem_bad_request" {
		t.Fatalf("move out of the trash: %v", err)
	}
	if w, err := c.Simple("RESTORE Test " + name); err != nil || w[0] != "0" || w[1] != "M.1.A" {
		t.Fatalf("restore: %v %v", w, err)
	}
	if w, err := c.Simple("DELETE Test . 0 M.1.A bob"); err != nil {
		t.Fatal(err)
	} else if _, err := c.Simple("PURGE 30"); err != nil {
		t.Fatal(err)
	} else if _, err := c.Simple("PURGE Test " + w[0]); err != nil {
		t.Fatal(err)
	} else if _, err := c.Simple("RESTORE Test " + w[0]); code(err) != storage.ErrNotFound.Code {
		t.Fatalf("restore what was purged: %v", err)
	}
}
