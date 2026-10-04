package daemon

import (
	"bufio"
	"bytes"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	"pttbbs/big5uao"
	"pttbbs/post/model"
	"pttbbs/post/storage"
)

type ServerConfig struct {
	BBSHome        string
	UnixSocket     string // e.g. "$BBSHOME/run/post.svc.sock"
	FilterEncoding string // "big5" (default) or "utf-8"
}

func (c ServerConfig) IsBig5() bool {
	enc := strings.ToLower(strings.TrimSpace(c.FilterEncoding))
	return enc == "" || enc == "big5" || enc == "big5-uao" || enc == "uao"
}

type Server struct {
	cfg       ServerConfig
	storage   *storage.Engine
	listeners []net.Listener
	wg        sync.WaitGroup
	quit      chan struct{}
}

func NewServer(cfg ServerConfig, st *storage.Engine) *Server {
	if cfg.BBSHome == "" {
		cfg.BBSHome = "/home/bbs"
	}
	if cfg.UnixSocket == "" {
		cfg.UnixSocket = filepath.Join(cfg.BBSHome, "run", "post.svc.sock")
	}
	return &Server{
		cfg:     cfg,
		storage: st,
		quit:    make(chan struct{}),
	}
}

// IsSocketOccupied checks if another active process is listening on the UNIX domain socket
func IsSocketOccupied(socketPath string) bool {
	conn, err := net.DialTimeout("unix", socketPath, 100*time.Millisecond)
	if err == nil {
		conn.Close()
		return true
	}
	return false
}

func (s *Server) Start() error {
	if s.cfg.UnixSocket == "" {
		return fmt.Errorf("unix socket path is required")
	}
	if IsSocketOccupied(s.cfg.UnixSocket) {
		return fmt.Errorf("unix domain socket %s is already occupied by another active instance", s.cfg.UnixSocket)
	}
	os.Remove(s.cfg.UnixSocket)
	if err := os.MkdirAll(filepath.Dir(s.cfg.UnixSocket), 0755); err != nil {
		log.Printf("[post.svc] Warning: failed to mkdir for unix socket: %v", err)
	}
	lnUnix, err := net.Listen("unix", s.cfg.UnixSocket)
	if err != nil {
		return fmt.Errorf("failed to listen unix socket on %s: %w", s.cfg.UnixSocket, err)
	}
	os.Chmod(s.cfg.UnixSocket, 0666)
	s.listeners = append(s.listeners, lnUnix)
	s.wg.Add(1)
	go func() {
		defer s.wg.Done()
		s.acceptLoop(lnUnix, s.handleIPC)
	}()
	log.Printf("[post.svc] Modern IPC socket listening on %s", s.cfg.UnixSocket)
	return nil
}

func (s *Server) Stop() {
	close(s.quit)
	for _, ln := range s.listeners {
		ln.Close()
	}
	s.wg.Wait()
}

func (s *Server) acceptLoop(ln net.Listener, handler func(net.Conn)) {
	for {
		conn, err := ln.Accept()
		if err != nil {
			select {
			case <-s.quit:
				return
			default:
				log.Printf("[post.svc] accept error: %v", err)
				return
			}
		}
		go handler(conn)
	}
}

// -----------------------------------------------------------------------------
// Protocol A: Header Line + Length-Prefixed Payload Handler
// -----------------------------------------------------------------------------

func extractHeaderField(raw []byte, prefixes ...[]byte) string {
	lines := bytes.Split(raw, []byte("\n"))
	limit := 10
	if len(lines) < limit {
		limit = len(lines)
	}
	for i := 0; i < limit; i++ {
		line := bytes.TrimSpace(lines[i])
		for _, prefix := range prefixes {
			if bytes.HasPrefix(line, prefix) {
				return string(bytes.TrimSpace(line[len(prefix):]))
			}
		}
	}
	return ""
}

func (s *Server) readDiskPostFile(community, postFile string) []byte {
	if postFile == "" || postFile == "-" {
		return nil
	}
	var paths []string
	if len(community) > 0 {
		c := string(community[0])
		paths = []string{
			filepath.Join(s.cfg.BBSHome, "boards", c, community, postFile),
			filepath.Join(s.cfg.BBSHome, "boards", strings.ToUpper(c), community, postFile),
			filepath.Join(s.cfg.BBSHome, "boards", strings.ToLower(c), community, postFile),
			filepath.Join(s.cfg.BBSHome, "boards", community, postFile),
		}
	} else {
		matches, _ := filepath.Glob(filepath.Join(s.cfg.BBSHome, "boards", "*", "*", postFile))
		if len(matches) == 0 {
			matches, _ = filepath.Glob(filepath.Join(s.cfg.BBSHome, "boards", "*", postFile))
		}
		paths = matches
	}
	for _, pth := range paths {
		if b, rErr := os.ReadFile(pth); rErr == nil && len(b) > 0 {
			return b
		}
	}
	return nil
}

func (s *Server) readDiskPostFileWithRetry(community, postFile string, retries int) []byte {
	for i := 0; i <= retries; i++ {
		b := s.readDiskPostFile(community, postFile)
		if len(b) > 0 {
			return b
		}
		if i < retries {
			time.Sleep(20 * time.Millisecond)
		}
	}
	return nil
}

func (s *Server) autoImportPost(community, postFile string) *model.Post {
	if len(community) == 0 || len(postFile) == 0 {
		return nil
	}
	rawBytes := s.readDiskPostFileWithRetry(community, postFile, 5)
	if len(rawBytes) == 0 {
		return nil
	}
	postContent := string(rawBytes)
	if s.cfg.IsBig5() {
		postContent = big5uao.DecodeSGR66(rawBytes)
	}
	t := extractHeaderField(rawBytes, []byte("標題: "), []byte("Title: "))
	if s.cfg.IsBig5() {
		t = big5uao.DecodeSGR66([]byte(t))
	}
	autoP := &model.Post{
		Community: community,
		PostFile:  postFile,
		Title:     t,
		Encoding:  "utf-8",
		Content:   postContent,
	}
	p, _ := s.storage.CreatePost(autoP)
	return p
}

func (s *Server) writeRenderOutput(conn net.Conn, p *model.Post, outputPath string) {
	rendered, err := s.storage.RenderFullPostText(p.ID, s.cfg.IsBig5())
	if err != nil {
		conn.Write([]byte(fmt.Sprintf("ERR render failed: %v\n", err)))
		return
	}

	if outputPath == "-" || outputPath == "" {
		conn.Write([]byte(fmt.Sprintf("OK %d\n", len(rendered))))
		conn.Write(rendered)
		return
	}

	targetPath := outputPath
	if fi, err := os.Stat(targetPath); err == nil && fi.IsDir() {
		targetPath = filepath.Join(targetPath, p.PostFile)
	}

	if err := os.MkdirAll(filepath.Dir(targetPath), 0755); err != nil {
		conn.Write([]byte(fmt.Sprintf("ERR mkdir failed: %v\n", err)))
		return
	}

	tmpPath := fmt.Sprintf("%s.tmp.%d", targetPath, os.Getpid())
	if err := os.WriteFile(tmpPath, rendered, 0644); err != nil {
		conn.Write([]byte(fmt.Sprintf("ERR write file failed: %v\n", err)))
		return
	}
	if err := os.Rename(tmpPath, targetPath); err != nil {
		os.Remove(tmpPath)
		conn.Write([]byte(fmt.Sprintf("ERR rename failed: %v\n", err)))
		return
	}

	conn.Write([]byte(fmt.Sprintf("OK %d\n", len(rendered))))
}

func (s *Server) handleIPC(conn net.Conn) {
	defer conn.Close()
	reader := bufio.NewReader(conn)

	line, err := reader.ReadString('\n')
	if err != nil {
		return
	}
	line = strings.TrimRight(line, "\r\n")
	parts := strings.Split(line, " ")
	if len(parts) == 0 || parts[0] == "" {
		return
	}

	cmd := strings.ToUpper(parts[0])
	switch cmd {
	case "PING":
		conn.Write([]byte("PONG\n"))

	case "POST_FILE":
		// POST_FILE <community> <post_file> <author> <token> <filemode> <ctime> <title_bytes>\n<title>
		if len(parts) < 8 {
			conn.Write([]byte("ERR invalid arguments for POST_FILE\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		author := parts[3]
		authorToken, _ := strconv.ParseUint(parts[4], 10, 32)
		filemode, _ := strconv.Atoi(parts[5])
		ctime, _ := strconv.ParseInt(parts[6], 10, 64)
		titleLen, _ := strconv.Atoi(parts[7])

		titleBytes := make([]byte, titleLen)
		if titleLen > 0 {
			if _, err := io.ReadFull(reader, titleBytes); err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR read title failed: %v\n", err)))
				return
			}
		}

		rawBytes := s.readDiskPostFileWithRetry(community, postFile, 5)
		if len(rawBytes) == 0 {
			conn.Write([]byte(fmt.Sprintf("ERR post file not found on disk: %s/%s\n", community, postFile)))
			return
		}

		var content string
		if s.cfg.IsBig5() {
			content = big5uao.DecodeSGR66(rawBytes)
		} else {
			content = string(rawBytes)
		}

		var title string
		if len(titleBytes) > 0 {
			if s.cfg.IsBig5() {
				title = big5uao.DecodeSGR66(titleBytes)
			} else {
				title = string(titleBytes)
			}
		}
		if title == "" {
			rawTitle := extractHeaderField(rawBytes, []byte("標題: "), []byte("Title: "))
			if s.cfg.IsBig5() {
				title = big5uao.DecodeSGR66([]byte(rawTitle))
			} else {
				title = rawTitle
			}
		}

		if ctime == 0 {
			ctime = time.Now().Unix()
		}

		p := &model.Post{
			Community:   community,
			PostFile:    postFile,
			Title:       title,
			Author:      author,
			AuthorToken: uint32(authorToken),
			Filemode:    filemode,
			CreatedAt:   ctime,
			Content:     content,
		}
		created, err := s.storage.CreatePost(p)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", created.ID)))

	case "POST":
		// POST <community> <post_file> <author> <token> <filemode> <ctime> <parent_id> <title_bytes> <content_bytes>\n<title><content>
		if len(parts) < 10 {
			conn.Write([]byte("ERR invalid arguments for POST\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		author := parts[3]
		authorToken, _ := strconv.ParseUint(parts[4], 10, 32)
		filemode, _ := strconv.Atoi(parts[5])
		ctime, _ := strconv.ParseInt(parts[6], 10, 64)
		parentID, _ := strconv.ParseUint(parts[7], 10, 64)
		titleLen, _ := strconv.Atoi(parts[8])
		contentLen, _ := strconv.Atoi(parts[9])

		titleBytes := make([]byte, titleLen)
		if titleLen > 0 {
			if _, err := io.ReadFull(reader, titleBytes); err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR read title failed: %v\n", err)))
				return
			}
		}
		contentBytes := make([]byte, contentLen)
		if contentLen > 0 {
			if _, err := io.ReadFull(reader, contentBytes); err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR read content failed: %v\n", err)))
				return
			}
		}

		if ctime == 0 {
			ctime = time.Now().Unix()
		}

		var postTitle string
		if s.cfg.IsBig5() {
			postTitle = big5uao.DecodeSGR66(titleBytes)
		} else {
			postTitle = string(titleBytes)
		}

		var postContent string
		if s.cfg.IsBig5() {
			postContent = big5uao.DecodeSGR66(contentBytes)
		} else {
			postContent = string(contentBytes)
		}

		p := &model.Post{
			ParentID:    parentID,
			Community:   community,
			PostFile:    postFile,
			Title:       postTitle,
			Author:      author,
			AuthorToken: uint32(authorToken),
			Filemode:    filemode,
			CreatedAt:   ctime,
			Content:     postContent,
		}
		created, err := s.storage.CreatePost(p)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", created.ID)))

	case "COMMENT":
		// COMMENT <community> <post_file> <author> <token> <ip> <ctime> <content_bytes>\n<content>
		if len(parts) < 8 {
			conn.Write([]byte("ERR invalid arguments for COMMENT\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		author := parts[3]
		authorToken, _ := strconv.ParseUint(parts[4], 10, 32)
		ip := parts[5]
		ctime, _ := strconv.ParseInt(parts[6], 10, 64)
		contentLen, _ := strconv.Atoi(parts[7])

		contentBytes := make([]byte, contentLen)
		if contentLen > 0 {
			if _, err := io.ReadFull(reader, contentBytes); err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR read comment content failed: %v\n", err)))
				return
			}
		}

		p, err := s.storage.GetPostByCommunityFile(community, postFile)
		if err != nil {
			p = s.autoImportPost(community, postFile)
		}
		if p == nil {
			conn.Write([]byte("ERR post not found\n"))
			return
		}

		if ctime == 0 {
			ctime = time.Now().Unix()
		}

		var commentContent string
		if s.cfg.IsBig5() {
			commentContent = big5uao.DecodeSGR66(contentBytes)
		} else {
			commentContent = string(contentBytes)
		}

		c := &model.Comment{
			PostID:      p.ID,
			Author:      author,
			AuthorToken: uint32(authorToken),
			IP:          ip,
			Content:     commentContent,
			CreatedAt:   ctime,
		}
		added, err := s.storage.AddComment(c)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", added.Sequence)))

	case "VOTE":
		// VOTE <community> <post_file> <author> <token> <vote>\n
		if len(parts) < 6 {
			conn.Write([]byte("ERR invalid arguments for VOTE\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		author := parts[3]
		authorToken, _ := strconv.ParseUint(parts[4], 10, 32)
		voteVal, _ := strconv.Atoi(parts[5])

		p, err := s.storage.GetPostByCommunityFile(community, postFile)
		if err != nil {
			p = s.autoImportPost(community, postFile)
		}
		if p == nil {
			conn.Write([]byte("ERR post not found\n"))
			return
		}

		upDelta, downDelta, err := s.storage.VotePost(p.ID, author, uint32(authorToken), model.VoteType(voteVal))
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d %d\n", upDelta, downDelta)))

	case "GET_POST":
		// GET_POST <community> <post_file>\n
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for GET_POST\n"))
			return
		}
		p, err := s.storage.GetPostByCommunityFile(parts[1], parts[2])
		if err != nil {
			conn.Write([]byte("ERR post not found\n"))
			return
		}
		titleB := []byte(p.Title)
		contentB := []byte(p.Content)
		conn.Write([]byte(fmt.Sprintf("OK %d %d %s %s %s %d %d %d %d %d %d %d %d\n",
			p.ID, p.ParentID, p.Community, p.PostFile, p.Author, p.AuthorToken,
			p.CreatedAt, p.Filemode, p.Upvotes, p.Downvotes, p.NumComments,
			len(titleB), len(contentB))))
		conn.Write(titleB)
		conn.Write(contentB)

	case "GET_POST_ID":
		// GET_POST_ID <id>\n
		if len(parts) < 2 {
			conn.Write([]byte("ERR invalid arguments for GET_POST_ID\n"))
			return
		}
		id, _ := strconv.ParseUint(parts[1], 10, 64)
		p, err := s.storage.GetPost(id)
		if err != nil {
			conn.Write([]byte("ERR post not found\n"))
			return
		}
		titleB := []byte(p.Title)
		contentB := []byte(p.Content)
		conn.Write([]byte(fmt.Sprintf("OK %d %d %s %s %s %d %d %d %d %d %d %d %d\n",
			p.ID, p.ParentID, p.Community, p.PostFile, p.Author, p.AuthorToken,
			p.CreatedAt, p.Filemode, p.Upvotes, p.Downvotes, p.NumComments,
			len(titleB), len(contentB))))
		conn.Write(titleB)
		conn.Write(contentB)

	case "PURGE_COMMENTS":
		// PURGE_COMMENTS <author> <days>\n
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for PURGE_COMMENTS\n"))
			return
		}
		author := parts[1]
		days, _ := strconv.Atoi(parts[2])
		if days <= 0 {
			days = 7
		}
		purged, err := s.storage.PurgeUserComments(author, time.Duration(days)*24*time.Hour)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", purged)))

	case "PURGE_POSTS":
		// PURGE_POSTS <author> <days>\n
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for PURGE_POSTS\n"))
			return
		}
		author := parts[1]
		days, _ := strconv.Atoi(parts[2])
		if days <= 0 {
			days = 7
		}
		purged, err := s.storage.PurgeUserPosts(author, time.Duration(days)*24*time.Hour)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", purged)))

	case "LIST":
		// LIST <community> [count [start]]
		if len(parts) < 2 || parts[1] == "" {
			conn.Write([]byte("ERR invalid arguments for LIST\n"))
			return
		}
		community := parts[1]
		count := 20
		if len(parts) >= 3 && parts[2] != "" {
			if c, err := strconv.Atoi(parts[2]); err == nil && c > 0 {
				count = c
			}
		}
		start := 0
		if len(parts) >= 4 && parts[3] != "" {
			if st, err := strconv.Atoi(parts[3]); err == nil && st >= 0 {
				start = st
			}
		}

		posts, err := s.storage.ListPosts(community, count, start)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		total, _ := s.storage.CountPosts(community)

		var buf bytes.Buffer
		buf.WriteString(fmt.Sprintf("OK %d %d\n", len(posts), total))
		for _, p := range posts {
			cleanTitle := strings.ReplaceAll(strings.TrimRight(p.Title, "\r\n"), "\t", " ")
			buf.WriteString(fmt.Sprintf("%d\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n",
				p.ID, p.PostFile, p.Author, p.CreatedAt, p.Filemode,
				p.Upvotes, p.Downvotes, p.NumComments, p.ParentID, cleanTitle))
		}
		conn.Write(buf.Bytes())

	case "RENDER":
		// RENDER <post_id> <output_path>
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for RENDER\n"))
			return
		}
		postID, err := strconv.ParseUint(parts[1], 10, 64)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid post_id: %s\n", parts[1])))
			return
		}
		outputPath := parts[2]
		p, err := s.storage.GetPost(postID)
		if err != nil || p == nil {
			conn.Write([]byte(fmt.Sprintf("ERR post not found: %d\n", postID)))
			return
		}
		s.writeRenderOutput(conn, p, outputPath)

	case "RENDER_FILE":
		// RENDER_FILE <community> <post_file> <output_path>
		if len(parts) < 4 {
			conn.Write([]byte("ERR invalid arguments for RENDER_FILE\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		outputPath := parts[3]

		p, err := s.storage.GetPostByCommunityFile(community, postFile)
		if err != nil || p == nil {
			p = s.autoImportPost(community, postFile)
		}
		if p == nil {
			conn.Write([]byte(fmt.Sprintf("ERR post not found: %s/%s\n", community, postFile)))
			return
		}
		s.writeRenderOutput(conn, p, outputPath)

	default:
		conn.Write([]byte(fmt.Sprintf("ERR unknown command: %s\n", cmd)))
	}
}
