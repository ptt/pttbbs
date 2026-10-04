package config

import (
	"os"
	"path/filepath"
	"testing"
)

func TestDefaultConfig(t *testing.T) {
	cfg := DefaultConfig("/v/bbshome")
	if cfg.BBSHome != "/v/bbshome" {
		t.Errorf("expected BBSHome /v/bbshome, got %s", cfg.BBSHome)
	}
	if len(cfg.Engines) != 1 {
		t.Fatalf("expected 1 default engine, got %d", len(cfg.Engines))
	}
	if cfg.Engines[0].DataDir != "/v/bbshome/db" {
		t.Errorf("expected default DataDir /v/bbshome/db, got %s", cfg.Engines[0].DataDir)
	}
	if cfg.Engines[0].CacheDir != "boards" {
		t.Errorf("expected default CacheDir boards, got %s", cfg.Engines[0].CacheDir)
	}
}

func TestLoadConfigFile(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "post_conf_test_*")
	if err != nil {
		t.Fatalf("create tmp dir failed: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	confContent := `
# Global test config
[global]
cache_size_mb = 512
flush_seconds = 5
min_reply_runes = 30
filter_encoding = utf-8
unix_socket = ~/run/custom.sock
log_path = $BBSHOME/log/custom.log

[engines]
engine = /disk1/bbs_db
engine = /disk2/bbs_db
engine = ~/disk3_db

[caches]
cache = /fast_nvme/cache1
cache = /fast_nvme/cache2

[pins]
Gossiping = 0
Marginalman = 1
Baseball = 2
`

	confPath := filepath.Join(tmpDir, "post.svc.conf")
	if err := os.WriteFile(confPath, []byte(confContent), 0644); err != nil {
		t.Fatalf("write conf failed: %v", err)
	}

	bbshome := "/v/bbshome"
	cfg, err := LoadConfigFile(confPath, bbshome)
	if err != nil {
		t.Fatalf("LoadConfigFile failed: %v", err)
	}

	if cfg.CacheSizeMB != 512 {
		t.Errorf("expected CacheSizeMB 512, got %d", cfg.CacheSizeMB)
	}
	if cfg.FlushSeconds != 5 {
		t.Errorf("expected FlushSeconds 5, got %d", cfg.FlushSeconds)
	}
	if cfg.MinReplyRunes != 30 {
		t.Errorf("expected MinReplyRunes 30, got %d", cfg.MinReplyRunes)
	}
	if cfg.FilterEncoding != "utf-8" || cfg.IsBig5() {
		t.Errorf("expected utf-8, got %s", cfg.FilterEncoding)
	}
	if cfg.UnixSocket != "/v/bbshome/run/custom.sock" {
		t.Errorf("expected expanded socket /v/bbshome/run/custom.sock, got %s", cfg.UnixSocket)
	}
	if cfg.LogPath != "/v/bbshome/log/custom.log" {
		t.Errorf("expected expanded log /v/bbshome/log/custom.log, got %s", cfg.LogPath)
	}

	if len(cfg.Engines) != 3 {
		t.Fatalf("expected 3 engines, got %d", len(cfg.Engines))
	}

	if cfg.Engines[0].DataDir != "/disk1/bbs_db" || cfg.Engines[0].CacheDir != "/fast_nvme/cache1" {
		t.Errorf("engine 0 mismatch: %+v", cfg.Engines[0])
	}
	if cfg.Engines[1].DataDir != "/disk2/bbs_db" || cfg.Engines[1].CacheDir != "/fast_nvme/cache2" {
		t.Errorf("engine 1 mismatch: %+v", cfg.Engines[1])
	}
	if cfg.Engines[2].DataDir != "/v/bbshome/disk3_db" || cfg.Engines[2].CacheDir != "boards" {
		t.Errorf("engine 2 mismatch: %+v", cfg.Engines[2])
	}

	if len(cfg.Pins) != 3 {
		t.Fatalf("expected 3 pins, got %d", len(cfg.Pins))
	}
	if cfg.Pins["Gossiping"] != 0 || cfg.Pins["Marginalman"] != 1 || cfg.Pins["Baseball"] != 2 {
		t.Errorf("pins mismatch: %+v", cfg.Pins)
	}
}
