package daemon

import (
	"bufio"
	"bytes"
	"context"
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
	"unicode/utf8"

	"pttbbs/big5uao"
	"pttbbs/post/importer"
	"pttbbs/post/model"
	"pttbbs/post/storage"
)

type ServerConfig struct {
	BBSHome        string
	UnixSocket     string // e.g. "$BBSHOME/run/post.svc.sock"
	FilterEncoding string // "big5" (default) or "utf-8"
	ImportWorkers  int    // Default worker threads for board import (default: 8)
	// Users resolves the person IDs of the users named in legacy articles and
	// .DIR (import-community, and posts imported on demand). Optional.
	Users importer.UserResolver
}

func (c ServerConfig) IsBig5() bool {
	enc := strings.ToLower(strings.TrimSpace(c.FilterEncoding))
	return enc == "" || enc == "big5" || enc == "big5-uao" || enc == "uao"
}

type Server struct {
	cfg             ServerConfig
	storage         storage.Storage
	listeners       []net.Listener
	wg              sync.WaitGroup
	quit            chan struct{}
	activeImportsMu sync.Mutex
	activeImports   map[string]bool
	users           importer.UserResolver
}

func NewServer(cfg ServerConfig, st storage.Storage) *Server {
	if cfg.BBSHome == "" {
		cfg.BBSHome = "/home/bbs"
	}
	if cfg.UnixSocket == "" {
		cfg.UnixSocket = filepath.Join(cfg.BBSHome, "run", "post.svc.sock")
	}
	if cfg.ImportWorkers <= 0 {
		cfg.ImportWorkers = 8
	}
	return &Server{
		cfg:           cfg,
		storage:       st,
		users:         importer.NewCachedResolver(cfg.Users),
		quit:          make(chan struct{}),
		activeImports: make(map[string]bool),
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
	_ = os.Chmod(s.cfg.UnixSocket, 0666)
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
		if b, rErr := importer.ReadArticleFile(pth); rErr == nil && len(b) > 0 {
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
	t := extractHeaderField(rawBytes, []byte("標題: "), []byte("Title: "))
	if s.cfg.IsBig5() && !utf8.Valid([]byte(t)) {
		t = big5uao.DecodeSGR66([]byte(t))
	}
	author := extractHeaderField(rawBytes, []byte("作者: "), []byte("Author: "))
	if idx := strings.IndexByte(author, ' '); idx != -1 {
		author = author[:idx]
	}
	if idx := strings.IndexByte(author, '('); idx != -1 {
		author = strings.TrimSpace(author[:idx])
	}

	postCtime := int64(0)
	parsed, err := importer.ParseArticleText(rawBytes, author, postCtime, community, postFile, s.cfg.IsBig5())
	if err == nil && parsed != nil && len(parsed.Comments) > 0 {
		comments, votes, upvotes, downvotes := importer.ConvertParsedComments(parsed.Comments, s.users)

		post := &model.Post{
			ParentID:    0,
			Community:   community,
			PostFile:    postFile,
			Title:       t,
			CreatorName: author,
			CreatedAt:   postCtime,
			Modified:    postCtime,
			Upvotes:     upvotes,
			Downvotes:   downvotes,
			NumComments: len(comments),
			Content:     parsed.BodyContent,
			Encoding:    "utf-8",
		}

		item := &storage.ImportedPostData{
			Post:     post,
			Comments: comments,
			Votes:    votes,
		}
		if err := s.storage.ImportPostBatch([]*storage.ImportedPostData{item}, ""); err == nil {
			return post
		}
	}

	postContent := string(rawBytes)
	if s.cfg.IsBig5() && !utf8.Valid(rawBytes) {
		postContent = big5uao.DecodeSGR66(rawBytes)
	}
	autoP := &model.Post{
		Community:   community,
		PostFile:    postFile,
		Title:       t,
		CreatorName: author,
		Encoding:    "utf-8",
		Content:     postContent,
	}
	p, _ := s.storage.CreatePost(autoP)
	return p
}

func (s *Server) writeRenderOutput(conn net.Conn, p *model.Post, outputPath string, legacyFormat bool) {
	var rendered []byte
	var err error
	if legacyFormat {
		rendered, err = s.storage.RenderLegacyPostText(p.ID, s.cfg.IsBig5())
	} else {
		rendered, err = s.storage.RenderFullPostText(p.ID, s.cfg.IsBig5())
	}
	if err != nil {
		_, _ = conn.Write([]byte(fmt.Sprintf("ERR render failed: %v\n", err)))
		return
	}

	if outputPath == "-" || outputPath == "" {
		_, _ = conn.Write([]byte(fmt.Sprintf("OK %d\n", len(rendered))))
		_, _ = conn.Write(rendered)
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
		// POST_FILE <community> <post_file> <creator_name> <creator_id> <filemode> <money> <ctime> <title_bytes>\n<title>
		if len(parts) < 9 {
			conn.Write([]byte("ERR invalid arguments for POST_FILE\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		author := parts[3]
		creatorID := model.ParsePersonID(parts[4])
		filemode, _ := strconv.Atoi(parts[5])
		money, _ := strconv.Atoi(parts[6])
		ctime, _ := strconv.ParseInt(parts[7], 10, 64)
		titleLen, _ := strconv.Atoi(parts[8])

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
		if s.cfg.IsBig5() && !utf8.Valid(rawBytes) {
			content = big5uao.DecodeSGR66(rawBytes)
		} else {
			content = string(rawBytes)
		}

		var title string
		if len(titleBytes) > 0 {
			title = string(titleBytes)
		}
		if title == "" {
			rawTitle := extractHeaderField(rawBytes, []byte("標題: "), []byte("Title: "))
			if s.cfg.IsBig5() && !utf8.Valid([]byte(rawTitle)) {
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
			CreatorName: author,
			CreatorID:   creatorID,
			Filemode:    filemode,
			Money:       money,
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
		// POST <community> <post_file> <creator_name> <creator_id> <filemode> <money> <ctime> <parent_id> <title_bytes> <content_bytes>\n<title><content>
		if len(parts) < 11 {
			conn.Write([]byte("ERR invalid arguments for POST\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		author := parts[3]
		creatorID := model.ParsePersonID(parts[4])
		filemode, _ := strconv.Atoi(parts[5])
		money, _ := strconv.Atoi(parts[6])
		ctime, _ := strconv.ParseInt(parts[7], 10, 64)
		parentID, _ := strconv.ParseUint(parts[8], 10, 64)
		titleLen, _ := strconv.Atoi(parts[9])
		contentLen, _ := strconv.Atoi(parts[10])

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

		postTitle := string(titleBytes)
		postContent := string(contentBytes)

		p := &model.Post{
			ParentID:    parentID,
			Community:   community,
			PostFile:    postFile,
			Title:       postTitle,
			CreatorName: author,
			CreatorID:   creatorID,
			Filemode:    filemode,
			Money:       money,
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
		// COMMENT <community> <post_file> <creator_name> <creator_id> <ip> <ctime> <content_bytes>\n<content>
		if len(parts) < 8 {
			conn.Write([]byte("ERR invalid arguments for COMMENT\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		author := parts[3]
		creatorID := model.ParsePersonID(parts[4])
		ip := parts[5]
		if ip == "-" {
			ip = ""
		}
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
		if err != nil || p == nil {
			p = s.autoImportPost(community, postFile)
		}
		if p == nil {
			conn.Write([]byte("ERR post not found\n"))
			return
		}

		if ctime == 0 {
			ctime = time.Now().Unix()
		}

		commentContent := string(contentBytes)
		commentContent = strings.ReplaceAll(commentContent, "\r\n", "\n")
		commentContent = strings.ReplaceAll(commentContent, "\r", "\n")

		var legType uint8 = model.LegacyTypeArrow
		if len(parts) >= 9 {
			if lt, err := strconv.Atoi(parts[8]); err == nil {
				legType = uint8(lt)
			}
		}

		c := &model.Comment{
			PostID:      p.ID,
			CreatorName: author,
			CreatorID:   creatorID,
			IP:          ip,
			Content:     commentContent,
			CreatedAt:   ctime,
			LegacyType:  legType,
		}
		added, err := s.storage.AddComment(c)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		if added.IsMerged {
			conn.Write([]byte(fmt.Sprintf("OK %d %d %d MERGED\n", added.Sequence, c.CreatedAt, p.Upvotes-p.Downvotes)))
		} else {
			conn.Write([]byte(fmt.Sprintf("OK %d %d %d\n", added.Sequence, c.CreatedAt, p.Upvotes-p.Downvotes)))
		}

	case "VOTE_FILE":
		// VOTE_FILE <community> <post_file> <person_name> <person_id> <vote> [ip] [ctime]\n
		if len(parts) < 6 {
			conn.Write([]byte("ERR invalid arguments for VOTE_FILE\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		author := parts[3]
		creatorID := model.ParsePersonID(parts[4])
		voteVal, _ := strconv.Atoi(parts[5])
		var ip string
		var ctime int64
		if len(parts) >= 7 {
			ip = parts[6]
			if ip == "-" {
				ip = ""
			}
		}
		if len(parts) >= 8 {
			ctime, _ = strconv.ParseInt(parts[7], 10, 64)
		}
		if ctime == 0 {
			ctime = time.Now().Unix()
		}

		p, err := s.storage.GetPostByCommunityFile(community, postFile)
		if err != nil || p == nil {
			p = s.autoImportPost(community, postFile)
		}
		if p == nil {
			conn.Write([]byte("ERR post not found\n"))
			return
		}

		upDelta, downDelta, err := s.storage.VotePost(p.ID, author, creatorID, model.VoteType(voteVal), ctime, ip)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d %d\n", upDelta, downDelta)))

	case "VOTE":
		// VOTE <community> <post_id> <person_name> <person_id> <vote> [ip] [ctime]\n
		// or: VOTE <post_id> <person_name> <person_id> <vote> [ip] [ctime]\n
		// (Also supports legacy VOTE <community> <post_file> <person_name> <person_id> <vote> [ip] [ctime])
		if len(parts) < 5 {
			conn.Write([]byte("ERR invalid arguments for VOTE\n"))
			return
		}
		var postID uint64
		var author string
		var creatorID model.PersonID
		var voteVal int
		var ip string
		var ctime int64

		if len(parts) >= 6 {
			id, err := strconv.ParseUint(parts[2], 10, 64)
			if err == nil {
				// VOTE <community> <post_id> <person_name> <person_id> <vote> [ip] [ctime]
				postID = id
				author = parts[3]
				creatorID = model.ParsePersonID(parts[4])
				voteVal, _ = strconv.Atoi(parts[5])
				if len(parts) >= 7 {
					ip = parts[6]
					if ip == "-" {
						ip = ""
					}
				}
				if len(parts) >= 8 {
					ctime, _ = strconv.ParseInt(parts[7], 10, 64)
				}
			} else {
				// Legacy: VOTE <community> <post_file> <person_name> <person_id> <vote> [ip] [ctime]
				community := parts[1]
				postFile := parts[2]
				author = parts[3]
				creatorID = model.ParsePersonID(parts[4])
				voteVal, _ = strconv.Atoi(parts[5])
				if len(parts) >= 7 {
					ip = parts[6]
					if ip == "-" {
						ip = ""
					}
				}
				if len(parts) >= 8 {
					ctime, _ = strconv.ParseInt(parts[7], 10, 64)
				}

				p, gErr := s.storage.GetPostByCommunityFile(community, postFile)
				if gErr != nil {
					p = s.autoImportPost(community, postFile)
				}
				if p == nil {
					conn.Write([]byte("ERR post not found\n"))
					return
				}
				postID = p.ID
			}
		} else {
			// VOTE <post_id> <person_name> <person_id> <vote>
			id, err := strconv.ParseUint(parts[1], 10, 64)
			if err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR invalid post_id: %s\n", parts[1])))
				return
			}
			postID = id
			author = parts[2]
			creatorID = model.ParsePersonID(parts[3])
			voteVal, _ = strconv.Atoi(parts[4])
		}
		if ctime == 0 {
			ctime = time.Now().Unix()
		}

		upDelta, downDelta, err := s.storage.VotePost(postID, author, creatorID, model.VoteType(voteVal), ctime, ip)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d %d\n", upDelta, downDelta)))

	case "VOTES":
		// VOTES <post_id>
		if len(parts) < 2 {
			conn.Write([]byte("ERR invalid arguments for VOTES\n"))
			return
		}
		postID, err := strconv.ParseUint(parts[1], 10, 64)
		if err != nil {
			conn.Write([]byte("ERR invalid post_id for VOTES\n"))
			return
		}
		votes, err := s.storage.GetVotes(postID)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		var buf bytes.Buffer
		buf.WriteString(fmt.Sprintf("OK %d\n", len(votes)))
		for _, v := range votes {
			buf.WriteString(fmt.Sprintf("%s\t%d\t%d\t%d\t%s\n",
				v.PersonName, v.PersonID, v.Vote, v.CreatedAt, v.IP))
		}
		conn.Write(buf.Bytes())

	case "VOTES_FILE":
		// VOTES_FILE <community> <post_file>
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for VOTES_FILE\n"))
			return
		}
		votes, err := s.storage.GetVotesByCommunityFile(parts[1], parts[2])
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		var buf bytes.Buffer
		buf.WriteString(fmt.Sprintf("OK %d\n", len(votes)))
		for _, v := range votes {
			buf.WriteString(fmt.Sprintf("%s\t%d\t%d\t%d\t%s\n",
				v.PersonName, v.PersonID, v.Vote, v.CreatedAt, v.IP))
		}
		conn.Write(buf.Bytes())

	case "RESET_VOTES_FILE":
		// RESET_VOTES_FILE <community> <post_file>\n
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for RESET_VOTES_FILE\n"))
			return
		}
		if err := s.storage.ResetVotesByCommunityFile(parts[1], parts[2]); err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte("OK\n"))

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
			p.ID, p.ParentID, p.Community, p.PostFile, p.CreatorName, p.CreatorID,
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
			p.ID, p.ParentID, p.Community, p.PostFile, p.CreatorName, p.CreatorID,
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

		filterMode := ""
		if len(parts) >= 5 {
			filterMode = strings.ToLower(parts[4])
		}

		var posts []*model.Post
		var err error
		if filterMode == "deleted" {
			posts, err = s.storage.ListDeletedPosts(community, count, start)
		} else {
			posts, err = s.storage.ListPosts(community, count, start)
		}
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
				p.ID, p.PostFile, p.CreatorName, p.CreatedAt, p.Filemode,
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
		legacy := false
		if len(parts) >= 4 && (strings.ToLower(parts[3]) == "legacy" || parts[3] == "1") {
			legacy = true
		}
		s.writeRenderOutput(conn, p, outputPath, legacy)

	case "IMPORT_BOARD", "MIGRATE_BOARD":
		// IMPORT_BOARD <community> [target_dir] [overwrite:0|1] [limit] [offset] [nomerge:0|1] [commentd_db] [legacy:0|1] [dryrun:0|1] [workers]
		if len(parts) < 2 {
			conn.Write([]byte("ERR invalid arguments for IMPORT_BOARD\n"))
			return
		}
		community := parts[1]
		normBoard := strings.ToLower(strings.TrimSpace(community))
		s.activeImportsMu.Lock()
		if s.activeImports[normBoard] {
			s.activeImportsMu.Unlock()
			conn.Write([]byte(fmt.Sprintf("ERR board '%s' is already being imported\n", community)))
			return
		}
		s.activeImports[normBoard] = true
		s.activeImportsMu.Unlock()

		defer func() {
			s.activeImportsMu.Lock()
			delete(s.activeImports, normBoard)
			s.activeImportsMu.Unlock()
		}()

		targetDir := ""
		if len(parts) >= 3 && parts[2] != "-" && parts[2] != "" {
			targetDir = parts[2]
		}
		overwrite := false
		if len(parts) >= 4 && (parts[3] == "1" || strings.ToLower(parts[3]) == "true" || strings.ToLower(parts[3]) == "overwrite") {
			overwrite = true
		}
		limit := 0
		if len(parts) >= 5 {
			limit, _ = strconv.Atoi(parts[4])
		}
		offset := 0
		if len(parts) >= 6 {
			offset, _ = strconv.Atoi(parts[5])
		}
		noMerge := false
		if len(parts) >= 7 && (parts[6] == "1" || strings.ToLower(parts[6]) == "true" || strings.ToLower(parts[6]) == "nomerge") {
			noMerge = true
		}
		commentdDB := ""
		if len(parts) >= 8 && parts[7] != "-" && parts[7] != "" {
			commentdDB = parts[7]
		}

		legacyFormat := false
		if len(parts) >= 9 && (parts[8] == "1" || strings.ToLower(parts[8]) == "true" || strings.ToLower(parts[8]) == "legacy") {
			legacyFormat = true
		}

		dryRun := false
		if len(parts) >= 10 && (parts[9] == "1" || strings.ToLower(parts[9]) == "true" || strings.ToLower(parts[9]) == "dryrun" || strings.ToLower(parts[9]) == "dry-run") {
			dryRun = true
		}

		workers := s.cfg.ImportWorkers
		if len(parts) >= 11 {
			if w, err := strconv.Atoi(parts[10]); err == nil && w > 0 {
				workers = w
			}
		}

		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()

		// Monitor connection disconnect to cancel migration immediately if client drops
		go func() {
			buf := make([]byte, 1024)
			for {
				_, err := reader.Read(buf)
				if err != nil {
					cancel()
					return
				}
			}
		}()

		lastProgress := time.Now()
		opts := importer.ImportBoardOptions{
			Ctx:          ctx,
			Workers:      workers,
			BBSHome:      s.cfg.BBSHome,
			Board:        community,
			RenderTarget: targetDir,
			Overwrite:    overwrite,
			Limit:        limit,
			Offset:       offset,
			IsBig5:       s.cfg.IsBig5(),
			NoMerge:      noMerge,
			CommentdDB:   commentdDB,
			LegacyFormat: legacyFormat,
			DryRun:       dryRun,
			Users:        s.users,
			ProgressFn: func(current, total int) {
				if time.Since(lastProgress) >= 100*time.Millisecond || current == total {
					lastProgress = time.Now()
					if _, err := conn.Write([]byte(fmt.Sprintf("PROGRESS %d %d\n", current, total))); err != nil {
						cancel()
					}
				}
			},
		}

		stats, err := importer.MigrateBoard(s.storage, opts)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		for _, errStr := range stats.ErrorDetails {
			conn.Write([]byte(fmt.Sprintf("ERR_DETAIL %s\n", errStr)))
		}
		conn.Write([]byte(fmt.Sprintf("OK %d %d %d %d %.2f %d %d\n",
			stats.PostsImported, stats.CommentsImported, stats.CrosspostsLogged, stats.Errors, stats.Elapsed.Seconds(),
			stats.DeletedImported, stats.DeletedMarked)))

	case "SET_META_FILE":
		// SET_META_FILE <community> <post_file> <filemode> <money>
		if len(parts) < 5 {
			conn.Write([]byte("ERR invalid arguments for SET_META_FILE\n"))
			return
		}
		filemode, _ := strconv.Atoi(parts[3])
		money, _ := strconv.Atoi(parts[4])
		if err := s.storage.SetPostMeta(parts[1], parts[2], filemode, money); err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte("OK\n"))

	case "SYNC_DIR":
		// SYNC_DIR <community> [dryrun:0|1] [modes: comma separated full,prune,add-missing, or -]
		if len(parts) < 2 {
			conn.Write([]byte("ERR invalid arguments for SYNC_DIR\n"))
			return
		}
		opts := importer.SyncDirOptions{
			BBSHome: s.cfg.BBSHome,
			Board:   parts[1],
			IsBig5:  s.cfg.IsBig5(),
			DryRun:  len(parts) >= 3 && (parts[2] == "1" || strings.ToLower(parts[2]) == "true" || strings.ToLower(parts[2]) == "dryrun"),
		}
		if len(parts) >= 4 && parts[3] != "-" {
			for _, mode := range strings.Split(parts[3], ",") {
				switch mode {
				case "full":
					opts.Full = true
				case "prune":
					opts.Prune = true
				case "add-missing":
					opts.AddMissing = true
				case "only-scores", "":
				default:
					conn.Write([]byte(fmt.Sprintf("ERR unknown SYNC_DIR mode: %s\n", mode)))
					return
				}
			}
		}
		stats, err := importer.SyncDir(s.storage, opts)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d %d %d %d %d %d\n",
			stats.Records, stats.Matched, stats.Updated, stats.Pruned, stats.Added, stats.AddedNoFile)))

	case "RENDER_FILE":
		// RENDER_FILE <community> <post_file> <output_path> [format]
		if len(parts) < 4 {
			conn.Write([]byte("ERR invalid arguments for RENDER_FILE\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		outputPath := parts[3]
		if outputPath != "-" && outputPath != "" && !filepath.IsAbs(outputPath) {
			outputPath = filepath.Join(s.cfg.BBSHome, outputPath)
		}
		legacy := false
		if len(parts) >= 5 && (strings.ToLower(parts[4]) == "legacy" || parts[4] == "1") {
			legacy = true
		}

		p, err := s.storage.GetPostByCommunityFile(community, postFile)
		if err != nil || p == nil {
			p = s.autoImportPost(community, postFile)
		}
		if p == nil {
			conn.Write([]byte(fmt.Sprintf("ERR post not found: %s/%s\n", community, postFile)))
			return
		}
		s.writeRenderOutput(conn, p, outputPath, legacy)

	case "RENDER_COMMUNITY":
		// RENDER_COMMUNITY <community> <target_dir> [format:modern|legacy]
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for RENDER_COMMUNITY\n"))
			return
		}
		community := parts[1]
		targetDir := parts[2]
		legacyFormat := false
		if len(parts) >= 4 && (strings.ToLower(parts[3]) == "legacy" || parts[3] == "1") {
			legacyFormat = true
		}

		lastProgress := time.Now()
		opts := storage.RenderCommunityOptions{
			Community:    community,
			TargetDir:    targetDir,
			LegacyFormat: legacyFormat,
			NumWorkers:   32,
			ProgressFn: func(current, total int) {
				if time.Since(lastProgress) >= 100*time.Millisecond || current == total {
					lastProgress = time.Now()
					conn.Write([]byte(fmt.Sprintf("PROGRESS %d %d\n", current, total)))
				}
			},
		}

		startTime := time.Now()
		totalRendered, err := s.storage.RenderCommunity(opts)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d %.2f\n", totalRendered, time.Since(startTime).Seconds())))

	case "FETCH_POST_FILE", "PULL_POST_FILE":
		// FETCH_POST_FILE <community> <post_file> <target_path> [source_path]
		if len(parts) < 4 {
			conn.Write([]byte("ERR invalid arguments for FETCH_POST_FILE\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		targetPath := parts[3]
		sourcePath := ""
		if len(parts) >= 5 && parts[4] != "-" && parts[4] != "" {
			sourcePath = parts[4]
		}

		if !filepath.IsAbs(targetPath) {
			targetPath = filepath.Join(s.cfg.BBSHome, targetPath)
		}

		p, err := s.storage.GetPostByCommunityFile(community, postFile)
		if err != nil || p == nil {
			if sourcePath != "" {
				srcAbs := sourcePath
				if !filepath.IsAbs(srcAbs) {
					srcAbs = filepath.Join(s.cfg.BBSHome, srcAbs)
				}
				if rawBytes, rErr := os.ReadFile(srcAbs); rErr == nil && len(rawBytes) > 0 {
					postContent := string(rawBytes)
					if s.cfg.IsBig5() && !utf8.Valid(rawBytes) {
						postContent = big5uao.DecodeSGR66(rawBytes)
					}
					t := extractHeaderField(rawBytes, []byte("標題: "), []byte("Title: "))
					if s.cfg.IsBig5() && !utf8.Valid([]byte(t)) {
						t = big5uao.DecodeSGR66([]byte(t))
					}
					autoP := &model.Post{
						Community: community,
						PostFile:  postFile,
						Title:     t,
						Encoding:  "utf-8",
						Content:   postContent,
					}
					p, _ = s.storage.CreatePost(autoP)
				}
			}
			if p == nil {
				p = s.autoImportPost(community, postFile)
			}
		}
		if p == nil {
			conn.Write([]byte(fmt.Sprintf("ERR post not found: %s/%s\n", community, postFile)))
			return
		}

		content := p.Content
		if content == "" && p.PostFile != "" && p.PostFile != "-" {
			rawBytes := s.readDiskPostFile(community, postFile)
			if len(rawBytes) > 0 {
				if s.cfg.IsBig5() && !utf8.Valid(rawBytes) {
					content = big5uao.DecodeSGR66(rawBytes)
				} else {
					content = string(rawBytes)
				}
				_ = s.storage.SetPostContent(p.ID, content)
			}
		}

		var data []byte
		if s.cfg.IsBig5() {
			data = big5uao.Encode(content)
		} else {
			data = []byte(content)
		}

		if err := os.MkdirAll(filepath.Dir(targetPath), 0755); err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR mkdir failed: %v\n", err)))
			return
		}

		tmpTarget := fmt.Sprintf("%s.tmp.%d", targetPath, os.Getpid())
		if err := os.WriteFile(tmpTarget, data, 0644); err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR write file failed: %v\n", err)))
			return
		}
		if err := os.Rename(tmpTarget, targetPath); err != nil {
			os.Remove(tmpTarget)
			conn.Write([]byte(fmt.Sprintf("ERR rename file failed: %v\n", err)))
			return
		}

		titleB := []byte(p.Title)
		m := p.Modified
		if m == 0 {
			m = p.CreatedAt
		}
		if m == 0 {
			m = time.Now().Unix()
		}
		conn.Write([]byte(fmt.Sprintf("OK %d %d\n", m, len(titleB))))
		if len(titleB) > 0 {
			conn.Write(titleB)
		}

	case "UPDATE_POST", "EDIT_POST":
		// UPDATE_POST <post_id> <editor_name> <editor_id> [<expected_modified>] <title_bytes> <content_bytes>\n<title><content>
		if len(parts) < 6 {
			conn.Write([]byte("ERR invalid arguments for UPDATE_POST\n"))
			return
		}
		postID, err := strconv.ParseUint(parts[1], 10, 64)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid post_id: %s\n", parts[1])))
			return
		}
		editor := parts[2]
		editorID := model.ParsePersonID(parts[3])
		var expectedModified int64
		var titleLen, contentLen int
		if len(parts) >= 7 {
			expectedModified, _ = strconv.ParseInt(parts[4], 10, 64)
			titleLen, _ = strconv.Atoi(parts[5])
			contentLen, _ = strconv.Atoi(parts[6])
		} else {
			titleLen, _ = strconv.Atoi(parts[4])
			contentLen, _ = strconv.Atoi(parts[5])
		}

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

		newTitle := string(titleBytes)
		newContent := string(contentBytes)
		newContent = strings.ReplaceAll(newContent, "\r\n", "\n")
		newContent = strings.ReplaceAll(newContent, "\r", "\n")

		rev, newModified, err := s.storage.UpdatePost(postID, newTitle, newContent, editor, editorID, expectedModified)
		if err != nil {
			if cErr, ok := err.(*model.ConflictError); ok {
				conn.Write([]byte(fmt.Sprintf("ERR CONFLICT %d\n", cErr.LatestModified)))
				return
			}
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		if rev == 0 {
			conn.Write([]byte(fmt.Sprintf("OK 0 NO_CHANGE %d\n", newModified)))
		} else {
			conn.Write([]byte(fmt.Sprintf("OK %d %d\n", rev, newModified)))
		}

	case "UPDATE_POST_FILE", "EDIT_POST_FILE":
		// UPDATE_POST_FILE <community> <post_file> <editor_name> <editor_id> [<expected_modified>] <title_bytes> <content_bytes>\n<title><content>
		if len(parts) < 7 {
			conn.Write([]byte("ERR invalid arguments for UPDATE_POST_FILE\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		editor := parts[3]
		editorID := model.ParsePersonID(parts[4])
		var expectedModified int64
		var titleLen, contentLen int
		if len(parts) >= 8 {
			expectedModified, _ = strconv.ParseInt(parts[5], 10, 64)
			titleLen, _ = strconv.Atoi(parts[6])
			contentLen, _ = strconv.Atoi(parts[7])
		} else {
			titleLen, _ = strconv.Atoi(parts[5])
			contentLen, _ = strconv.Atoi(parts[6])
		}

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

		newTitle := string(titleBytes)
		newContent := string(contentBytes)

		rev, newModified, err := s.storage.UpdatePostByCommunityFile(community, postFile, newTitle, newContent, editor, editorID, expectedModified)
		if err != nil {
			if cErr, ok := err.(*model.ConflictError); ok {
				conn.Write([]byte(fmt.Sprintf("ERR CONFLICT %d\n", cErr.LatestModified)))
				return
			}
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		if rev == 0 {
			conn.Write([]byte(fmt.Sprintf("OK 0 NO_CHANGE %d\n", newModified)))
		} else {
			conn.Write([]byte(fmt.Sprintf("OK %d %d\n", rev, newModified)))
		}

	case "UPDATE_POST_TITLE":
		// UPDATE_POST_TITLE <post_id> <editor_name> <editor_id> <title_bytes>\n<title>
		if len(parts) < 5 {
			conn.Write([]byte("ERR invalid arguments for UPDATE_POST_TITLE\n"))
			return
		}
		postID, err := strconv.ParseUint(parts[1], 10, 64)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid post_id: %s\n", parts[1])))
			return
		}
		editor := parts[2]
		editorID := model.ParsePersonID(parts[3])
		titleLen, _ := strconv.Atoi(parts[4])
		titleBytes := make([]byte, titleLen)
		if titleLen > 0 {
			if _, err := io.ReadFull(reader, titleBytes); err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR read title failed: %v\n", err)))
				return
			}
		}
		newTitle := string(titleBytes)
		rev, err := s.storage.UpdatePostTitle(postID, newTitle, editor, editorID)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", rev)))

	case "UPDATE_POST_TITLE_FILE":
		// UPDATE_POST_TITLE_FILE <community> <post_file> <editor_name> <editor_id> <title_bytes>\n<title>
		if len(parts) < 6 {
			conn.Write([]byte("ERR invalid arguments for UPDATE_POST_TITLE_FILE\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		editor := parts[3]
		editorID := model.ParsePersonID(parts[4])
		titleLen, _ := strconv.Atoi(parts[5])
		titleBytes := make([]byte, titleLen)
		if titleLen > 0 {
			if _, err := io.ReadFull(reader, titleBytes); err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR read title failed: %v\n", err)))
				return
			}
		}
		newTitle := string(titleBytes)
		rev, err := s.storage.UpdatePostTitleByCommunityFile(community, postFile, newTitle, editor, editorID)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", rev)))

	case "UPDATE_COMMENT", "EDIT_COMMENT":
		// UPDATE_COMMENT <post_id> <seq> <editor_name> <editor_id> <content_bytes>\n<content>
		if len(parts) < 6 {
			conn.Write([]byte("ERR invalid arguments for UPDATE_COMMENT\n"))
			return
		}
		postID, err := strconv.ParseUint(parts[1], 10, 64)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid post_id: %s\n", parts[1])))
			return
		}
		seqVal, _ := strconv.ParseUint(parts[2], 10, 32)
		editor := parts[3]
		editorID := model.ParsePersonID(parts[4])
		contentLen, _ := strconv.Atoi(parts[5])

		contentBytes := make([]byte, contentLen)
		if contentLen > 0 {
			if _, err := io.ReadFull(reader, contentBytes); err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR read content failed: %v\n", err)))
				return
			}
		}

		newContent := string(contentBytes)

		rev, err := s.storage.UpdateComment(postID, uint32(seqVal), newContent, editor, editorID)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		if rev == 0 {
			conn.Write([]byte("OK 0 NO_CHANGE\n"))
		} else {
			conn.Write([]byte(fmt.Sprintf("OK %d\n", rev)))
		}

	case "UPDATE_COMMENT_FILE", "EDIT_COMMENT_FILE":
		// UPDATE_COMMENT_FILE <community> <post_file> <seq> <editor_name> <editor_id> <content_bytes>\n<content>
		if len(parts) < 7 {
			conn.Write([]byte("ERR invalid arguments for UPDATE_COMMENT_FILE\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		seqVal, _ := strconv.ParseUint(parts[3], 10, 32)
		editor := parts[4]
		editorID := model.ParsePersonID(parts[5])
		contentLen, _ := strconv.Atoi(parts[6])

		contentBytes := make([]byte, contentLen)
		if contentLen > 0 {
			if _, err := io.ReadFull(reader, contentBytes); err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR read content failed: %v\n", err)))
				return
			}
		}

		newContent := string(contentBytes)

		p, err := s.storage.GetPostByCommunityFile(community, postFile)
		if err != nil || p == nil {
			p = s.autoImportPost(community, postFile)
		}
		if p == nil {
			conn.Write([]byte(fmt.Sprintf("ERR post not found: %s/%s\n", community, postFile)))
			return
		}
		rev, err := s.storage.UpdateComment(p.ID, uint32(seqVal), newContent, editor, editorID)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		if rev == 0 {
			conn.Write([]byte("OK 0 NO_CHANGE\n"))
		} else {
			conn.Write([]byte(fmt.Sprintf("OK %d\n", rev)))
		}

	case "DELETE_COMMUNITY", "PURGE_COMMUNITY":
		// DELETE_COMMUNITY <community>
		if len(parts) < 2 {
			conn.Write([]byte("ERR invalid arguments for DELETE_COMMUNITY\n"))
			return
		}
		community := parts[1]
		postsDel, commentsDel, err := s.storage.DeleteCommunity(community)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d %d\n", postsDel, commentsDel)))

	case "PURGE_POST":
		// PURGE_POST <post_id>
		if len(parts) < 2 {
			conn.Write([]byte("ERR invalid arguments for PURGE_POST\n"))
			return
		}
		postID, err := strconv.ParseUint(parts[1], 10, 64)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid post_id: %s\n", parts[1])))
			return
		}
		if err := s.storage.PurgePost(postID); err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte("OK\n"))

	case "PURGE_POST_FILE":
		// PURGE_POST_FILE <community> <post_file>
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for PURGE_POST_FILE\n"))
			return
		}
		if err := s.storage.PurgePostByCommunityFile(parts[1], parts[2]); err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte("OK\n"))

	case "DELETE_POST":
		// DELETE_POST <post_id> <deleter_name> <deleter_id> [reason...]
		if len(parts) < 4 {
			conn.Write([]byte("ERR invalid arguments for DELETE_POST\n"))
			return
		}
		postID, err := strconv.ParseUint(parts[1], 10, 64)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid post_id: %s\n", parts[1])))
			return
		}
		deleter := parts[2]
		deleterID := model.ParsePersonID(parts[3])
		reason := ""
		if len(parts) >= 5 {
			reason = strings.Join(parts[4:], " ")
		}
		rev, err := s.storage.DeletePost(postID, deleter, deleterID, reason)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", rev)))

	case "DELETE_POST_FILE":
		// DELETE_POST_FILE <community> <post_file> <deleter_name> <deleter_id> [reason...]
		if len(parts) < 5 {
			conn.Write([]byte("ERR invalid arguments for DELETE_POST_FILE\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		deleter := parts[3]
		deleterID := model.ParsePersonID(parts[4])
		reason := ""
		if len(parts) >= 6 {
			reason = strings.Join(parts[5:], " ")
		}
		if !utf8.ValidString(reason) {
			reason = big5uao.Decode([]byte(reason))
		}
		rev, err := s.storage.DeletePostByCommunityFile(community, postFile, deleter, deleterID, reason)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", rev)))

	case "UNDELETE_POST":
		// UNDELETE_POST <post_id>
		if len(parts) < 2 {
			conn.Write([]byte("ERR invalid arguments for UNDELETE_POST\n"))
			return
		}
		postID, err := strconv.ParseUint(parts[1], 10, 64)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid post_id: %s\n", parts[1])))
			return
		}
		if err := s.storage.UndeletePost(postID); err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte("OK\n"))

	case "UNDELETE_POST_FILE":
		// UNDELETE_POST_FILE <community> <post_file>
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for UNDELETE_POST_FILE\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		if err := s.storage.UndeletePostByCommunityFile(community, postFile); err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte("OK\n"))

	case "DELETE_COMMENT":
		// DELETE_COMMENT <post_id> <seq> <deleter_name> <deleter_id> [reason...]
		if len(parts) < 5 {
			conn.Write([]byte("ERR invalid arguments for DELETE_COMMENT\n"))
			return
		}
		postID, err := strconv.ParseUint(parts[1], 10, 64)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid post_id: %s\n", parts[1])))
			return
		}
		seqVal, err := strconv.ParseUint(parts[2], 10, 32)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid seq: %s\n", parts[2])))
			return
		}
		deleter := parts[3]
		deleterID := model.ParsePersonID(parts[4])
		reason := ""
		if len(parts) >= 6 {
			reason = strings.Join(parts[5:], " ")
		}
		if !utf8.ValidString(reason) {
			reason = big5uao.Decode([]byte(reason))
		}
		rev, err := s.storage.DeleteComment(postID, uint32(seqVal), deleter, deleterID, reason)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", rev)))

	case "DELETE_COMMENT_FILE":
		// DELETE_COMMENT_FILE <community> <post_file> <seq> <deleter_name> <deleter_id> [reason...]
		if len(parts) < 6 {
			conn.Write([]byte("ERR invalid arguments for DELETE_COMMENT_FILE\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		seqVal, err := strconv.ParseUint(parts[3], 10, 32)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid seq: %s\n", parts[3])))
			return
		}
		deleter := parts[4]
		deleterID := model.ParsePersonID(parts[5])
		reason := ""
		if len(parts) >= 7 {
			reason = strings.Join(parts[6:], " ")
		}
		if !utf8.ValidString(reason) {
			reason = big5uao.Decode([]byte(reason))
		}
		p, err := s.storage.GetPostByCommunityFile(community, postFile)
		if err != nil || p == nil {
			p = s.autoImportPost(community, postFile)
		}
		if p == nil {
			conn.Write([]byte(fmt.Sprintf("ERR post not found: %s/%s\n", community, postFile)))
			return
		}
		rev, err := s.storage.DeleteComment(p.ID, uint32(seqVal), deleter, deleterID, reason)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", rev)))

	case "UNDELETE_COMMENT":
		// UNDELETE_COMMENT <post_id> <seq>
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for UNDELETE_COMMENT\n"))
			return
		}
		postID, err := strconv.ParseUint(parts[1], 10, 64)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid post_id: %s\n", parts[1])))
			return
		}
		seqVal, err := strconv.ParseUint(parts[2], 10, 32)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid seq: %s\n", parts[2])))
			return
		}
		if err := s.storage.UndeleteComment(postID, uint32(seqVal)); err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte("OK\n"))

	case "UNDELETE_COMMENT_FILE":
		// UNDELETE_COMMENT_FILE <community> <post_file> <seq>
		if len(parts) < 4 {
			conn.Write([]byte("ERR invalid arguments for UNDELETE_COMMENT_FILE\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		seqVal, err := strconv.ParseUint(parts[3], 10, 32)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid seq: %s\n", parts[3])))
			return
		}
		p, err := s.storage.GetPostByCommunityFile(community, postFile)
		if err != nil || p == nil {
			p = s.autoImportPost(community, postFile)
		}
		if p == nil {
			conn.Write([]byte(fmt.Sprintf("ERR post not found: %s/%s\n", community, postFile)))
			return
		}
		if err := s.storage.UndeleteComment(p.ID, uint32(seqVal)); err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte("OK\n"))

	case "COMMENTS":
		// COMMENTS <post_id> [start [limit]]
		if len(parts) < 2 {
			conn.Write([]byte("ERR invalid arguments for COMMENTS\n"))
			return
		}
		postID, err := strconv.ParseUint(parts[1], 10, 64)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid post_id: %s\n", parts[1])))
			return
		}
		var startFloor uint32 = 1
		var limit uint32 = 1000
		if len(parts) >= 3 {
			if s, err := strconv.ParseUint(parts[2], 10, 32); err == nil && s > 0 {
				startFloor = uint32(s)
			}
		}
		if len(parts) >= 4 {
			if l, err := strconv.ParseUint(parts[3], 10, 32); err == nil && l > 0 {
				limit = uint32(l)
			}
		}
		comments, err := s.storage.GetComments(postID, startFloor, limit)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		totalCount, _ := s.storage.GetCommentCount(postID)
		var buf bytes.Buffer
		buf.WriteString(fmt.Sprintf("OK %d %d\n", len(comments), totalCount))
		for _, c := range comments {
			buf.WriteString(formatTSVComment(c))
		}
		conn.Write(buf.Bytes())

	case "COMMENTS_FILE":
		// COMMENTS_FILE <community> <post_file> [start [limit [creator_name [creator_id]]]]
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for COMMENTS_FILE\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		var startFloor uint32 = 1
		var limit uint32 = 1000
		if len(parts) >= 4 {
			if s, err := strconv.ParseUint(parts[3], 10, 32); err == nil && s > 0 {
				startFloor = uint32(s)
			}
		}
		if len(parts) >= 5 {
			if l, err := strconv.ParseUint(parts[4], 10, 32); err == nil && l > 0 {
				limit = uint32(l)
			}
		}
		author := ""
		if len(parts) >= 6 && parts[5] != "-" {
			author = parts[5]
		}
		var creatorID model.PersonID
		if len(parts) >= 7 && parts[6] != "-" {
			creatorID = model.ParsePersonID(parts[6])
		}
		p, _ := s.storage.GetPostByCommunityFile(community, postFile)
		if p == nil {
			_ = s.autoImportPost(community, postFile)
		}
		comments, err := s.storage.GetCommentsByCommunityFileFiltered(community, postFile, startFloor, limit, author, creatorID)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		totalCount, _ := s.storage.GetCommentCountByCommunityFile(community, postFile)
		if author != "" {
			totalCount = len(comments)
		}
		var buf bytes.Buffer
		buf.WriteString(fmt.Sprintf("OK %d %d\n", len(comments), totalCount))
		for _, c := range comments {
			buf.WriteString(formatTSVComment(c))
		}
		conn.Write(buf.Bytes())

	case "COMMENT_COUNT":
		// COMMENT_COUNT <post_id>
		if len(parts) < 2 {
			conn.Write([]byte("ERR invalid arguments for COMMENT_COUNT\n"))
			return
		}
		postID, err := strconv.ParseUint(parts[1], 10, 64)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid post_id: %s\n", parts[1])))
			return
		}
		count, err := s.storage.GetCommentCount(postID)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", count)))

	case "COMMENT_COUNT_FILE":
		// COMMENT_COUNT_FILE <community> <post_file>
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for COMMENT_COUNT_FILE\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		count, err := s.storage.GetCommentCountByCommunityFile(community, postFile)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", count)))

	case "POST_HISTORY":
		// POST_HISTORY <post_id> [rev]
		if len(parts) < 2 {
			conn.Write([]byte("ERR invalid arguments for POST_HISTORY\n"))
			return
		}
		postID, err := strconv.ParseUint(parts[1], 10, 64)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid post_id: %s\n", parts[1])))
			return
		}
		var rev uint32
		if len(parts) >= 3 {
			rVal, _ := strconv.ParseUint(parts[2], 10, 32)
			rev = uint32(rVal)
		}

		if rev == 0 {
			revs, err := s.storage.ListPostHistory(postID)
			if err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
				return
			}
			var buf bytes.Buffer
			buf.WriteString(fmt.Sprintf("OK %d\n", len(revs)))
			for _, r := range revs {
				cleanTitle := strings.ReplaceAll(strings.TrimRight(r.Title, "\r\n"), "\t", " ")
				buf.WriteString(fmt.Sprintf("%d\t%d\t%s\t%s\n", r.Revision, r.EditedAt, r.EditorName, cleanTitle))
			}
			conn.Write(buf.Bytes())
		} else {
			r, err := s.storage.GetPostHistory(postID, rev)
			if err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
				return
			}
			titleB := []byte(r.Title)
			contentB := []byte(r.Content)
			conn.Write([]byte(fmt.Sprintf("OK %d %d %d %s %d %d\n",
				r.PostID, r.Revision, r.EditedAt, r.EditorName, len(titleB), len(contentB))))
			conn.Write(titleB)
			conn.Write(contentB)
		}

	case "POST_HISTORY_FILE":
		// POST_HISTORY_FILE <community> <post_file> [rev] [out_file]
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for POST_HISTORY_FILE\n"))
			return
		}
		p, err := s.storage.GetPostByCommunityFile(parts[1], parts[2])
		if err != nil || p == nil {
			p = s.autoImportPost(parts[1], parts[2])
		}
		if p == nil {
			conn.Write([]byte("ERR post not found\n"))
			return
		}
		var rev uint32
		if len(parts) >= 4 {
			rVal, _ := strconv.ParseUint(parts[3], 10, 32)
			rev = uint32(rVal)
		}

		if rev == 0 {
			revs, err := s.storage.ListPostHistory(p.ID)
			if err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
				return
			}
			var buf bytes.Buffer
			buf.WriteString(fmt.Sprintf("OK %d\n", len(revs)))
			for _, r := range revs {
				cleanTitle := strings.ReplaceAll(strings.TrimRight(r.Title, "\r\n"), "\t", " ")
				buf.WriteString(fmt.Sprintf("%d\t%d\t%s\t%s\n", r.Revision, r.EditedAt, r.EditorName, cleanTitle))
			}
			conn.Write(buf.Bytes())
		} else {
			r, err := s.storage.GetPostHistory(p.ID, rev)
			if err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
				return
			}
			titleB := []byte(r.Title)
			contentB := []byte(r.Content)
			if len(parts) >= 5 && parts[4] != "-" && parts[4] != "" {
				targetPath := parts[4]
				if err := os.MkdirAll(filepath.Dir(targetPath), 0755); err != nil {
					conn.Write([]byte(fmt.Sprintf("ERR mkdir failed: %v\n", err)))
					return
				}
				fileContentB := contentB
				if s.cfg.IsBig5() {
					fileContentB = big5uao.Encode(r.Content)
				}
				tmpTarget := fmt.Sprintf("%s.tmp.%d", targetPath, os.Getpid())
				if err := os.WriteFile(tmpTarget, fileContentB, 0644); err != nil {
					conn.Write([]byte(fmt.Sprintf("ERR write file failed: %v\n", err)))
					return
				}
				if err := os.Rename(tmpTarget, targetPath); err != nil {
					os.Remove(tmpTarget)
					conn.Write([]byte(fmt.Sprintf("ERR rename file failed: %v\n", err)))
					return
				}
				conn.Write([]byte(fmt.Sprintf("OK %d %d %d %s %d %d\n",
					r.PostID, r.Revision, r.EditedAt, r.EditorName, len(titleB), len(contentB))))
				return
			}
			conn.Write([]byte(fmt.Sprintf("OK %d %d %d %s %d %d\n",
				r.PostID, r.Revision, r.EditedAt, r.EditorName, len(titleB), len(contentB))))
			conn.Write(titleB)
			conn.Write(contentB)
		}

	case "COMMENT_HISTORY":
		// COMMENT_HISTORY <post_id> <seq> [rev]
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for COMMENT_HISTORY\n"))
			return
		}
		postID, err := strconv.ParseUint(parts[1], 10, 64)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid post_id: %s\n", parts[1])))
			return
		}
		seqVal, _ := strconv.ParseUint(parts[2], 10, 32)
		var rev uint32
		if len(parts) >= 4 {
			rVal, _ := strconv.ParseUint(parts[3], 10, 32)
			rev = uint32(rVal)
		}

		if rev == 0 {
			revs, err := s.storage.ListCommentHistory(postID, uint32(seqVal))
			if err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
				return
			}
			var buf bytes.Buffer
			buf.WriteString(fmt.Sprintf("OK %d\n", len(revs)))
			for _, r := range revs {
				buf.WriteString(formatTSVCommentRevision(r))
			}
			conn.Write(buf.Bytes())
		} else {
			r, err := s.storage.GetCommentHistory(postID, uint32(seqVal), rev)
			if err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
				return
			}
			contentB := []byte(r.Content)
			conn.Write([]byte(fmt.Sprintf("OK %d %d %d %d %s %d\n",
				r.PostID, r.Sequence, r.Revision, r.EditedAt, r.EditorName, len(contentB))))
			conn.Write(contentB)
		}

	case "COMMENT_HISTORY_FILE":
		// COMMENT_HISTORY_FILE <community> <post_file> <seq> [rev]
		if len(parts) < 4 {
			conn.Write([]byte("ERR invalid arguments for COMMENT_HISTORY_FILE\n"))
			return
		}
		p, err := s.storage.GetPostByCommunityFile(parts[1], parts[2])
		if err != nil || p == nil {
			p = s.autoImportPost(parts[1], parts[2])
		}
		if p == nil {
			conn.Write([]byte("ERR post not found\n"))
			return
		}
		seqVal, _ := strconv.ParseUint(parts[3], 10, 32)
		var rev uint32
		if len(parts) >= 5 {
			rVal, _ := strconv.ParseUint(parts[4], 10, 32)
			rev = uint32(rVal)
		}

		if rev == 0 {
			revs, err := s.storage.ListCommentHistory(p.ID, uint32(seqVal))
			if err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
				return
			}
			var buf bytes.Buffer
			buf.WriteString(fmt.Sprintf("OK %d\n", len(revs)))
			for _, r := range revs {
				buf.WriteString(formatTSVCommentRevision(r))
			}
			conn.Write(buf.Bytes())
		} else {
			r, err := s.storage.GetCommentHistory(p.ID, uint32(seqVal), rev)
			if err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
				return
			}
			contentB := []byte(r.Content)
			conn.Write([]byte(fmt.Sprintf("OK %d %d %d %d %s %d\n",
				r.PostID, r.Sequence, r.Revision, r.EditedAt, r.EditorName, len(contentB))))
			conn.Write(contentB)
		}

	case "CROSSPOST":
		// CROSSPOST <src_community> <src_file> <target_community> <target_file> <creator_name> <creator_id> [ctime]
		if len(parts) < 7 {
			conn.Write([]byte("ERR invalid arguments for CROSSPOST\n"))
			return
		}
		srcComm := parts[1]
		srcFile := parts[2]
		targetComm := parts[3]
		targetFile := parts[4]
		operator := parts[5]
		creatorID := model.ParsePersonID(parts[6])
		var ctime int64
		if len(parts) >= 8 {
			ctime, _ = strconv.ParseInt(parts[7], 10, 64)
		}

		rec, count, err := s.storage.AddCrosspost(srcComm, srcFile, targetComm, targetFile, operator, creatorID, ctime)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d %d\n", rec.ID, count)))

	case "CROSSPOSTS", "GET_CROSSPOSTS":
		// CROSSPOSTS <community> <post_file>
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for CROSSPOSTS\n"))
			return
		}
		records, err := s.storage.GetCrossposts(parts[1], parts[2])
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		var buf bytes.Buffer
		buf.WriteString(fmt.Sprintf("OK %d\n", len(records)))
		for _, r := range records {
			buf.WriteString(fmt.Sprintf("%s\t%s\t%s\t%d\n", r.TargetCommunity, r.TargetPostFile, r.CreatorName, r.CreatedAt))
		}
		conn.Write(buf.Bytes())

	case "ORIGIN", "GET_ORIGIN":
		// ORIGIN <target_community> <target_post_file>
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for ORIGIN\n"))
			return
		}
		srcPost, r, err := s.storage.GetOriginPost(parts[1], parts[2])
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		if srcPost == nil {
			conn.Write([]byte("ERR not a crosspost\n"))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %s\t%s\t%s\t%s\t%s\t%d\n",
			srcPost.Community, srcPost.PostFile, srcPost.CreatorName, srcPost.Title, r.CreatorName, r.CreatedAt)))

	default:
		conn.Write([]byte(fmt.Sprintf("ERR unknown command: %s\n", cmd)))
	}
}

func formatTSVComment(c *model.Comment) string {
	cleanReason := strings.ReplaceAll(strings.TrimRight(c.Reason, "\r\n"), "\t", " ")
	if !utf8.ValidString(cleanReason) {
		cleanReason = big5uao.Decode([]byte(cleanReason))
	}
	typeVal := int(c.LegacyType)
	if c.DeletedOrRemoved() {
		typeVal = -1
	}

	rawContent := strings.ReplaceAll(c.Content, "\r\n", "\n")
	rawContent = strings.ReplaceAll(rawContent, "\r", "\n")
	rawLines := strings.Split(rawContent, "\n")
	var contentLines []string
	for _, l := range rawLines {
		trimmed := strings.TrimRight(l, "\r ")
		if trimmed != "" {
			contentLines = append(contentLines, strings.ReplaceAll(trimmed, "\t", " "))
		}
	}
	contentField := strings.Join(contentLines, "\t")
	if contentField == "" {
		contentField = " "
	}

	return fmt.Sprintf("%d\t%s\t%d\t%s\t%d\t%s\t%s\n",
		c.Sequence, c.CreatorName, c.CreatedAt, c.IP, typeVal, cleanReason, contentField)
}

func formatTSVCommentRevision(r *model.CommentRevision) string {
	action := r.Action
	if action == "" {
		action = "edit"
	}
	cleanReason := strings.ReplaceAll(strings.TrimRight(r.Reason, "\r\n"), "\t", " ")
	if !utf8.ValidString(cleanReason) {
		cleanReason = big5uao.Decode([]byte(cleanReason))
	}
	if cleanReason == "" {
		cleanReason = "-"
	}

	rawContent := strings.ReplaceAll(r.Content, "\r\n", "\n")
	rawContent = strings.ReplaceAll(rawContent, "\r", "\n")
	rawLines := strings.Split(rawContent, "\n")
	var contentLines []string
	for _, l := range rawLines {
		trimmed := strings.TrimRight(l, "\r ")
		if trimmed != "" {
			contentLines = append(contentLines, strings.ReplaceAll(trimmed, "\t", " "))
		}
	}
	contentField := strings.Join(contentLines, "\t")
	if contentField == "" {
		contentField = " "
	}
	return fmt.Sprintf("%d\t%d\t%s\t%s\t%s\t%s\n",
		r.Revision, r.EditedAt, r.EditorName, action, cleanReason, contentField)
}
