package main

import (
	"bufio"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"

	"pttbbs/big5uao"
	"pttbbs/nest/daemon"
	"pttbbs/nest/importer"
	"pttbbs/nest/model"
	"pttbbs/nest/storage"
)

// testUsers is an importer.UserResolver of a fixed set of accounts.
type testUsers map[string]struct {
	usernum    int
	firstlogin uint32
}

func (u testUsers) LookupUserid(userid string) (int, uint32, bool) {
	e, ok := u[userid]
	return e.usernum, e.firstlogin, ok
}

func (u testUsers) LookupUsernum(usernum int) (string, uint32, bool) {
	for name, e := range u {
		if e.usernum == usernum {
			return name, e.firstlogin, true
		}
	}
	return "", 0, false
}

// legacyRecord is a fileheader_t record as mbbsd writes it to .DIR.
func legacyRecord(filename, owner, title string, modified uint32, filemode uint16, multi uint32) []byte {
	rec := make([]byte, importer.FileHeaderSize)
	copy(rec[0:28], filename)
	putU32 := func(off int, v uint32) {
		rec[off], rec[off+1], rec[off+2], rec[off+3] = byte(v), byte(v>>8), byte(v>>16), byte(v>>24)
	}
	putU32(28, modified)
	copy(rec[34:48], owner)
	copy(rec[54:119], big5uao.Encode(title))
	putU32(120, multi)
	rec[124], rec[125] = byte(filemode), byte(filemode>>8)
	return rec
}

func legacyArticleText(owner, title string) []byte {
	return big5uao.Encode("\x1b[1;37;44m 作者 \x1b[0;34;47m " + owner + " (暱稱) \x1b[1;37;44m 看板 \x1b[0;34;47m Legacy \x1b[m\n" +
		"\x1b[1;37;44m 標題 \x1b[0;34;47m " + title + " \x1b[m\n" +
		"\n本文\n\n--\n※ 發信站: 批踢踢實業坊(ptt.cc)\n" +
		"\x1b[1;37m推 \x1b[33muserB\x1b[m\x1b[33m:好文                            \x1b[m 10/05 12:34\n")
}

// Commands on legacy posts (never imported) import them first from .DIR,
// and the fixes for the nest backend of mbbsd.
func TestServerLegacyPosts(t *testing.T) {
	tmpDir := t.TempDir()
	bbsHome := filepath.Join(tmpDir, "bbshome")
	boardDir := filepath.Join(bbsHome, "boards", "L", "Legacy")
	if err := os.MkdirAll(boardDir, 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Join(bbsHome, "tmp"), 0755); err != nil {
		t.Fatal(err)
	}
	post := func(n int) string { return fmt.Sprintf("M.17280000%02d.A.%03d", n, n) }
	type legacy struct {
		owner, title string
		filemode     uint16
		multi        uint32
	}
	posts := map[int]legacy{
		1: {"userA", "版本測試", 0, 0},
		2: {"userA", "鎖文測試", 0, 0},
		3: {"oldUser", "大小寫刪文", 0, 0},
		4: {"Anonymous.", "匿名文", model.FileAnonymous, 1},
		5: {"userA", "改標題", 0, 0},
		6: {"userA", "設定", 0, 0},
		7: {"userA", "重設分數", 0, 0},
		8: {"userA", "列出推文", 0, 0},
		9: {"userA", "取得文章", 0, 0},
	}
	var dir []byte
	for n := 1; n <= 9; n++ {
		l := posts[n]
		dir = append(dir, legacyRecord(post(n), l.owner, l.title, uint32(1728000500+n), l.filemode, l.multi)...)
		if err := os.WriteFile(filepath.Join(boardDir, post(n)), legacyArticleText(l.owner, l.title), 0644); err != nil {
			t.Fatal(err)
		}
	}
	// Post 10 was deleted before the switch (its record is safe-deleted),
	// and mbbsd has put its article back to restore it.
	dir = append(dir, legacyRecord(".d1728000010.A.010", "-", "(本文已被刪除) [userA]", 1728000600, 0, 0)...)
	if err := os.WriteFile(filepath.Join(boardDir, post(10)), legacyArticleText("userA", "舊文還原"), 0644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(boardDir, ".DIR"), dir, 0644); err != nil {
		t.Fatal(err)
	}

	st, err := storage.OpenEngine(storage.Config{
		BBSHome:        bbsHome,
		DataDir:        filepath.Join(tmpDir, "data"),
		CacheDir:       "boards",
		CacheSizeMB:    16,
		FilterEncoding: "big5",
	})
	if err != nil {
		t.Fatal(err)
	}
	defer st.Close()
	users := testUsers{"userA": {1, 1600000000}, "userB": {2, 1600000000}}
	sockPath := filepath.Join(tmpDir, "post.sock")
	srv := daemon.NewServer(daemon.ServerConfig{BBSHome: bbsHome, UnixSocket: sockPath, Users: users}, st)
	if err := srv.Start(); err != nil {
		t.Fatal(err)
	}
	defer srv.Stop()

	// ipc sends cmd and returns the first line of the reply and what
	// follows it.
	ipc := func(cmd string) (string, string) {
		t.Helper()
		conn, err := net.Dial("unix", sockPath)
		if err != nil {
			t.Fatal(err)
		}
		defer conn.Close()
		conn.Write([]byte(cmd + "\n"))
		conn.SetReadDeadline(time.Now().Add(2 * time.Second))
		r := bufio.NewReader(conn)
		line, _ := r.ReadString('\n')
		rest, _ := io.ReadAll(r)
		return strings.TrimSpace(line), string(rest)
	}
	get := func(n int) *model.Post {
		t.Helper()
		p, err := st.GetPostByCommunityFile("Legacy", post(n))
		if err != nil {
			t.Fatalf("post %d not in nest.svc: %v", n, err)
		}
		return p
	}

	// FETCH_POST_FILE to "-" only tells the version: no file is written,
	// and the version (the .DIR modified) is the same each time.
	resp, title := ipc("FETCH_POST_FILE Legacy " + post(1) + " -")
	if resp != "OK 1728000501 "+strconv.Itoa(len("版本測試")) || title != "版本測試" {
		t.Fatalf("FETCH_POST_FILE -: %q %q", resp, title)
	}
	if again, _ := ipc("FETCH_POST_FILE Legacy " + post(1) + " -"); again != resp {
		t.Errorf("version changed: %q -> %q", resp, again)
	}
	if _, err := os.Stat(filepath.Join(bbsHome, "-")); err == nil {
		t.Errorf("FETCH_POST_FILE - wrote a file")
	}
	p1 := get(1)
	if p1.CreatorName != "userA" || p1.CreatorID != model.MakePersonID(1, 1600000000) || p1.CreatedAt != 1728000001 {
		t.Errorf("imported post 1: %+v", p1)
	}
	// Targets outside BBSHOME (or not regular files) are refused.
	for _, target := range []string{"/dev/null", filepath.Join(tmpDir, "outside.txt"), filepath.Join(bbsHome, "tmp")} {
		if resp, _ := ipc("FETCH_POST_FILE Legacy " + post(1) + " " + target); !strings.HasPrefix(resp, "ERR") {
			t.Errorf("FETCH_POST_FILE to %s: %q, want ERR", target, resp)
		}
	}
	if fi, err := os.Stat("/dev/null"); err != nil || fi.Mode().IsRegular() {
		t.Fatalf("/dev/null was replaced")
	}
	if resp, _ := ipc("FETCH_POST_FILE Legacy " + post(1) + " tmp/edit.txt"); !strings.HasPrefix(resp, "OK 1728000501 ") {
		t.Errorf("FETCH_POST_FILE to tmp/edit.txt: %q", resp)
	}
	if body, err := os.ReadFile(filepath.Join(bbsHome, "tmp", "edit.txt")); err != nil || !strings.Contains(big5uao.Decode(body), "本文") {
		t.Errorf("fetched body: %q, %v", body, err)
	}

	// GET_COMMENTS_FILE gives "-" for a comment without an IP.
	resp, rest := ipc("GET_COMMENTS_FILE Legacy " + post(1))
	if !strings.HasPrefix(resp, "OK 1 ") {
		t.Fatalf("GET_COMMENTS_FILE: %q", resp)
	}
	if fields := strings.Split(rest, "\t"); len(fields) < 4 || fields[3] != "-" {
		t.Errorf("comment line %q: ip field is not -", rest)
	}

	// A moderator may edit someone else's comment (mbbsd's mask); an
	// edited comment has <created_at>/<modified_at>.
	if resp, _ := ipc("EDIT_COMMENT_FILE Legacy " + post(1) + " 1 sysop 0 6\nmasked"); resp != "OK 1" {
		t.Fatalf("EDIT_COMMENT_FILE by a moderator: %q", resp)
	}
	_, rest = ipc("GET_COMMENTS_FILE Legacy " + post(1))
	if fields := strings.Split(rest, "\t"); len(fields) < 8 || !strings.Contains(fields[2], "/") || !strings.HasPrefix(fields[7], "masked") {
		t.Errorf("edited comment line %q: want <ctime>/<modified>", rest)
	}

	// LOCK_POST_FILE on a legacy post imports it, then locks it.
	if resp, _ := ipc("LOCK_POST_FILE Legacy " + post(2) + " 1"); resp != "OK" {
		t.Fatalf("LOCK_POST_FILE: %q", resp)
	}
	if p := get(2); !p.Locked {
		t.Errorf("post 2 not locked")
	}
	xName, lName := model.LockedFileName(post(2)), model.LockedListName(post(2))
	if _, err := os.Stat(filepath.Join(boardDir, xName)); err != nil {
		t.Errorf("locked file %s missing: %v", xName, err)
	}
	// mbbsd renders a locked post by its .DIR name (L.): the board file
	// (X.) is written, and no stray L. file appears.
	if resp, _ := ipc("RENDER_FILE Legacy " + lName + " " + filepath.Join(boardDir, lName) + " modern"); !strings.HasPrefix(resp, "OK") {
		t.Fatalf("RENDER_FILE of a locked post: %q", resp)
	}
	if _, err := os.Stat(filepath.Join(boardDir, lName)); err == nil {
		t.Errorf("RENDER_FILE wrote a stray %s", lName)
	}
	if _, err := os.Stat(filepath.Join(boardDir, xName)); err != nil {
		t.Errorf("RENDER_FILE did not write %s: %v", xName, err)
	}

	// The creator of an imported post whose account cannot be resolved
	// (creator_id 0) is matched by name, ignoring case.
	if resp, _ := ipc("DELETE_POST_FILE Legacy " + post(3) + " someone 0"); resp != "ERR no_post_edit_allowed" {
		t.Errorf("DELETE_POST_FILE by another user: %q", resp)
	}
	if resp, _ := ipc("DELETE_POST_FILE Legacy " + post(3) + " OLDUSER 0"); !strings.HasPrefix(resp, "OK") {
		t.Errorf("DELETE_POST_FILE by the creator in another case: %q", resp)
	}
	if p := get(3); !p.Deleted || p.Removed {
		t.Errorf("post 3 deletion = %+v, want deleted by its creator", p.Deletion)
	}
	// An anonymous post is deleted by its real account, not by its
	// nickname.
	if resp, _ := ipc("DELETE_POST_FILE Legacy " + post(4) + " Anonymous. 0"); resp != "ERR no_post_edit_allowed" {
		t.Errorf("DELETE_POST_FILE of an anonymous post by its nickname: %q", resp)
	}
	if resp, _ := ipc("DELETE_POST_FILE Legacy " + post(4) + " userA 0"); !strings.HasPrefix(resp, "OK") {
		t.Errorf("DELETE_POST_FILE of an anonymous post by its author: %q", resp)
	}
	if p := get(4); !p.Deleted || p.Removed {
		t.Errorf("post 4 deletion = %+v, want deleted by its creator", p.Deletion)
	}

	// RECOVER_POST_FILE of a post deleted before the switch imports it
	// from its article (put back by mbbsd).
	if _, err := st.GetPostByCommunityFile("Legacy", post(10)); err == nil {
		t.Fatalf("post 10 imported too early")
	}
	if resp, _ := ipc("RECOVER_POST_FILE Legacy " + post(10)); resp != "OK" {
		t.Fatalf("RECOVER_POST_FILE of a legacy deleted post: %q", resp)
	}
	if p := get(10); p.DeletedOrRemoved() || p.Title != "舊文還原" || p.CreatorName != "userA" {
		t.Errorf("restored post 10: %+v", p)
	}
	if resp, _ := ipc("RECOVER_POST_FILE Legacy M.1728009999.A.999"); !strings.HasPrefix(resp, "ERR") {
		t.Errorf("RECOVER_POST_FILE of a missing post: %q", resp)
	}

	// The other *_FILE commands import legacy posts too.
	for _, tc := range []struct{ cmd, want string }{
		{"EDIT_POST_TITLE_FILE Legacy " + post(5) + " userA 0 3\nabc", "OK "},
		{"SET_META_FILE Legacy " + post(6) + " 2 10", "OK"},
		{"RESET_POST_SCORE_FILE Legacy " + post(7), "OK"},
		{"LIST_POST_LIKES_FILE Legacy " + post(8), "OK 1"},
		{"GET_POST_FILE Legacy " + post(9), "OK "},
	} {
		if resp, _ := ipc(tc.cmd); !strings.HasPrefix(resp, tc.want) {
			t.Errorf("%s: %q, want %s", strings.SplitN(tc.cmd, "\n", 2)[0], resp, tc.want)
		}
	}
	if p := get(5); p.Title != "abc" {
		t.Errorf("post 5 title = %q", p.Title)
	}
	if p := get(6); p.Filemode != 2 || p.Money != 10 {
		t.Errorf("post 6 filemode/money = %d/%d", p.Filemode, p.Money)
	}
	if resp, _ := ipc("GET_POST_FILE Legacy M.1728009999.A.999"); !strings.HasPrefix(resp, "ERR") {
		t.Errorf("GET_POST_FILE of a missing post: %q", resp)
	}
}
