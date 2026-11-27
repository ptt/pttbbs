package importer

import (
	"bytes"
	"errors"
	"os"
	"path/filepath"
	"testing"

	"pttbbs/big5uao"
	"pttbbs/nest/model"
)

func writeDir(t *testing.T, bbsHome, board string, recs [][]byte, files map[string][]byte) string {
	t.Helper()
	dir := filepath.Join(bbsHome, "boards", board[:1], board)
	if err := os.MkdirAll(dir, 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, ".DIR"), bytes.Join(recs, nil), 0644); err != nil {
		t.Fatal(err)
	}
	for name, content := range files {
		if err := os.WriteFile(filepath.Join(dir, name), content, 0644); err != nil {
			t.Fatal(err)
		}
	}
	return dir
}

// A cplog line to a board that is not in .BRD (nor a community mirrored
// from it) is no crosspost, and registers no community.
func TestCplogToUnknownBoard(t *testing.T) {
	tempDir := t.TempDir()
	brd := append(boardRecord("Src", "測試", "src", 0, 0), boardRecord("Dst", "測試", "dst", 0, 0)...)
	if err := os.WriteFile(filepath.Join(tempDir, ".BRD"), brd, 0644); err != nil {
		t.Fatal(err)
	}
	const name = "M.1700000000.A.001"
	article := originalArticle(cplogLine("userA", "Dst", 1700000600) + cplogLine("userA", "Gone", 1700000700))
	writeDir(t, tempDir, "Src", [][]byte{dirFixture{filename: name, owner: "userA", title: "t", modified: 1700000000}.record()},
		map[string][]byte{name: []byte(article)})
	engine := openImportEngine(t, tempDir)
	if _, err := ImportBoard(engine, ImportBoardOptions{BBSHome: tempDir, Board: "Src"}); err != nil {
		t.Fatal(err)
	}
	cps, err := engine.GetCrossposts("Src", name)
	if err != nil || len(cps) != 1 || cps[0].TargetCommunity != "Dst" {
		t.Errorf("crossposts %+v, %v; want only Dst", cps, err)
	}
	if _, ok := engine.Communities().Lookup("Gone"); ok {
		t.Errorf("community Gone registered")
	}
}

// Damaged .DIR records (modified 56 or 2936015393) are the owner's, with
// no anonymous flag, marks or money from their garbage; sane anonymous
// records stay anonymous.
func TestRepairDamagedRecord(t *testing.T) {
	tempDir := t.TempDir()
	const (
		small = "M.1075482372.A.646"
		huge  = "M.1075482400.A.001"
		anon  = "M.1075482500.A.002"
	)
	recs := [][]byte{
		dirFixture{filename: small, owner: "winnielin", title: "Re: 同學會", modified: 55,
			filemode: model.FileAnonymous | model.FileBottom, multi: 6}.record(),
		dirFixture{filename: huge, owner: "winnielin", title: "t", modified: 2936015393, filemode: model.FileMarked, multi: 99}.record(),
		dirFixture{filename: anon, owner: string(big5uao.Encode("匿名者.")), title: "t", modified: 1075482500,
			filemode: model.FileAnonymous, multi: 7}.record(),
	}
	files := map[string][]byte{}
	for _, n := range []string{small, huge, anon} {
		files[n] = legacyArticle("winnielin", "t")
	}
	writeDir(t, tempDir, "TFG01Cheng", recs, files)
	engine := openImportEngine(t, tempDir)
	opts := ImportBoardOptions{BBSHome: tempDir, Board: "TFG01Cheng", IsBig5: true,
		Users: fakeUsers{"winnielin": {3, 1000000000}, "other": {6, 1000000000}, "anonAuthor": {7, 1000000000}}}
	if _, err := ImportBoard(engine, opts); err != nil {
		t.Fatal(err)
	}
	for _, n := range []string{small, huge} {
		p, err := engine.GetPostByCommunityFile("TFG01Cheng", n)
		if err != nil {
			t.Fatal(err)
		}
		if p.CreatorID != model.MakePersonID(3, 1000000000) || p.Filemode != 0 || p.Money != 0 || p.Modified != p.CreatedAt {
			t.Errorf("%s: creator %d filemode %#x money %d modified %d (created %d)", n, p.CreatorID, p.Filemode, p.Money, p.Modified, p.CreatedAt)
		}
	}
	p, err := engine.GetPostByCommunityFile("TFG01Cheng", anon)
	if err != nil {
		t.Fatal(err)
	}
	if p.CreatorID != model.MakePersonID(7, 1000000000) || p.Filemode != model.FileAnonymous || p.CreatorName != "匿名者." {
		t.Errorf("anonymous: creator %q %d filemode %#x", p.CreatorName, p.CreatorID, p.Filemode)
	}
}

// Records with an empty owner (PttDigest) or title take them from the
// headers of the article; records titled as deleted are not imported,
// even if old mbbsd left their file.
func TestEmptyOwnerAndDeletedTitle(t *testing.T) {
	tempDir := t.TempDir()
	const (
		digest  = "M.1282448091.A.17D"
		deleted = "M.1285539357.A.BA8"
		removed = "M.1285539400.A.001"
		oldDel  = "M.1085539400.A.002"
	)
	recs := [][]byte{
		dirFixture{filename: digest, owner: "", title: "", modified: 1282448091}.record(),
		dirFixture{filename: deleted, owner: "jjkkll9", title: "(本文已被刪除) [jjkkll9]", modified: 1285539357}.record(),
		dirFixture{filename: removed, owner: "-", title: "(本文已被移除) <userA>", modified: 1285539400}.record(),
		dirFixture{filename: oldDel, owner: "userA", title: "(已被SYSOP刪除) <userA>", modified: 1085539400}.record(),
	}
	files := map[string][]byte{
		digest:  legacyArticle("userD", "[文摘] 本週精選"),
		deleted: legacyArticle("jjkkll9", "[新聞] 科博館"),
		removed: legacyArticle("userA", "x"),
		oldDel:  legacyArticle("userA", "y"),
	}
	writeDir(t, tempDir, "PttDigest", recs, files)
	engine := openImportEngine(t, tempDir)
	opts := ImportBoardOptions{BBSHome: tempDir, Board: "PttDigest", IsBig5: true}
	stats, err := ImportBoard(engine, opts)
	if err != nil {
		t.Fatal(err)
	}
	if stats.PostsImported != 1 {
		t.Errorf("imported %d posts, want only the digest", stats.PostsImported)
	}
	p, err := engine.GetPostByCommunityFile("PttDigest", digest)
	if err != nil || p.CreatorName != "userD" || p.Title != "[文摘] 本週精選" {
		t.Errorf("digest = %+v, %v", p, err)
	}
	for _, n := range []string{deleted, removed, oldDel} {
		if _, err := ImportPost(engine, opts, n); !errors.Is(err, ErrPostNotFound) {
			t.Errorf("ImportPost(%s): %v, want ErrPostNotFound", n, err)
		}
	}
	for _, title := range []string{"(本文已被刪除)", "Re: (已被刪除)", "(已被移除的) 文"} {
		if got := isDeletedTitle(title); got != (title == "(本文已被刪除)") {
			t.Errorf("isDeletedTitle(%q) = %v", title, got)
		}
	}
}

// A community imported from a board is dated from its oldest post, also
// when fixed afterwards; a later post leaves the date as it is.
func TestCommunityPublished(t *testing.T) {
	tempDir := t.TempDir()
	const (
		older = "M.1100000000.A.001"
		newer = "M.1200000000.A.002"
	)
	writeDir(t, tempDir, "Old", [][]byte{
		dirFixture{filename: newer, owner: "userA", title: "t", modified: 1200000000}.record(),
		dirFixture{filename: older, owner: "userA", title: "t", modified: 1100000000}.record(),
	}, map[string][]byte{newer: legacyArticle("userA", "t"), older: legacyArticle("userA", "t")})
	engine := openImportEngine(t, tempDir)
	if _, err := ImportBoard(engine, ImportBoardOptions{BBSHome: tempDir, Board: "Old", IsBig5: true}); err != nil {
		t.Fatal(err)
	}
	c, ok := engine.Communities().GetByName("Old")
	if !ok || c.Published != 1100000000 {
		t.Errorf("published %d, want the oldest post 1100000000", c.Published)
	}

	// Registered at import time, as before: fixed without importing.
	id, err := engine.Communities().Ensure("Empty")
	if err != nil {
		t.Fatal(err)
	}
	before, _ := engine.Communities().Get(id)
	checked, fixed, err := FixCommunityPublished(engine)
	if err != nil || checked != 2 || fixed != 0 {
		t.Errorf("FixCommunityPublished = %d %d %v, want 2 checked, none fixed", checked, fixed, err)
	}
	if after, _ := engine.Communities().Get(id); after.Published != before.Published {
		t.Errorf("community without posts changed: %d -> %d", before.Published, after.Published)
	}
	if changed, err := engine.Communities().LowerPublished(c.ID, 1300000000); err != nil || changed {
		t.Errorf("LowerPublished to a later time = %v, %v", changed, err)
	}
	if changed, err := engine.Communities().LowerPublished(c.ID, 1000000000); err != nil || !changed {
		t.Errorf("LowerPublished = %v, %v", changed, err)
	}
	if _, fixed, _ := FixCommunityPublished(engine, "Old"); fixed != 0 {
		t.Errorf("fixed again")
	}
}
