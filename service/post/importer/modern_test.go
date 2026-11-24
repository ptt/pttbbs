package importer

import (
	"strings"
	"testing"
	"time"
)

// Articles post.svc rendered before (its floors) are imported floor by floor,
// so new comments go on from the last floor.
func TestParseModernFloors(t *testing.T) {
	article := "作者: op (op) 看板: TestBoard\n標題: t\n時間: Sun Oct  4 21:00:00 2026\n\nbody\n\n--\n" +
		"※ 發信站: 批踢踢實業坊(ptt.cc), 來自: 1.2.3.4\n" +
		"※ 文章網址: https://www.ptt.cc/bbs/TestBoard/M.1791118800.A.001.html\n" +
		"|alice|first|10/04/2026 21:27|1\n" +
		"|bob|has a | inside|1.2.3.4 10/04/2026 21:30|2\n" +
		"| second line\n" +
		"| third line\n" +
		"|carol!|留言已刪除|10/04/2026 21:31|3\n" +
		"|dave!|留言已隱藏 (spam)|10/04/2026 21:32|4\n" +
		"|alice|edited|10/04/2026 21:33|~5\n" +
		"|erin!|留言已移除 (ad)|10/04/2026 21:33|6\n" +
		"|alice|same author soon after|10/04/2026 21:34|7\n"
	base := time.Date(2026, 10, 4, 21, 0, 0, 0, time.Local).Unix()
	parsed, err := ParseArticleText([]byte(article), "op", base, "TestBoard", "M.1791118800.A.001", false)
	if err != nil {
		t.Fatal(err)
	}
	cs := parsed.Comments
	if len(cs) != 7 {
		for i, c := range cs {
			t.Logf("%d: %+v", i, c)
		}
		t.Fatalf("got %d floors, want 7", len(cs))
	}
	if cs[0].CreatorName != "alice" || cs[0].Content != "first" {
		t.Errorf("floor 1: %+v", cs[0])
	}
	if want := time.Date(2026, 10, 4, 21, 27, 0, 0, time.Local).Unix(); cs[0].CTime != want {
		t.Errorf("floor 1 time %d, want %d", cs[0].CTime, want)
	}
	if cs[1].Content != "has a | inside\nsecond line\nthird line" || cs[1].IP != "1.2.3.4" {
		t.Errorf("floor 2: %+v", cs[1])
	}
	if !cs[2].Deleted || cs[2].Content != "" {
		t.Errorf("floor 3: %+v", cs[2])
	}
	if !cs[3].Removed || cs[3].Reason != "spam" {
		t.Errorf("floor 4: %+v", cs[3])
	}
	if !cs[4].Edited || cs[4].Content != "edited" {
		t.Errorf("floor 5: %+v", cs[4])
	}
	if !cs[5].Removed || cs[5].Reason != "ad" {
		t.Errorf("floor 6: %+v", cs[5])
	}
	if cs[6].Content != "same author soon after" {
		t.Errorf("floor 7 merged or lost: %+v", cs[6])
	}
	for _, l := range []string{"|alice|", "| second line"} {
		if strings.Contains(parsed.BodyContent, l) {
			t.Errorf("body still has %q:\n%s", l, parsed.BodyContent)
		}
	}

	comments, _, up, down := ConvertParsedComments(cs, nil)
	if up != 0 || down != 0 {
		t.Errorf("floors of post.svc carry no votes: up %d down %d", up, down)
	}
	if !comments[2].Deleted || !comments[3].Removed || comments[3].Reason != "spam" || comments[4].ModifiedAt == 0 {
		t.Errorf("converted: %+v %+v %+v", comments[2], comments[3], comments[4])
	}
}

// The tombstones of mbbsd (".deleted") have lost the name of their post:
// they are not posts to import, while those sync-dir writes are.
func TestSafeDeletedNames(t *testing.T) {
	for fn, want := range map[string]string{
		".d1791120379.A.546": "M.1791120379.A.546",
		".deleted":           "",
		".dummy":             "",
	} {
		got, ok := IsSafeDeletedFilename(fn)
		if (want != "") != ok || (ok && got != want) {
			t.Errorf("IsSafeDeletedFilename(%q) = %q, %v; want %q", fn, got, ok, want)
		}
	}
}
