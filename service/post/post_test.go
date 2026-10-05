package main

import (
	"bufio"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"pttbbs/big5uao"
	"pttbbs/post/config"
	"pttbbs/post/daemon"
	"pttbbs/post/importer"
	"pttbbs/post/model"
	"pttbbs/post/storage"
)

func TestStorageEngineAllFeatures(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "post_svc_test_*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tmpDir)

	dataDir := filepath.Join(tmpDir, "data")
	cacheDir := filepath.Join(tmpDir, "cache")

	engine, err := storage.OpenEngine(storage.Config{
		BBSHome:        filepath.Join(tmpDir, "bbshome"),
		DataDir:        dataDir,
		CacheDir:       cacheDir,
		CacheSizeMB:    32,
		FlushSeconds:   1,
		FilterEncoding: "utf-8",
	})
	if err != nil {
		t.Fatalf("OpenEngine failed: %v", err)
	}
	defer engine.Close()

	// 1. Create a Post in "gossiping" with AuthorToken
	p := &model.Post{
		Community:   "gossiping",
		PostFile:    "M.1727800000.A.001",
		Title:       "[爆卦] Reddit化架構測試成功",
		Author:      "piaip",
		AuthorToken: 1112607986, // cuser.firstlogin
		CreatedAt:   time.Now().Unix(),
		Content:     "這是純粹 UTF-8 儲存的文章本文內容。\n支持多行與繁體中文。\n",
	}

	created, err := engine.CreatePost(p)
	if err != nil {
		t.Fatalf("CreatePost failed: %v", err)
	}
	if created.ID == 0 || created.AuthorToken != 1112607986 {
		t.Fatalf("Expected non-zero PostID and valid AuthorToken, got %+v", created)
	}

	// 2. Add Pure Comments (Decoupled from Votes, RFC 822 stored)
	c1, err := engine.AddComment(&model.Comment{
		PostID:      created.ID,
		Author:      "spammer123",
		AuthorToken: 777666555,
		Content:     "買茶加LINE: abc12345 (廣告)",
		IP:          "1.2.3.4",
		CreatedAt:   time.Now().Unix(),
	})
	if err != nil || c1.Sequence != 1 || c1.AuthorToken != 777666555 {
		t.Fatalf("AddComment 1 failed: %v, seq: %d, token: %d", err, c1.Sequence, c1.AuthorToken)
	}

	c2, err := engine.AddComment(&model.Comment{
		PostID:    created.ID,
		Author:    "userB",
		Content:   "好棒的架構！",
		IP:        "127.0.0.2",
		CreatedAt: time.Now().Unix(),
	})
	if err != nil || c2.Sequence != 2 {
		t.Fatalf("AddComment 2 failed: %v, seq: %d", err, c2.Sequence)
	}

	c3, err := engine.AddComment(&model.Comment{
		PostID:    created.ID,
		Author:    "spammer123",
		Content:   "高價收購帳號LINE (廣告2)",
		IP:        "1.2.3.4",
		CreatedAt: time.Now().Unix(),
	})
	if err != nil || c3.Sequence != 3 {
		t.Fatalf("AddComment 3 failed: %v, seq: %d", err, c3.Sequence)
	}

	// 3. Test Votes (One Vote per user enforced in votes.pebble, tracks AuthorToken)
	// userB Upvotes
	up, down, err := engine.VotePost(created.ID, "userB", 10001, model.VoteUp)
	if err != nil || up != 1 || down != 0 {
		t.Fatalf("Vote 1 failed: up=%d, down=%d, err=%v", up, down, err)
	}
	// userB duplicates Upvote -> should be no-op (0, 0)
	up, down, err = engine.VotePost(created.ID, "userB", 10001, model.VoteUp)
	if err != nil || up != 0 || down != 0 {
		t.Fatalf("Duplicate Vote should be 0 delta, got up=%d, down=%d", up, down)
	}

	// userC Downvotes
	up, down, err = engine.VotePost(created.ID, "userC", 10002, model.VoteDown)
	if err != nil || up != 0 || down != 1 {
		t.Fatalf("Vote Down failed: up=%d, down=%d", up, down)
	}

	// userC changes mind to Upvote -> upDelta = +1, downDelta = -1
	up, down, err = engine.VotePost(created.ID, "userC", 10002, model.VoteUp)
	if err != nil || up != 1 || down != -1 {
		t.Fatalf("Change Vote failed: up=%d, down=%d", up, down)
	}

	// 4. Test PurgeUserComments (Delete all comments from spammer123 within 7 days)
	purged, err := engine.PurgeUserComments("spammer123", 7*24*time.Hour)
	if err != nil {
		t.Fatalf("PurgeUserComments failed: %v", err)
	}
	if purged != 2 {
		t.Fatalf("Expected 2 comments purged, got %d", purged)
	}

	// Verify comments after purge: floor 1 and 3 should be marked deleted
	comments, err := engine.GetComments(created.ID, 1, 10)
	if err != nil || len(comments) != 3 {
		t.Fatalf("Expected 3 comments returned, got %d", len(comments))
	}
	if !comments[0].IsDeleted || comments[1].IsDeleted || !comments[2].IsDeleted {
		t.Fatalf("Purge status mismatch: [0]=%v, [1]=%v, [2]=%v", comments[0].IsDeleted, comments[1].IsDeleted, comments[2].IsDeleted)
	}
	if comments[0].AuthorToken != 777666555 {
		t.Fatalf("Expected comment[0] AuthorToken 777666555, got %d", comments[0].AuthorToken)
	}

	// 5. Test SGR 66 一字雙色 Conversion
	// Construct a split DBCS Big5 character: "中" (\xa4 \xa4) split by ANSI \x1b[31m
	splitDBCS := []byte{0xa4, 0x1b, '[', '3', '1', 'm', 0xa4}
	sgr66Converted := big5uao.DecodeSGR66(splitDBCS)
	expectedPrefix := "\x1b[66;31m中"
	if sgr66Converted != expectedPrefix {
		t.Fatalf("Expected SGR 66 conversion %q, got %q", expectedPrefix, sgr66Converted)
	}
	t.Logf("SGR 66 conversion verified: %q -> %q", splitDBCS, sgr66Converted)

	// 6. Test ParentID and Reply Posts (回文)
	reply1, err := engine.CreatePost(&model.Post{
		ParentID:    created.ID,
		Community:   "gossiping",
		PostFile:    "M.1727800000.A.002",
		Title:       "Re: [爆卦] Reddit化架構測試成功",
		Author:      "Mayaman",
		AuthorToken: 222333444,
		CreatedAt:   time.Now().Unix(),
		Content:     "大家好，我是馬雅人。關於這個架構，我從馬雅曆法的角度補充幾點...\n",
	})
	if err != nil || reply1.ParentID != created.ID {
		t.Fatalf("Create reply 1 failed: %v, parent: %d", err, reply1.ParentID)
	}

	reply2, err := engine.CreatePost(&model.Post{
		ParentID:    created.ID,
		Community:   "gossiping",
		PostFile:    "M.1727800000.A.003",
		Title:       "Re: [爆卦] Reddit化架構測試成功",
		Author:      "expertDoc",
		AuthorToken: 333444555,
		CreatedAt:   time.Now().Unix(),
		Content:     "身為系統工程師，我也來回文分析一下...\n",
	})
	if err != nil || reply2.ParentID != created.ID {
		t.Fatalf("Create reply 2 failed: %v", err)
	}

	// Test GetReplies
	replies, err := engine.GetReplies(created.ID, 10, 0)
	if err != nil || len(replies) != 2 {
		t.Fatalf("Expected 2 replies, got %d", len(replies))
	}

	// Test GetThread (root + 2 replies = 3)
	thread, err := engine.GetThread(created.ID)
	if err != nil || len(thread) != 3 {
		t.Fatalf("Expected 3 posts in thread, got %d", len(thread))
	}
	t.Logf("Thread verified! Root + %d replies", len(replies))

	// 6.5. Test Auto-Demotion of short 1-line reply into a comment
	spamReply, err := engine.CreatePost(&model.Post{
		ParentID:    created.ID,
		Community:   "gossiping",
		PostFile:    "M.1727800000.A.004",
		Title:       "Re: [爆卦] Reddit化架構測試成功",
		Author:      "oneLiner",
		AuthorToken: 999888777,
		CreatedAt:   time.Now().Unix(),
		Content:     ": 引言好長好長...\n: 引言好長好長...\n真的。(只有這幾個字)\n",
	})
	if err != nil {
		t.Fatalf("Create spamReply failed: %v", err)
	}
	if !spamReply.DemotedToComment || spamReply.ID != 0 {
		t.Fatalf("Expected spam reply to be auto-demoted to comment, got %+v", spamReply)
	}
	t.Logf("Spam reply successfully auto-demoted to comment on parent post %d!", created.ID)

	// 6.8. Test PurgeUserPosts (Delete all posts from spammer123 within 7 days)
	spamPost, err := engine.CreatePost(&model.Post{
		Community:   "gossiping",
		PostFile:    "M.1727800000.A.099",
		Title:       "[廣告] 買帳號送現金LINE: spam999",
		Author:      "spammer123",
		AuthorToken: 777666555,
		CreatedAt:   time.Now().Unix(),
		Content:     "大量收購PTT帳號與發文，意者加LINE...\n",
	})
	if err != nil {
		t.Fatalf("Create spamPost failed: %v", err)
	}

	purgedPosts, err := engine.PurgeUserPosts("spammer123", 7*24*time.Hour)
	if err != nil || purgedPosts != 1 {
		t.Fatalf("PurgeUserPosts failed: %v, purged: %d", err, purgedPosts)
	}

	// Verify spamPost is gone from SQLite
	_, err = engine.GetPost(spamPost.ID)
	if err == nil {
		t.Fatalf("Expected spamPost %d to be deleted from SQLite", spamPost.ID)
	}
	t.Logf("Spam post %d successfully purged!", spamPost.ID)

	// 7. Test Disaster Recovery Rebuild (Rebuild SQLite from Pebble)
	postsRebuilt, commentsRebuilt, err := engine.RebuildSQLiteFromPebble()
	if err != nil {
		t.Fatalf("RebuildSQLiteFromPebble failed: %v", err)
	}
	if postsRebuilt != 3 || commentsRebuilt != 2 {
		t.Fatalf("Rebuild count mismatch: posts=%d (expected 3), comments=%d (expected 2)", postsRebuilt, commentsRebuilt)
	}

	rebuiltPost, err := engine.GetPost(created.ID)
	if err != nil {
		t.Fatalf("GetPost failed: %v", err)
	}
	// userB (+1) and userC (+1) = 2 upvotes, 0 downvotes, 2 active comments
	if rebuiltPost.Upvotes != 2 || rebuiltPost.Downvotes != 0 || rebuiltPost.NumComments != 2 {
		t.Fatalf("Rebuilt post score mismatch: up=%d, down=%d, comments=%d", rebuiltPost.Upvotes, rebuiltPost.Downvotes, rebuiltPost.NumComments)
	}
	if rebuiltPost.AuthorToken != 1112607986 {
		t.Fatalf("Rebuilt post AuthorToken mismatch: %d", rebuiltPost.AuthorToken)
	}

	// Verify reply ParentID is preserved after rebuild
	rebuiltReply, err := engine.GetPost(reply1.ID)
	if err != nil || rebuiltReply.ParentID != created.ID {
		t.Fatalf("Rebuilt reply ParentID mismatch: %d (expected %d)", rebuiltReply.ParentID, created.ID)
	}
	t.Logf("Disaster recovery verified! Rebuilt Root Post: %+v, Reply ParentID: %d", rebuiltPost, rebuiltReply.ParentID)

	// 8. Test Comment Cache formatting & RenderFullPostText (No ANSI, No Escape sequences)
	cachePost, err := engine.CreatePost(&model.Post{
		Community: "testboard",
		PostFile:  "M.1727800000.A.888",
		Title:     "推文排版測試",
		Author:    "testuser",
		CreatedAt: time.Now().Unix(),
		Content:   "文章本文第一行\n--\n※ 發信站: 批踢踢實業坊\n",
	})
	if err != nil {
		t.Fatalf("CreatePost failed: %v", err)
	}

	_, err = engine.AddComment(&model.Comment{
		PostID:    cachePost.ID,
		Author:    "commenter1",
		Content:   "\x1b[1;31m這是彩色推文\x1b[m第一行\n\x1b[32m第二行也是彩色\x1b[m",
		IP:        "192.168.1.1",
		CreatedAt: time.Now().Unix(),
	})
	if err != nil {
		t.Fatalf("AddComment with ANSI failed: %v", err)
	}

	// Verify Cache File on disk
	cachePath := engine.CacheFilePath(cachePost)
	cacheData, err := os.ReadFile(cachePath)
	if err != nil {
		t.Fatalf("ReadFile cache failed: %v", err)
	}
	cacheStr := string(cacheData)

	// Cache must NOT contain ANSI escape \x1b
	if strings.Contains(cacheStr, "\x1b") {
		t.Fatalf("Cache file should not contain any ANSI escape characters, got: %q", cacheStr)
	}
	// Cache must contain floor header [1] commenter1 ... 192.168.1.1
	if !strings.Contains(cacheStr, "[1] commenter1 ") || !strings.Contains(cacheStr, "192.168.1.1 ") {
		t.Fatalf("Cache file header missing expected floor info, got: %s", cacheStr)
	}
	// Cache must contain 2-space indented lines with stripped ANSI
	if !strings.Contains(cacheStr, "  這是彩色推文第一行\n") || !strings.Contains(cacheStr, "  第二行也是彩色\n") {
		t.Fatalf("Cache file comment lines not properly indented or ANSI not stripped, got: %s", cacheStr)
	}

	// Verify RenderFullPostText
	renderedBytes, err := engine.RenderFullPostText(cachePost.ID, false)
	if err != nil {
		t.Fatalf("RenderFullPostText failed: %v", err)
	}
	renderedStr := string(renderedBytes)
	if strings.Contains(renderedStr, "\x1b") {
		t.Fatalf("RenderFullPostText should not contain ANSI escape codes, got: %q", renderedStr)
	}
	if !strings.Contains(renderedStr, "  這是彩色推文第一行\n") {
		t.Fatalf("RenderFullPostText missing comment body: %s", renderedStr)
	}
	t.Logf("Cache file formatting and RenderFullPostText verified cleanly:\n%s", cacheStr)

	// 8. Verify Fallback when Content was initially empty in database
	emptyPost, err := engine.CreatePost(&model.Post{
		Community: "testbrd",
		PostFile:  "M.1727800000.A.EMPTY",
		Title:     "空內文補讀測試",
		Author:    "testuser",
		CreatedAt: time.Now().Unix(),
		Content:   "", // initially empty
	})
	if err != nil {
		t.Fatalf("CreatePost empty failed: %v", err)
	}
	// Write real content to BBS boards directory
	diskBrdDir := filepath.Join(tmpDir, "bbshome", "boards", "t", "testbrd")
	os.MkdirAll(diskBrdDir, 0755)
	os.WriteFile(filepath.Join(diskBrdDir, "M.1727800000.A.EMPTY"), []byte("這是磁碟上的補讀本文\n"), 0644)
	engine.AddComment(&model.Comment{
		PostID:    emptyPost.ID,
		Author:    "commenter2",
		Content:   "推文內容",
		CreatedAt: time.Now().Unix(),
	})
	renderedEmpty, err := engine.RenderFullPostText(emptyPost.ID, false)
	if err != nil {
		t.Fatalf("RenderFullPostText fallback failed: %v", err)
	}
	if !strings.Contains(string(renderedEmpty), "這是磁碟上的補讀本文") ||
		!strings.Contains(string(renderedEmpty), "推文內容") {
		t.Fatalf("Fallback reload failed to recover body: %s", string(renderedEmpty))
	}
	t.Logf("Fallback reload recovered body and comment verified: %s", string(renderedEmpty))
}

func TestServerIPC(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "post_server_test_*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tmpDir)

	sockPath := filepath.Join(tmpDir, "post.sock")
	st, err := storage.OpenEngine(storage.Config{
		DataDir:     filepath.Join(tmpDir, "data"),
		CacheDir:    filepath.Join(tmpDir, "cache"),
		CacheSizeMB: 16,
	})
	if err != nil {
		t.Fatal(err)
	}
	defer st.Close()

	srv := daemon.NewServer(daemon.ServerConfig{
		BBSHome:    filepath.Join(tmpDir, "bbshome"),
		UnixSocket: sockPath,
	}, st)

	if err := srv.Start(); err != nil {
		t.Fatal(err)
	}
	defer srv.Stop()

	// 1. Send Ping
	conn, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatalf("Dial unix socket failed: %v", err)
	}
	defer conn.Close()

	conn.Write([]byte("PING\n"))
	r := bufio.NewReader(conn)
	resp, _ := r.ReadString('\n')
	if strings.TrimSpace(resp) != "PONG" {
		t.Fatalf("Expected PONG, got %q", resp)
	}

	// 2. Create Post via IPC (Protocol A: POST)
	conn2, _ := net.Dial("unix", sockPath)
	defer conn2.Close()
	titleB := big5uao.Encode("[閒聊] 新世代 post.svc 上線")
	contentB := big5uao.Encode("全站 Reddit 化與 Pebble+SQLite 混合儲存測試。\n")
	hdr := fmt.Sprintf("POST c_chat M.1727800000.A.002 hungte 12345678 0 0 0 %d %d\n", len(titleB), len(contentB))
	conn2.Write([]byte(hdr))
	conn2.Write(titleB)
	conn2.Write(contentB)

	r2 := bufio.NewReader(conn2)
	pResp, _ := r2.ReadString('\n')
	if !strings.HasPrefix(pResp, "OK") {
		t.Fatalf("create_post failed: %s", pResp)
	}
	t.Logf("Create post via IPC succeeded: %s", strings.TrimSpace(pResp))

	// Verify post 1 in database has correct UTF-8 title and content
	p1, err := st.GetPost(1)
	if err != nil {
		t.Fatalf("GetPost(1) failed: %v", err)
	}
	if p1.Title != "[閒聊] 新世代 post.svc 上線" {
		t.Fatalf("Expected UTF-8 title, got: %q", p1.Title)
	}

	// 3. Test create_post_file (Protocol A: POST_FILE)
	boardDir := filepath.Join(tmpDir, "bbshome", "boards", "g", "gossiping")
	os.MkdirAll(boardDir, 0755)
	filePath := filepath.Join(boardDir, "M.1727800000.A.003")
	os.WriteFile(filePath, big5uao.Encode("這是由 mbbsd 寫入磁碟的文章檔案內容。\n--\n※ 發信站: 批踢踢實業坊\n"), 0644)

	conn3, _ := net.Dial("unix", sockPath)
	defer conn3.Close()
	title3B := big5uao.Encode("[爆卦] 檔案直入 post.svc 測試")
	hdr3 := fmt.Sprintf("POST_FILE gossiping M.1727800000.A.003 hungte 888999 0 0 %d\n", len(title3B))
	conn3.Write([]byte(hdr3))
	conn3.Write(title3B)

	r3 := bufio.NewReader(conn3)
	fResp, _ := r3.ReadString('\n')
	if !strings.HasPrefix(fResp, "OK") {
		t.Fatalf("create_post_file failed: %s", fResp)
	}
	t.Logf("create_post_file via IPC succeeded: %s", strings.TrimSpace(fResp))

	// Verify post in database has correct UTF-8 title
	p2, err := st.GetPostByCommunityFile("gossiping", "M.1727800000.A.003")
	if err != nil {
		t.Fatalf("GetPostByCommunityFile failed: %v", err)
	}
	if p2.Title != "[爆卦] 檔案直入 post.svc 測試" {
		t.Fatalf("Expected UTF-8 title, got: %q", p2.Title)
	}

	// 4. Test add_comment (Protocol A: COMMENT)
	conn4, _ := net.Dial("unix", sockPath)
	defer conn4.Close()
	commentB := big5uao.Encode("這是純推文，完全不帶 vote 資訊")
	hdr4 := fmt.Sprintf("COMMENT gossiping M.1727800000.A.003 piaip 777111 140.112.1.1 0 %d\n", len(commentB))
	conn4.Write([]byte(hdr4))
	conn4.Write(commentB)

	r4 := bufio.NewReader(conn4)
	cResp, _ := r4.ReadString('\n')
	if !strings.HasPrefix(cResp, "OK") {
		t.Fatalf("add_comment by file failed: %s", cResp)
	}
	t.Logf("add_comment via IPC succeeded: %s", strings.TrimSpace(cResp))

	// Verify comment in database
	comments, err := st.GetComments(p2.ID, 1, 10)
	if err != nil || len(comments) != 1 {
		t.Fatalf("Expected 1 comment, got: %v (err: %v)", comments, err)
	}
	if comments[0].Content != "這是純推文，完全不帶 vote 資訊" {
		t.Fatalf("Expected comment content '這是純推文，完全不帶 vote 資訊', got: %q", comments[0].Content)
	}
	if comments[0].Sequence != 1 {
		t.Fatalf("Expected comment Sequence 1, got: %d", comments[0].Sequence)
	}
	if comments[0].AuthorToken != 777111 {
		t.Fatalf("Expected AuthorToken 777111, got: %d", comments[0].AuthorToken)
	}

	// 5. Test vote_post (Protocol A: VOTE_FILE and VOTE by ID)
	conn5, _ := net.Dial("unix", sockPath)
	defer conn5.Close()
	conn5.Write([]byte("VOTE_FILE gossiping M.1727800000.A.003 voter_user 999123 1\n"))

	r5 := bufio.NewReader(conn5)
	vResp, _ := r5.ReadString('\n')
	if !strings.HasPrefix(vResp, "OK") {
		t.Fatalf("vote_post by file failed: %s", vResp)
	}
	t.Logf("VOTE_FILE via IPC succeeded: %s", strings.TrimSpace(vResp))

	conn5b, _ := net.Dial("unix", sockPath)
	defer conn5b.Close()
	conn5b.Write([]byte("VOTE gossiping 2 voter_user2 999124 -1\n"))
	r5b := bufio.NewReader(conn5b)
	vResp2, _ := r5b.ReadString('\n')
	if !strings.HasPrefix(vResp2, "OK") {
		t.Fatalf("VOTE by ID failed: %s", vResp2)
	}
	t.Logf("VOTE by ID via IPC succeeded: %s", strings.TrimSpace(vResp2))

	// 6. Test LIST community posts (LIST <community> [count [start]])
	conn6, _ := net.Dial("unix", sockPath)
	defer conn6.Close()
	conn6.Write([]byte("LIST gossiping 10 0\n"))
	r6 := bufio.NewReader(conn6)
	listResp, _ := r6.ReadString('\n')
	if !strings.HasPrefix(listResp, "OK 1") {
		t.Fatalf("LIST gossiping failed: %s", listResp)
	}
	itemLine, _ := r6.ReadString('\n')
	cols := strings.Split(strings.TrimRight(itemLine, "\r\n"), "\t")
	if len(cols) < 10 {
		t.Fatalf("Expected at least 10 columns in LIST output, got: %v", cols)
	}
	if cols[1] != "M.1727800000.A.003" || cols[2] != "hungte" || cols[9] != "[爆卦] 檔案直入 post.svc 測試" {
		t.Fatalf("LIST columns mismatch: %v", cols)
	}
	t.Logf("LIST via IPC succeeded: %s -> %s", strings.TrimSpace(listResp), strings.TrimSpace(itemLine))

	// 7. Test RENDER by ID to stdout (RENDER <post_id> -)
	conn7, _ := net.Dial("unix", sockPath)
	defer conn7.Close()
	conn7.Write([]byte("RENDER 2 -\n"))
	r7 := bufio.NewReader(conn7)
	renderResp, _ := r7.ReadString('\n')
	if !strings.HasPrefix(renderResp, "OK") {
		t.Fatalf("RENDER stdout failed: %s", renderResp)
	}
	renderedBytes, _ := io.ReadAll(r7)
	decodedRendered := big5uao.DecodeSGR66(renderedBytes)
	if !strings.Contains(decodedRendered, "這是由 mbbsd 寫入磁碟的文章檔案內容。") ||
		!strings.Contains(decodedRendered, "這是純推文，完全不帶 vote 資訊") {
		t.Fatalf("Rendered content missing post or comment: %q", decodedRendered)
	}
	t.Logf("RENDER by ID to stdout succeeded, length=%d bytes", len(renderedBytes))

	// 8. Test RENDER by ID to disk file (RENDER <post_id> <output_path>)
	renderedOutPath := filepath.Join(tmpDir, "rendered_output_id.txt")
	conn8, _ := net.Dial("unix", sockPath)
	defer conn8.Close()
	conn8.Write([]byte(fmt.Sprintf("RENDER 2 %s\n", renderedOutPath)))
	r8 := bufio.NewReader(conn8)
	renderFileResp, _ := r8.ReadString('\n')
	if !strings.HasPrefix(renderFileResp, "OK") {
		t.Fatalf("RENDER by ID to file failed: %s", renderFileResp)
	}
	fileBytes, err := os.ReadFile(renderedOutPath)
	if err != nil {
		t.Fatalf("Failed to read rendered file from disk: %v", err)
	}
	decodedFile := big5uao.DecodeSGR66(fileBytes)
	if !strings.Contains(decodedFile, "這是由 mbbsd 寫入磁碟的文章檔案內容。") ||
		!strings.Contains(decodedFile, "這是純推文，完全不帶 vote 資訊") {
		t.Fatalf("Rendered file content mismatch: %q", decodedFile)
	}
	t.Logf("RENDER by ID to disk file succeeded: %s (%d bytes)", renderedOutPath, len(fileBytes))

	// 9. Test RENDER_FILE by community and post_file (RENDER_FILE <community> <post_file> <output_path>)
	renderedFileOutPath := filepath.Join(tmpDir, "rendered_output_file.txt")
	conn9, _ := net.Dial("unix", sockPath)
	defer conn9.Close()
	conn9.Write([]byte(fmt.Sprintf("RENDER_FILE gossiping M.1727800000.A.003 %s\n", renderedFileOutPath)))
	r9 := bufio.NewReader(conn9)
	rfResp, _ := r9.ReadString('\n')
	if !strings.HasPrefix(rfResp, "OK") {
		t.Fatalf("RENDER_FILE to file failed: %s", rfResp)
	}
	rfBytes, err := os.ReadFile(renderedFileOutPath)
	if err != nil {
		t.Fatalf("Failed to read rendered_file file from disk: %v", err)
	}
	decodedRF := big5uao.DecodeSGR66(rfBytes)
	if !strings.Contains(decodedRF, "這是由 mbbsd 寫入磁碟的文章檔案內容。") ||
		!strings.Contains(decodedRF, "這是純推文，完全不帶 vote 資訊") {
		t.Fatalf("Rendered file content mismatch: %q", decodedRF)
	}
	t.Logf("RENDER_FILE to disk file succeeded: %s (%d bytes)", renderedFileOutPath, len(rfBytes))

	// 10. Test UPDATE_POST and POST_HISTORY (history.pebble)
	conn10, _ := net.Dial("unix", sockPath)
	defer conn10.Close()
	newTitleB := big5uao.Encode("[閒聊] 新世代 post.svc 上線 (已修改)")
	newContentB := big5uao.Encode("修改後的新內文。\n")
	hdr10 := fmt.Sprintf("UPDATE_POST 1 sysop %d %d\n", len(newTitleB), len(newContentB))
	conn10.Write([]byte(hdr10))
	conn10.Write(newTitleB)
	conn10.Write(newContentB)
	r10 := bufio.NewReader(conn10)
	editResp, _ := r10.ReadString('\n')
	if !strings.HasPrefix(editResp, "OK 1") {
		t.Fatalf("UPDATE_POST failed: %s", editResp)
	}

	// Query post history list
	conn11, _ := net.Dial("unix", sockPath)
	defer conn11.Close()
	conn11.Write([]byte("POST_HISTORY 1 0\n"))
	r11 := bufio.NewReader(conn11)
	histListResp, _ := r11.ReadString('\n')
	if !strings.HasPrefix(histListResp, "OK 1") {
		t.Fatalf("POST_HISTORY list failed: %s", histListResp)
	}
	histLine, _ := r11.ReadString('\n')
	if !strings.Contains(histLine, "[閒聊] 新世代 post.svc 上線") {
		t.Fatalf("Expected original title in history: %s", histLine)
	}

	// Query specific revision 1
	conn12, _ := net.Dial("unix", sockPath)
	defer conn12.Close()
	conn12.Write([]byte("POST_HISTORY 1 1\n"))
	r12 := bufio.NewReader(conn12)
	rev1Resp, _ := r12.ReadString('\n')
	if !strings.HasPrefix(rev1Resp, "OK 1 1") {
		t.Fatalf("POST_HISTORY rev 1 failed: %s", rev1Resp)
	}
	rev1Payload, _ := io.ReadAll(r12)
	if !strings.Contains(string(rev1Payload), "全站 Reddit 化與 Pebble+SQLite 混合儲存測試。") {
		t.Fatalf("History revision 1 content missing original text: %s", string(rev1Payload))
	}
	t.Logf("UPDATE_POST and POST_HISTORY verified: %s", strings.TrimSpace(histLine))

	// 11. Test UPDATE_COMMENT and COMMENT_HISTORY (history.pebble)
	conn13, _ := net.Dial("unix", sockPath)
	defer conn13.Close()
	newCommentB := big5uao.Encode("修改後的推文內容")
	hdr13 := fmt.Sprintf("UPDATE_COMMENT 2 1 sysop %d\n", len(newCommentB))
	conn13.Write([]byte(hdr13))
	conn13.Write(newCommentB)
	r13 := bufio.NewReader(conn13)
	editCResp, _ := r13.ReadString('\n')
	if !strings.HasPrefix(editCResp, "OK 1") {
		t.Fatalf("UPDATE_COMMENT failed: %s", editCResp)
	}

	conn14, _ := net.Dial("unix", sockPath)
	defer conn14.Close()
	conn14.Write([]byte("COMMENT_HISTORY 2 1 1\n"))
	r14 := bufio.NewReader(conn14)
	cHistResp, _ := r14.ReadString('\n')
	if !strings.HasPrefix(cHistResp, "OK 2 1 1") {
		t.Fatalf("COMMENT_HISTORY rev 1 failed: %s", cHistResp)
	}
	cHistPayload, _ := io.ReadAll(r14)
	if !strings.Contains(string(cHistPayload), "這是純推文，完全不帶 vote 資訊") {
		t.Fatalf("Comment history missing original text: %s", string(cHistPayload))
	}
	t.Logf("UPDATE_COMMENT and COMMENT_HISTORY verified: %s", string(cHistPayload))

	// 15. CROSSPOST via IPC
	conn15, _ := net.Dial("unix", sockPath)
	defer conn15.Close()
	conn15.Write([]byte("CROSSPOST gossiping M.1727800000.A.003 test M.1727800000.A.999 operator1 12345 1791048300\n"))
	r15 := bufio.NewReader(conn15)
	cpResp, _ := r15.ReadString('\n')
	if !strings.HasPrefix(cpResp, "OK 1 1") {
		t.Fatalf("CROSSPOST failed: %s", cpResp)
	}

	// 16. CROSSPOSTS query via IPC
	conn16, _ := net.Dial("unix", sockPath)
	defer conn16.Close()
	conn16.Write([]byte("CROSSPOSTS gossiping M.1727800000.A.003\n"))
	r16 := bufio.NewReader(conn16)
	cpListHdr, _ := r16.ReadString('\n')
	if !strings.HasPrefix(cpListHdr, "OK 1") {
		t.Fatalf("CROSSPOSTS query failed: %s", cpListHdr)
	}
	cpListBody, _ := io.ReadAll(r16)
	if !strings.Contains(string(cpListBody), "test\tM.1727800000.A.999\toperator1") {
		t.Fatalf("CROSSPOSTS list missing record: %s", string(cpListBody))
	}

	// 17. ORIGIN query via IPC
	conn17, _ := net.Dial("unix", sockPath)
	defer conn17.Close()
	conn17.Write([]byte("ORIGIN test M.1727800000.A.999\n"))
	r17 := bufio.NewReader(conn17)
	originResp, _ := r17.ReadString('\n')
	if !strings.HasPrefix(originResp, "OK gossiping\tM.1727800000.A.003") {
		t.Fatalf("ORIGIN query failed: %s", originResp)
	}
	t.Logf("CROSSPOST, CROSSPOSTS, and ORIGIN IPC verified: %s", strings.TrimSpace(originResp))

	// 18. FETCH_POST_FILE via IPC
	pulledTarget := filepath.Join(tmpDir, "pulled_post.txt")
	conn18, _ := net.Dial("unix", sockPath)
	defer conn18.Close()
	conn18.Write([]byte(fmt.Sprintf("FETCH_POST_FILE gossiping M.1727800000.A.003 %s\n", pulledTarget)))
	r18 := bufio.NewReader(conn18)
	pullResp, _ := r18.ReadString('\n')
	if !strings.HasPrefix(pullResp, "OK ") {
		t.Fatalf("FETCH_POST_FILE failed: %s", pullResp)
	}
	var origModified int64
	var titleLen int
	fmt.Sscanf(pullResp, "OK %d %d", &origModified, &titleLen)
	if origModified <= 0 {
		t.Fatalf("Expected positive modified timestamp from FETCH_POST_FILE, got %d", origModified)
	}
	pulledBytes, err := os.ReadFile(pulledTarget)
	if err != nil || len(pulledBytes) == 0 {
		t.Fatalf("Read pulled file failed: %v", err)
	}
	t.Logf("FETCH_POST_FILE verified: %d bytes pulled, modified=%d", len(pulledBytes), origModified)

	// 19. UPDATE_POST_FILE with no change returns OK 0 NO_CHANGE
	conn19, _ := net.Dial("unix", sockPath)
	defer conn19.Close()
	noChangeContent := string(pulledBytes)
	noChangeHdr := fmt.Sprintf("UPDATE_POST_FILE gossiping M.1727800000.A.003 sysop %d 0 %d\n%s", origModified, len(noChangeContent), noChangeContent)
	conn19.Write([]byte(noChangeHdr))
	r19 := bufio.NewReader(conn19)
	noChangeResp, _ := r19.ReadString('\n')
	if !strings.HasPrefix(noChangeResp, "OK 0") {
		t.Fatalf("Expected OK 0 for no change, got: %s", noChangeResp)
	}
	t.Logf("UPDATE_POST_FILE no-change verified: %s", strings.TrimSpace(noChangeResp))

	// 20. OCC Test: First edit updates post and bumps modified timestamp
	conn20a, _ := net.Dial("unix", sockPath)
	defer conn20a.Close()
	editedContent1 := string(pulledBytes) + "\n這是第一個編輯者追加的內容\n"
	edit1Hdr := fmt.Sprintf("UPDATE_POST_FILE gossiping M.1727800000.A.003 bob %d 0 %d\n%s", origModified, len(editedContent1), editedContent1)
	conn20a.Write([]byte(edit1Hdr))
	r20a := bufio.NewReader(conn20a)
	edit1Resp, _ := r20a.ReadString('\n')
	if !strings.HasPrefix(edit1Resp, "OK 1 ") {
		t.Fatalf("Expected OK 1 for first edit, got: %s", edit1Resp)
	}
	var rev1 int
	var newModified int64
	fmt.Sscanf(edit1Resp, "OK %d %d", &rev1, &newModified)
	if newModified <= origModified {
		t.Fatalf("Expected newModified (%d) > origModified (%d)", newModified, origModified)
	}

	// Concurrent edit with stale origModified must return ERR CONFLICT <newModified>
	conn20b, _ := net.Dial("unix", sockPath)
	defer conn20b.Close()
	editedContent2 := string(pulledBytes) + "\n這是衝突的第二個編輯者內容\n"
	edit2Hdr := fmt.Sprintf("UPDATE_POST_FILE gossiping M.1727800000.A.003 alice %d 0 %d\n%s", origModified, len(editedContent2), editedContent2)
	conn20b.Write([]byte(edit2Hdr))
	r20b := bufio.NewReader(conn20b)
	conflictResp, _ := r20b.ReadString('\n')
	expectedConflict := fmt.Sprintf("ERR CONFLICT %d\n", newModified)
	if conflictResp != expectedConflict {
		t.Fatalf("Expected conflict '%s', got: '%s'", expectedConflict, conflictResp)
	}
	t.Logf("OCC Conflict verified: %s", strings.TrimSpace(conflictResp))

	// Retrying with latest modified timestamp must succeed (force overwrite)
	conn20c, _ := net.Dial("unix", sockPath)
	defer conn20c.Close()
	retryHdr := fmt.Sprintf("UPDATE_POST_FILE gossiping M.1727800000.A.003 alice %d 0 %d\n%s", newModified, len(editedContent2), editedContent2)
	conn20c.Write([]byte(retryHdr))
	r20c := bufio.NewReader(conn20c)
	retryResp, _ := r20c.ReadString('\n')
	if !strings.HasPrefix(retryResp, "OK 2 ") {
		t.Fatalf("Expected OK 2 for retry edit, got: %s", retryResp)
	}
	t.Logf("OCC Overwrite retry verified: %s", strings.TrimSpace(retryResp))

	// 21. DELETE_COMMENT_FILE via IPC
	conn21, _ := net.Dial("unix", sockPath)
	defer conn21.Close()
	conn21.Write([]byte("DELETE_COMMENT_FILE gossiping M.1727800000.A.003 1 sysop 違規廣告\n"))
	r21 := bufio.NewReader(conn21)
	delCommentResp, _ := r21.ReadString('\n')
	if !strings.HasPrefix(delCommentResp, "OK ") {
		t.Fatalf("DELETE_COMMENT_FILE failed: %s", delCommentResp)
	}

	// 22. RENDER_FILE to check deleted comment notice
	conn22, _ := net.Dial("unix", sockPath)
	defer conn22.Close()
	conn22.Write([]byte("RENDER_FILE gossiping M.1727800000.A.003 -\n"))
	r22 := bufio.NewReader(conn22)
	delCommentRenderHdr, _ := r22.ReadString('\n')
	if !strings.HasPrefix(delCommentRenderHdr, "OK") {
		t.Fatalf("RENDER_FILE after delete comment failed: %s", delCommentRenderHdr)
	}
	delCommentRenderBody, _ := io.ReadAll(r22)
	decodedDelComment := big5uao.DecodeSGR66(delCommentRenderBody)
	if !strings.Contains(decodedDelComment, "違規廣告") {
		t.Fatalf("RENDER_FILE missing deleted comment reason: %s", decodedDelComment)
	}
	t.Logf("DELETE_COMMENT_FILE and tombstone render verified: %s", decodedDelComment)

	// 23. UNDELETE_COMMENT_FILE via IPC
	conn23, _ := net.Dial("unix", sockPath)
	defer conn23.Close()
	conn23.Write([]byte("UNDELETE_COMMENT_FILE gossiping M.1727800000.A.003 1\n"))
	r23 := bufio.NewReader(conn23)
	undelCommentResp, _ := r23.ReadString('\n')
	if strings.TrimSpace(undelCommentResp) != "OK" {
		t.Fatalf("UNDELETE_COMMENT_FILE failed: %s", undelCommentResp)
	}

	// 24. DELETE_POST_FILE via IPC
	conn24, _ := net.Dial("unix", sockPath)
	defer conn24.Close()
	conn24.Write([]byte("DELETE_POST_FILE gossiping M.1727800000.A.003 sysop 板規刪除\n"))
	r24 := bufio.NewReader(conn24)
	delPostResp, _ := r24.ReadString('\n')
	if !strings.HasPrefix(delPostResp, "OK ") {
		t.Fatalf("DELETE_POST_FILE failed: %s", delPostResp)
	}

	// 25. LIST should not include deleted post
	conn25, _ := net.Dial("unix", sockPath)
	defer conn25.Close()
	conn25.Write([]byte("LIST gossiping 10 0\n"))
	r25 := bufio.NewReader(conn25)
	listHdr25, _ := r25.ReadString('\n')
	listBody25, _ := io.ReadAll(r25)
	if strings.Contains(string(listBody25), "M.1727800000.A.003") {
		t.Fatalf("LIST should not contain deleted post: %s\n%s", listHdr25, string(listBody25))
	}

	// 26. LIST gossiping 10 0 deleted should include deleted post
	conn26, _ := net.Dial("unix", sockPath)
	defer conn26.Close()
	conn26.Write([]byte("LIST gossiping 10 0 deleted\n"))
	r26 := bufio.NewReader(conn26)
	listHdr26, _ := r26.ReadString('\n')
	listBody26, _ := io.ReadAll(r26)
	if !strings.Contains(string(listBody26), "M.1727800000.A.003") {
		t.Fatalf("LIST deleted should contain deleted post: %s\n%s", listHdr26, string(listBody26))
	}
	t.Logf("LIST deleted verified: %s\n%s", listHdr26, string(listBody26))

	// 27. UNDELETE_POST_FILE via IPC
	conn27, _ := net.Dial("unix", sockPath)
	defer conn27.Close()
	conn27.Write([]byte("UNDELETE_POST_FILE gossiping M.1727800000.A.003\n"))
	r27 := bufio.NewReader(conn27)
	undelPostResp, _ := r27.ReadString('\n')
	if strings.TrimSpace(undelPostResp) != "OK" {
		t.Fatalf("UNDELETE_POST_FILE failed: %s", undelPostResp)
	}

	// 28. LIST should now contain undeleted post again
	conn28, _ := net.Dial("unix", sockPath)
	defer conn28.Close()
	conn28.Write([]byte("LIST gossiping 10 0\n"))
	r28 := bufio.NewReader(conn28)
	r28.ReadString('\n')
	listBody28, _ := io.ReadAll(r28)
	if !strings.Contains(string(listBody28), "M.1727800000.A.003") {
		t.Fatalf("LIST should contain restored post: %s", string(listBody28))
	}
	t.Logf("UNDELETE_POST_FILE and LIST restore verified: %s", string(listBody28))

	// 29. UPDATE_POST_TITLE_FILE via IPC
	conn29, _ := net.Dial("unix", sockPath)
	defer conn29.Close()
	newTitleB = big5uao.Encode("[爆卦] 標題修改測試成功")
	hdr29 := fmt.Sprintf("UPDATE_POST_TITLE_FILE gossiping M.1727800000.A.003 sysop %d\n", len(newTitleB))
	conn29.Write([]byte(hdr29))
	conn29.Write(newTitleB)
	r29 := bufio.NewReader(conn29)
	upTitleResp, _ := r29.ReadString('\n')
	if !strings.HasPrefix(upTitleResp, "OK ") {
		t.Fatalf("UPDATE_POST_TITLE_FILE failed: %s", upTitleResp)
	}
	t.Logf("UPDATE_POST_TITLE_FILE verified: %s", strings.TrimSpace(upTitleResp))

	// 30. COMMENTS_FILE query via IPC
	conn30, _ := net.Dial("unix", sockPath)
	defer conn30.Close()
	conn30.Write([]byte("COMMENTS_FILE gossiping M.1727800000.A.003 1 10\n"))
	r30 := bufio.NewReader(conn30)
	cmtsHdr, _ := r30.ReadString('\n')
	if !strings.HasPrefix(cmtsHdr, "OK ") {
		t.Fatalf("COMMENTS_FILE failed: %s", cmtsHdr)
	}
	cmtsBody, _ := io.ReadAll(r30)
	if !strings.Contains(string(cmtsBody), "piaip") {
		t.Fatalf("COMMENTS_FILE list missing piaip: %s", string(cmtsBody))
	}
	t.Logf("COMMENTS_FILE verified: %s\n%s", strings.TrimSpace(cmtsHdr), string(cmtsBody))

	// 30b. COMMENTS_FILE query filtered by author
	conn30b, _ := net.Dial("unix", sockPath)
	defer conn30b.Close()
	conn30b.Write([]byte("COMMENTS_FILE gossiping M.1727800000.A.003 1 10 piaip -\n"))
	r30b := bufio.NewReader(conn30b)
	cmtsHdrB, _ := r30b.ReadString('\n')
	if !strings.HasPrefix(cmtsHdrB, "OK 1 1") {
		t.Fatalf("COMMENTS_FILE filtered by author failed: %s", cmtsHdrB)
	}

	// 30c. COMMENTS_FILE query filtered by non-existent author
	conn30c, _ := net.Dial("unix", sockPath)
	defer conn30c.Close()
	conn30c.Write([]byte("COMMENTS_FILE gossiping M.1727800000.A.003 1 10 non_existent_user -\n"))
	r30c := bufio.NewReader(conn30c)
	cmtsHdrC, _ := r30c.ReadString('\n')
	if !strings.HasPrefix(cmtsHdrC, "OK 0 0") {
		t.Fatalf("COMMENTS_FILE filtered by non-existent author failed: %s", cmtsHdrC)
	}

	// 30d. Multiline comment and COMMENTS_FILE single-line TSV escaping test
	conn30d, _ := net.Dial("unix", sockPath)
	defer conn30d.Close()
	multilineB := big5uao.Encode("第一行留言\n第二行留言\n第三行留言")
	hdr30d := fmt.Sprintf("COMMENT gossiping M.1727800000.A.003 hungte 12345 127.0.0.1 0 %d 1\n", len(multilineB))
	conn30d.Write([]byte(hdr30d))
	conn30d.Write(multilineB)
	r30d := bufio.NewReader(conn30d)
	cmtRespD, _ := r30d.ReadString('\n')
	if !strings.HasPrefix(cmtRespD, "OK 2") {
		t.Fatalf("Add multiline comment failed: %s", cmtRespD)
	}

	conn30e, _ := net.Dial("unix", sockPath)
	defer conn30e.Close()
	conn30e.Write([]byte("COMMENTS_FILE gossiping M.1727800000.A.003 2 1 hungte -\n"))
	r30e := bufio.NewReader(conn30e)
	cmtsHdrE, _ := r30e.ReadString('\n')
	if !strings.HasPrefix(cmtsHdrE, "OK 1 1") {
		t.Fatalf("COMMENTS_FILE multiline query header failed: %s", cmtsHdrE)
	}
	cmtsBodyE, _ := io.ReadAll(r30e)
	bodyStr := strings.TrimRight(string(cmtsBodyE), "\n")
	tsvLines := strings.Split(bodyStr, "\n")
	if len(tsvLines) != 1 {
		t.Fatalf("Expected exactly 1 TSV line for multiline comment, got %d lines: %q", len(tsvLines), bodyStr)
	}
	if !strings.Contains(tsvLines[0], "第一行留言\t第二行留言\t第三行留言") {
		t.Fatalf("Expected escaped newlines in TSV, got: %s", tsvLines[0])
	}

	// 30f. Test UPDATE_COMMENT_FILE with multiline and verify COMMENTS_FILE returns \r
	conn30f, _ := net.Dial("unix", sockPath)
	defer conn30f.Close()
	multilineUpdateB := big5uao.Encode("修改第一行\n修改第二行\n修改第三行")
	hdr30f := fmt.Sprintf("UPDATE_COMMENT_FILE gossiping M.1727800000.A.003 1 piaip %d\n", len(multilineUpdateB))
	conn30f.Write([]byte(hdr30f))
	conn30f.Write(multilineUpdateB)
	r30f := bufio.NewReader(conn30f)
	upRespF, _ := r30f.ReadString('\n')
	if !strings.HasPrefix(upRespF, "OK") {
		t.Fatalf("UPDATE_COMMENT_FILE with multiline failed: %s", upRespF)
	}

	conn30g, _ := net.Dial("unix", sockPath)
	defer conn30g.Close()
	conn30g.Write([]byte("COMMENTS_FILE gossiping M.1727800000.A.003 1 1 piaip -\n"))
	r30g := bufio.NewReader(conn30g)
	cmtsHdrG, _ := r30g.ReadString('\n')
	if !strings.HasPrefix(cmtsHdrG, "OK 1 1") {
		t.Fatalf("COMMENTS_FILE after multiline update failed: %s", cmtsHdrG)
	}
	cmtsBodyG, _ := io.ReadAll(r30g)
	bodyStrG := strings.TrimRight(string(cmtsBodyG), "\n")
	if !strings.Contains(bodyStrG, "修改第一行	修改第二行	修改第三行") {
		t.Fatalf("Expected \\r multiline in COMMENTS_FILE after update, got: %s", bodyStrG)
	}

	// 31. COMMENT_COUNT_FILE query via IPC
	conn31, _ := net.Dial("unix", sockPath)
	defer conn31.Close()
	conn31.Write([]byte("COMMENT_COUNT_FILE gossiping M.1727800000.A.003\n"))
	r31 := bufio.NewReader(conn31)
	cmtCountResp, _ := r31.ReadString('\n')
	if !strings.HasPrefix(cmtCountResp, "OK 1") {
		t.Fatalf("COMMENT_COUNT_FILE failed: %s", cmtCountResp)
	}
	t.Logf("COMMENT_COUNT_FILE verified: %s", strings.TrimSpace(cmtCountResp))
}

func TestFilterEncodingBig5(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "post_big5_test_*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tmpDir)

	dataDir := filepath.Join(tmpDir, "data")
	cacheDir := filepath.Join(tmpDir, "cache")

	engine, err := storage.OpenEngine(storage.Config{
		DataDir:        dataDir,
		CacheDir:       cacheDir,
		CacheSizeMB:    16,
		FlushSeconds:   1,
		FilterEncoding: "big5", // Default
	})
	if err != nil {
		t.Fatal(err)
	}
	defer engine.Close()

	// 1. Create a Post with UTF-8 content
	p, err := engine.CreatePost(&model.Post{
		Community: "gossiping",
		PostFile:  "M.1727800000.A.999",
		Title:     "繁體中文字串",
		Author:    "hungte",
		Content:   "繁體中文測試內容。\n",
	})
	if err != nil {
		t.Fatal(err)
	}

	// 2. Add Comment with UTF-8
	_, err = engine.AddComment(&model.Comment{
		PostID:  p.ID,
		Author:  "piaip",
		Content: "推推繁體推文",
	})
	if err != nil {
		t.Fatal(err)
	}

	// 3. Verify Cache File on disk is converted to Big5-UAO!
	cachePath := engine.CacheFilePath(p)
	cacheBytes, err := os.ReadFile(cachePath)
	if err != nil {
		t.Fatal(err)
	}

	// Decoding it with big5uao.Decode MUST yield the original UTF-8 text!
	decoded := string(big5uao.Decode(cacheBytes))
	if !strings.Contains(decoded, "繁體中文測試內容") || !strings.Contains(decoded, "推推繁體推文") {
		t.Fatalf("Expected decoded Big5 cache to contain original UTF-8 content, got: %s", decoded)
	}

	// 4. Verify DB (Pebble) is ALWAYS strictly UTF-8!
	fetchedPost, err := engine.GetPost(p.ID)
	if err != nil {
		t.Fatal(err)
	}
	if fetchedPost.Content != "繁體中文測試內容。\n" {
		t.Fatalf("Database content should be strictly UTF-8, got: %q", fetchedPost.Content)
	}
	t.Logf("FilterEncoding=big5 verified: Cache file is Big5-UAO, DB is strictly UTF-8!")
}

func TestCrosspostTracking(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "post_crosspost_test_*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tempDir)

	cfg := storage.Config{
		BBSHome:      tempDir,
		DataDir:      filepath.Join(tempDir, "db"),
		CacheDir:     filepath.Join(tempDir, "cache"),
		CacheSizeMB:  16,
		FlushSeconds: 1,
	}

	engine, err := storage.OpenEngine(cfg)
	if err != nil {
		t.Fatal(err)
	}
	defer engine.Close()

	// 1. Create source post in Gossiping
	srcPost, err := engine.CreatePost(&model.Post{
		Community: "Gossiping",
		PostFile:  "M.1727800000.A.001",
		Title:     "[爆卦] 原創好文",
		Author:    "alice",
		CreatedAt: 1727800000,
		Content:   "這是一篇原創好文本文內容。",
	})
	if err != nil {
		t.Fatal(err)
	}
	if srcPost.NumCrossposts != 0 {
		t.Fatalf("expected 0 crossposts initially, got %d", srcPost.NumCrossposts)
	}

	// 2. Add crosspost to "Test"
	rec1, count1, err := engine.AddCrosspost("Gossiping", "M.1727800000.A.001", "Test", "M.1727800000.A.002", "bob", 12345, 1727800100)
	if err != nil {
		t.Fatal(err)
	}
	if count1 != 1 || rec1.TargetCommunity != "Test" {
		t.Fatalf("unexpected crosspost result: count=%d, rec=%+v", count1, rec1)
	}

	// 3. Add second crosspost to "HatePolitics"
	rec2, count2, err := engine.AddCrosspost("Gossiping", "M.1727800000.A.001", "HatePolitics", "M.1727800000.A.003", "charlie", 67890, 1727800200)
	if err != nil {
		t.Fatal(err)
	}
	if count2 != 2 || rec2.TargetCommunity != "HatePolitics" {
		t.Fatalf("unexpected second crosspost result: count=%d, rec=%+v", count2, rec2)
	}

	// Verify updated source post has NumCrossposts == 2
	refetched, err := engine.GetPost(srcPost.ID)
	if err != nil {
		t.Fatal(err)
	}
	if refetched.NumCrossposts != 2 {
		t.Fatalf("expected 2 crossposts on refetched post, got %d", refetched.NumCrossposts)
	}

	// 4. Query crossposts list
	list, err := engine.GetCrossposts("Gossiping", "M.1727800000.A.001")
	if err != nil {
		t.Fatal(err)
	}
	if len(list) != 2 {
		t.Fatalf("expected 2 crossposts in list, got %d", len(list))
	}
	if list[0].TargetCommunity != "Test" || list[1].TargetCommunity != "HatePolitics" {
		t.Fatalf("unexpected crosspost targets: %+v, %+v", list[0], list[1])
	}

	// 5. Query origin of crossposted post
	originPost, originRec, err := engine.GetOriginPost("Test", "M.1727800000.A.002")
	if err != nil {
		t.Fatal(err)
	}
	if originPost == nil || originPost.ID != srcPost.ID {
		t.Fatalf("expected origin post ID %d, got %+v", srcPost.ID, originPost)
	}
	if originRec.Operator != "bob" {
		t.Fatalf("expected operator bob, got %s", originRec.Operator)
	}

	// 6. Non-crosspost origin query returns nil
	nonOrigin, _, err := engine.GetOriginPost("Gossiping", "M.1727800000.A.001")
	if err != nil {
		t.Fatal(err)
	}
	if nonOrigin != nil {
		t.Fatalf("expected nil for non-crosspost post, got %+v", nonOrigin)
	}

	// 7. Disaster Recovery Rebuild Test: Rebuild SQLite from Pebble
	rebuiltPosts, _, err := engine.RebuildSQLiteFromPebble()
	if err != nil {
		t.Fatal(err)
	}
	if rebuiltPosts != 1 {
		t.Fatalf("expected 1 rebuilt post, got %d", rebuiltPosts)
	}

	postAfterRebuild, err := engine.GetPost(srcPost.ID)
	if err != nil {
		t.Fatal(err)
	}
	if postAfterRebuild.NumCrossposts != 2 {
		t.Fatalf("expected 2 crossposts after rebuild, got %d", postAfterRebuild.NumCrossposts)
	}

	rebuiltList, err := engine.GetCrossposts("Gossiping", "M.1727800000.A.001")
	if err != nil {
		t.Fatal(err)
	}
	if len(rebuiltList) != 2 {
		t.Fatalf("expected 2 crossposts after rebuild, got %d", len(rebuiltList))
	}

	rebuiltOrigin, _, err := engine.GetOriginPost("HatePolitics", "M.1727800000.A.003")
	if err != nil {
		t.Fatal(err)
	}
	if rebuiltOrigin == nil || rebuiltOrigin.ID != srcPost.ID {
		t.Fatalf("expected origin post ID %d after rebuild, got %+v", srcPost.ID, rebuiltOrigin)
	}

	t.Logf("Crosspost tracking, Pebble persistence, and Disaster Recovery verified cleanly!")
}

func TestPostAndCommentDeletion(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "post_delete_test_*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tmpDir)

	dataDir := filepath.Join(tmpDir, "data")
	cacheDir := filepath.Join(tmpDir, "cache")
	bbsHome := filepath.Join(tmpDir, "bbs")

	engine, err := storage.OpenEngine(storage.Config{
		DataDir:        dataDir,
		CacheDir:       cacheDir,
		CacheSizeMB:    16,
		BBSHome:        bbsHome,
		FilterEncoding: "utf-8",
	})
	if err != nil {
		t.Fatal(err)
	}
	defer engine.Close()

	// 1. Create a post with comments
	post, err := engine.CreatePost(&model.Post{
		Community:   "Gossiping",
		PostFile:    "M.1727800000.A.001",
		Title:       "[問卦] 刪除與軟刪除機制測試",
		Author:      "testuser",
		AuthorToken: 12345,
		CreatedAt:   1791000000,
		Content:     "這是一篇測試刪除的文章本文內容。\n第二行內容。",
	})
	if err != nil {
		t.Fatal(err)
	}

	_, _ = engine.AddComment(&model.Comment{
		PostID:    post.ID,
		Author:    "commenter1",
		CreatedAt: 1791000010,
		Content:   "推文第一則",
	})
	_, _ = engine.AddComment(&model.Comment{
		PostID:    post.ID,
		Author:    "spammer",
		CreatedAt: 1791000020,
		Content:   "廣告推文：買點數卡加Line",
	})
	_, _ = engine.AddComment(&model.Comment{
		PostID:    post.ID,
		Author:    "commenter3",
		CreatedAt: 1791000030,
		Content:   "推文第三則",
	})

	// Verify initial state
	engine.FlushDirtyDeltas()
	initialPost, _ := engine.GetPost(post.ID)
	if initialPost.NumComments != 3 {
		t.Fatalf("expected 3 comments, got %d", initialPost.NumComments)
	}

	// 2. Delete Comment #2 (spammer)
	cRev, err := engine.DeleteComment(post.ID, 2, "sysop", "違規廣告推文")
	if err != nil {
		t.Fatalf("DeleteComment failed: %v", err)
	}
	if cRev != 1 {
		t.Fatalf("expected comment revision 1, got %d", cRev)
	}

	// Verify comment history created
	cHist, err := engine.GetCommentHistory(post.ID, 2, 1)
	if err != nil {
		t.Fatalf("GetCommentHistory failed: %v", err)
	}
	if cHist.Content != "廣告推文：買點數卡加Line" {
		t.Fatalf("expected original comment in history, got %s", cHist.Content)
	}
	if cHist.Action != "delete" || cHist.Reason != "違規廣告推文" {
		t.Fatalf("expected action=delete, reason=違規廣告推文, got %+v", cHist)
	}

	// Verify rendered text shows tombstone and preserves sequence
	rendered, err := engine.RenderFullPostText(post.ID, false)
	if err != nil {
		t.Fatal(err)
	}
	rStr := string(rendered)
	if !strings.Contains(rStr, "[1] commenter1") || !strings.Contains(rStr, "[2] spammer") || !strings.Contains(rStr, "[3] commenter3") {
		t.Fatalf("rendered output broke comment sequence:\n%s", rStr)
	}
	if strings.Contains(rStr, "買點數卡") {
		t.Fatalf("rendered output still contains deleted spam content:\n%s", rStr)
	}
	if !strings.Contains(rStr, "違規廣告推文") {
		t.Fatalf("rendered output missing tombstone reason:\n%s", rStr)
	}
	t.Logf("Rendered text with deleted comment:\n%s", rStr)

	// Verify SQLite num_comments was decremented to 2
	updatedPost, _ := engine.GetPost(post.ID)
	if updatedPost.NumComments != 2 {
		t.Fatalf("expected num_comments=2 after comment deletion, got %d", updatedPost.NumComments)
	}

	// 3. Undelete Comment #2
	if err := engine.UndeleteComment(post.ID, 2); err != nil {
		t.Fatalf("UndeleteComment failed: %v", err)
	}
	restoredRender, _ := engine.RenderFullPostText(post.ID, false)
	if !strings.Contains(string(restoredRender), "買點數卡加Line") {
		t.Fatalf("undeleted comment content not restored:\n%s", string(restoredRender))
	}

	// Re-delete comment for subsequent tests
	_, _ = engine.DeleteComment(post.ID, 2, "sysop", "違規廣告推文")

	// 4. Delete Post
	pRev, err := engine.DeletePost(post.ID, "sysop", "板規違規刪除")
	if err != nil {
		t.Fatalf("DeletePost failed: %v", err)
	}
	if pRev != 1 {
		t.Fatalf("expected post revision 1, got %d", pRev)
	}

	// Verify post history created
	pHist, err := engine.GetPostHistory(post.ID, 1)
	if err != nil {
		t.Fatalf("GetPostHistory failed: %v", err)
	}
	if !strings.Contains(pHist.Content, "這是一篇測試刪除的文章本文內容") {
		t.Fatalf("expected original post content in history, got %s", pHist.Content)
	}
	if pHist.Action != "delete" || pHist.Reason != "板規違規刪除" {
		t.Fatalf("expected action=delete, reason=板規違規刪除, got %+v", pHist)
	}

	// Check post state
	delPost, err := engine.GetPost(post.ID)
	if err != nil {
		t.Fatal(err)
	}
	if !delPost.IsDeleted || delPost.DeletedBy != "sysop" || delPost.DeleteReason != "板規違規刪除" {
		t.Fatalf("post not marked deleted properly: %+v", delPost)
	}

	// Check ListPosts: should NOT return deleted post
	activePosts, err := engine.ListPosts("Gossiping", 10, 0)
	if err != nil {
		t.Fatal(err)
	}
	if len(activePosts) != 0 {
		t.Fatalf("expected 0 active posts, got %d", len(activePosts))
	}

	// Check ListDeletedPosts: SHOULD return deleted post
	deletedPosts, err := engine.ListDeletedPosts("Gossiping", 10, 0)
	if err != nil {
		t.Fatal(err)
	}
	if len(deletedPosts) != 1 || deletedPosts[0].ID != post.ID {
		t.Fatalf("expected 1 deleted post, got %+v", deletedPosts)
	}

	// Check rendered output for deleted post
	delRendered, err := engine.RenderFullPostText(post.ID, false)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(delRendered), "板規違規刪除") {
		t.Fatalf("expected deletion notice in render, got: %s", string(delRendered))
	}

	// 5. Test Disaster Recovery Rebuild
	rebuiltPosts, rebuiltComments, err := engine.RebuildSQLiteFromPebble()
	if err != nil {
		t.Fatal(err)
	}
	// rebuiltPosts counts non-deleted posts (0 active posts), and rebuiltComments counts active comments (2)
	if rebuiltPosts != 0 {
		t.Fatalf("expected 0 active rebuilt posts, got %d", rebuiltPosts)
	}
	if rebuiltComments != 2 {
		t.Fatalf("expected 2 active comments after rebuild, got %d", rebuiltComments)
	}

	// Verify post can still be queried from SQLite as deleted after rebuild
	postAfterRebuild, err := engine.GetPost(post.ID)
	if err != nil {
		t.Fatal(err)
	}
	if !postAfterRebuild.IsDeleted || postAfterRebuild.DeleteReason != "板規違規刪除" {
		t.Fatalf("expected post to remain deleted after rebuild, got %+v", postAfterRebuild)
	}

	// 6. Undelete Post
	if err := engine.UndeletePost(post.ID); err != nil {
		t.Fatalf("UndeletePost failed: %v", err)
	}
	undelPost, err := engine.GetPost(post.ID)
	if err != nil {
		t.Fatal(err)
	}
	if undelPost.IsDeleted {
		t.Fatalf("post should not be deleted after undelete: %+v", undelPost)
	}

	restoredActive, err := engine.ListPosts("Gossiping", 10, 0)
	if err != nil {
		t.Fatal(err)
	}
	if len(restoredActive) != 1 {
		t.Fatalf("expected 1 restored post in ListPosts, got %d", len(restoredActive))
	}

	t.Logf("Post and Comment soft-deletion, history archiving, and undeletion verified successfully!")
}

func TestUpdateTitleAndCommentManagement(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "post_title_test_*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tmpDir)

	dataDir := filepath.Join(tmpDir, "data")
	cacheDir := filepath.Join(tmpDir, "cache")
	bbsHome := filepath.Join(tmpDir, "bbs")

	engine, err := storage.OpenEngine(storage.Config{
		DataDir:        dataDir,
		CacheDir:       cacheDir,
		CacheSizeMB:    16,
		BBSHome:        bbsHome,
		FilterEncoding: "utf-8",
	})
	if err != nil {
		t.Fatal(err)
	}
	defer engine.Close()

	// 1. Create post
	p, err := engine.CreatePost(&model.Post{
		Community:   "TestBoard",
		PostFile:    "M.1727800000.A.001",
		Title:       "[舊標題] 原始文章標題",
		Author:      "author1",
		AuthorToken: 1234,
		CreatedAt:   1791000000,
		Content:     "這是文章本文內容。",
	})
	if err != nil {
		t.Fatal(err)
	}

	// 2. Update post title
	rev, err := engine.UpdatePostTitle(p.ID, "[新標題] 已修改文章標題", "mod1")
	if err != nil {
		t.Fatalf("UpdatePostTitle failed: %v", err)
	}
	if rev != 1 {
		t.Fatalf("expected revision 1, got %d", rev)
	}

	// Verify title updated in DB
	updatedP, err := engine.GetPost(p.ID)
	if err != nil {
		t.Fatal(err)
	}
	if updatedP.Title != "[新標題] 已修改文章標題" {
		t.Fatalf("expected updated title, got: %s", updatedP.Title)
	}

	// Verify previous title saved in history
	hist, err := engine.GetPostHistory(p.ID, 1)
	if err != nil {
		t.Fatalf("GetPostHistory failed: %v", err)
	}
	if hist.Title != "[舊標題] 原始文章標題" {
		t.Fatalf("expected old title in history, got: %s", hist.Title)
	}
	if hist.Action != "update_title" || hist.Editor != "mod1" {
		t.Fatalf("expected action=update_title, editor=mod1, got %+v", hist)
	}

	// Update with same title should be no-op (revision 0)
	revNoChange, err := engine.UpdatePostTitle(p.ID, "[新標題] 已修改文章標題", "mod1")
	if err != nil || revNoChange != 0 {
		t.Fatalf("expected rev=0 for identical title, got %d (err: %v)", revNoChange, err)
	}

	// 3. Comment Management: add 5 comments
	for i := 1; i <= 5; i++ {
		_, _ = engine.AddComment(&model.Comment{
			PostID:    p.ID,
			Author:    fmt.Sprintf("user%d", i),
			CreatedAt: int64(1791000000 + i*10),
			Content:   fmt.Sprintf("第 %d 則推文內容", i),
		})
	}
	engine.FlushDirtyDeltas()

	// Query comments
	cmts, err := engine.GetComments(p.ID, 1, 10)
	if err != nil || len(cmts) != 5 {
		t.Fatalf("expected 5 comments, got %d (err: %v)", len(cmts), err)
	}

	cnt, err := engine.GetCommentCount(p.ID)
	if err != nil || cnt != 5 {
		t.Fatalf("expected comment count 5, got %d", cnt)
	}

	// 4. Delete comment #3
	cRev, err := engine.DeleteComment(p.ID, 3, "mod1", "洗板水桶")
	if err != nil || cRev != 1 {
		t.Fatalf("DeleteComment failed: rev=%d, err=%v", cRev, err)
	}

	// Verify comment #3 is marked deleted with reason
	cmtsAfterDel, err := engine.GetComments(p.ID, 1, 10)
	if err != nil || len(cmtsAfterDel) != 5 {
		t.Fatalf("expected 5 comments after delete, got %d", len(cmtsAfterDel))
	}
	if !cmtsAfterDel[2].IsDeleted || cmtsAfterDel[2].DeleteReason != "洗板水桶" {
		t.Fatalf("expected comment #3 to be deleted with reason, got %+v", cmtsAfterDel[2])
	}
	if cmtsAfterDel[0].IsDeleted || cmtsAfterDel[1].IsDeleted || cmtsAfterDel[3].IsDeleted {
		t.Fatalf("other comments should not be deleted")
	}

	// 5. Undelete comment #3
	if err := engine.UndeleteComment(p.ID, 3); err != nil {
		t.Fatalf("UndeleteComment failed: %v", err)
	}
	cmtsAfterUndel, _ := engine.GetComments(p.ID, 1, 10)
	if cmtsAfterUndel[2].IsDeleted {
		t.Fatalf("expected comment #3 to be restored")
	}

	t.Logf("UpdateTitle and Comment Management verified successfully!")
}

func TestDeleteCommunity(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "delete_comm_test_*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tempDir)

	cfg := storage.Config{
		DataDir:        tempDir,
		CacheDir:       filepath.Join(tempDir, "cache"),
		CacheSizeMB:    16,
		FlushSeconds:   1,
		FilterEncoding: "utf-8",
	}

	engine, err := storage.OpenEngine(cfg)
	if err != nil {
		t.Fatal(err)
	}
	defer engine.Close()

	// 1. Create posts and comments in "trashboard"
	p1, err := engine.CreatePost(&model.Post{
		Community: "trashboard",
		PostFile:  "M.111.A.001",
		Title:     "文章一",
		Author:    "user1",
		Content:   "內文一",
	})
	if err != nil {
		t.Fatal(err)
	}
	_, _ = engine.AddComment(&model.Comment{PostID: p1.ID, Author: "c1", Content: "推文一"})
	_, _ = engine.AddComment(&model.Comment{PostID: p1.ID, Author: "c2", Content: "推文二"})

	p2, err := engine.CreatePost(&model.Post{
		Community: "trashboard",
		PostFile:  "M.111.A.002",
		Title:     "文章二",
		Author:    "user2",
		Content:   "內文二",
	})
	if err != nil {
		t.Fatal(err)
	}
	_, _ = engine.AddComment(&model.Comment{PostID: p2.ID, Author: "c3", Content: "推文三"})

	// 2. Create post in "keepboard"
	pKeep, err := engine.CreatePost(&model.Post{
		Community: "keepboard",
		PostFile:  "M.222.A.001",
		Title:     "保留文章",
		Author:    "user3",
		Content:   "保留內文",
	})
	if err != nil {
		t.Fatal(err)
	}
	_, _ = engine.AddComment(&model.Comment{PostID: pKeep.ID, Author: "c4", Content: "保留推文"})

	// 3. Delete community "trashboard"
	postsDel, commentsDel, err := engine.DeleteCommunity("trashboard")
	if err != nil {
		t.Fatalf("DeleteCommunity failed: %v", err)
	}
	if postsDel != 2 || commentsDel != 3 {
		t.Fatalf("expected 2 posts and 3 comments deleted, got posts=%d, comments=%d", postsDel, commentsDel)
	}

	// 4. Verify trashboard is gone
	if _, err := engine.GetPost(p1.ID); err == nil {
		t.Fatalf("expected p1 to be deleted")
	}
	if _, err := engine.GetPost(p2.ID); err == nil {
		t.Fatalf("expected p2 to be deleted")
	}
	cmts1, _ := engine.GetComments(p1.ID, 1, 10)
	if len(cmts1) != 0 {
		t.Fatalf("expected p1 comments to be deleted, got %d", len(cmts1))
	}
	cmts2, _ := engine.GetComments(p2.ID, 1, 10)
	if len(cmts2) != 0 {
		t.Fatalf("expected p2 comments to be deleted, got %d", len(cmts2))
	}

	// 5. Verify keepboard is intact
	keptPost, err := engine.GetPost(pKeep.ID)
	if err != nil || keptPost == nil {
		t.Fatalf("expected pKeep to remain, got err: %v", err)
	}
	keptCmts, _ := engine.GetComments(pKeep.ID, 1, 10)
	if len(keptCmts) != 1 {
		t.Fatalf("expected 1 comment for keepboard, got %d", len(keptCmts))
	}

	t.Logf("DeleteCommunity verified successfully!")
}

func TestPurgePost(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "purge_post_test_*")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	cfg := storage.Config{
		DataDir:        tempDir,
		CacheDir:       filepath.Join(tempDir, "cache"),
		CacheSizeMB:    16,
		FlushSeconds:   1,
		FilterEncoding: "utf-8",
	}

	engine, err := storage.OpenEngine(cfg)
	if err != nil {
		t.Fatalf("failed to create engine: %v", err)
	}
	defer engine.Close()

	p, err := engine.CreatePost(&model.Post{
		Community: "testboard",
		PostFile:  "M.111.A.001",
		Title:     "To be purged",
		Author:    "tester",
		CreatedAt: 1727800000,
		Content:   "This post will be purged",
	})
	if err != nil {
		t.Fatalf("CreatePost failed: %v", err)
	}

	_, err = engine.AddComment(&model.Comment{
		PostID:    p.ID,
		Author:    "commenter",
		Content:   "A comment",
		CreatedAt: 1727800001,
	})
	if err != nil {
		t.Fatalf("AddComment failed: %v", err)
	}

	// Purge post
	if err := engine.PurgePostByCommunityFile("testboard", "M.111.A.001"); err != nil {
		t.Fatalf("PurgePostByCommunityFile failed: %v", err)
	}

	// Verify post is gone
	if _, err := engine.GetPost(p.ID); err == nil {
		t.Fatalf("expected post to be deleted from DB")
	}
	cmts, _ := engine.GetComments(p.ID, 1, 10)
	if len(cmts) != 0 {
		t.Fatalf("expected comments to be deleted, got %d", len(cmts))
	}

	// Re-create should succeed with same community and post_file (unique constraint not violated)
	p2, err := engine.CreatePost(&model.Post{
		Community: "testboard",
		PostFile:  "M.111.A.001",
		Title:     "Re-created post",
		Author:    "tester",
		CreatedAt: 1727800000,
		Content:   "This is new content",
	})
	if err != nil {
		t.Fatalf("Re-creating post after purge failed: %v", err)
	}
	if p2.ID == 0 {
		t.Fatalf("expected positive ID on recreated post")
	}
	t.Logf("PurgePost and recreation verified successfully!")
}

func TestImportBoardCancellationAndWorkers(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "post_import_cancel_test_*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tmpDir)

	sockPath := filepath.Join(tmpDir, "post.sock")
	dbDir := filepath.Join(tmpDir, "db")
	cacheDir := filepath.Join(tmpDir, "cache")
	bbsHome := filepath.Join(tmpDir, "bbshome")

	cfg := config.DefaultConfig(bbsHome)
	cfg.Engines = []config.EngineLocation{
		{
			Index:    0,
			DataDir:  dbDir,
			CacheDir: cacheDir,
		},
	}
	cfg.UnixSocket = sockPath
	cfg.ImportWorkers = 2

	st, err := storage.OpenStorage(cfg)
	if err != nil {
		t.Fatal(err)
	}
	defer st.Close()

	srv := daemon.NewServer(daemon.ServerConfig{
		BBSHome:        bbsHome,
		UnixSocket:     sockPath,
		FilterEncoding: "utf-8",
		ImportWorkers:  cfg.ImportWorkers,
	}, st)

	if err := srv.Start(); err != nil {
		t.Fatal(err)
	}
	defer srv.Stop()

	// Create test board with 50 posts
	boardDir := filepath.Join(bbsHome, "boards", "t", "testboard")
	if err := os.MkdirAll(boardDir, 0755); err != nil {
		t.Fatal(err)
	}

	var dirBuf []byte
	for i := 1; i <= 50; i++ {
		fn := fmt.Sprintf("M.1728000000.A.%03d", i)
		content := fmt.Sprintf("Article content %d\n--\n※ 發信站: 批踢踢實業坊\n推 user: 推文 %d 10/01 12:00\n", i, i)
		if err := os.WriteFile(filepath.Join(boardDir, fn), []byte(content), 0644); err != nil {
			t.Fatal(err)
		}

		hdr := make([]byte, importer.FileHeaderSize)
		copy(hdr[0:28], fn)
		binary.LittleEndian.PutUint32(hdr[28:32], uint32(1728000000+i))
		hdr[33] = 1
		copy(hdr[34:48], "tester")
		copy(hdr[48:54], "10/01")
		copy(hdr[54:119], fmt.Sprintf("Test post %d", i))
		dirBuf = append(dirBuf, hdr...)
	}
	if err := os.WriteFile(filepath.Join(boardDir, ".DIR"), dirBuf, 0644); err != nil {
		t.Fatal(err)
	}

	// 1. Test Cancellation on Disconnect: start import with workers=2, read 1 byte, then close conn immediately!
	conn1, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatalf("Dial unix socket failed: %v", err)
	}
	// IMPORT_BOARD testboard - 0 0 0 0 - 0 0 2
	conn1.Write([]byte("IMPORT_BOARD testboard - 0 0 0 0 - 0 0 2\n"))
	buf := make([]byte, 16)
	conn1.Read(buf)
	conn1.Close() // Disconnect abruptly!

	// Give a few ms for cancel to propagate
	time.Sleep(50 * time.Millisecond)

	// 2. Server should remain responsive and healthy after client dropped
	conn2, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatalf("Dial unix socket failed after disconnect: %v", err)
	}
	defer conn2.Close()
	conn2.Write([]byte("PING\n"))
	r2 := bufio.NewReader(conn2)
	pingResp, err := r2.ReadString('\n')
	if err != nil || strings.TrimSpace(pingResp) != "PONG" {
		t.Fatalf("Server not responsive after cancellation, ping got: %q, err: %v", pingResp, err)
	}

	// 3. Perform a full migration with custom workers=4
	conn3, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatalf("Dial unix socket failed: %v", err)
	}
	defer conn3.Close()
	conn3.Write([]byte("IMPORT_BOARD testboard - 1 0 0 0 - 0 0 4\n"))
	r3 := bufio.NewReader(conn3)
	var finalResp string
	for {
		line, err := r3.ReadString('\n')
		if err != nil {
			break
		}
		if strings.HasPrefix(line, "OK") {
			finalResp = strings.TrimSpace(line)
			break
		}
	}
	if !strings.HasPrefix(finalResp, "OK") {
		t.Fatalf("Expected OK response, got: %q", finalResp)
	}
	t.Logf("Import finished successfully with workers=4: %s", finalResp)
}

func TestImportBoardDuplicateFilenamesAndUpsert(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "post_import_dup_test_*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tmpDir)

	sockPath := filepath.Join(tmpDir, "post.sock")
	dbDir := filepath.Join(tmpDir, "db")
	cacheDir := filepath.Join(tmpDir, "cache")
	bbsHome := filepath.Join(tmpDir, "bbshome")

	cfg := config.DefaultConfig(bbsHome)
	cfg.Engines = []config.EngineLocation{
		{
			Index:    0,
			DataDir:  dbDir,
			CacheDir: cacheDir,
		},
	}
	cfg.UnixSocket = sockPath
	cfg.ImportWorkers = 2

	st, err := storage.OpenStorage(cfg)
	if err != nil {
		t.Fatal(err)
	}
	defer st.Close()

	srv := daemon.NewServer(daemon.ServerConfig{
		BBSHome:        bbsHome,
		UnixSocket:     sockPath,
		FilterEncoding: "utf-8",
		ImportWorkers:  cfg.ImportWorkers,
	}, st)

	if err := srv.Start(); err != nil {
		t.Fatal(err)
	}
	defer srv.Stop()

	boardDir := filepath.Join(bbsHome, "boards", "d", "dupboard")
	if err := os.MkdirAll(boardDir, 0755); err != nil {
		t.Fatal(err)
	}

	// Create 2 post files on disk
	fn1 := "M.1004284007.A"
	fn2 := "M.1004284008.A"
	if err := os.WriteFile(filepath.Join(boardDir, fn1), []byte("Article 1\n--\n※ 發信站: 批踢踢實業坊\n"), 0644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(boardDir, fn2), []byte("Article 2\n--\n※ 發信站: 批踢踢實業坊\n"), 0644); err != nil {
		t.Fatal(err)
	}

	// Pack .DIR with fn1, fn2, AND fn1 again (duplicate entry in .DIR!)
	var dirBuf []byte
	packHdr := func(fn, title string) {
		hdr := make([]byte, importer.FileHeaderSize)
		copy(hdr[0:28], fn)
		binary.LittleEndian.PutUint32(hdr[28:32], uint32(1004284007))
		hdr[33] = 1
		copy(hdr[34:48], "tester")
		copy(hdr[48:54], "10/01")
		copy(hdr[54:119], title)
		dirBuf = append(dirBuf, hdr...)
	}

	packHdr(fn1, "Title 1")
	packHdr(fn2, "Title 2")
	packHdr(fn1, "Title 1 Duplicate In DIR") // Duplicate!

	// Add more posts to make board substantial
	for i := 3; i <= 200; i++ {
		fn := fmt.Sprintf("M.1004284%03d.A", i)
		if err := os.WriteFile(filepath.Join(boardDir, fn), []byte("Article\n--\n※ 發信站: 批踢踢實業坊\n"), 0644); err != nil {
			t.Fatal(err)
		}
		packHdr(fn, fmt.Sprintf("Title %d", i))
	}
	if err := os.WriteFile(filepath.Join(boardDir, ".DIR"), dirBuf, 0644); err != nil {
		t.Fatal(err)
	}

	// 1. First import: must succeed without unique constraint failure
	conn, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatalf("Dial unix socket failed: %v", err)
	}
	defer conn.Close()
	if _, err := conn.Write([]byte("IMPORT_BOARD dupboard - 0 0 0 0 - 0 0 2\n")); err != nil {
		t.Fatal(err)
	}
	r := bufio.NewReader(conn)
	var finalResp string
	for {
		line, err := r.ReadString('\n')
		if err != nil {
			break
		}
		if strings.HasPrefix(line, "OK") {
			finalResp = strings.TrimSpace(line)
			break
		}
	}
	if !strings.HasPrefix(finalResp, "OK") {
		t.Fatalf("First import failed with duplicate in .DIR: %q", finalResp)
	}

	// 2. Second import without --overwrite: must also succeed via UPSERT without unique constraint error!
	conn2, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatalf("Dial unix socket failed: %v", err)
	}
	defer conn2.Close()
	if _, err := conn2.Write([]byte("IMPORT_BOARD dupboard - 0 0 0 0 - 0 0 2\n")); err != nil {
		t.Fatal(err)
	}
	r2 := bufio.NewReader(conn2)
	var finalResp2 string
	for {
		line, err := r2.ReadString('\n')
		if err != nil {
			break
		}
		if strings.HasPrefix(line, "OK") {
			finalResp2 = strings.TrimSpace(line)
			break
		}
	}
	if !strings.HasPrefix(finalResp2, "OK") {
		t.Fatalf("Second re-import failed without overwrite: %q", finalResp2)
	}
	t.Logf("Both imports with duplicate filenames succeeded: %s, %s", finalResp, finalResp2)

	// 3. Test concurrent import on the same board: should reject immediately
	connA, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatal(err)
	}
	defer connA.Close()

	started := make(chan struct{})
	go func() {
		close(started)
		_, _ = connA.Write([]byte("IMPORT_BOARD dupboard - 0 0 0 0 - 0 0 1\n"))
		buf := make([]byte, 1024)
		for {
			if _, err := connA.Read(buf); err != nil {
				return
			}
		}
	}()

	<-started
	var respB string
	for attempt := 0; attempt < 10; attempt++ {
		connB, err := net.Dial("unix", sockPath)
		if err == nil {
			_, _ = connB.Write([]byte("IMPORT_BOARD dupboard - 0 0 0 0 - 0 0 1\n"))
			rB := bufio.NewReader(connB)
			resp, _ := rB.ReadString('\n')
			connB.Close()
			if strings.Contains(resp, "already being imported") {
				respB = resp
				break
			}
		}
		time.Sleep(time.Millisecond)
	}
	if !strings.Contains(respB, "already being imported") {
		t.Fatalf("Expected already being imported error, got: %q", respB)
	}
	t.Logf("Concurrent import rejection verified: %s", strings.TrimSpace(respB))
}
