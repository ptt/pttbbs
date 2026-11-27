package importer

import (
	"bytes"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"

	"pttbbs/nest/model"
)

func cplogLine(op, board string, ts int64) string {
	return "※ " + op + ":轉錄至看板 " + board + "                       " + time.Unix(ts, 0).Format("01/02 15:04") + "\n"
}

func minuteOf(ts int64) int64 { return ts - ts%60 }

// copyArticle is a copy as mbbsd writes it: the header naming the source,
// then the whole original (with its comments and cplog lines), then the
// note of who copied it.
func copyArticle(header, original, tail string) string {
	return "作者: userB (B) 看板: Dst\n標題: [轉錄] t\n時間: Mon Jan  1 00:00:00 2024\n\n" +
		header + "\n\n" + original +
		"\n※ 發信站: 批踢踢實業坊(ptt.cc)\n※ 轉錄者: userB (1.2.3.4), 01/01/2024 00:00:00\n" + tail
}

func originalArticle(cplogs string) string {
	return "作者: userA (A) 看板: Src\n標題: t\n時間: Mon Jan  1 00:00:00 2024\n\n本文\n\n--\n" +
		"※ 發信站: 批踢踢實業坊(ptt.cc), 來自: 1.2.3.4\n" + cplogs
}

func TestParseCopyHeader(t *testing.T) {
	const ctime = int64(1700000000)
	for _, tc := range []struct {
		header, board, file string
	}{
		{"※ [本文轉錄自 Gossiping 看板 #19bWBIgy ]", "Gossiping", "M.1234567890.A.ABC"},
		{"\x1b[1;32m※ [本文轉錄自 Test-1 看板 #0uRKE000 ]\x1b[m", "Test-1", "M.946684800.A.000"},
		{"※ [本文轉錄自 Old 看板 M.1234567890.A.ABC]", "Old", "M.1234567890.A.ABC"},
		{"※ [本文轉錄自 Old 看板]", "", ""},
		{"※ [本文轉錄自某隱形看板]", "", ""},
		{"※ [本文轉錄自 userA 信箱]", "", ""},
	} {
		text := copyArticle(tc.header, originalArticle(""), "")
		parsed, err := ParseArticleText([]byte(text), "userB", ctime, "Dst", "M.1700000000.A.001", false)
		if err != nil {
			t.Fatal(err)
		}
		cf := parsed.CopiedFrom
		if tc.board == "" {
			if cf != nil {
				t.Errorf("%q: CopiedFrom = %+v, want none", tc.header, cf)
			}
			continue
		}
		if cf == nil || cf.SrcBoard != tc.board || cf.SrcFile != tc.file || cf.TargetBoard != "Dst" ||
			cf.TargetFile != "M.1700000000.A.001" || cf.CreatorName != "userB" || cf.CTime != ctime {
			t.Errorf("%q: CopiedFrom = %+v", tc.header, cf)
		}
		if len(parsed.Crossposts) != 0 {
			t.Errorf("%q: crossposts %+v", tc.header, parsed.Crossposts)
		}
	}
}

// The cplog lines a copy has from its original are crossposts of the
// original (kept as text); only those after the copy are its own.
func TestCopySkipsCopiedCplog(t *testing.T) {
	ctime := minuteOf(time.Now().Unix()) - 100*86400
	original := originalArticle(cplogLine("userA", "Other", ctime-86400) +
		cplogLine("userA", "SameMinute", ctime) +
		"※ userA:轉錄至看板 NoDate\n")
	for _, header := range []string{"※ [本文轉錄自 Src 看板 #19bWBIgy ]", "※ [本文轉錄自某隱形看板]"} {
		text := copyArticle(header, original, cplogLine("userC", "Later", ctime+2*86400))
		parsed, err := ParseArticleText([]byte(text), "userB", ctime+30, "Dst", "M.1.A.001", false)
		if err != nil {
			t.Fatal(err)
		}
		if len(parsed.Crossposts) != 1 {
			t.Fatalf("%q: crossposts %+v, want only Later", header, parsed.Crossposts)
		}
		cp := parsed.Crossposts[0]
		if cp.TargetBoard != "Later" || cp.SrcBoard != "Dst" || cp.SrcFile != "M.1.A.001" || cp.CTime != ctime+2*86400 {
			t.Errorf("%q: crosspost %+v", header, cp)
		}
		for _, board := range []string{"Other", "SameMinute", "NoDate"} {
			if !strings.Contains(parsed.BodyContent, "轉錄至看板 "+board) {
				t.Errorf("%q: copied cplog line of %s dropped from the body", header, board)
			}
		}
	}

	// In an original, every cplog line is a crosspost.
	parsed, err := ParseArticleText([]byte(original), "userA", ctime-2*86400, "Src", "M.2.A.001", false)
	if err != nil {
		t.Fatal(err)
	}
	if len(parsed.Crossposts) != 3 || parsed.CopiedFrom != nil {
		t.Fatalf("original: crossposts %+v, from %+v", parsed.Crossposts, parsed.CopiedFrom)
	}
}

// A cplog line dated a little before its post is not a year later, and
// none is in the future.
func TestCplogTimeNotInFuture(t *testing.T) {
	now := time.Now().Unix()
	ctime := minuteOf(now) - 30*86400
	text := originalArticle(cplogLine("userA", "Before", ctime-2*86400) + cplogLine("userA", "After", ctime+5*86400))
	parsed, err := ParseArticleText([]byte(text), "userA", ctime, "Src", "M.1.A.001", false)
	if err != nil {
		t.Fatal(err)
	}
	if len(parsed.Crossposts) != 2 {
		t.Fatalf("crossposts %+v", parsed.Crossposts)
	}
	if got := parsed.Crossposts[0].CTime; got != ctime {
		t.Errorf("Before: %v, want the post time %v", time.Unix(got, 0), time.Unix(ctime, 0))
	}
	if got := parsed.Crossposts[1].CTime; got != ctime+5*86400 {
		t.Errorf("After: %v, want %v", time.Unix(got, 0), time.Unix(ctime+5*86400, 0))
	}

	// A cplog line dated later than now (and a year earlier, before the
	// post) is at the post.
	soon := minuteOf(now) + 3*86400
	post := soon - 300*86400
	text = originalArticle(cplogLine("userA", "Soon", soon))
	parsed, err = ParseArticleText([]byte(text), "userA", post, "Src", "M.1.A.001", false)
	if err != nil {
		t.Fatal(err)
	}
	if len(parsed.Crossposts) != 1 || parsed.Crossposts[0].CTime != post {
		t.Errorf("Soon: %+v, want the post time", parsed.Crossposts)
	}
}

func writeBoard(t *testing.T, bbsHome, board string, posts map[string]string, owner string) {
	t.Helper()
	dir := filepath.Join(bbsHome, "boards", board[:1], board)
	if err := os.MkdirAll(dir, 0755); err != nil {
		t.Fatal(err)
	}
	var recs [][]byte
	for name, content := range posts {
		recs = append(recs, dirFixture{filename: name, owner: owner, title: "t", modified: uint32(stampTime(name))}.record())
		if err := os.WriteFile(filepath.Join(dir, name), []byte(content), 0644); err != nil {
			t.Fatal(err)
		}
	}
	if err := os.WriteFile(filepath.Join(dir, ".DIR"), bytes.Join(recs, nil), 0644); err != nil {
		t.Fatal(err)
	}
}

// A copy imported before its source is linked when the source is
// imported: its record at the source completes the cplog record there,
// and stays so when the source is imported again, with or without
// overwrite.
func TestImportCopyAndSource(t *testing.T) {
	tempDir := t.TempDir()
	t0 := minuteOf(time.Now().Unix()) - 100*86400
	t1 := t0 + 3600 + 20 // the copy
	srcFile := "M." + itoa(t0) + ".A.ABC"
	copyFile := "M." + itoa(t1) + ".A.001"
	aid, _ := model.PostFileToAID(srcFile)

	original := originalArticle(cplogLine("userA", "Other", t0+600))
	writeBoard(t, tempDir, "Src", map[string]string{srcFile: original + cplogLine("userB", "Dst", t1+15)}, "userA")
	writeBoard(t, tempDir, "Dst", map[string]string{
		copyFile: copyArticle("※ [本文轉錄自 Src 看板 #"+aid+" ]", original, cplogLine("userC", "Later", t1+2*86400)),
	}, "userB")
	engine := openImportEngine(t, tempDir)
	imp := func(board string, overwrite bool) {
		t.Helper()
		if _, err := ImportBoard(engine, ImportBoardOptions{BBSHome: tempDir, Board: board, Overwrite: overwrite}); err != nil {
			t.Fatalf("ImportBoard %s: %v", board, err)
		}
	}

	imp("Dst", false)
	if cps, _ := engine.GetCrossposts("Dst", copyFile); len(cps) != 1 || cps[0].TargetCommunity != "Later" {
		t.Fatalf("crossposts of the copy: %+v", cps)
	}
	if st, err := engine.LinkCrossposts(); err != nil || st.Pending != 1 {
		t.Fatalf("LinkCrossposts before the source = %+v, %v", st, err)
	}

	for _, overwrite := range []bool{false, false, true} {
		imp("Src", overwrite)
		cps, err := engine.GetCrossposts("Src", srcFile)
		if err != nil || len(cps) != 2 {
			t.Fatalf("overwrite=%v: crossposts of the source: %+v, %v", overwrite, cps, err)
		}
		for _, cp := range cps {
			switch cp.TargetCommunity {
			case "Dst":
				if cp.TargetPostFile != copyFile || cp.CreatedAt != t1 || cp.CreatorName != "userB" {
					t.Errorf("overwrite=%v: record of the copy %+v", overwrite, cp)
				}
			case "Other":
				if cp.TargetPostFile != "-" || cp.CreatedAt != t0+600 {
					t.Errorf("overwrite=%v: cplog record %+v", overwrite, cp)
				}
			default:
				t.Errorf("overwrite=%v: record %+v", overwrite, cp)
			}
		}
		if p, _ := engine.GetPostByCommunityFile("Src", srcFile); p == nil || p.NumCrossposts != 2 {
			t.Fatalf("overwrite=%v: source post %+v", overwrite, p)
		}
		if src, _, _ := engine.GetOriginPost("Dst", copyFile); src == nil || src.PostFile != srcFile {
			t.Fatalf("overwrite=%v: origin of the copy %+v", overwrite, src)
		}
	}
	if st, err := engine.LinkCrossposts(); err != nil || st.Linked != 1 || st.Pending != 0 {
		t.Fatalf("LinkCrossposts = %+v, %v", st, err)
	}
}

func itoa(v int64) string { return strconv.FormatInt(v, 10) }

// Copies imported one by one (ImportPost, as nest.svc does on demand) or
// by sync-dir --rebuild are linked as ImportBoard links them.
func TestImportCopyIncrementally(t *testing.T) {
	tempDir := t.TempDir()
	t0 := minuteOf(time.Now().Unix()) - 100*86400
	t1 := t0 + 3600
	srcFile := "M." + itoa(t0) + ".A.ABC"
	copyFile := "M." + itoa(t1) + ".A.001"
	aid, _ := model.PostFileToAID(srcFile)
	header := "※ [本文轉錄自 Src 看板 #" + aid + " ]"
	original := originalArticle("")
	writeBoard(t, tempDir, "Src", map[string]string{srcFile: original + cplogLine("userB", "Dst", t1)}, "userA")
	writeBoard(t, tempDir, "Dst", map[string]string{copyFile: copyArticle(header, original, "")}, "userB")
	writeBoard(t, tempDir, "Dst2", map[string]string{copyFile: copyArticle(header, original, "")}, "userB")
	engine := openImportEngine(t, tempDir)
	if _, err := ImportBoard(engine, ImportBoardOptions{BBSHome: tempDir, Board: "Src"}); err != nil {
		t.Fatal(err)
	}

	if _, err := ImportPost(engine, ImportBoardOptions{BBSHome: tempDir, Board: "Dst"}, copyFile); err != nil {
		t.Fatal(err)
	}
	if _, err := engine.Communities().Ensure("Dst2"); err != nil {
		t.Fatal(err)
	}
	if st, err := SyncDir(engine, SyncDirOptions{BBSHome: tempDir, Board: "Dst2", Rebuild: true}); err != nil || st.Imported != 1 {
		t.Fatalf("SyncDir = %+v, %v", st, err)
	}

	cps, err := engine.GetCrossposts("Src", srcFile)
	if err != nil || len(cps) != 2 {
		t.Fatalf("crossposts of the source: %+v, %v", cps, err)
	}
	for _, cp := range cps {
		if cp.TargetPostFile != copyFile || (cp.TargetCommunity != "Dst" && cp.TargetCommunity != "Dst2") {
			t.Errorf("record %+v", cp)
		}
	}
	if p, _ := engine.GetPostByCommunityFile("Src", srcFile); p == nil || p.NumCrossposts != 2 {
		t.Fatalf("source post %+v", p)
	}
}
