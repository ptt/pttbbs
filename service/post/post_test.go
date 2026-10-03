package main

import (
	"bufio"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"pttbbs/big5uao"
	"pttbbs/post/daemon"
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
	if !strings.Contains(cacheStr, "[1] commenter1 ") || !strings.Contains(cacheStr, "192.168.1.1\n") {
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

	// 5. Test vote_post (Protocol A: VOTE)
	conn5, _ := net.Dial("unix", sockPath)
	defer conn5.Close()
	conn5.Write([]byte("VOTE gossiping M.1727800000.A.003 voter_user 999123 1\n"))

	r5 := bufio.NewReader(conn5)
	vResp, _ := r5.ReadString('\n')
	if !strings.HasPrefix(vResp, "OK") {
		t.Fatalf("vote_post by file failed: %s", vResp)
	}
	t.Logf("vote_post via IPC succeeded: %s", strings.TrimSpace(vResp))

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
