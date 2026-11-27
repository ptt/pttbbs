package storage

import (
	"strings"
	"testing"

	"pttbbs/nest/model"
)

// Authors that are not userids (imported from .DIR owners) keep the comment
// line readable: no field separator, mark or empty name, and further lines
// as wide as the name is on a terminal.
func TestCommentAuthorField(t *testing.T) {
	for _, tc := range []struct{ in, want string }{
		{"userA", "userA"},
		{"[系統安全局]", "[系統安全局]"},
		{"  恰恰好. ", "恰恰好."},
		{"a|b", "a?b"},
		{"弋陽", "?陽"}, // Big5 A47C: its trail byte is '|'
		{"", "?"},
		{"-.-", "_.-"},
		{"*star", "_star"},
		{"!bang", "_bang"},
	} {
		if got := commentAuthor(tc.in); got != tc.want {
			t.Errorf("commentAuthor(%q) = %q, want %q", tc.in, got, tc.want)
		}
	}

	p := &model.Post{CreatorName: "op"}
	c := &model.Comment{CreatorName: "[系統安全局]", Sequence: 3, CreatedAt: 1728000000, Content: "一\n二"}
	lines := strings.Split(strings.TrimSuffix(FormatCommentBlock(p, c), "\n"), "\n")
	if len(lines) != 2 || !strings.HasPrefix(lines[0], "\x1e[系統安全局]|3|") {
		t.Fatalf("lines = %q", lines)
	}
	if want := "\x1e" + strings.Repeat(" ", 12) + "|二"; lines[1] != want {
		t.Errorf("further line %q, want %q", lines[1], want)
	}
}
