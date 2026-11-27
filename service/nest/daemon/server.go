package daemon

import (
	"bufio"
	"bytes"
	"context"
	"encoding/hex"
	"errors"
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
	"pttbbs/nest/importer"
	"pttbbs/nest/lemmy"
	"pttbbs/nest/model"
	"pttbbs/nest/storage"
)

type ServerConfig struct {
	BBSHome        string
	UnixSocket     string // e.g. "$BBSHOME/run/nest.svc.sock"
	FilterEncoding string // "big5" (default) or "utf-8"
	ImportWorkers  int    // Default worker threads for board import (default: 8)
	// Users resolves the person IDs of the users named in legacy articles and
	// .DIR (IMPORT_BOARD, and posts imported on demand). Optional.
	Users importer.UserResolver
	// SiteURL makes the ActivityPub IDs in the Lemmy API (see package lemmy).
	SiteURL string
	// SyncBoardsAtStart mirrors all of $BBSHOME/.BRD when the server
	// starts, for what changed while it was down (see SyncBoards).
	SyncBoardsAtStart bool
	// Crosspost limits the crossposts that CHECK_CROSSPOST allows.
	Crosspost storage.CrosspostLimits
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
	importMu        sync.Mutex // serializes postByFile imports
	users           importer.UserResolver

	// ctx is cancelled by Stop, which then closes the connections still
	// open and waits for their handlers, so that the storage is not closed
	// under a command still running (e.g. IMPORT_BOARD).
	ctx      context.Context
	cancel   context.CancelFunc
	connsMu  sync.Mutex
	conns    map[net.Conn]struct{}
	handlers sync.WaitGroup
}

func NewServer(cfg ServerConfig, st storage.Storage) *Server {
	if cfg.BBSHome == "" {
		cfg.BBSHome = "/home/bbs"
	}
	if cfg.UnixSocket == "" {
		cfg.UnixSocket = filepath.Join(cfg.BBSHome, "run", "nest.svc.sock")
	}
	if cfg.ImportWorkers <= 0 {
		cfg.ImportWorkers = 8
	}
	ctx, cancel := context.WithCancel(context.Background())
	return &Server{
		cfg:           cfg,
		storage:       st,
		users:         importer.NewCachedResolver(cfg.Users),
		quit:          make(chan struct{}),
		activeImports: make(map[string]bool),
		ctx:           ctx,
		cancel:        cancel,
		conns:         make(map[net.Conn]struct{}),
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
		log.Printf("[nest.svc] Warning: failed to mkdir for unix socket: %v", err)
	}
	lnUnix, err := net.Listen("unix", s.cfg.UnixSocket)
	if err != nil {
		return fmt.Errorf("failed to listen unix socket on %s: %w", s.cfg.UnixSocket, err)
	}
	_ = os.Chmod(s.cfg.UnixSocket, 0660)
	s.listeners = append(s.listeners, lnUnix)
	s.wg.Add(1)
	go func() {
		defer s.wg.Done()
		s.acceptLoop(lnUnix, s.handleIPC)
	}()
	log.Printf("[nest.svc] Modern IPC socket listening on %s", s.cfg.UnixSocket)
	if s.cfg.SyncBoardsAtStart {
		if _, err := s.SyncBoards(); err != nil && !os.IsNotExist(err) {
			log.Printf("[nest.svc] Cannot mirror .BRD: %v", err)
		}
	}
	return nil
}

// Stop stops accepting connections, cancels the commands that can be
// (IMPORT_BOARD), closes the connections and waits until no handler runs,
// so the caller may close the storage then.
func (s *Server) Stop() {
	close(s.quit)
	for _, ln := range s.listeners {
		ln.Close()
	}
	s.wg.Wait()
	s.cancel()
	s.connsMu.Lock()
	for conn := range s.conns {
		conn.Close()
	}
	s.connsMu.Unlock()
	s.handlers.Wait()
}

func (s *Server) acceptLoop(ln net.Listener, handler func(net.Conn)) {
	for {
		conn, err := ln.Accept()
		if err != nil {
			select {
			case <-s.quit:
				return
			default:
				log.Printf("[nest.svc] accept error: %v", err)
				return
			}
		}
		s.connsMu.Lock()
		s.conns[conn] = struct{}{}
		s.handlers.Add(1)
		s.connsMu.Unlock()
		go func() {
			defer func() {
				s.connsMu.Lock()
				delete(s.conns, conn)
				s.connsMu.Unlock()
				s.handlers.Done()
			}()
			handler(conn)
		}()
	}
}

// -----------------------------------------------------------------------------
// Protocol A: Header Line + Length-Prefixed Payload Handler
// -----------------------------------------------------------------------------

// postByFile returns the post postFile (its M., L. or X. name) of
// community. A legacy post that nest.svc does not have yet is imported
// first, from its .DIR record and article, the way IMPORT_BOARD imports it
// (importer.ImportPost), so that every command works on posts from before
// the switch to nest.svc.
func (s *Server) postByFile(community, postFile string) (*model.Post, error) {
	if community == "" || postFile == "" || postFile == "-" {
		return nil, fmt.Errorf("post not found: %s/%s", community, postFile)
	}
	if p, err := s.storage.GetPostByCommunityFile(community, postFile); err == nil && p != nil {
		return p, nil
	}
	// One import at a time, so that two commands on the same new post do
	// not both import it.
	s.importMu.Lock()
	defer s.importMu.Unlock()
	p, err := importer.ImportPost(s.storage, importer.ImportBoardOptions{
		BBSHome: s.cfg.BBSHome,
		Board:   community,
		IsBig5:  s.cfg.IsBig5(),
		Users:   s.users,
	}, postFile)
	if err != nil {
		if !errors.Is(err, importer.ErrPostNotFound) {
			log.Printf("[nest.svc] Cannot import %s/%s: %v", community, postFile, err)
		}
		return nil, fmt.Errorf("post not found: %s/%s", community, postFile)
	}
	return p, nil
}

// autoImportPost is postByFile for the callers that only need the post.
func (s *Server) autoImportPost(community, postFile string) *model.Post {
	p, _ := s.postByFile(community, postFile)
	return p
}

// readDiskPostFile reads the article file of a post from its board.
func (s *Server) readDiskPostFile(community, postFile string) []byte {
	if postFile == "" || postFile == "-" {
		return nil
	}
	if boardDir, err := importer.LocateBoardDir(s.cfg.BBSHome, community); err == nil {
		if b, rErr := importer.ReadArticleFile(filepath.Join(boardDir, filepath.Base(postFile))); rErr == nil && len(b) > 0 {
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

// checkOutputPath refuses to write over anything but a regular file (e.g.
// /dev/null), and, if inBBSHome, anywhere outside BBSHOME.
func (s *Server) checkOutputPath(path string, inBBSHome bool) error {
	clean := filepath.Clean(path)
	if inBBSHome {
		home := filepath.Clean(s.cfg.BBSHome)
		if !strings.HasPrefix(clean, home+string(filepath.Separator)) {
			return fmt.Errorf("target outside BBSHOME: %s", path)
		}
	}
	if fi, err := os.Lstat(clean); err == nil && !fi.Mode().IsRegular() && !fi.IsDir() {
		return fmt.Errorf("target is not a regular file: %s", path)
	}
	return nil
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
		targetPath = filepath.Join(targetPath, boardFileName(p))
	} else if base := filepath.Base(targetPath); base != boardFileName(p) && model.CanonicalPostFile(base) == p.PostFile {
		// The file of the post under another of its names: mbbsd names a
		// locked post L.<stamp> in .DIR but keeps its file as X.<stamp>
		// (bof_fs_lock_post), and asks to render the .DIR name. Write the
		// file the board has, so no stray L. file appears.
		targetPath = filepath.Join(filepath.Dir(targetPath), boardFileName(p))
	}
	if err := s.checkOutputPath(targetPath, false); err != nil {
		conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
		return
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

// boardFileName returns the name of the board file of p: X.<stamp> while it
// is locked (see model.CanonicalPostFile).
func boardFileName(p *model.Post) string {
	if p.Locked {
		return model.LockedFileName(p.PostFile)
	}
	return p.PostFile
}

// decodeText converts text that mbbsd sent in Big5 to UTF-8.
func (s *Server) decodeText(text string) string {
	if s.cfg.IsBig5() && !utf8.ValidString(text) {
		return big5uao.Decode([]byte(text))
	}
	return text
}

// resolveCommunityRef resolves an active community name, or #<id> for any
// registered community (including taken-down ones), to its ID.
func (s *Server) resolveCommunityRef(ref string) (int64, error) {
	reg := s.storage.Communities()
	if strings.HasPrefix(ref, "#") {
		id, err := strconv.ParseInt(ref[1:], 10, 64)
		if err != nil || id <= 0 {
			return 0, fmt.Errorf("invalid community id: %s", ref)
		}
		if _, ok := reg.Get(id); !ok {
			return 0, fmt.Errorf("%w: %s", storage.ErrCommunityNotFound, ref)
		}
		return id, nil
	}
	id, ok := reg.Lookup(ref)
	if !ok {
		return 0, fmt.Errorf("%w: %s", storage.ErrCommunityNotFound, ref)
	}
	return id, nil
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
	case "API":
		// API <method> <json_bytes>\n<json>
		// Lemmy's API (see package lemmy). Replies OK <json_bytes>\n<json>,
		// or ERR <lemmy error>.
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for API\n"))
			return
		}
		n, err := strconv.Atoi(parts[2])
		if err != nil || n < 0 || n > 1<<20 {
			conn.Write([]byte(fmt.Sprintf("ERR invalid json_bytes: %s\n", parts[2])))
			return
		}
		req := make([]byte, n)
		if _, err := io.ReadFull(reader, req); err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR read request failed: %v\n", err)))
			return
		}
		api := &lemmy.Service{Storage: s.storage, SiteURL: s.cfg.SiteURL}
		resp, err := api.Handle(parts[1], req)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", len(resp))))
		conn.Write(resp)

	case "PING":
		conn.Write([]byte("PONG\n"))

	case "BACKUP":
		// BACKUP [<name> | <absolute_path>]
		//   <name>: each engine writes to <DataDir>/backup/<name> on its own disk
		//           (default name: current local time, YYYYMMDD-HHMMSS).
		//   <absolute_path>: written there (<path>/shard-<N> with multiple engines).
		// Reply: OK <elapsed_ms> <count>\n followed by <count> lines of written dirs.
		dest := strings.Join(parts[1:], " ")
		if dest == "" {
			dest = time.Now().Format("20060102-150405")
		}
		if !filepath.IsAbs(dest) && !storage.IsValidBackupName(dest) {
			conn.Write([]byte("ERR BACKUP destination must be a name ([A-Za-z0-9._-]) or an absolute path\n"))
			return
		}
		start := time.Now()
		dirs, err := s.storage.Backup(dest)
		if err != nil {
			log.Printf("[nest.svc] Backup %s failed: %v", dest, err)
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		elapsed := time.Since(start)
		log.Printf("[nest.svc] Backup %s written in %v: %s", dest, elapsed, strings.Join(dirs, ", "))
		var resp strings.Builder
		fmt.Fprintf(&resp, "OK %d %d\n", elapsed.Milliseconds(), len(dirs))
		for _, dir := range dirs {
			resp.WriteString(dir + "\n")
		}
		conn.Write([]byte(resp.String()))

	case "CREATE_POST_FILE":
		// CREATE_POST_FILE <community> <post_file> <creator_name> <creator_id> <filemode> <money> <ctime> <title_bytes>\n<title>
		if len(parts) < 9 {
			conn.Write([]byte("ERR invalid arguments for CREATE_POST_FILE\n"))
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
			_, title = importer.ArticleHeader(rawBytes, s.cfg.IsBig5())
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

	case "CREATE_POST":
		// CREATE_POST <community> <post_file> <creator_name> <creator_id> <filemode> <money> <ctime> <parent_id> <title_bytes> <content_bytes>\n<title><content>
		if len(parts) < 11 {
			conn.Write([]byte("ERR invalid arguments for CREATE_POST\n"))
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

	case "CREATE_COMMENT":
		// CREATE_COMMENT <community> <post_file> <creator_name> <creator_id> <ip> <ctime> <content_bytes>\n<content>
		if len(parts) < 8 {
			conn.Write([]byte("ERR invalid arguments for CREATE_COMMENT\n"))
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
		// OK <seq> <ctime> <score> NEW|MERGED <comment_id>; a merged comment
		// has the ID of the floor it was merged into.
		status := "NEW"
		if added.IsMerged {
			status = "MERGED"
		}
		conn.Write([]byte(fmt.Sprintf("OK %d %d %d %s %d\n", added.Sequence, c.CreatedAt, p.Upvotes-p.Downvotes, status, added.ID)))

	case "CREATE_POST_LIKE_FILE":
		// CREATE_POST_LIKE_FILE <community> <post_file> <person_name> <person_id> <vote> [ip] [ctime]\n
		if len(parts) < 6 {
			conn.Write([]byte("ERR invalid arguments for CREATE_POST_LIKE_FILE\n"))
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

	case "CREATE_POST_LIKE":
		// CREATE_POST_LIKE <community> <post_id> <person_name> <person_id> <vote> [ip] [ctime]\n
		// or: CREATE_POST_LIKE <post_id> <person_name> <person_id> <vote> [ip] [ctime]\n
		// (Also supports legacy CREATE_POST_LIKE <community> <post_file> <person_name> <person_id> <vote> [ip] [ctime])
		if len(parts) < 5 {
			conn.Write([]byte("ERR invalid arguments for CREATE_POST_LIKE\n"))
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
				// CREATE_POST_LIKE <community> <post_id> <person_name> <person_id> <vote> [ip] [ctime]
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
				// Legacy: CREATE_POST_LIKE <community> <post_file> <person_name> <person_id> <vote> [ip] [ctime]
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
			// CREATE_POST_LIKE <post_id> <person_name> <person_id> <vote>
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

	case "LIST_POST_LIKES":
		// LIST_POST_LIKES <post_id>
		if len(parts) < 2 {
			conn.Write([]byte("ERR invalid arguments for LIST_POST_LIKES\n"))
			return
		}
		postID, err := strconv.ParseUint(parts[1], 10, 64)
		if err != nil {
			conn.Write([]byte("ERR invalid post_id for LIST_POST_LIKES\n"))
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

	case "LIST_POST_LIKES_FILE":
		// LIST_POST_LIKES_FILE <community> <post_file>
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for LIST_POST_LIKES_FILE\n"))
			return
		}
		if !s.ensurePost(conn, parts[1], parts[2]) {
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

	case "RESET_POST_SCORE_FILE":
		// RESET_POST_SCORE_FILE <community> <post_file>\n
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for RESET_POST_SCORE_FILE\n"))
			return
		}
		if !s.ensurePost(conn, parts[1], parts[2]) {
			return
		}
		if err := s.storage.ResetVotesByCommunityFile(parts[1], parts[2]); err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte("OK\n"))

	case "GET_POST_FILE":
		// GET_POST_FILE <community> <post_file>\n
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for GET_POST_FILE\n"))
			return
		}
		p, err := s.postByFile(parts[1], parts[2])
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

	case "GET_POST":
		// GET_POST <id>\n
		if len(parts) < 2 {
			conn.Write([]byte("ERR invalid arguments for GET_POST\n"))
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

	case "PURGE_PERSON_COMMENTS":
		// PURGE_PERSON_COMMENTS <author> <days>\n
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for PURGE_PERSON_COMMENTS\n"))
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

	case "PURGE_PERSON_POSTS":
		// PURGE_PERSON_POSTS <author> <days>\n
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for PURGE_PERSON_POSTS\n"))
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

	case "GET_POSTS":
		// GET_POSTS <community> [count [start]]
		if len(parts) < 2 || parts[1] == "" {
			conn.Write([]byte("ERR invalid arguments for GET_POSTS\n"))
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

	case "IMPORT_BOARD":
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

		ctx, cancel := context.WithCancel(s.ctx)
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

		stats, err := importer.ImportBoard(s.storage, opts)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		for _, errStr := range stats.ErrorDetails {
			conn.Write([]byte(fmt.Sprintf("ERR_DETAIL %s\n", errStr)))
		}
		conn.Write([]byte(fmt.Sprintf("OK %d %d %d %d %.2f %d %d %d\n",
			stats.PostsImported, stats.CommentsImported, stats.CrosspostsLogged, stats.Errors, stats.Elapsed.Seconds(),
			stats.DeletedImported, stats.DeletedMarked, stats.MissingFiles)))

	case "SET_META_FILE":
		// SET_META_FILE <community> <post_file> <filemode> <money>
		if len(parts) < 5 {
			conn.Write([]byte("ERR invalid arguments for SET_META_FILE\n"))
			return
		}
		filemode, _ := strconv.Atoi(parts[3])
		money, _ := strconv.Atoi(parts[4])
		if !s.ensurePost(conn, parts[1], parts[2]) {
			return
		}
		if err := s.storage.SetPostMeta(parts[1], parts[2], filemode, money); err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte("OK\n"))

	case "SYNC_DIR":
		// SYNC_DIR <board> [dryrun:0|1] [modes: comma separated full,prune,add-missing,rebuild, or -]
		// Replies OK <records> <matched> <updated> <pruned> <added> <added_no_file> <rendered> <imported> <kept>.
		// rebuild writes .DIR purely from nest.svc and renders the missing
		// article files into the board directory; it first imports the
		// posts of the old records that nest.svc does not have, and keeps
		// the records it cannot import as they are.
		if len(parts) < 2 {
			conn.Write([]byte("ERR invalid arguments for SYNC_DIR\n"))
			return
		}
		opts := importer.SyncDirOptions{
			BBSHome: s.cfg.BBSHome,
			Board:   parts[1],
			IsBig5:  s.cfg.IsBig5(),
			Users:   s.users,
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
				case "rebuild":
					opts.Rebuild = true
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
		conn.Write([]byte(fmt.Sprintf("OK %d %d %d %d %d %d %d %d %d\n",
			stats.Records, stats.Matched, stats.Updated, stats.Pruned, stats.Added, stats.AddedNoFile, stats.Rendered,
			stats.Imported, stats.Kept)))

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
		// target_dir "boards" renders into the board directory of the
		// community: the cache that mbbsd reads.
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
			BoardFiles:   targetDir == "boards",
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
		// Replies OK <modified> <title_len>\n<title>. target_path "-" writes
		// nothing (to learn the version only); other targets must be
		// (regular) files under BBSHOME.
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

		noFile := targetPath == "-"
		if !noFile {
			if !filepath.IsAbs(targetPath) {
				targetPath = filepath.Join(s.cfg.BBSHome, targetPath)
			}
			if err := s.checkOutputPath(targetPath, true); err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
				return
			}
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
					_, t := importer.ArticleHeader(rawBytes, s.cfg.IsBig5())
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

		if !noFile {
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

	case "EDIT_POST":
		// EDIT_POST <post_id> <editor_name> <editor_id> [<expected_modified>] <title_bytes> <content_bytes>\n<title><content>
		if len(parts) < 6 {
			conn.Write([]byte("ERR invalid arguments for EDIT_POST\n"))
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

	case "EDIT_POST_FILE":
		// EDIT_POST_FILE <community> <post_file> <editor_name> <editor_id> [<expected_modified>] <title_bytes> <content_bytes> [<edit_mark>]\n<title><content>
		// edit_mark: model.Post.BBSEditMark as hex UTF-8; "-" or none keeps it.
		if len(parts) < 7 {
			conn.Write([]byte("ERR invalid arguments for EDIT_POST_FILE\n"))
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

		if !s.ensurePost(conn, community, postFile) {
			return
		}
		rev, newModified, err := s.storage.UpdatePostByCommunityFile(community, postFile, newTitle, newContent, editor, editorID, expectedModified)
		if err != nil {
			if cErr, ok := err.(*model.ConflictError); ok {
				conn.Write([]byte(fmt.Sprintf("ERR CONFLICT %d\n", cErr.LatestModified)))
				return
			}
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		if len(parts) >= 9 && parts[8] != "-" {
			if mark, err := hex.DecodeString(parts[8]); err == nil {
				_ = s.storage.SetPostEditMarkByCommunityFile(community, postFile, string(mark))
			}
		}
		if rev == 0 {
			conn.Write([]byte(fmt.Sprintf("OK 0 NO_CHANGE %d\n", newModified)))
		} else {
			conn.Write([]byte(fmt.Sprintf("OK %d %d\n", rev, newModified)))
		}

	case "EDIT_POST_TITLE":
		// EDIT_POST_TITLE <post_id> <editor_name> <editor_id> <title_bytes>\n<title>
		if len(parts) < 5 {
			conn.Write([]byte("ERR invalid arguments for EDIT_POST_TITLE\n"))
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

	case "EDIT_POST_TITLE_FILE":
		// EDIT_POST_TITLE_FILE <community> <post_file> <editor_name> <editor_id> <title_bytes>\n<title>
		if len(parts) < 6 {
			conn.Write([]byte("ERR invalid arguments for EDIT_POST_TITLE_FILE\n"))
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
		if !s.ensurePost(conn, community, postFile) {
			return
		}
		rev, err := s.storage.UpdatePostTitleByCommunityFile(community, postFile, newTitle, editor, editorID)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", rev)))

	case "EDIT_COMMENT":
		// EDIT_COMMENT <comment_id> <editor_name> <editor_id> <content_bytes>\n<content>
		if len(parts) < 5 {
			conn.Write([]byte("ERR invalid arguments for EDIT_COMMENT\n"))
			return
		}
		c, ok := s.commentByID(conn, parts[1])
		if !ok {
			return
		}
		postID, seqVal := c.PostID, c.Sequence
		editor := parts[2]
		editorID := model.ParsePersonID(parts[3])
		contentLen, _ := strconv.Atoi(parts[4])

		contentBytes := make([]byte, contentLen)
		if contentLen > 0 {
			if _, err := io.ReadFull(reader, contentBytes); err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR read content failed: %v\n", err)))
				return
			}
		}

		newContent := string(contentBytes)

		rev, err := s.storage.UpdateComment(postID, seqVal, newContent, editor, editorID)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		if rev == 0 {
			conn.Write([]byte("OK 0 NO_CHANGE\n"))
		} else {
			conn.Write([]byte(fmt.Sprintf("OK %d\n", rev)))
		}

	case "EDIT_COMMENT_FILE":
		// EDIT_COMMENT_FILE <community> <post_file> <seq> <editor_name> <editor_id> <content_bytes>\n<content>
		if len(parts) < 7 {
			conn.Write([]byte("ERR invalid arguments for EDIT_COMMENT_FILE\n"))
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

	case "REMOVE_COMMUNITY":
		// REMOVE_COMMUNITY <community> [<mod_person_id> [<reason>]]
		// Lemmy's RemoveCommunity: the site takes the community down (mbbsd
		// does when a board is deleted), by mod (0: the system) for reason
		// (may be Big5), recorded as Lemmy's mod_remove_community. The
		// posts are kept under the ID of the community (until
		// PURGE_COMMUNITY), and the name is free for a new board.
		// Replies OK <id>, or OK 0 if nest.svc does not know the community.
		if len(parts) < 2 || parts[1] == "" {
			conn.Write([]byte("ERR invalid arguments for REMOVE_COMMUNITY\n"))
			return
		}
		var mod model.PersonID
		if len(parts) >= 3 {
			mod = model.ParsePersonID(parts[2])
		}
		reason := ""
		if len(parts) >= 4 {
			reason = s.decodeText(strings.Join(parts[3:], " "))
		}
		id, err := s.storage.RemoveCommunity(parts[1], mod, reason)
		if err != nil && !errors.Is(err, storage.ErrCommunityNotFound) {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", id)))

	case "PURGE_COMMUNITY":
		// PURGE_COMMUNITY <community>|#<id>
		// Removes all posts, comments, votes and cache of the community
		// (which may be taken down when given by ID) and unregisters it.
		// Replies OK <id> <posts> <comments>.
		if len(parts) < 2 || parts[1] == "" {
			conn.Write([]byte("ERR invalid arguments for PURGE_COMMUNITY\n"))
			return
		}
		id, err := s.resolveCommunityRef(parts[1])
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		postsDel, commentsDel, err := s.storage.PurgeCommunity(id)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d %d %d\n", id, postsDel, commentsDel)))

	case "RENAME_COMMUNITY":
		// RENAME_COMMUNITY <old> <new>
		// Replies OK <id>, or OK 0 if nest.svc does not know <old>.
		if len(parts) < 3 || parts[1] == "" || parts[2] == "" {
			conn.Write([]byte("ERR invalid arguments for RENAME_COMMUNITY\n"))
			return
		}
		id, err := s.storage.RenameCommunity(parts[1], parts[2])
		if err != nil && !errors.Is(err, storage.ErrCommunityNotFound) {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", id)))

	case "MERGE_COMMUNITY":
		// MERGE_COMMUNITY <from> <to>
		// Moves the posts of <from> to <to>, except those whose post file
		// <to> already has. Replies OK <moved> <collisions>.
		if len(parts) < 3 || parts[1] == "" || parts[2] == "" {
			conn.Write([]byte("ERR invalid arguments for MERGE_COMMUNITY\n"))
			return
		}
		moved, collisions, err := s.storage.MergeCommunity(parts[1], parts[2])
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d %d\n", moved, collisions)))

	case "MOVE_COMMUNITY":
		// MOVE_COMMUNITY <community> <shard>
		// Moves the community, with its posts (which keep their IDs), to
		// engine <shard>. Replies OK <moved>.
		if len(parts) < 3 || parts[1] == "" {
			conn.Write([]byte("ERR invalid arguments for MOVE_COMMUNITY\n"))
			return
		}
		shard, err := strconv.Atoi(parts[2])
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid shard: %s\n", parts[2])))
			return
		}
		moved, err := s.storage.MoveCommunity(parts[1], shard)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", moved)))

	case "LIST_COMMUNITIES":
		// LIST_COMMUNITIES
		// Replies OK <count>, then one line per community, tab-separated:
		// <id> <name> <published> <deleted_at> <flags> <category> <title> <moderators>
		// where moderators is a comma-separated list of names (or person IDs).
		list := s.storage.Communities().List()
		var buf bytes.Buffer
		buf.WriteString(fmt.Sprintf("OK %d\n", len(list)))
		clean := strings.NewReplacer("\t", " ", "\n", " ", "\r", " ")
		for _, c := range list {
			var mods []string
			for _, m := range c.Moderators {
				if m.PersonName != "" {
					mods = append(mods, m.PersonName)
				} else {
					mods = append(mods, m.PersonID.String())
				}
			}
			buf.WriteString(fmt.Sprintf("%d\t%s\t%d\t%d\t%d\t%s\t%s\t%s\n", c.ID, c.Name, c.Published, c.DeletedAt, c.Flags,
				clean.Replace(c.Category), clean.Replace(c.Title), strings.Join(mods, ",")))
		}
		conn.Write(buf.Bytes())

	case "GET_COMMUNITY":
		// GET_COMMUNITY <community>|#<id>
		// Replies OK <bytes>, then the community as RFC 822 headers (see
		// model.EncodeRFC822Community).
		if len(parts) < 2 || parts[1] == "" {
			conn.Write([]byte("ERR invalid arguments for GET_COMMUNITY\n"))
			return
		}
		id, err := s.resolveCommunityRef(parts[1])
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		c, _ := s.storage.Communities().Get(id)
		payload := model.EncodeRFC822Community(&c)
		conn.Write([]byte(fmt.Sprintf("OK %d\n", len(payload))))
		conn.Write(payload)

	case "EDIT_COMMUNITY":
		// EDIT_COMMUNITY <bid>
		// Lemmy's EditCommunity (and CreateCommunity), as BoF sends it once
		// it has written slot <bid> of .BRD: nest.svc mirrors that slot
		// (its fields are read from .BRD, the source of truth). Replies
		// OK <id>, or OK 0 for an empty slot or a link.
		bid, err := strconv.Atoi(strings.TrimSpace(strings.Join(parts[1:], " ")))
		if len(parts) < 2 || err != nil || bid < 1 {
			conn.Write([]byte("ERR invalid arguments for EDIT_COMMUNITY\n"))
			return
		}
		id, err := importer.ImportBoardHeader(s.storage, importer.ImportBoardsOptions{
			BBSHome: s.cfg.BBSHome,
			IsBig5:  s.cfg.IsBig5(),
			Users:   s.users,
		}, bid)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", id)))

	case "UPSERT_COMMUNITY":
		// UPSERT_COMMUNITY <community> <bytes>\n<RFC 822 community>
		// Mirrors a board header (see model.EncodeRFC822Community): sets
		// the fields of the community (registering it if needed) and
		// replaces its moderators, whose names are looked up if they are
		// still the same accounts. Text in Big5 is converted to UTF-8.
		// BoF sends EDIT_COMMUNITY instead; this is for tools.
		// Replies OK <id>.
		if len(parts) < 3 || parts[1] == "" {
			conn.Write([]byte("ERR invalid arguments for UPSERT_COMMUNITY\n"))
			return
		}
		n, err := strconv.Atoi(parts[2])
		if err != nil || n < 0 || n > 1<<20 {
			conn.Write([]byte(fmt.Sprintf("ERR invalid payload size: %s\n", parts[2])))
			return
		}
		payload := make([]byte, n)
		if _, err := io.ReadFull(reader, payload); err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR read payload failed: %v\n", err)))
			return
		}
		// The name is in the command; a payload may leave it out.
		payload = append([]byte("Name: "+parts[1]+"\n"), payload...)
		c, err := model.DecodeRFC822Community(payload, s.decodeText)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		c.Name = parts[1]
		// mbbsd sends only the person IDs of the moderators.
		if s.users != nil {
			for i, m := range c.Moderators {
				if userid, firstlogin, ok := s.users.LookupUsernum(m.PersonID.Usernum()); ok && firstlogin == m.PersonID.Firstlogin() {
					c.Moderators[i].PersonName = userid
				}
			}
		}
		id, err := s.storage.Communities().Upsert(c)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", id)))

	case "IMPORT_BOARDS":
		// IMPORT_BOARDS [dryrun:0|1]
		// Mirrors every board of $BBSHOME/.BRD. Replies an ERR_DETAIL line
		// per board that failed, then OK <records> <upserted> <skipped> <failed>.
		stats, err := importer.ImportBoards(s.storage, importer.ImportBoardsOptions{
			BBSHome: s.cfg.BBSHome,
			IsBig5:  s.cfg.IsBig5(),
			Users:   s.users,
			DryRun:  len(parts) >= 2 && (parts[1] == "1" || strings.EqualFold(parts[1], "dryrun")),
		})
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		for _, e := range stats.Errors {
			conn.Write([]byte(fmt.Sprintf("ERR_DETAIL %s\n", e)))
		}
		conn.Write([]byte(fmt.Sprintf("OK %d %d %d %d\n", stats.Records, stats.Upserted, stats.Skipped, stats.Failed)))

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

	case "DELETE_POST", "DELETE_POST_FILE":
		// DELETE_POST <post_id> <person_name> <person_id>
		// DELETE_POST_FILE <community> <post_file> <person_name> <person_id>
		// Lemmy's DeletePost: only its creator deletes a post (else ERR
		// no_post_edit_allowed); moderators use REMOVE_POST. Replies OK <rev>.
		p, args, ok := s.postArgs(conn, cmd, parts, 2)
		if !ok {
			return
		}
		name, id := args[0], model.ParsePersonID(args[1])
		id, isCreator := s.postCreator(p, name, id)
		if !isCreator {
			conn.Write([]byte("ERR no_post_edit_allowed\n"))
			return
		}
		rev, err := s.storage.DeletePost(p.ID, name, id, "")
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", rev)))

	case "REMOVE_POST", "REMOVE_POST_FILE":
		// REMOVE_POST <post_id> <mod_name> <mod_id> [reason...]
		// REMOVE_POST_FILE <community> <post_file> <mod_name> <mod_id> [reason...]
		// Lemmy's RemovePost: a moderator (or the system) takes the post
		// down for reason. Replies OK <rev>.
		p, args, ok := s.postArgs(conn, cmd, parts, 2)
		if !ok {
			return
		}
		rev, err := s.storage.RemovePost(p.ID, args[0], model.ParsePersonID(args[1]), reasonArg(args[2:]))
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", rev)))

	case "LOCK_POST":
		// LOCK_POST <post_id> <1|0>  (Lemmy's LockPost)
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for LOCK_POST\n"))
			return
		}
		postID, err := strconv.ParseUint(parts[1], 10, 64)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid post_id: %s\n", parts[1])))
			return
		}
		if err := s.storage.LockPost(postID, parts[2] == "1"); err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte("OK\n"))

	case "LOCK_POST_FILE":
		// LOCK_POST_FILE <community> <post_file> <1|0>
		// post_file may be the M., L. (as in .DIR) or X. name of the post.
		if len(parts) < 4 {
			conn.Write([]byte("ERR invalid arguments for LOCK_POST_FILE\n"))
			return
		}
		if !s.ensurePost(conn, parts[1], parts[2]) {
			return
		}
		if err := s.storage.LockPostByCommunityFile(parts[1], parts[2], parts[3] == "1"); err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte("OK\n"))

	case "RECOVER_POST":
		// RECOVER_POST <post_id>
		if len(parts) < 2 {
			conn.Write([]byte("ERR invalid arguments for RECOVER_POST\n"))
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

	case "RECOVER_POST_FILE":
		// RECOVER_POST_FILE <community> <post_file>
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for RECOVER_POST_FILE\n"))
			return
		}
		community := parts[1]
		postFile := parts[2]
		// A post deleted before the switch to nest.svc is not in it:
		// import it from its article, which mbbsd has put back first.
		if p, err := s.storage.GetPostByCommunityFile(community, postFile); err != nil || p == nil {
			s.importMu.Lock()
			_, err := importer.ImportRestoredPost(s.storage, importer.ImportBoardOptions{
				BBSHome: s.cfg.BBSHome,
				Board:   community,
				IsBig5:  s.cfg.IsBig5(),
				Users:   s.users,
			}, postFile)
			s.importMu.Unlock()
			if err != nil {
				conn.Write([]byte(fmt.Sprintf("ERR post not found: %s/%s\n", community, postFile)))
				return
			}
		}
		if err := s.storage.UndeletePostByCommunityFile(community, postFile); err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte("OK\n"))

	case "DELETE_COMMENT", "DELETE_COMMENT_FILE":
		// DELETE_COMMENT <comment_id> <person_name> <person_id>
		// DELETE_COMMENT_FILE <community> <post_file> <seq> <person_name> <person_id>
		// Lemmy's DeleteComment: only its creator deletes a comment (else
		// ERR no_comment_edit_allowed); moderators use REMOVE_COMMENT.
		// Replies OK <rev>.
		c, args, ok := s.commentArgs(conn, cmd, parts, 2)
		if !ok {
			return
		}
		name, id := args[0], model.ParsePersonID(args[1])
		if !model.IsCreator(c.CreatorName, c.CreatorID, name, id) {
			conn.Write([]byte("ERR no_comment_edit_allowed\n"))
			return
		}
		rev, err := s.storage.DeleteComment(c.PostID, c.Sequence, name, id, "")
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", rev)))

	case "REMOVE_COMMENT", "REMOVE_COMMENT_FILE":
		// REMOVE_COMMENT <comment_id> <mod_name> <mod_id> [reason...]
		// REMOVE_COMMENT_FILE <community> <post_file> <seq> <mod_name> <mod_id> [reason...]
		// Lemmy's RemoveComment: a moderator takes the comment down for
		// reason. Replies OK <rev>.
		c, args, ok := s.commentArgs(conn, cmd, parts, 2)
		if !ok {
			return
		}
		rev, err := s.storage.RemoveComment(c.PostID, c.Sequence, args[0], model.ParsePersonID(args[1]), reasonArg(args[2:]))
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d\n", rev)))

	case "RECOVER_COMMENT":
		// RECOVER_COMMENT <comment_id>
		if len(parts) < 2 {
			conn.Write([]byte("ERR invalid arguments for RECOVER_COMMENT\n"))
			return
		}
		c, ok := s.commentByID(conn, parts[1])
		if !ok {
			return
		}
		if err := s.storage.UndeleteComment(c.PostID, c.Sequence); err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte("OK\n"))

	case "RECOVER_COMMENT_FILE":
		// RECOVER_COMMENT_FILE <community> <post_file> <seq>
		if len(parts) < 4 {
			conn.Write([]byte("ERR invalid arguments for RECOVER_COMMENT_FILE\n"))
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

	case "GET_COMMENT":
		// GET_COMMENT <comment_id>
		// Replies OK <post_id> <seq>, then the comment as one line of
		// GET_COMMENTS.
		if len(parts) < 2 {
			conn.Write([]byte("ERR invalid arguments for GET_COMMENT\n"))
			return
		}
		commentID, err := strconv.ParseUint(parts[1], 10, 64)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid comment_id: %s\n", parts[1])))
			return
		}
		c, err := s.storage.GetComment(commentID)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d %d\n%s", c.PostID, c.Sequence, formatTSVComment(c))))

	case "GET_COMMENTS":
		// GET_COMMENTS <post_id> [start [limit]]
		if len(parts) < 2 {
			conn.Write([]byte("ERR invalid arguments for GET_COMMENTS\n"))
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

	case "GET_COMMENTS_FILE":
		// GET_COMMENTS_FILE <community> <post_file> [start [limit [creator_name [creator_id]]]]
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for GET_COMMENTS_FILE\n"))
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

	case "GET_POST_REVISIONS":
		// GET_POST_REVISIONS <post_id> [rev]
		if len(parts) < 2 {
			conn.Write([]byte("ERR invalid arguments for GET_POST_REVISIONS\n"))
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

	case "GET_POST_REVISIONS_FILE":
		// GET_POST_REVISIONS_FILE <community> <post_file> [rev] [out_file]
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for GET_POST_REVISIONS_FILE\n"))
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

	case "GET_COMMENT_REVISIONS":
		// GET_COMMENT_REVISIONS <comment_id> [rev]
		if len(parts) < 2 {
			conn.Write([]byte("ERR invalid arguments for GET_COMMENT_REVISIONS\n"))
			return
		}
		c, ok := s.commentByID(conn, parts[1])
		if !ok {
			return
		}
		postID, seqVal := c.PostID, c.Sequence
		var rev uint32
		if len(parts) >= 3 {
			rVal, _ := strconv.ParseUint(parts[2], 10, 32)
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

	case "GET_COMMENT_REVISIONS_FILE":
		// GET_COMMENT_REVISIONS_FILE <community> <post_file> <seq> [rev]
		if len(parts) < 4 {
			conn.Write([]byte("ERR invalid arguments for GET_COMMENT_REVISIONS_FILE\n"))
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

	case "CHECK_CROSSPOST":
		// CHECK_CROSSPOST <src_community> <src_file> <target_community> <creator_name> <creator_id> <exempt:0|1>
		// Replies OK, or ERR <bbs_ name> [limit] (see storage.CheckCrosspost).
		if len(parts) < 7 {
			conn.Write([]byte("ERR invalid arguments for CHECK_CROSSPOST\n"))
			return
		}
		if !s.ensurePost(conn, parts[1], parts[2]) {
			return
		}
		err := s.storage.CheckCrosspost(storage.CrosspostCheck{
			SourceCommunity: parts[1],
			SourcePostFile:  parts[2],
			TargetCommunity: parts[3],
			CreatorName:     parts[4],
			CreatorID:       model.ParsePersonID(parts[5]),
			Exempt:          parts[6] == "1",
		}, s.crosspostLimits())
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte("OK\n"))

	case "CROSSPOST":
		// CROSSPOST <src_community> <src_file> <target_community> <target_file> <creator_name> <creator_id> [ctime] [exempt:0|1]
		// Records the crosspost with the original of the source, whatever
		// the limits: the copy is made already.
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

		if !s.ensurePost(conn, srcComm, srcFile) {
			return
		}
		exempt := len(parts) >= 9 && parts[8] == "1"
		if !exempt {
			if root, _, err := s.storage.CrosspostRoot(srcComm, srcFile); err == nil {
				exempt = s.crosspostLimits().ExemptBoard(root)
			}
		}
		rec, count, err := s.storage.AddCrosspost(srcComm, srcFile, targetComm, targetFile, operator, creatorID, ctime, exempt)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d %d\n", rec.ID, count)))

	case "GET_CROSSPOSTS":
		// GET_CROSSPOSTS <community> <post_file>
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for GET_CROSSPOSTS\n"))
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

	case "GET_CROSSPOST_PARENT":
		// GET_CROSSPOST_PARENT <target_community> <target_post_file>
		if len(parts) < 3 {
			conn.Write([]byte("ERR invalid arguments for GET_CROSSPOST_PARENT\n"))
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

	case "LINK_CROSSPOSTS":
		// LINK_CROSSPOSTS: adds the crosspost records of the imported copies
		// whose sources are there now. Replies OK <origins> <added> <merged>
		// <linked> <pending>.
		st, err := s.storage.LinkCrossposts()
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d %d %d %d %d\n", st.Origins, st.Added, st.Merged, st.Linked, st.Pending)))

	case "FIX_COMMUNITY_PUBLISHED":
		// FIX_COMMUNITY_PUBLISHED [community...]: moves the creation time
		// of the communities (all if none) back to their oldest post.
		// Replies OK <checked> <fixed>.
		checked, fixed, err := importer.FixCommunityPublished(s.storage, parts[1:]...)
		if err != nil {
			conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
			return
		}
		conn.Write([]byte(fmt.Sprintf("OK %d %d\n", checked, fixed)))

	default:
		conn.Write([]byte(fmt.Sprintf("ERR unknown command: %s\n", cmd)))
	}
}

// crosspostLimits returns the crosspost limits of the configuration.
func (s *Server) crosspostLimits() storage.CrosspostLimits {
	return s.cfg.Crosspost
}

// postCreator tells whether name / id (the person ID resolved from name
// if not given) is the creator of p, who alone may delete it (Lemmy's
// DeletePost), and returns that person ID. Posts imported from .DIR whose
// author cannot be resolved (creator_id 0) are matched by name, ignoring
// case as BBS user IDs do. The name of an anonymous post is a nickname, so
// only its real account (creator_id) is its creator.
func (s *Server) postCreator(p *model.Post, name string, id model.PersonID) (model.PersonID, bool) {
	if id == 0 && name != "" {
		id = importer.ResolvePersonID(s.users, name, 0, 0)
	}
	if p.Filemode&model.FileAnonymous != 0 {
		return id, p.CreatorID != 0 && id == p.CreatorID
	}
	return id, model.IsCreator(p.CreatorName, p.CreatorID, name, id)
}

// ensurePost makes sure nest.svc has the post postFile of community,
// importing a legacy one (see postByFile); it answers ERR itself if it
// cannot.
func (s *Server) ensurePost(conn net.Conn, community, postFile string) bool {
	if _, err := s.postByFile(community, postFile); err != nil {
		conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
		return false
	}
	return true
}

// commentByID parses a comment ID argument and returns the comment, or
// writes the error to conn.
func (s *Server) commentByID(conn net.Conn, arg string) (*model.Comment, bool) {
	id, err := strconv.ParseUint(arg, 10, 64)
	if err != nil || id == 0 {
		conn.Write([]byte(fmt.Sprintf("ERR invalid comment_id: %s\n", arg)))
		return nil, false
	}
	c, err := s.storage.GetComment(id)
	if err != nil {
		conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
		return nil, false
	}
	return c, true
}

// postArgs finds the post a command names, by <post_id> or, for a
// *_FILE command, by <community> <post_file>, and returns the arguments
// after it, at least n of them; it answers ERR itself if it cannot.
func (s *Server) postArgs(conn net.Conn, cmd string, parts []string, n int) (*model.Post, []string, bool) {
	var p *model.Post
	var err error
	var args []string
	if strings.HasSuffix(cmd, "_FILE") {
		if len(parts) < 3+n {
			conn.Write([]byte(fmt.Sprintf("ERR invalid arguments for %s\n", cmd)))
			return nil, nil, false
		}
		p, err = s.postByFile(parts[1], parts[2])
		args = parts[3:]
	} else {
		if len(parts) < 2+n {
			conn.Write([]byte(fmt.Sprintf("ERR invalid arguments for %s\n", cmd)))
			return nil, nil, false
		}
		postID, perr := strconv.ParseUint(parts[1], 10, 64)
		if perr != nil {
			conn.Write([]byte(fmt.Sprintf("ERR invalid post_id: %s\n", parts[1])))
			return nil, nil, false
		}
		p, err = s.storage.GetPost(postID)
		args = parts[2:]
	}
	if err == nil && p == nil {
		err = fmt.Errorf("post not found")
	}
	if err != nil {
		conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
		return nil, nil, false
	}
	return p, args, true
}

// commentArgs finds the comment a command names, by <comment_id> or, for a
// *_FILE command, by <community> <post_file> <seq>, and returns the
// arguments after it, at least n of them; it answers ERR itself if it
// cannot.
func (s *Server) commentArgs(conn net.Conn, cmd string, parts []string, n int) (*model.Comment, []string, bool) {
	if !strings.HasSuffix(cmd, "_FILE") {
		if len(parts) < 2+n {
			conn.Write([]byte(fmt.Sprintf("ERR invalid arguments for %s\n", cmd)))
			return nil, nil, false
		}
		c, ok := s.commentByID(conn, parts[1])
		return c, parts[2:], ok
	}
	if len(parts) < 4+n {
		conn.Write([]byte(fmt.Sprintf("ERR invalid arguments for %s\n", cmd)))
		return nil, nil, false
	}
	community, postFile := parts[1], parts[2]
	seq, err := strconv.ParseUint(parts[3], 10, 32)
	if err != nil {
		conn.Write([]byte(fmt.Sprintf("ERR invalid seq: %s\n", parts[3])))
		return nil, nil, false
	}
	p, err := s.storage.GetPostByCommunityFile(community, postFile)
	if err != nil || p == nil {
		p = s.autoImportPost(community, postFile)
	}
	if p == nil {
		conn.Write([]byte(fmt.Sprintf("ERR post not found: %s/%s\n", community, postFile)))
		return nil, nil, false
	}
	c, err := s.storage.GetComment(model.MakeCommentID(p.ID, uint32(seq)))
	if err != nil {
		conn.Write([]byte(fmt.Sprintf("ERR %v\n", err)))
		return nil, nil, false
	}
	return c, parts[4:], true
}

// reasonArg is the reason given as the rest of a command (in UTF-8, or in
// Big5 from mbbsd).
func reasonArg(args []string) string {
	reason := strings.Join(args, " ")
	if !utf8.ValidString(reason) {
		reason = big5uao.Decode([]byte(reason))
	}
	return reason
}

// formatTSVComment formats a comment as one line of GET_COMMENTS:
// <seq> <creator_name> <created_at>[/<modified_at>] <ip> <type> <reason>
// <comment_id> <content lines...>, tab-separated, where modified_at is there
// only if the comment was edited, type is -1 for deleted comments and ip
// is "-" if unknown (or not given).
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

	// mbbsd (bof/nest/nest.c) takes "-" for no IP.
	ip := c.IP
	if ip == "" {
		ip = "-"
	}
	// An edited comment has <created_at>/<modified_at> (Lemmy's
	// comment.updated), which mbbsd shows as edited.
	ctime := strconv.FormatInt(c.CreatedAt, 10)
	if c.ModifiedAt > 0 {
		ctime += "/" + strconv.FormatInt(c.ModifiedAt, 10)
	}
	return fmt.Sprintf("%d\t%s\t%s\t%s\t%d\t%s\t%d\t%s\n",
		c.Sequence, c.CreatorName, ctime, ip, typeVal, cleanReason, c.ID, contentField)
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
