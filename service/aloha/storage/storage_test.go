package storage

import (
	"os"
	"path/filepath"
	"testing"
)

func TestLoadAlohaTargets(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "aloha_storage_test_*")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	subID := "testuser"
	alohaedFile, err := GetHomeFile(tempDir, subID, "alohaed")
	if err != nil {
		t.Fatalf("GetHomeFile failed: %v", err)
	}

	if err := os.MkdirAll(filepath.Dir(alohaedFile), 0755); err != nil {
		t.Fatalf("MkdirAll failed: %v", err)
	}

	content := "target1\ntarget2\ntarget1\n"
	if err := os.WriteFile(alohaedFile, []byte(content), 0644); err != nil {
		t.Fatalf("WriteFile failed: %v", err)
	}

	targets, err := LoadAlohaTargets(tempDir, subID)
	if err != nil {
		t.Fatalf("LoadAlohaTargets failed: %v", err)
	}

	if len(targets) != 2 {
		t.Errorf("Expected 2 unique targets, got %d", len(targets))
	}
}
