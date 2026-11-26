package storage

import (
	"strings"
	"testing"

	"pttbbs/nest/model"
)

// TestTakeDownKeepsBodies checks that taking a post or comment down keeps
// its text (Lemmy's deleted/removed plus modlog): the notes are only
// rendered, and putting it back shows the text again.
func TestTakeDownKeepsBodies(t *testing.T) {
	e, _ := openTestEngine(t)
	p := mustCreatePost(t, e, "test", "M.1700000000.A.ABC", "the body\n")
	if _, err := e.AddComment(&model.Comment{PostID: p.ID, CreatorName: "bob", Content: "a comment", CreatedAt: 1700000100}); err != nil {
		t.Fatal(err)
	}

	if _, err := e.RemoveComment(p.ID, 1, "boss", 0, "spam"); err != nil {
		t.Fatal(err)
	}
	if _, err := e.RemovePost(p.ID, "boss", 0, "off topic"); err != nil {
		t.Fatal(err)
	}
	got, err := e.GetPost(p.ID)
	if err != nil {
		t.Fatal(err)
	}
	if got.Content != "the body\n" || !got.Removed || got.Reason != "off topic" || got.ModPersonName != "boss" {
		t.Fatalf("removed post: content %q removed %v reason %q by %q", got.Content, got.Removed, got.Reason, got.ModPersonName)
	}
	cs, err := e.GetComments(p.ID, 1, 10)
	if err != nil || len(cs) != 1 {
		t.Fatalf("comments: %v, %v", cs, err)
	}
	if cs[0].Content != "a comment" || !cs[0].Removed || cs[0].Reason != "spam" {
		t.Fatalf("removed comment: %+v", cs[0])
	}
	if out, _ := e.RenderFullPostText(p.ID, false); strings.Contains(string(out), "the body") {
		t.Fatalf("a removed post renders its body:\n%s", out)
	}

	if err := e.UndeletePost(p.ID); err != nil {
		t.Fatal(err)
	}
	if err := e.UndeleteComment(p.ID, 1); err != nil {
		t.Fatal(err)
	}
	out, _ := e.RenderFullPostText(p.ID, false)
	if !strings.HasPrefix(string(out), "the body\n") || !strings.Contains(string(out), "a comment") || strings.Contains(string(out), "spam") {
		t.Fatalf("put back:\n%s", out)
	}
}
