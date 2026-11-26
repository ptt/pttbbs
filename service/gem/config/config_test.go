package config

import (
	"os"
	"path/filepath"
	"testing"

	"pttbbs/gem/storage"
)

func TestLoad(t *testing.T) {
	dir := t.TempDir()
	p := filepath.Join(dir, "gem.svc.conf")
	os.WriteFile(p, []byte(`
[global]
cache_size_mb = 64   # comment
data_dir = ~/db/elsewhere

[board C_Chat]
nodes = 0
bytes = 20G

[quota]
nodes = 50000
bytes = 512M
`), 0644)
	c, err := Load(p, "/home/bbs")
	if err != nil {
		t.Fatal(err)
	}
	if c.CacheMB != 64 || c.DataDir != "/home/bbs/db/elsewhere" || c.UnixSocket != "/home/bbs/run/gem.svc.sock" {
		t.Fatalf("%+v", c)
	}
	if c.Quota.Nodes != 50000 || c.Quota.Bytes != 512<<20 || c.Quota.Width != storage.DefaultLimits.Width {
		t.Fatalf("quota %+v", c.Quota)
	}
	// Boards start from [quota] even when it comes later.
	l := c.LimitsFor("board:c_chat")
	if l.Nodes != 0 || l.Bytes != 20<<30 || l.Dirs != storage.DefaultLimits.Dirs {
		t.Fatalf("C_Chat %+v", l)
	}
	if c.LimitsFor("board:Other") != c.Quota || c.LimitsFor("user:x") != c.UserQuota || c.LimitsFor("site") != (storage.Limits{}) {
		t.Fatal("other boards")
	}
	os.WriteFile(p, []byte("[quota]\nfoo = 1\n"), 0644)
	if _, err := Load(p, "/home/bbs"); err == nil {
		t.Fatal("unknown key accepted")
	}
}
