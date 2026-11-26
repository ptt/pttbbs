package storage

import (
	"testing"
	"time"

	"pttbbs/nest/model"
)

// TestCommentOPMark checks that every line of a comment of the OP opens with
// "|*", that further lines are as wide as the userid, so that each line can
// be drawn alone; and that anonymous posts never mark them.
func TestCommentOPMark(t *testing.T) {
	at := time.Date(2026, 11, 27, 1, 0, 0, 0, time.Local).Unix()
	post := &model.Post{CreatorName: "Alice", CreatorID: 7}
	anon := &model.Post{CreatorName: "Anonymous.", CreatorID: 7, Filemode: model.FileAnonymous}
	legacy := &model.Post{CreatorName: "alice"} // creator_id unknown: by name

	op := &model.Comment{Sequence: 1, CreatorName: "alice", CreatorID: 7, CreatedAt: at, Content: "one\ntwo\nthree"}
	other := &model.Comment{Sequence: 2, CreatorName: "bob", CreatorID: 8, CreatedAt: at, Content: "one\ntwo"}
	gone := &model.Comment{Sequence: 3, CreatorName: "alice", CreatorID: 7, CreatedAt: at,
		Deletion: model.Deletion{Deleted: true}}

	for _, tc := range []struct {
		name string
		p    *model.Post
		c    *model.Comment
		want string
	}{
		{"op", post, op, "\x1e*alice|1|11/27/2026 01:00||one\n\x1e*     |two\n\x1e*     |three\n"},
		{"other", post, other, "\x1ebob|2|11/27/2026 01:00||one\n\x1e   |two\n"},
		{"op deleted", post, gone, "\x1e*-alice|3|11/27/2026 01:00||留言已刪除\n"},
		{"anonymous", anon, op, "\x1ealice|1|11/27/2026 01:00||one\n\x1e     |two\n\x1e     |three\n"},
		{"legacy by name", legacy, op, "\x1e*alice|1|11/27/2026 01:00||one\n\x1e*     |two\n\x1e*     |three\n"},
		{"no post", nil, op, "\x1ealice|1|11/27/2026 01:00||one\n\x1e     |two\n\x1e     |three\n"},
	} {
		if got := FormatCommentBlock(tc.p, tc.c); got != tc.want {
			t.Errorf("%s: FormatCommentBlock = %q, want %q", tc.name, got, tc.want)
		}
	}

	if got, want := FormatMergedCommentLines(post, op, "four\nfive"), "\x1e*     |four\n\x1e*     |five\n"; got != want {
		t.Errorf("merged by the OP = %q, want %q", got, want)
	}
	if got, want := FormatMergedCommentLines(post, other, "four"), "\x1e   |four\n"; got != want {
		t.Errorf("merged by others = %q, want %q", got, want)
	}
}
