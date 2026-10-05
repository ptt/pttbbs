package importer

import (
	"bytes"
	"encoding/binary"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/syndtr/goleveldb/leveldb"

	"pttbbs/post/model"
	"pttbbs/post/storage"
)

func TestImporterP2AndP1Comments(t *testing.T) {
	article := `作者: op (operator) 看板: TestBoard
標題: 測試文章
時間: Wed Oct  1 20:00:00 2026

這是一篇測試文章本文。
--
※ 發信站: 批踢踢實業坊(ptt.cc), 來自: 1.2.3.4
※ 文章網址: https://ptt2.cc/bbs/TestBoard/M.1234567890.A.001.html
→ lantw44:咦 PTT2 會解讀 Markdown 嗎？                         推 10/01 21:40
→ lantw44:看起來好像真的可以耶！                               推 10/01 21:40
這是原 po 的白字補充
這又是原 po 的第二行補充
推 user1: 這是 PTT1 標準推文                                   10/01 21:45
推 jim: 推                                                     10/01 21:46
→ user2: 這是 PTT1 箭頭推文                                   10/01 21:47
※ 編輯: op (1.2.3.4), 10/01/2026 22:00:00
`

	baseTime := time.Date(2026, 10, 1, 20, 0, 0, 0, time.Local).Unix()
	parsed, err := ParseArticleText([]byte(article), "op", baseTime, "TestBoard", "M.1234567890.A.001", false)
	if err != nil {
		t.Fatalf("ParseArticleText failed: %v", err)
	}

	// Verify system lines preserved in body
	if !strings.Contains(parsed.BodyContent, "※ 發信站: 批踢踢實業坊(ptt.cc)") {
		t.Errorf("Expected '※ 發信站' to be preserved in BodyContent")
	}
	if !strings.Contains(parsed.BodyContent, "※ 編輯: op") {
		t.Errorf("Expected '※ 編輯' to be preserved in BodyContent")
	}

	// Expected comments:
	// 0: lantw44 (P2 merged 2 lines: "咦 PTT2 會解讀 Markdown 嗎？\n看起來好像真的可以耶！", type: 推)
	// 1: op (white text merged: "這是原 po 的白字補充\n這又是原 po 的第二行補充", type: white)
	// 2: user1 ("這是 PTT1 標準推文", type: 推)
	// 3: jim ("推", type: 推)
	// 4: user2 ("這是 PTT1 箭頭推文", type: →)

	if len(parsed.Comments) != 5 {
		t.Fatalf("Expected 5 comments, got %d", len(parsed.Comments))
	}

	// Comment 0: P2 merged
	c0 := parsed.Comments[0]
	if c0.Author != "lantw44" || c0.CommentType != "推" {
		t.Errorf("c0 author/type mismatch: %s / %s", c0.Author, c0.CommentType)
	}
	expectedContent0 := "咦 PTT2 會解讀 Markdown 嗎？\n看起來好像真的可以耶！"
	if c0.Content != expectedContent0 {
		t.Errorf("c0 content mismatch: got %q, expected %q", c0.Content, expectedContent0)
	}

	// Comment 1: White text
	c1 := parsed.Comments[1]
	if c1.Author != "op" || c1.CommentType != "white" {
		t.Errorf("c1 author/type mismatch: %s / %s", c1.Author, c1.CommentType)
	}

	// Comment 2: PTT1
	c2 := parsed.Comments[2]
	if c2.Author != "user1" || c2.CommentType != "推" || c2.Content != "這是 PTT1 標準推文" {
		t.Errorf("c2 mismatch: %+v", c2)
	}

	// Comment 3: jim '推' not eaten
	c3 := parsed.Comments[3]
	if c3.Author != "jim" || c3.CommentType != "推" || c3.Content != "推" {
		t.Errorf("c3 mismatch: got content %q, expected '推'", c3.Content)
	}

	// Comment 4: user2 arrow
	c4 := parsed.Comments[4]
	if c4.Author != "user2" || c4.CommentType != "→" || c4.Content != "這是 PTT1 箭頭推文" {
		t.Errorf("c4 mismatch: %+v", c4)
	}
	if c0.LegacyType != model.LegacyTypeOldRecommend {
		t.Errorf("c0 legacy type mismatch: %d", c0.LegacyType)
	}
	if c2.LegacyType != model.LegacyTypePush {
		t.Errorf("c2 legacy type mismatch: %d", c2.LegacyType)
	}
	if c4.LegacyType != model.LegacyTypeArrow {
		t.Errorf("c4 legacy type mismatch: %d", c4.LegacyType)
	}
}

func TestEditMarksDeduplication(t *testing.T) {
	article := `作者: markban 看板: TestBoard
標題: 編輯測試
時間: Fri Nov 25 18:00:00 2016

這是一篇測試編輯重複的文章。
--
※ 發信站: 批踢踢實業坊(ptt.cc), 來自: 1.2.3.4
※  編輯: markban (115.82.178.95), 11/25/2016 19:18:40
※ 編輯: other (1.1.1.1), 11/26/2016 10:00:00
※  編輯: markban (101.9.179.5), 11/30/2016 00:16:46
推 test: 推文 11/30 01:00
`
	baseTime := time.Date(2016, 11, 25, 18, 0, 0, 0, time.Local).Unix()
	parsed, err := ParseArticleText([]byte(article), "markban", baseTime, "TestBoard", "M.1480068000.A.001", false)
	if err != nil {
		t.Fatalf("ParseArticleText failed: %v", err)
	}

	if strings.Contains(parsed.BodyContent, "115.82.178.95") {
		t.Errorf("Expected earlier edit mark (115.82.178.95) to be deduplicated/removed")
	}
	if strings.Contains(parsed.BodyContent, "1.1.1.1") {
		t.Errorf("Expected intermediate edit mark (1.1.1.1) to be deduplicated/removed")
	}
	if !strings.Contains(parsed.BodyContent, "101.9.179.5") {
		t.Errorf("Expected latest edit mark (101.9.179.5) to be kept in body")
	}
}

func TestCommentMergeRules(t *testing.T) {
	article := `作者: op (operator) 看板: TestBoard
標題: 合併推文規則測試
時間: Fri Nov 25 18:00:00 2016

本文
--
※ 發信站: 批踢踢實業坊
推 userA: 推文第一行                                             11/25 18:01
→ userA: 箭頭第二行                                             11/25 18:01
推 userA: 第二次推文不能合併                                     11/25 18:01
噓 userA: 噓文也不能合併                                         11/25 18:02
→ userA: 噓文下面的箭頭要合併                                   11/25 18:02
噓 userA: 第二次噓文不能合併                                     11/25 18:02
→ userB: 純箭頭第一行                                           11/25 18:03
→ userB: 純箭頭第二行要合併                                     11/25 18:03
`
	baseTime := time.Date(2016, 11, 25, 18, 0, 0, 0, time.Local).Unix()
	parsed, err := ParseArticleText([]byte(article), "op", baseTime, "TestBoard", "M.1480068000.A.002", false)
	if err != nil {
		t.Fatalf("ParseArticleText failed: %v", err)
	}

	if len(parsed.Comments) != 5 {
		for i, c := range parsed.Comments {
			t.Logf("[%d] type=%d author=%s content=%q", i, c.LegacyType, c.Author, c.Content)
		}
		t.Fatalf("Expected 5 comments, got %d", len(parsed.Comments))
	}

	c0 := parsed.Comments[0]
	if c0.LegacyType != model.LegacyTypePush || c0.Content != "推文第一行\n箭頭第二行" {
		t.Errorf("c0 mismatch: type=%d, content=%q", c0.LegacyType, c0.Content)
	}

	c1 := parsed.Comments[1]
	if c1.LegacyType != model.LegacyTypePush || c1.Content != "第二次推文不能合併" {
		t.Errorf("c1 mismatch: type=%d, content=%q", c1.LegacyType, c1.Content)
	}

	c2 := parsed.Comments[2]
	if c2.LegacyType != model.LegacyTypeBoo || c2.Content != "噓文也不能合併\n噓文下面的箭頭要合併" {
		t.Errorf("c2 mismatch: type=%d, content=%q", c2.LegacyType, c2.Content)
	}

	c3 := parsed.Comments[3]
	if c3.LegacyType != model.LegacyTypeBoo || c3.Content != "第二次噓文不能合併" {
		t.Errorf("c3 mismatch: type=%d, content=%q", c3.LegacyType, c3.Content)
	}

	c4 := parsed.Comments[4]
	if c4.Author != "userB" || c4.LegacyType != model.LegacyTypeArrow || c4.Content != "純箭頭第一行\n純箭頭第二行要合併" {
		t.Errorf("c4 mismatch: type=%d, content=%q", c4.LegacyType, c4.Content)
	}
}

func TestImportBoard(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "import_board_test_*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tempDir)

	boardDir := filepath.Join(tempDir, "boards", "TestBoard")
	renderTarget := filepath.Join(tempDir, "rendered")
	if err := os.MkdirAll(boardDir, 0755); err != nil {
		t.Fatal(err)
	}

	// Create 2 post files and pack into .DIR
	type postFixture struct {
		filename string
		owner    string
		title    string
		content  string
	}
	posts := []postFixture{
		{
			filename: "M.1728000001.A.001",
			owner:    "userA",
			title:    "第一篇文章",
			content:  "本文第一篇\n--\n※ 發信站: 批踢踢實業坊\n推 userB: 推第一篇 10/01 12:00\n",
		},
		{
			filename: "M.1728000002.A.002",
			owner:    "userB",
			title:    "第二篇文章",
			content:  "本文第二篇\n--\n※ 發信站: 批踢踢實業坊\n→ userA: 箭頭推文 10/01 12:05\n",
		},
	}

	var dirBuf []byte
	for _, p := range posts {
		filePath := filepath.Join(boardDir, p.filename)
		if err := os.WriteFile(filePath, []byte(p.content), 0644); err != nil {
			t.Fatal(err)
		}

		hdr := make([]byte, FileHeaderSize)
		copy(hdr[0:28], p.filename)
		binary.LittleEndian.PutUint32(hdr[28:32], 1728000000)
		hdr[33] = 1
		copy(hdr[34:48], p.owner)
		copy(hdr[48:54], "10/01")
		copy(hdr[54:119], p.title)
		dirBuf = append(dirBuf, hdr...)
	}

	dirPath := filepath.Join(boardDir, ".DIR")
	if err := os.WriteFile(dirPath, dirBuf, 0644); err != nil {
		t.Fatal(err)
	}

	engineDir := filepath.Join(tempDir, "engine")
	engine, err := storage.OpenEngine(storage.Config{
		DataDir:        engineDir,
		CacheDir:       filepath.Join(engineDir, "cache"),
		FlushSeconds:   2,
		FilterEncoding: "utf-8",
	})
	if err != nil {
		t.Fatal(err)
	}
	defer engine.Close()

	// 1. Dry-run test: should calculate stats, but not write to DB or disk
	dryStats, err := MigrateBoard(engine, ImportBoardOptions{
		BBSHome:      tempDir,
		Board:        "TestBoard",
		RenderTarget: renderTarget,
		DryRun:       true,
		Overwrite:    true,
	})
	if err != nil {
		t.Fatalf("MigrateBoard dry-run failed: %v", err)
	}
	if dryStats.ValidPosts != 2 || dryStats.PostsImported != 2 || dryStats.CommentsImported != 2 {
		t.Errorf("Unexpected dry-run stats: %+v", dryStats)
	}
	for _, p := range posts {
		renderedPath := filepath.Join(renderTarget, p.filename)
		if _, err := os.Stat(renderedPath); err == nil {
			t.Errorf("Dry-run should not create rendered file %s", renderedPath)
		}
		if _, err := engine.GetPostByCommunityFile("TestBoard", p.filename); err == nil {
			t.Errorf("Dry-run should not write post %s to database", p.filename)
		}
	}

	progressCalls := 0
	stats, err := MigrateBoard(engine, ImportBoardOptions{
		BBSHome:      tempDir,
		Board:        "TestBoard",
		RenderTarget: renderTarget,
		Overwrite:    true,
		ProgressFn: func(processed, total int) {
			progressCalls++
		},
	})
	if err != nil {
		t.Fatalf("MigrateBoard failed: %v", err)
	}

	if stats.ValidPosts != 2 {
		t.Errorf("Expected 2 valid posts, got %d", stats.ValidPosts)
	}
	if stats.Errors != 0 {
		t.Errorf("Expected 0 errors, got %d", stats.Errors)
	}
	if progressCalls == 0 {
		t.Errorf("Expected progress callback to be invoked")
	}

	// Verify rendered files
	for _, p := range posts {
		renderedPath := filepath.Join(renderTarget, p.filename)
		data, err := os.ReadFile(renderedPath)
		if err != nil {
			t.Errorf("Failed to read rendered file %s: %v", p.filename, err)
			continue
		}
		if !strings.Contains(string(data), "本文") || !strings.Contains(string(data), "[1]") {
			t.Errorf("Rendered file missing expected content: %s", string(data))
		}
	}
}

func TestRenderLegacyPostWithComments(t *testing.T) {
	p := &model.Post{
		Title:   "測試",
		Content: "文章本文\n--\n※ 發信站: 批踢踢實業坊\n",
	}
	comments := []*model.Comment{
		{
			Author:     "user1",
			Content:    "這是推文",
			LegacyType: model.LegacyTypePush,
			CreatedAt:  1728000000,
		},
		{
			Author:     "user2",
			Content:    "這是噓文",
			LegacyType: model.LegacyTypeBoo,
			CreatedAt:  1728000000,
		},
		{
			Author:     "user3",
			Content:    "這是箭頭",
			LegacyType: model.LegacyTypeArrow,
			CreatedAt:  1728000000,
		},
		{
			Author:     "lantw44",
			Content:    "這是 P2 推文",
			LegacyType: model.LegacyTypeOldRecommend,
			CreatedAt:  1728000000,
		},
	}

	rendered := storage.RenderLegacyPostWithComments(p, comments, false)
	renderedStr := string(rendered)

	if !strings.Contains(renderedStr, "\x1b[1;37m推 \x1b[33muser1") {
		t.Errorf("Expected legacy 推 formatting for user1, got %q", renderedStr)
	}
	if !strings.Contains(renderedStr, "\x1b[1;31m噓 \x1b[33muser2") {
		t.Errorf("Expected legacy 噓 formatting for user2, got %q", renderedStr)
	}
	if !strings.Contains(renderedStr, "\x1b[1;31m→ \x1b[33muser3") {
		t.Errorf("Expected legacy 箭頭 formatting for user3, got %q", renderedStr)
	}
	if !strings.Contains(renderedStr, "\x1b[1;31m→ \x1b[33mlantw44") || !strings.Contains(renderedStr, "推 ") {
		t.Errorf("Expected P2 OLD_RECOMMEND formatting for lantw44, got %q", renderedStr)
	}
}

func TestIPv6AndIPParsing(t *testing.T) {
	article := `作者: test (tester) 看板: TestBoard
標題: IP 測試文章
時間: Wed Oct  1 20:00:00 2026

本文內容。
--
※ 發信站: 批踢踢實業坊(ptt.cc), 來自: 1.2.3.4
※ 文章網址: https://ptt.cc/bbs/TestBoard/M.1234567890.A.002.html
推 user1: 77777                                                 10/01 21:01
推 user2: 100000                                                10/01 21:02
推 user3: 2800                                                  10/01 21:03
推 user4: C8763                                                 10/01 21:04
推 user5: bad                                                   10/01 21:05
推 user6: 19:27                                                 10/01 21:06
推 user7: 19:13:49                                              10/01 21:07
推 user8: :000                                                  10/01 21:08
推 user9: 我要投錢巨 BGD 7414                                   10/01 21:09
推 user10: 這是 IPv4 留言                       111.248.250.62 10/01 21:10
推 user11: 這是 IPv4 masked 留言                 111.248.250.* 10/01 21:11
推 user12: 這是 IPv6 留言                     2001:b011:7c01:: 10/01 21:12
推 user13: 這是 IPv6 full 留言   2001:db8:85a3:0:0:8a2e:370:7334 10/01 21:13
推 user14: 這是 IPv6 loopback                              ::1 10/01 21:14
→ user15: PTT2 IPv6 測試                         推 2001:db8::1 10/01 21:15
`

	baseTime := time.Date(2026, 10, 1, 20, 0, 0, 0, time.Local).Unix()
	parsed, err := ParseArticleText([]byte(article), "test", baseTime, "TestBoard", "M.1234567890.A.002", false, true)
	if err != nil {
		t.Fatalf("ParseArticleText failed: %v", err)
	}

	expected := []struct {
		author  string
		content string
		ip      string
	}{
		{"user1", "77777", ""},
		{"user2", "100000", ""},
		{"user3", "2800", ""},
		{"user4", "C8763", ""},
		{"user5", "bad", ""},
		{"user6", "19:27", ""},
		{"user7", "19:13:49", ""},
		{"user8", ":000", ""},
		{"user9", "我要投錢巨 BGD 7414", ""},
		{"user10", "這是 IPv4 留言", "111.248.250.62"},
		{"user11", "這是 IPv4 masked 留言", "111.248.250.*"},
		{"user12", "這是 IPv6 留言", "2001:b011:7c01::"},
		{"user13", "這是 IPv6 full 留言", "2001:db8:85a3:0:0:8a2e:370:7334"},
		{"user14", "這是 IPv6 loopback", "::1"},
		{"user15", "PTT2 IPv6 測試", "2001:db8::1"},
	}

	if len(parsed.Comments) != len(expected) {
		t.Fatalf("Expected %d comments, got %d", len(expected), len(parsed.Comments))
	}

	for i, exp := range expected {
		c := parsed.Comments[i]
		if c.Author != exp.author {
			t.Errorf("[%d] Author mismatch: got %q, expected %q", i, c.Author, exp.author)
		}
		if c.Content != exp.content {
			t.Errorf("[%d] Content mismatch: got %q, expected %q", i, c.Content, exp.content)
		}
		if c.IP != exp.ip {
			t.Errorf("[%d] IP mismatch: got %q, expected %q", i, c.IP, exp.ip)
		}
	}
}

func TestSignaturePreservation(t *testing.T) {
	// Case 1: Pure signature with ANSI / ASCII art, no push comments
	article1 := `發信人: skysky.bbs@ofo.csie.ntu.edu.tw (小黑), 看板: asciiart
  標題: 測試圖
發信站: 國立臺灣大學 (Wed May  3 12:44:43 2006)

這是內文。

--
 ◢█◣▏◤█◣      ◆未來最舊小棧 Oldest Future Object
 █●▇█▁˙█      ◆通訊頻率 OfO.twbbs.org
 ◥█◢▉◥█◤      ◆來源座標 LL-211-78-147-215.LL.sparqnet.net
`

	baseTime := time.Date(2006, 5, 3, 12, 44, 43, 0, time.Local).Unix()
	parsed1, err := ParseArticleText([]byte(article1), "skysky", baseTime, "asciiart", "M.1146632464.A", false)
	if err != nil {
		t.Fatalf("ParseArticleText failed: %v", err)
	}

	if len(parsed1.Comments) != 0 {
		t.Errorf("Expected 0 comments for pure signature post, got %d", len(parsed1.Comments))
	}
	if !strings.Contains(parsed1.BodyContent, "未來最舊小棧") {
		t.Errorf("Expected signature to be preserved in BodyContent")
	}

	// Case 2: Post with signature, system station line, and comments
	article2 := `作者: test (tester) 看板: TestBoard
標題: 測試文章
時間: Wed Oct  1 20:00:00 2026

這是內文。
--
這是我的自訂簽名檔
第二行簽名檔內容
※ 發信站: 批踢踢實業坊(ptt.cc), 來自: 1.2.3.4
※ 文章網址: https://ptt.cc/bbs/TestBoard/M.1.A.001.html
推 user1: 第一推                                                10/01 21:00
作者回覆的白字補充
推 user2: 第二推                                                10/01 21:05
`

	parsed2, err := ParseArticleText([]byte(article2), "test", baseTime, "TestBoard", "M.1.A.001", false)
	if err != nil {
		t.Fatalf("ParseArticleText failed: %v", err)
	}

	// Signature must be in BodyContent, not in comments!
	if !strings.Contains(parsed2.BodyContent, "這是我的自訂簽名檔") {
		t.Errorf("Expected signature in BodyContent")
	}
	if !strings.Contains(parsed2.BodyContent, "※ 發信站:") {
		t.Errorf("Expected system line in BodyContent")
	}

	// Comments must only be: user1, author white text, user2
	if len(parsed2.Comments) != 3 {
		t.Fatalf("Expected 3 comments, got %d", len(parsed2.Comments))
	}
	if parsed2.Comments[0].Author != "user1" || parsed2.Comments[0].Content != "第一推" {
		t.Errorf("Comment 0 mismatch: %+v", parsed2.Comments[0])
	}
	if parsed2.Comments[1].Author != "test" || parsed2.Comments[1].Content != "作者回覆的白字補充" || parsed2.Comments[1].CommentType != "white" {
		t.Errorf("Comment 1 mismatch: %+v", parsed2.Comments[1])
	}
	if parsed2.Comments[2].Author != "user2" || parsed2.Comments[2].Content != "第二推" {
		t.Errorf("Comment 2 mismatch: %+v", parsed2.Comments[2])
	}
}

func TestReadArticleFileRules(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "read_article_test_*")
	if err != nil {
		t.Fatalf("MkdirTemp failed: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	// Case 1: Normal file
	normalPath := filepath.Join(tmpDir, "normal.txt")
	normalContent := []byte("Normal BBS article content line 1\nline 2\n")
	if err := os.WriteFile(normalPath, normalContent, 0644); err != nil {
		t.Fatalf("WriteFile failed: %v", err)
	}
	b1, err := ReadArticleFile(normalPath)
	if err != nil {
		t.Fatalf("ReadArticleFile normal failed: %v", err)
	}
	if !bytes.Equal(b1, normalContent) {
		t.Errorf("Expected %q, got %q", string(normalContent), string(b1))
	}

	// Case 2: Rule 2 - NUL byte truncation
	nulPath := filepath.Join(tmpDir, "with_nul.txt")
	nulContent := []byte("Header and valid body text\n--\n\x00Corrupted garbage data that should be discarded")
	if err := os.WriteFile(nulPath, nulContent, 0644); err != nil {
		t.Fatalf("WriteFile failed: %v", err)
	}
	b2, err := ReadArticleFile(nulPath)
	if err != nil {
		t.Fatalf("ReadArticleFile nul failed: %v", err)
	}
	expectedB2 := []byte("Header and valid body text\n--\n")
	if !bytes.Equal(b2, expectedB2) {
		t.Errorf("Expected truncated at NUL %q, got %q", string(expectedB2), string(b2))
	}

	// Case 3: Rule 1 - Sparse file detection (claimed 1GB, allocated 4KB)
	sparsePath := filepath.Join(tmpDir, "sparse.txt")
	sf, err := os.Create(sparsePath)
	if err != nil {
		t.Fatalf("Create sparse failed: %v", err)
	}
	prefixData := []byte("Important BBS post text\n--\n※ 發信站: 批踢踢實業坊\n")
	sf.Write(prefixData)
	// Seek to 1GB to create a sparse file with a huge hole
	const oneGB = int64(1024 * 1024 * 1024)
	if _, err := sf.Seek(oneGB, io.SeekStart); err == nil {
		sf.Write([]byte("Z"))
	}
	sf.Close()

	fi, err := os.Stat(sparsePath)
	if err != nil {
		t.Fatalf("Stat sparse failed: %v", err)
	}
	if fi.Size() < oneGB {
		t.Skip("Filesystem does not support sparse files, skipping sparse test")
	}

	b3, err := ReadArticleFile(sparsePath)
	if err != nil {
		t.Fatalf("ReadArticleFile sparse failed: %v", err)
	}
	// It should NOT read 1GB, and because the hole is filled with NUL, Rule 2 truncates at prefixData!
	if len(b3) > 1024*1024 {
		t.Errorf("Expected sparse file to be capped, got len=%d", len(b3))
	}
	if !bytes.Equal(b3, prefixData) {
		t.Errorf("Expected prefix data %q, got %q", string(prefixData), string(b3))
	}
}

func TestQueryCommentDB(t *testing.T) {
	dbDir := t.TempDir()
	db, err := leveldb.OpenFile(dbDir, nil)
	if err != nil {
		t.Fatalf("OpenFile failed: %v", err)
	}

	// Pack comment using commentd.py format (IIII13s81s = 110 bytes)
	val := make([]byte, 110)
	binary.LittleEndian.PutUint32(val[0:4], 1727800000) // time
	val[4] = 140
	val[5] = 112
	val[6] = 1
	val[7] = 1 // ip: 140.112.1.1
	binary.LittleEndian.PutUint32(val[8:12], 999) // userref
	binary.LittleEndian.PutUint32(val[12:16], 1) // type (推)
	copy(val[16:29], "testuser\x00")
	copy(val[29:110], "這是測試推文\x00")

	// 1st comment for TestBoard/M.1727800000.A.001
	key := "TestBoard/M.1727800000.A.001#00000001"
	if err := db.Put([]byte(key), val, nil); err != nil {
		t.Fatalf("Put failed: %v", err)
	}
	db.Close()

	// Reopen read-only like MigrateBoard does
	rdb, err := leveldb.OpenFile(dbDir, nil)
	if err != nil {
		t.Fatalf("OpenFile read failed: %v", err)
	}
	defer rdb.Close()

	// Query 1st comment (seq 0)
	ts, ip, err := QueryCommentDB(rdb, "TestBoard", "M.1727800000.A.001", 0)
	if err != nil {
		t.Fatalf("QueryCommentDB failed: %v", err)
	}
	if ts != 1727800000 {
		t.Errorf("Expected ts=1727800000, got %d", ts)
	}
	if ip != "140.112.1.1" {
		t.Errorf("Expected ip=140.112.1.1, got %s", ip)
	}

	// Query non-existent comment (seq 1)
	ts2, ip2, err := QueryCommentDB(rdb, "TestBoard", "M.1727800000.A.001", 1)
	if err != nil {
		t.Fatalf("QueryCommentDB seq 1 failed: %v", err)
	}
	if ts2 != 0 || ip2 != "" {
		t.Errorf("Expected empty result for seq 1, got ts=%d, ip=%s", ts2, ip2)
	}

	// Query nil db
	tsNil, ipNil, err := QueryCommentDB(nil, "TestBoard", "M.1727800000.A.001", 0)
	if err != nil || tsNil != 0 || ipNil != "" {
		t.Errorf("Expected zero values for nil db, got ts=%d, ip=%s, err=%v", tsNil, ipNil, err)
	}
}
