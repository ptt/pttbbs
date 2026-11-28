// Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only

package storage

import "testing"

func TestOverwritePostContent(t *testing.T) {
	e, _ := openTestEngine(t)
	p := mustCreatePost(t, e, "Test", "M.1700000000.A.001", "old body\n")

	got, err := e.OverwritePostContent(p.ID, "new body\n", false)
	if err != nil {
		t.Fatal(err)
	}
	if got.Modified != p.Modified {
		t.Errorf("modified moved without touch: %d -> %d", p.Modified, got.Modified)
	}
	after, err := e.GetPost(p.ID)
	if err != nil {
		t.Fatal(err)
	}
	if after.Content != "new body\n" || after.Modified != p.Modified {
		t.Errorf("after overwrite: content %q, modified %d", after.Content, after.Modified)
	}
	if revs, _ := e.ListPostHistory(p.ID); len(revs) != 0 {
		t.Errorf("overwrite left %d revisions", len(revs))
	}

	got, err = e.OverwritePostContent(p.ID, "newer body\n", true)
	if err != nil {
		t.Fatal(err)
	}
	after, _ = e.GetPost(p.ID)
	if got.Modified <= p.Modified || after.Modified != got.Modified {
		t.Errorf("touch: modified %d -> %d (stored %d)", p.Modified, got.Modified, after.Modified)
	}
	if after.Content != "newer body\n" {
		t.Errorf("content %q", after.Content)
	}
	if revs, _ := e.ListPostHistory(p.ID); len(revs) != 0 {
		t.Errorf("touch left %d revisions", len(revs))
	}
}
