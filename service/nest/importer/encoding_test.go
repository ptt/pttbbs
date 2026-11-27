package importer

import (
	"bytes"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"pttbbs/big5uao"
	"pttbbs/nest/storage"
)

func TestDecodeHeaderText(t *testing.T) {
	cut := append(big5uao.Encode("家教順"), big5uao.Encode("利")[0])
	for _, tc := range []struct {
		in   string
		want string
	}{
		{string(big5uao.Encode("[系統安全局]")), "[系統安全局]"},
		{string(cut), "家教順"},
		{string(big5uao.Encode("abc")) + "\xdc", "abc"},
		{"plain", "plain"},
		{"已是 UTF-8", "已是 UTF-8"},
	} {
		if got := decodeHeaderText(tc.in, true); got != tc.want {
			t.Errorf("decodeHeaderText(%q) = %q, want %q", tc.in, got, tc.want)
		}
	}
	// Not Big5: kept as it is.
	if got := decodeHeaderText("\xdc", false); got != "\xdc" {
		t.Errorf("not Big5: %q", got)
	}
}

// Owners that are not userids (system bots, anonymous nicknames) are Big5 in
// .DIR, and are imported in UTF-8, also as the author of the OP's text among
// the comments; the comment lines nest.svc writes for them read back.
func TestImportBig5Owner(t *testing.T) {
	tempDir := t.TempDir()
	boardDir := filepath.Join(tempDir, "boards", "I", "ImpBoard")
	if err := os.MkdirAll(boardDir, 0755); err != nil {
		t.Fatal(err)
	}
	const (
		bot  = "M.1728000001.A.001"
		anon = "M.1728000002.A.002"
	)
	cutTitle := dirFixture{filename: anon, owner: string(big5uao.Encode("  恰恰好.")), title: "x", modified: 1728000002}.record()
	title := append(big5uao.Encode("匿名的家教順"), big5uao.Encode("利")[0])
	copy(cutTitle[fhdrOffTitle:fhdrOffTitle+fhdrLenTitle], make([]byte, fhdrLenTitle))
	copy(cutTitle[fhdrOffTitle:], title)
	recs := [][]byte{
		dirFixture{filename: bot, owner: string(big5uao.Encode("[系統安全局]")), title: "[公告] 測試", modified: 1728000001}.record(),
		cutTitle,
	}
	if err := os.WriteFile(filepath.Join(boardDir, ".DIR"), bytes.Join(recs, nil), 0644); err != nil {
		t.Fatal(err)
	}
	article := append(legacyArticle("[系統安全局]", "[公告] 測試"), big5uao.Encode("站方補充說明\n")...)
	for _, name := range []string{bot, anon} {
		if err := os.WriteFile(filepath.Join(boardDir, name), article, 0644); err != nil {
			t.Fatal(err)
		}
	}
	engine := openImportEngine(t, tempDir)
	opts := ImportBoardOptions{BBSHome: tempDir, Board: "ImpBoard", IsBig5: true, Users: fakeUsers{"userB": {2, 1600000000}}}

	p, err := ImportPost(engine, opts, bot)
	if err != nil {
		t.Fatal(err)
	}
	if p.CreatorName != "[系統安全局]" {
		t.Errorf("creator %q, want [系統安全局]", p.CreatorName)
	}
	comments, err := engine.GetCommentsByCommunityFileFiltered("ImpBoard", bot, 1, 100, "", 0)
	if err != nil || len(comments) != 2 {
		t.Fatalf("comments = %d, %v; want the push and the OP's text", len(comments), err)
	}
	white := comments[1]
	if white.CreatorName != "[系統安全局]" || !strings.Contains(white.Content, "站方補充說明") {
		t.Errorf("white comment = %q %q", white.CreatorName, white.Content)
	}

	// Its comment line, written by nest.svc, is read back as a floor.
	white.Content += "\n第二行"
	line := storage.FormatCommentBlock(p, white)
	parsed, err := ParseArticleText([]byte("本文\n--\n"+line), "x", p.CreatedAt, "ImpBoard", bot, false)
	if err != nil {
		t.Fatal(err)
	}
	if len(parsed.Comments) != 1 || parsed.Comments[0].CreatorName != "[系統安全局]" || !parsed.Comments[0].Floor ||
		parsed.Comments[0].Content != "站方補充說明\n第二行" {
		t.Errorf("floor %q read back as %+v", line, parsed.Comments)
	}

	ap, err := ImportPost(engine, opts, anon)
	if err != nil {
		t.Fatal(err)
	}
	if ap.CreatorName != "恰恰好." || ap.Title != "匿名的家教順" {
		t.Errorf("anonymous owner/cut title = %q %q", ap.CreatorName, ap.Title)
	}

	// sync-dir writes the owner back in Big5.
	rec := make([]byte, FileHeaderSize)
	fillRecord(rec, p, true)
	if got := cString(rec[fhdrOffOwner : fhdrOffOwner+fhdrLenOwner]); got != string(big5uao.Encode("[系統安全局]")) {
		t.Errorf(".DIR owner %q, want Big5", got)
	}
}
