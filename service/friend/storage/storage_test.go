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

func TestLoadFriendListsBig5Descriptions(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "friend_big5_storage_test_*")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	subID := "TestUser"
	overridesFile, err := GetHomeFile(tempDir, subID, "overrides")
	if err != nil {
		t.Fatalf("GetHomeFile failed: %v", err)
	}
	if err := os.MkdirAll(filepath.Dir(overridesFile), 0755); err != nil {
		t.Fatalf("MkdirAll failed: %v", err)
	}

	// Simulate %-13s<Big5 description> entries including 12-char userid and Big5 bytes with 0x40..0x7E trailing bytes
	var raw []byte
	raw = append(raw, []byte("alice        ")...)
	raw = append(raw, 0xa7, 0xbc, 0xb7, 0xf0, 0xb7, 0xf0, '\n') // Big5: 批踢踢
	raw = append(raw, []byte("user12345678 ")...)
	raw = append(raw, 0xc2, 0x85, 0xa4, 0x40, '\n') // Big5 bytes including 0xC2 0x85 and trailing 0x40 ('@')
	raw = append(raw, []byte("bob")...)
	raw = append(raw, 0xa6, 0x6e, 0xa4, 0xcd, '\n') // Legacy entry with Big5 directly after userid without space

	if err := os.WriteFile(overridesFile, raw, 0644); err != nil {
		t.Fatalf("WriteFile failed: %v", err)
	}

	friends, _, err := LoadFriendLists(tempDir, subID)
	if err != nil {
		t.Fatalf("LoadFriendLists failed: %v", err)
	}
	if len(friends) != 3 || friends[0] != "alice" || friends[1] != "user12345678" || friends[2] != "bob" {
		t.Fatalf("Unexpected parsed friends from Big5 file: %v", friends)
	}
}
