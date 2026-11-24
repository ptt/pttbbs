package model

import "testing"

// Delete is a deletion by the creator, else a removal; Remove is always a
// removal (Lemmy's RemovePost and RemoveComment), even by the creator.
func TestDeleteAndRemove(t *testing.T) {
	var d Deletion
	d.Delete("alice", 1, "alice", 1, "ignored", 100)
	if !d.Deleted || d.Removed || d.Reason != "" || d.DeletedAt != 100 {
		t.Fatalf("Delete by its creator: %+v", d)
	}
	d.Delete("alice", 1, "boss", 2, "spam", 200)
	if d.Deleted || !d.Removed || d.ModPersonName != "boss" || d.ModPersonID != 2 || d.Reason != "spam" {
		t.Fatalf("Delete by another person: %+v", d)
	}
	d.Remove("alice", 1, "own rules", 300)
	if d.Deleted || !d.Removed || d.ModPersonName != "alice" || d.Reason != "own rules" || d.DeletedAt != 300 {
		t.Fatalf("Remove by its creator: %+v", d)
	}
	d.Restore()
	if d.DeletedOrRemoved() {
		t.Fatalf("Restore: %+v", d)
	}
	if !IsCreator("alice", 0, "alice", 5) || IsCreator("alice", 1, "alice", 5) || IsCreator("alice", 0, "bob", 0) {
		t.Fatalf("IsCreator: by ID when both are known, else by name")
	}
	// BBS user IDs ignore case.
	if !IsCreator("OldUser", 0, "olduser", 0) || !IsCreator("alice", 0, "ALICE", 7) {
		t.Fatalf("IsCreator: names ignore case")
	}
}
