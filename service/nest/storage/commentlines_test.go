package storage

import (
	"errors"
	"path/filepath"
	"testing"

	"pttbbs/nest/model"
)

// TestMaxPostCommentLines checks the per-post comment line limit (each line
// of a multi-line or merged comment counts, a taken-down one counts as one)
// and that the count is rebuilt from the comments when lost.
func TestMaxPostCommentLines(t *testing.T) {
	dir := t.TempDir()
	e, err := OpenEngine(Config{
		BBSHome:             filepath.Join(dir, "bbshome"),
		DataDir:             filepath.Join(dir, "data"),
		CacheSizeMB:         16,
		FlushSeconds:        1,
		FilterEncoding:      "utf-8",
		MaxPostCommentLines: 6,
	})
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	p := mustCreatePost(t, e, "test", "M.1700000000.A.ABC", "body")
	at := int64(1700000000)
	add := func(author, content string) error {
		at += 1000 // apart, so that they are not merged
		_, err := e.AddComment(&model.Comment{PostID: p.ID, CreatorName: author, Content: content, CreatedAt: at})
		return err
	}
	if err := add("a", "one\ntwo\nthree"); err != nil { // 3 lines
		t.Fatal(err)
	}
	if err := add("b", "four"); err != nil { // 4
		t.Fatal(err)
	}
	// Merged into b's floor (same author, within 3 minutes): 5.
	if _, err := e.AddComment(&model.Comment{PostID: p.ID, CreatorName: "b", Content: "five", CreatedAt: at + 10}); err != nil {
		t.Fatal(err)
	}
	if err := add("c", "six\nseven"); !errors.Is(err, ErrPostTooLarge) {
		t.Fatalf("7 lines: err = %v, want ErrPostTooLarge", err)
	}
	if err := add("c", "six"); err != nil {
		t.Fatalf("6 lines: %v", err)
	}
	if err := add("d", "seven"); !errors.Is(err, ErrPostTooLarge) {
		t.Fatalf("7 lines: err = %v, want ErrPostTooLarge", err)
	}

	// Taking down the comment of 3 lines leaves one: 4 lines, counted
	// again from the comments.
	if _, err := e.takeDownComment(p.ID, 1, "a", 0, "", false); err != nil {
		t.Fatal(err)
	}
	if err := add("d", "five\nsix"); err != nil {
		t.Fatalf("6 lines after taking down: %v", err)
	}
	if err := add("e", "seven"); !errors.Is(err, ErrPostTooLarge) {
		t.Fatalf("7 lines after taking down: err = %v, want ErrPostTooLarge", err)
	}
	e.forgetCommentLines()
	if err := add("e", "seven"); !errors.Is(err, ErrPostTooLarge) {
		t.Fatalf("7 lines counted again: err = %v, want ErrPostTooLarge", err)
	}
}
