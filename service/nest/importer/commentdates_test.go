package importer

import (
	"os"
	"path/filepath"
	"strconv"
	"testing"
	"time"

	"pttbbs/big5uao"
)

func localTime(y int, m time.Month, d, hh, mm int) int64 {
	return time.Date(y, m, d, hh, mm, 0, 0, time.Local).Unix()
}

func TestRollDate(t *testing.T) {
	now := localTime(2026, 3, 10, 12, 0)
	for _, tc := range []struct {
		name   string
		date   string
		lastTs int64
		want   int64
	}{
		{"later the same year", "05/12 10:00", localTime(2024, 5, 11, 5, 40), localTime(2024, 5, 12, 10, 0)},
		{"next year", "01/01 09:00", localTime(2023, 12, 31, 10, 0), localTime(2024, 1, 1, 9, 0)},
		{"a day earlier: out of order", "05/10 20:00", localTime(2024, 5, 11, 5, 40), localTime(2024, 5, 10, 20, 0)},
		{"weeks earlier: out of order", "04/20", localTime(2024, 5, 11, 5, 40), localTime(2024, 4, 20, 0, 0)},
		{"months earlier: next year", "03/01 08:00", localTime(2024, 5, 11, 5, 40), localTime(2025, 3, 1, 8, 0)},
		{"just before new year", "12/31 23:59", localTime(2024, 1, 1, 0, 0), localTime(2023, 12, 31, 23, 59)},
		{"never in the future", "03/20 10:00", localTime(2026, 3, 1, 0, 0), localTime(2025, 3, 20, 10, 0)},
		{"four weeks earlier: out of order", "02/01", localTime(2026, 3, 1, 0, 0), localTime(2026, 2, 1, 0, 0)},
	} {
		got, ok := RollDate(tc.date, tc.lastTs, now)
		if !ok || got != tc.want {
			t.Errorf("%s: RollDate(%q, %v) = %v, want %v", tc.name, tc.date, time.Unix(tc.lastTs, 0), time.Unix(got, 0), time.Unix(tc.want, 0))
		}
	}
	if _, ok := RollDate("", 1, now); ok {
		t.Errorf("no date: ok")
	}
}

func TestCommentBase(t *testing.T) {
	post := localTime(2004, 5, 11, 5, 40)
	for _, tc := range []struct {
		date  string
		ctime int64
		want  int64
	}{
		{" 5/10", post, localTime(2004, 5, 10, 0, 0)},
		{"05/11", post, post},
		{" 5/12", post, localTime(2003, 5, 12, 0, 0)},
		{"12/30", localTime(2005, 1, 2, 3, 0), localTime(2004, 12, 30, 0, 0)},
		{"", post, post},
		{"\xa4\xa4/x", post, post},
		{"13/01", post, post},
	} {
		if got := commentBase(tc.date, tc.ctime); got != tc.want {
			t.Errorf("commentBase(%q, %v) = %v, want %v", tc.date, time.Unix(tc.ctime, 0), time.Unix(got, 0), time.Unix(tc.want, 0))
		}
	}
}

// Comments dated a little before the one before them (or the post) stay in
// their year, and none is dated after now.
func TestCommentDatesOutOfOrder(t *testing.T) {
	now := time.Now().Unix()
	post := minuteOf(now) - 200*86400
	article := "作者: userA (A) 看板: T\n標題: t\n\n本文\n--\n※ 發信站: 批踢踢實業坊(ptt.cc)\n" +
		pushLine("推", "userB", "一", post+3600) +
		pushLine("推", "userC", "二", post+2*3600) +
		pushLine("推", "userD", "早一天", post+2*3600-86400) +
		pushLine("推", "userE", "三", post+3*86400)
	parsed, err := ParseArticleText([]byte(article), "userA", post, "T", "M.1.A.001", false)
	if err != nil {
		t.Fatal(err)
	}
	if len(parsed.Comments) != 4 {
		t.Fatalf("comments %d", len(parsed.Comments))
	}
	want := []int64{post + 3600, post + 2*3600, post + 2*3600 + 1, post + 3*86400}
	for i, c := range parsed.Comments {
		if c.CTime != want[i] {
			t.Errorf("comment %d at %v, want %v", i, time.Unix(c.CTime, 0), time.Unix(want[i], 0))
		}
	}

	// Dated later than now: a year earlier.
	recent := minuteOf(now) - 10*86400
	article = "本文\n--\n" + pushLine("推", "userB", "未來", now+5*86400)
	parsed, err = ParseArticleText([]byte(article), "userA", recent-300*86400, "T", "M.2.A.001", false)
	if err != nil {
		t.Fatal(err)
	}
	if len(parsed.Comments) != 1 || parsed.Comments[0].CTime > now {
		t.Errorf("comment in the future: %+v", parsed.Comments[0])
	}
}

// Old mbbsd gave an edited post a new file name, later than its comments;
// the comments are dated from the day in its .DIR record, not a year later.
func TestCommentsBeforeRenamedFile(t *testing.T) {
	tempDir := t.TempDir()
	boardDir := filepath.Join(tempDir, "boards", "T", "TennisTeam")
	if err := os.MkdirAll(boardDir, 0755); err != nil {
		t.Fatal(err)
	}
	stamp := localTime(2004, 5, 11, 5, 40)
	name := "M." + strconv.FormatInt(stamp, 10) + ".A.B40"
	rec := dirFixture{filename: name, owner: "userA", title: "t", modified: uint32(stamp)}.record()
	copy(rec[fhdrOffDate:], " 5/10")
	if err := os.WriteFile(filepath.Join(boardDir, ".DIR"), rec, 0644); err != nil {
		t.Fatal(err)
	}
	article := big5uao.Encode("作者: userA (A) 看板: TennisTeam\n標題: t\n\n本文\n--\n※ 發信站: 批踢踢實業坊(ptt.cc)\n" +
		pushLine("推", "userB", "一", localTime(2004, 5, 10, 21, 0)) +
		pushLine("推", "userC", "二", localTime(2004, 5, 11, 1, 0)))
	if err := os.WriteFile(filepath.Join(boardDir, name), article, 0644); err != nil {
		t.Fatal(err)
	}
	engine := openImportEngine(t, tempDir)
	opts := ImportBoardOptions{BBSHome: tempDir, Board: "TennisTeam", IsBig5: true}
	if _, err := ImportPost(engine, opts, name); err != nil {
		t.Fatal(err)
	}
	comments, err := engine.GetCommentsByCommunityFileFiltered("TennisTeam", name, 1, 100, "", 0)
	if err != nil || len(comments) != 2 {
		t.Fatalf("comments = %d, %v", len(comments), err)
	}
	for i, want := range []int64{localTime(2004, 5, 10, 21, 0), localTime(2004, 5, 11, 1, 0)} {
		if comments[i].CreatedAt != want {
			t.Errorf("comment %d at %v, want %v", i, time.Unix(comments[i].CreatedAt, 0), time.Unix(want, 0))
		}
	}
}
