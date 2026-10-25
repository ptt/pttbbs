package storage

import (
	"os"
	"path/filepath"
	"testing"

	"pttbbs/stat/model"
)

func TestDailyStatsOperations(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "stat_test")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	dbPath := filepath.Join(tempDir, "stat.db")
	store := NewStorage(dbPath)

	// Initial get for non-existent user
	stats := store.Get("testuser")
	if stats.PostCount != 0 || stats.MailCount != 0 {
		t.Errorf("expected 0 stats, got post=%d mail=%d", stats.PostCount, stats.MailCount)
	}

	// Increment post
	postCnt, err := store.IncPost("testuser")
	if err != nil {
		t.Fatalf("IncPost failed: %v", err)
	}
	if postCnt != 1 {
		t.Errorf("expected postCnt=1, got %d", postCnt)
	}

	postCnt, err = store.IncPost("testuser")
	if err != nil {
		t.Fatalf("IncPost 2 failed: %v", err)
	}
	if postCnt != 2 {
		t.Errorf("expected postCnt=2, got %d", postCnt)
	}

	// Add mail
	mailCnt, err := store.AddMail("testuser", 3)
	if err != nil {
		t.Fatalf("AddMail failed: %v", err)
	}
	if mailCnt != 3 {
		t.Errorf("expected mailCnt=3, got %d", mailCnt)
	}

	// Inc song
	songCnt, err := store.IncSong("testuser")
	if err != nil {
		t.Fatalf("IncSong failed: %v", err)
	}
	if songCnt != 1 {
		t.Errorf("expected songCnt=1, got %d", songCnt)
	}

	// Query by UserID (case-insensitive)
	stats = store.Get("TestUser")
	if stats.PostCount != 2 || stats.MailCount != 3 || stats.SongCount != 1 {
		t.Errorf("expected post=2 mail=3, got post=%d mail=%d", stats.PostCount, stats.MailCount)
	}

	// Save to file
	n, err := store.Save()
	if err != nil {
		t.Fatalf("Save failed: %v", err)
	}
	if n != 1 {
		t.Errorf("expected 1 entry saved, got %d", n)
	}

	// Reload in fresh storage
	store2 := NewStorage(dbPath)
	loaded, err := store2.Load()
	if err != nil {
		t.Fatalf("Load failed: %v", err)
	}
	if loaded != 1 {
		t.Errorf("expected 1 entry loaded, got %d", loaded)
	}

	stats2 := store2.Get("testuser")
	if stats2.PostCount != 2 || stats2.MailCount != 3 || stats2.SongCount != 1 {
		t.Errorf("reloaded stats mismatch: post=%d mail=%d", stats2.PostCount, stats2.MailCount)
	}

	// Test Prune
	pruned := store2.Prune(model.TodayDate() + 1)
	if pruned != 1 {
		t.Errorf("expected 1 entry pruned, got %d", pruned)
	}
	if store2.CountTracked() != 0 {
		t.Errorf("expected 0 tracked entries after prune, got %d", store2.CountTracked())
	}
}
