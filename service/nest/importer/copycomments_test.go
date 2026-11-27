package importer

import (
	"strings"
	"testing"
	"time"
)

func pushLine(tag, user, text string, ts int64) string {
	return tag + " " + user + ":" + text + "                    " + time.Unix(ts, 0).Format("01/02 15:04") + "\n"
}

// The comments of the original copied into a copy (between the original's
// origin lines and "※ 轉錄者:") are quoted text, not comments of the copy.
func TestCopyKeepsOriginalComments(t *testing.T) {
	ctime := minuteOf(time.Now().Unix()) - 100*86400
	original := originalArticle("※ 文章網址: https://www.ptt.cc/bbs/Src/M.1.A.001.html\n" +
		pushLine("推", "userX", "原文推文", ctime-3600) +
		pushLine("噓", "userY", "原文噓文", ctime-1800))
	text := copyArticle("※ [本文轉錄自 Src 看板 #19bWBIgy ]", original,
		pushLine("推", "userZ", "複本推文", ctime+600)+
			cplogLine("userB", "Later", ctime+700))
	parsed, err := ParseArticleText([]byte(text), "userB", ctime, "Dst", "M.1700000000.A.001", false)
	if err != nil {
		t.Fatal(err)
	}
	if len(parsed.Comments) != 1 || parsed.Comments[0].CreatorName != "userZ" || parsed.Comments[0].CTime != ctime+600 {
		t.Fatalf("comments %+v, want only userZ's", parsed.Comments)
	}
	for _, s := range []string{"原文推文", "原文噓文", "※ 轉錄者:"} {
		if !strings.Contains(parsed.BodyContent, s) {
			t.Errorf("body lost %q", s)
		}
	}
	if strings.Index(parsed.BodyContent, "原文噓文") > strings.Index(parsed.BodyContent, "※ 轉錄者:") {
		t.Errorf("quoted comments out of order:\n%s", parsed.BodyContent)
	}
	if len(parsed.Crossposts) != 1 || parsed.Crossposts[0].TargetBoard != "Later" {
		t.Errorf("crossposts %+v", parsed.Crossposts)
	}

	// Older copies end with "--" and their origin: the original's comments
	// are before the last "--" already.
	old := "作者: userB (B) 看板: Dst\n標題: [轉錄] t\n\n※ [本文轉錄自 Src 看板]\n\n" + original +
		"\n--\n※ 發信站: 批踢踢實業坊(ptt.cc)\n◆ From: 1.2.3.4\n" + pushLine("推", "userZ", "複本推文", ctime+600)
	parsed, err = ParseArticleText([]byte(old), "userB", ctime, "Dst", "M.1700000000.A.002", false)
	if err != nil {
		t.Fatal(err)
	}
	if len(parsed.Comments) != 1 || parsed.Comments[0].CreatorName != "userZ" {
		t.Errorf("old copy: comments %+v, want only userZ's", parsed.Comments)
	}

	// Not a copy: "※ 轉錄者:" changes nothing.
	plain := originalArticle("※ 轉錄者: userB (1.2.3.4), 01/01/2024 00:00:00\n" + pushLine("推", "userX", "推文", ctime+60))
	parsed, err = ParseArticleText([]byte(plain), "userA", ctime, "Src", "M.1700000000.A.003", false)
	if err != nil {
		t.Fatal(err)
	}
	if len(parsed.Comments) != 1 || parsed.Comments[0].CreatorName != "userX" {
		t.Errorf("original: comments %+v", parsed.Comments)
	}
}
