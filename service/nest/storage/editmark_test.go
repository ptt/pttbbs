package storage

import (
	"strings"
	"testing"

	"pttbbs/nest/model"
)

// TestEditMark checks that the edit line of a post is rendered after its
// body and before its comments, once however often it is set, and kept
// by an edit that does not set it.
func TestEditMark(t *testing.T) {
	e, _ := openTestEngine(t)
	p := mustCreatePost(t, e, "test", "M.1700000000.A.ABC", "body\n--\nsig\n")
	if _, err := e.AddComment(&model.Comment{PostID: p.ID, CreatorName: "bob", Content: "hi", CreatedAt: 1700000100}); err != nil {
		t.Fatal(err)
	}
	render := func() string {
		b, err := e.RenderFullPostText(p.ID, false)
		if err != nil {
			t.Fatal(err)
		}
		return string(b)
	}
	if strings.Contains(render(), "※ 編輯") {
		t.Fatalf("a post never edited has an edit line:\n%s", render())
	}
	for _, mark := range []string{"※ 編輯: alice (1.2.3.4), 11/27/2026 01:00:00", "※ 編輯: alice (1.2.3.4), 11/27/2026 02:00:00"} {
		if err := e.SetPostEditMark(p.ID, mark); err != nil {
			t.Fatal(err)
		}
	}
	out := render()
	if strings.Count(out, "※ 編輯") != 1 || !strings.Contains(out, "sig\n※ 編輯: alice (1.2.3.4), 11/27/2026 02:00:00\n|bob|hi|") {
		t.Fatalf("rendered:\n%s", out)
	}
	if _, _, err := e.UpdatePost(p.ID, "", "body2\n", "alice", 0, 0); err != nil {
		t.Fatal(err)
	}
	if out := render(); !strings.HasPrefix(out, "body2\n※ 編輯: alice (1.2.3.4), 11/27/2026 02:00:00\n") {
		t.Fatalf("after an edit without a mark:\n%s", out)
	}
	got, _ := e.GetPost(p.ID)
	if strings.Contains(got.Content, "編輯") {
		t.Fatalf("the edit line went into the body: %q", got.Content)
	}
}
