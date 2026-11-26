package importer

import (
	"bytes"
	"errors"
	"os"
	"path/filepath"
	"testing"
	"time"

	"pttbbs/big5uao"
	"pttbbs/nest/model"
	"pttbbs/nest/storage"
)

// legacyArticle is an article as mbbsd writes it, in Big5, with colored
// headers and one push.
func legacyArticle(owner, title string) []byte {
	return big5uao.Encode("\x1b[1;37;44m 作者 \x1b[0;34;47m " + owner + " (暱稱) \x1b[1;37;44m 看板 \x1b[0;34;47m ImpBoard \x1b[m\n" +
		"\x1b[1;37;44m 標題 \x1b[0;34;47m " + title + " \x1b[m\n" +
		"\x1b[1;37;44m 時間 \x1b[0;34;47m Fri Oct  4 08:00:01 2024 \x1b[m\n" +
		"\n本文\n\n--\n※ 發信站: 批踢踢實業坊(ptt.cc), 來自: 1.2.3.4\n" +
		"\x1b[1;37m推 \x1b[33muserB\x1b[m\x1b[33m:好文                            \x1b[m 10/05 12:34\n" +
		"\x1b[1;31m→ \x1b[33muserB\x1b[m\x1b[33m:補充                            \x1b[m 10/05 12:35\n")
}

func openImportEngine(t *testing.T, tempDir string) storage.Storage {
	t.Helper()
	engine, err := storage.OpenEngine(storage.Config{
		BBSHome:        tempDir,
		DataDir:        filepath.Join(tempDir, "engine"),
		CacheDir:       filepath.Join(tempDir, "cache"),
		FilterEncoding: "big5",
	})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { engine.Close() })
	return engine
}

// ImportPost imports one legacy post the way ImportBoard does: from its
// .DIR record (Big5 title, owner, filemode, money, modified, locked),
// created_at from its file name, its author's person ID, and its comments
// with times rolled from the post's year (not 1970).
func TestImportPostFromDir(t *testing.T) {
	tempDir := t.TempDir()
	boardDir := filepath.Join(tempDir, "boards", "I", "ImpBoard")
	if err := os.MkdirAll(boardDir, 0755); err != nil {
		t.Fatal(err)
	}
	const (
		live   = "M.1728000001.A.001"
		locked = "M.1728000002.A.002"
		gone   = "M.1728000003.A.003"
		noDir  = "M.1728000004.A.004"
	)
	recs := [][]byte{
		dirFixture{filename: live, owner: "userA", title: "[問題] 測試標題", modified: 1728000500, filemode: model.FileMarked, multi: 50}.record(),
		dirFixture{filename: model.LockedListName(locked), owner: "userA", title: "鎖住的文", modified: 1728000600}.record(),
		dirFixture{filename: ".d1728000003.A.003", owner: "-", title: "(本文已被刪除) [userA]", modified: 1728000700}.record(),
	}
	if err := os.WriteFile(filepath.Join(boardDir, ".DIR"), bytes.Join(recs, nil), 0644); err != nil {
		t.Fatal(err)
	}
	files := map[string][]byte{
		live:                         legacyArticle("userA", "[問題] 測試標題"),
		model.LockedFileName(locked): legacyArticle("userA", "鎖住的文"),
		noDir:                        legacyArticle("userC", "還沒進 .DIR"),
	}
	for name, content := range files {
		if err := os.WriteFile(filepath.Join(boardDir, name), content, 0644); err != nil {
			t.Fatal(err)
		}
	}
	mtime := time.Unix(1728000900, 0)
	if err := os.Chtimes(filepath.Join(boardDir, noDir), mtime, mtime); err != nil {
		t.Fatal(err)
	}

	engine := openImportEngine(t, tempDir)
	opts := ImportBoardOptions{
		BBSHome: tempDir,
		Board:   "ImpBoard",
		IsBig5:  true,
		Users:   fakeUsers{"userA": {1, 1600000000}, "userB": {2, 1600000000}},
	}

	p, err := ImportPost(engine, opts, live)
	if err != nil {
		t.Fatalf("ImportPost(%s): %v", live, err)
	}
	if p.Title != "[問題] 測試標題" || p.CreatorName != "userA" || p.CreatorID != model.MakePersonID(1, 1600000000) {
		t.Errorf("title/creator = %q %q %d", p.Title, p.CreatorName, p.CreatorID)
	}
	if p.CreatedAt != 1728000001 || p.Modified != 1728000500 || p.Filemode != model.FileMarked || p.Money != 50 || p.Locked {
		t.Errorf("record fields: created %d modified %d filemode %d money %d locked %v", p.CreatedAt, p.Modified, p.Filemode, p.Money, p.Locked)
	}
	comments, err := engine.GetCommentsByCommunityFileFiltered("ImpBoard", live, 1, 100, "", 0)
	if err != nil || len(comments) != 1 {
		t.Fatalf("comments = %d, %v; want the push and its arrow as one floor", len(comments), err)
	}
	if y := time.Unix(comments[0].CreatedAt, 0).Year(); y != 2024 {
		t.Errorf("comment time %d is in %d, want 2024", comments[0].CreatedAt, y)
	}
	if comments[0].CreatorID != model.MakePersonID(2, 1600000000) || comments[0].LegacyType != model.LegacyTypePush {
		t.Errorf("comment = %+v", comments[0])
	}
	if p.Upvotes != 1 || p.Downvotes != 0 {
		t.Errorf("votes = +%d -%d, want +1 from the push", p.Upvotes, p.Downvotes)
	}
	// Importing again returns the same post.
	if again, err := ImportPost(engine, opts, live); err != nil || again.ID != p.ID {
		t.Errorf("second ImportPost = %+v, %v", again, err)
	}
	// The file is left as it was.
	if got, _ := os.ReadFile(filepath.Join(boardDir, live)); !bytes.Equal(got, files[live]) {
		t.Errorf("ImportPost changed the article file")
	}

	// A locked post, by its L. name (as in .DIR) or its X. file name.
	for _, name := range []string{model.LockedListName(locked), model.LockedFileName(locked)} {
		lp, err := ImportPost(engine, opts, name)
		if err != nil || lp.PostFile != locked || !lp.Locked || lp.Title != "鎖住的文" {
			t.Errorf("ImportPost(%s) = %+v, %v", name, lp, err)
		}
	}

	// A safe-deleted post is not imported on demand.
	if _, err := ImportPost(engine, opts, gone); !errors.Is(err, ErrPostNotFound) {
		t.Errorf("ImportPost of a safe-deleted post: %v, want ErrPostNotFound", err)
	}
	// ... unless it is to be restored, with its article put back.
	if _, err := ImportRestoredPost(engine, opts, gone); !errors.Is(err, ErrPostNotFound) {
		t.Errorf("ImportRestoredPost without the article: %v, want ErrPostNotFound", err)
	}
	if err := os.WriteFile(filepath.Join(boardDir, gone), legacyArticle("userA", "還原的文"), 0644); err != nil {
		t.Fatal(err)
	}
	if rp, err := ImportRestoredPost(engine, opts, gone); err != nil || rp.Title != "還原的文" || rp.CreatorName != "userA" || rp.DeletedOrRemoved() {
		t.Errorf("ImportRestoredPost = %+v, %v", rp, err)
	}
	if _, err := ImportPost(engine, opts, "M.1728000099.A.099"); !errors.Is(err, ErrPostNotFound) {
		t.Errorf("ImportPost of a missing post: %v, want ErrPostNotFound", err)
	}

	// Not in .DIR yet: owner and title from the (Big5, colored) headers.
	np, err := ImportPost(engine, opts, noDir)
	if err != nil {
		t.Fatalf("ImportPost(%s): %v", noDir, err)
	}
	if np.CreatorName != "userC" || np.Title != "還沒進 .DIR" || np.CreatedAt != 1728000004 || np.Modified != 1728000900 {
		t.Errorf("post from its headers = %q %q created %d modified %d", np.CreatorName, np.Title, np.CreatedAt, np.Modified)
	}
}

func TestArticleHeader(t *testing.T) {
	for _, tc := range []struct {
		raw          []byte
		owner, title string
	}{
		{legacyArticle("userA", "[問卦] 有沒有"), "userA", "[問卦] 有沒有"},
		{[]byte("作者: userB (B) 看板: Test\n標題: Re: hello\n時間: now\n"), "userB", "Re: hello"},
		{[]byte("Author: userC\nTitle: plain\n"), "userC", "plain"},
		{[]byte("no headers\n"), "", ""},
	} {
		owner, title := ArticleHeader(tc.raw, true)
		if owner != tc.owner || title != tc.title {
			t.Errorf("ArticleHeader(%q) = %q, %q; want %q, %q", tc.raw, owner, title, tc.owner, tc.title)
		}
	}
}

// TestPostCtimeInTheFuture checks that a post whose file name has a stamp
// later than now (a clock gone wrong) is dated by its file instead.
func TestPostCtimeInTheFuture(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "M.2085983221.A")
	if err := os.WriteFile(path, []byte("x"), 0644); err != nil {
		t.Fatal(err)
	}
	written := time.Date(2001, 10, 12, 15, 47, 1, 0, time.UTC)
	if err := os.Chtimes(path, written, written); err != nil {
		t.Fatal(err)
	}
	if got := postCtimeOf(&FileHeader{Filename: "M.2085983221.A"}, path); got != written.Unix() {
		t.Errorf("future stamp: ctime = %d, want the time of the file %d", got, written.Unix())
	}
	if got := postCtimeOf(&FileHeader{Filename: "M.1002901621.A"}, path); got != 1002901621 {
		t.Errorf("past stamp: ctime = %d, want the stamp", got)
	}
}
