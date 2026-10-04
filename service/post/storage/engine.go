package storage

import (
	"bytes"
	"database/sql"
	"encoding/binary"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/cockroachdb/pebble"
	"golang.org/x/sys/unix"
	_ "modernc.org/sqlite"

	"pttbbs/big5uao"
	"pttbbs/post/model"
)

type Config struct {
	BBSHome        string
	DataDir        string
	CacheDir       string
	CacheSizeMB    int
	FlushSeconds   int
	MinReplyRunes  int
	FilterEncoding string // "big5" (default) or "utf-8"
	ShardID        int
	NumShards      int
	MaxOpenFiles   int
}

func (c Config) IsBig5() bool {
	enc := strings.ToLower(strings.TrimSpace(c.FilterEncoding))
	return enc == "" || enc == "big5" || enc == "big5-uao" || enc == "uao"
}

type Engine struct {
	cfg            Config
	postsDB        *pebble.DB
	commentsDB     *pebble.DB
	votesDB        *pebble.DB
	historyDB      *pebble.DB
	metaDB         *sql.DB
	metaWriteMu    sync.Mutex
	postSeqCounter uint64

	// In-memory atomic sequence counters per post
	seqMu       sync.RWMutex
	postSeqs    map[uint64]*uint32
	dirtyDeltas map[uint64]*postDelta
	dirtyMu     sync.Mutex
	flushTicker *time.Ticker
	stopFlush   chan struct{}
	closeOnce   sync.Once
}

type postDelta struct {
	UpvotesDelta   int32
	DownvotesDelta int32
	CommentsDelta  int32
}

func OpenEngine(cfg Config) (*Engine, error) {
	if cfg.BBSHome == "" {
		cfg.BBSHome = "/home/bbs"
	}
	if cfg.DataDir == "" {
		cfg.DataDir = "/home/bbs/db"
	}
	if cfg.CacheDir == "" {
		cfg.CacheDir = "boards"
	}
	if cfg.CacheSizeMB <= 0 {
		cfg.CacheSizeMB = 256
	}
	if cfg.FlushSeconds <= 0 {
		cfg.FlushSeconds = 2
	}
	if cfg.MinReplyRunes <= 0 {
		cfg.MinReplyRunes = 20
	}

	if err := os.MkdirAll(cfg.DataDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to create data dir: %w", err)
	}
	if cfg.CacheDir != "boards" && cfg.CacheDir != filepath.Join(cfg.BBSHome, "boards") {
		if err := os.MkdirAll(cfg.CacheDir, 0755); err != nil {
			return nil, fmt.Errorf("failed to create cache dir: %w", err)
		}
	}

	// Calculate MaxOpenFiles for each Pebble DB
	maxOpenFiles := cfg.MaxOpenFiles
	if maxOpenFiles <= 0 {
		totalFD := 1024
		var rlim unix.Rlimit
		if err := unix.Getrlimit(unix.RLIMIT_NOFILE, &rlim); err == nil && rlim.Cur > 0 {
			totalFD = int(rlim.Cur)
		}
		numShards := cfg.NumShards
		if numShards <= 0 {
			numShards = 1
		}
		// Reserve 256 FDs for SQLite, sockets, and general system I/O
		avail := totalFD - 256
		if avail < 128 {
			avail = 128
		}
		maxOpenFiles = avail / (4 * numShards)
		if maxOpenFiles < 32 {
			maxOpenFiles = 32
		} else if maxOpenFiles > 1000 {
			maxOpenFiles = 1000
		}
	}

	// 1. Posts Pebble DB
	postsPath := filepath.Join(cfg.DataDir, "posts.pebble")
	postsOpts := &pebble.Options{
		Cache:        pebble.NewCache(int64(cfg.CacheSizeMB/3) * 1024 * 1024),
		MemTableSize: 16 * 1024 * 1024,
		MaxOpenFiles: maxOpenFiles,
	}
	postsDB, err := pebble.Open(postsPath, postsOpts)
	if err != nil {
		return nil, fmt.Errorf("open posts pebble failed: %w", err)
	}

	// 2. Comments Pebble DB
	commentsPath := filepath.Join(cfg.DataDir, "comments.pebble")
	commentsOpts := &pebble.Options{
		Cache:        pebble.NewCache(int64(cfg.CacheSizeMB/3) * 1024 * 1024),
		MemTableSize: 32 * 1024 * 1024,
		MaxOpenFiles: maxOpenFiles,
	}
	commentsDB, err := pebble.Open(commentsPath, commentsOpts)
	if err != nil {
		postsDB.Close()
		return nil, fmt.Errorf("open comments pebble failed: %w", err)
	}

	// 3. Votes Pebble DB (Separate DB to enforce 1 vote per user without write amplification)
	votesPath := filepath.Join(cfg.DataDir, "votes.pebble")
	votesOpts := &pebble.Options{
		Cache:        pebble.NewCache(int64(cfg.CacheSizeMB/3) * 1024 * 1024),
		MemTableSize: 16 * 1024 * 1024,
		MaxOpenFiles: maxOpenFiles,
	}
	votesDB, err := pebble.Open(votesPath, votesOpts)
	if err != nil {
		postsDB.Close()
		commentsDB.Close()
		return nil, fmt.Errorf("open votes pebble failed: %w", err)
	}

	// 4. SQLite Metadata DB
	sqlitePath := filepath.Join(cfg.DataDir, "meta.sqlite3")
	metaDB, err := sql.Open("sqlite", sqlitePath+"?_pragma=journal_mode(WAL)&_pragma=busy_timeout(10000)&_pragma=synchronous(NORMAL)&_pragma=temp_store(MEMORY)&_pragma=cache_size(-64000)")
	if err != nil {
		postsDB.Close()
		commentsDB.Close()
		votesDB.Close()
		return nil, fmt.Errorf("open sqlite meta failed: %w", err)
	}
	metaDB.SetMaxOpenConns(4)
	metaDB.SetMaxIdleConns(4)

	if err := initSQLiteSchema(metaDB); err != nil {
		postsDB.Close()
		commentsDB.Close()
		votesDB.Close()
		metaDB.Close()
		return nil, fmt.Errorf("init sqlite schema failed: %w", err)
	}

	// 5. History Pebble DB (Old revisions of edited posts and comments)
	historyPath := filepath.Join(cfg.DataDir, "history.pebble")
	historyOpts := &pebble.Options{
		Cache:        pebble.NewCache(int64(cfg.CacheSizeMB/4) * 1024 * 1024),
		MemTableSize: 16 * 1024 * 1024,
		MaxOpenFiles: maxOpenFiles,
	}
	historyDB, err := pebble.Open(historyPath, historyOpts)
	if err != nil {
		postsDB.Close()
		commentsDB.Close()
		votesDB.Close()
		metaDB.Close()
		return nil, fmt.Errorf("open history pebble failed: %w", err)
	}

	e := &Engine{
		cfg:         cfg,
		postsDB:     postsDB,
		commentsDB:  commentsDB,
		votesDB:     votesDB,
		historyDB:   historyDB,
		metaDB:      metaDB,
		postSeqs:    make(map[uint64]*uint32),
		dirtyDeltas: make(map[uint64]*postDelta),
		flushTicker: time.NewTicker(time.Duration(cfg.FlushSeconds) * time.Second),
		stopFlush:   make(chan struct{}),
	}

	if cfg.NumShards > 1 {
		var maxID uint64
		_ = metaDB.QueryRow("SELECT COALESCE(MAX(id), 0) FROM posts").Scan(&maxID)
		e.postSeqCounter = maxID / uint64(cfg.NumShards)
	}

	go e.flusherLoop()
	return e, nil
}

func initSQLiteSchema(db *sql.DB) error {
	schema := `
	CREATE TABLE IF NOT EXISTS posts (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		parent_id INTEGER DEFAULT 0,
		community TEXT NOT NULL,
		post_file TEXT NOT NULL,
		title TEXT NOT NULL,
		author TEXT NOT NULL,
		author_token INTEGER NOT NULL DEFAULT 0,
		created_at INTEGER NOT NULL,
		modified INTEGER NOT NULL DEFAULT 0,
		filemode INTEGER DEFAULT 0,
		upvotes INTEGER DEFAULT 0,
		downvotes INTEGER DEFAULT 0,
		num_comments INTEGER DEFAULT 0,
		num_crossposts INTEGER DEFAULT 0,
		encoding TEXT DEFAULT 'utf-8',
		is_deleted INTEGER DEFAULT 0,
		deleted_at INTEGER DEFAULT 0,
		deleted_by TEXT DEFAULT '',
		delete_reason TEXT DEFAULT ''
	);
	CREATE INDEX IF NOT EXISTS idx_posts_comm_id ON posts(community, id DESC);
	CREATE INDEX IF NOT EXISTS idx_posts_comm_del ON posts(community, is_deleted, id DESC);
	CREATE UNIQUE INDEX IF NOT EXISTS idx_posts_comm_file ON posts(community, post_file);
	CREATE INDEX IF NOT EXISTS idx_posts_author ON posts(author);
	CREATE INDEX IF NOT EXISTS idx_posts_parent_id ON posts(parent_id);

	CREATE TABLE IF NOT EXISTS crossposts (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		source_post_id INTEGER NOT NULL,
		target_community TEXT NOT NULL,
		target_post_file TEXT NOT NULL,
		operator TEXT NOT NULL,
		operator_token INTEGER NOT NULL DEFAULT 0,
		created_at INTEGER NOT NULL
	);
	CREATE INDEX IF NOT EXISTS idx_crossposts_source ON crossposts(source_post_id);
	CREATE INDEX IF NOT EXISTS idx_crossposts_target ON crossposts(target_community, target_post_file);
	`
	if _, err := db.Exec(schema); err != nil {
		return err
	}
	// Migration for existing databases
	_, _ = db.Exec("ALTER TABLE posts ADD COLUMN num_crossposts INTEGER DEFAULT 0")
	_, _ = db.Exec("ALTER TABLE posts ADD COLUMN modified INTEGER DEFAULT 0")
	_, _ = db.Exec("ALTER TABLE posts ADD COLUMN is_deleted INTEGER DEFAULT 0")
	_, _ = db.Exec("ALTER TABLE posts ADD COLUMN deleted_at INTEGER DEFAULT 0")
	_, _ = db.Exec("ALTER TABLE posts ADD COLUMN deleted_by TEXT DEFAULT ''")
	_, _ = db.Exec("ALTER TABLE posts ADD COLUMN delete_reason TEXT DEFAULT ''")
	_, _ = db.Exec("CREATE INDEX IF NOT EXISTS idx_posts_comm_del ON posts(community, is_deleted, id DESC)")
	return nil
}

func (e *Engine) Close() error {
	var firstErr error
	e.closeOnce.Do(func() {
		close(e.stopFlush)
		e.flushTicker.Stop()
		e.flushDirtyDeltas()

		if err := e.postsDB.Close(); err != nil && firstErr == nil {
			firstErr = err
		}
		if err := e.commentsDB.Close(); err != nil && firstErr == nil {
			firstErr = err
		}
		if err := e.votesDB.Close(); err != nil && firstErr == nil {
			firstErr = err
		}
		if err := e.historyDB.Close(); err != nil && firstErr == nil {
			firstErr = err
		}
		if err := e.metaDB.Close(); err != nil && firstErr == nil {
			firstErr = err
		}
	})
	return firstErr
}

// CreatePost creates a new submission, storing UTF-8 content in Pebble and metadata in SQLite
func (e *Engine) CreatePost(p *model.Post) (*model.Post, error) {
	if p.CreatedAt == 0 {
		p.CreatedAt = time.Now().Unix()
	}
	if p.Modified == 0 {
		p.Modified = p.CreatedAt
	}
	p.Encoding = "utf-8"

	// Auto-demotion: if a reply (ParentID > 0) has too little net new content, convert it to a comment on parent!
	if p.ParentID > 0 && e.cfg.MinReplyRunes > 0 {
		netText := model.ExtractNetNewContent(p.Content)
		if len([]rune(netText)) < e.cfg.MinReplyRunes {
			comment := &model.Comment{
				PostID:    p.ParentID,
				Author:    p.Author,
				Content:   netText,
				CreatedAt: p.CreatedAt,
			}
			if len(comment.Content) == 0 {
				comment.Content = "(回文無實質內容)"
			}
			added, err := e.AddComment(comment)
			if err != nil {
				return nil, fmt.Errorf("auto-demote reply to comment failed: %w", err)
			}
			p.ID = 0
			p.DemotedToComment = true
			p.NumComments = int(added.Sequence)
			return p, nil
		}
	}

	// 1. Insert into SQLite to get unique ID if not provided
	if p.ID == 0 {
		if e.cfg.NumShards > 1 {
			seq := atomic.AddUint64(&e.postSeqCounter, 1)
			p.ID = (seq * uint64(e.cfg.NumShards)) + uint64(e.cfg.ShardID)
			e.metaWriteMu.Lock()
			_, err := e.metaDB.Exec(`
				INSERT INTO posts (id, parent_id, community, post_file, title, author, author_token, created_at, modified, filemode, upvotes, downvotes, num_comments, num_crossposts, encoding)
				VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
			`, p.ID, p.ParentID, p.Community, p.PostFile, p.Title, p.Author, p.AuthorToken, p.CreatedAt, p.Modified, p.Filemode, p.Upvotes, p.Downvotes, p.NumComments, p.NumCrossposts, p.Encoding)
			e.metaWriteMu.Unlock()
			if err != nil {
				return nil, fmt.Errorf("insert sqlite post with shard id failed: %w", err)
			}
		} else {
			e.metaWriteMu.Lock()
			res, err := e.metaDB.Exec(`
				INSERT INTO posts (parent_id, community, post_file, title, author, author_token, created_at, modified, filemode, upvotes, downvotes, num_comments, num_crossposts, encoding)
				VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
			`, p.ParentID, p.Community, p.PostFile, p.Title, p.Author, p.AuthorToken, p.CreatedAt, p.Modified, p.Filemode, p.Upvotes, p.Downvotes, p.NumComments, p.NumCrossposts, p.Encoding)
			if err != nil {
				e.metaWriteMu.Unlock()
				return nil, fmt.Errorf("insert sqlite post failed: %w", err)
			}
			id, err := res.LastInsertId()
			e.metaWriteMu.Unlock()
			if err != nil {
				return nil, err
			}
			p.ID = uint64(id)
		}
	} else {
		e.metaWriteMu.Lock()
		_, err := e.metaDB.Exec(`
			INSERT INTO posts (id, parent_id, community, post_file, title, author, author_token, created_at, modified, filemode, upvotes, downvotes, num_comments, num_crossposts, encoding)
			VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
		`, p.ID, p.ParentID, p.Community, p.PostFile, p.Title, p.Author, p.AuthorToken, p.CreatedAt, p.Modified, p.Filemode, p.Upvotes, p.Downvotes, p.NumComments, p.NumCrossposts, p.Encoding)
		e.metaWriteMu.Unlock()
		if err != nil {
			return nil, fmt.Errorf("insert sqlite post with explicit id failed: %w", err)
		}
	}

	// 2. Package RFC 822 format (Encoding as the FIRST header!) and write to posts.db
	val := model.EncodeRFC822Post(p)
	key := model.EncodePostKey(p.ID)
	if err := e.postsDB.Set(key, val, pebble.NoSync); err != nil {
		return nil, fmt.Errorf("write pebble post failed: %w", err)
	}

	// 3. Update local Cache File
	_ = e.WritePostCache(p)

	return p, nil
}

func (e *Engine) GetPost(postID uint64) (*model.Post, error) {
	var p model.Post
	var isDeleted int
	var deletedAt int64
	var deletedBy, deleteReason string
	err := e.metaDB.QueryRow(`
		SELECT id, parent_id, community, post_file, title, author, author_token, created_at, modified, filemode, upvotes, downvotes, num_comments, num_crossposts, encoding, is_deleted, deleted_at, deleted_by, delete_reason
		FROM posts WHERE id = ?
	`, postID).Scan(&p.ID, &p.ParentID, &p.Community, &p.PostFile, &p.Title, &p.Author, &p.AuthorToken, &p.CreatedAt, &p.Modified, &p.Filemode, &p.Upvotes, &p.Downvotes, &p.NumComments, &p.NumCrossposts, &p.Encoding, &isDeleted, &deletedAt, &deletedBy, &deleteReason)
	if err != nil {
		return nil, err
	}
	if p.Modified == 0 {
		p.Modified = p.CreatedAt
	}
	p.IsDeleted = (isDeleted != 0)
	p.DeletedAt = deletedAt
	p.DeletedBy = deletedBy
	p.DeleteReason = deleteReason

	// Read content from Pebble
	key := model.EncodePostKey(postID)
	val, closer, err := e.postsDB.Get(key)
	if err != nil {
		return nil, fmt.Errorf("get pebble post %d: %w", postID, err)
	}
	defer closer.Close()

	meta, body := model.DecodeRFC822Post(val)
	p.Content = body
	if meta["Deleted"] == "true" {
		p.IsDeleted = true
	}
	if p.DeletedAt == 0 && meta["DeletedAt"] != "" {
		p.DeletedAt, _ = strconv.ParseInt(meta["DeletedAt"], 10, 64)
	}
	if p.DeletedBy == "" {
		p.DeletedBy = meta["DeletedBy"]
	}
	if p.DeleteReason == "" {
		p.DeleteReason = meta["DeleteReason"]
	}
	return &p, nil
}

func (e *Engine) GetPostByCommunityFile(community, postFile string) (*model.Post, error) {
	var id uint64
	err := e.metaDB.QueryRow(`
		SELECT id FROM posts WHERE community = ? AND post_file = ?
	`, community, postFile).Scan(&id)
	if err != nil {
		return nil, err
	}
	return e.GetPost(id)
}

func (e *Engine) CountPosts(community string) (int, error) {
	var count int
	err := e.metaDB.QueryRow(`
		SELECT count(*) FROM posts WHERE community = ? AND is_deleted = 0
	`, community).Scan(&count)
	return count, err
}

func (e *Engine) ListPosts(community string, limit, offset int) ([]*model.Post, error) {
	if limit <= 0 {
		limit = 20
	}
	if offset < 0 {
		offset = 0
	}
	rows, err := e.metaDB.Query(`
		SELECT id, parent_id, community, post_file, title, author, author_token, created_at, modified, filemode, upvotes, downvotes, num_comments, num_crossposts, encoding, is_deleted, deleted_at, deleted_by, delete_reason
		FROM posts WHERE community = ? AND is_deleted = 0 ORDER BY id DESC LIMIT ? OFFSET ?
	`, community, limit, offset)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var posts []*model.Post
	for rows.Next() {
		var p model.Post
		var isDeleted int
		var deletedAt int64
		var deletedBy, deleteReason string
		if err := rows.Scan(&p.ID, &p.ParentID, &p.Community, &p.PostFile, &p.Title, &p.Author, &p.AuthorToken, &p.CreatedAt, &p.Modified, &p.Filemode, &p.Upvotes, &p.Downvotes, &p.NumComments, &p.NumCrossposts, &p.Encoding, &isDeleted, &deletedAt, &deletedBy, &deleteReason); err != nil {
			return nil, err
		}
		if p.Modified == 0 {
			p.Modified = p.CreatedAt
		}
		p.IsDeleted = (isDeleted != 0)
		p.DeletedAt = deletedAt
		p.DeletedBy = deletedBy
		p.DeleteReason = deleteReason
		posts = append(posts, &p)
	}
	return posts, nil
}

func (e *Engine) ListDeletedPosts(community string, limit, offset int) ([]*model.Post, error) {
	if limit <= 0 {
		limit = 20
	}
	if offset < 0 {
		offset = 0
	}
	rows, err := e.metaDB.Query(`
		SELECT id, parent_id, community, post_file, title, author, author_token, created_at, modified, filemode, upvotes, downvotes, num_comments, num_crossposts, encoding, is_deleted, deleted_at, deleted_by, delete_reason
		FROM posts WHERE community = ? AND is_deleted = 1 ORDER BY id DESC LIMIT ? OFFSET ?
	`, community, limit, offset)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var posts []*model.Post
	for rows.Next() {
		var p model.Post
		var isDeleted int
		var deletedAt int64
		var deletedBy, deleteReason string
		if err := rows.Scan(&p.ID, &p.ParentID, &p.Community, &p.PostFile, &p.Title, &p.Author, &p.AuthorToken, &p.CreatedAt, &p.Modified, &p.Filemode, &p.Upvotes, &p.Downvotes, &p.NumComments, &p.NumCrossposts, &p.Encoding, &isDeleted, &deletedAt, &deletedBy, &deleteReason); err != nil {
			return nil, err
		}
		if p.Modified == 0 {
			p.Modified = p.CreatedAt
		}
		p.IsDeleted = (isDeleted != 0)
		p.DeletedAt = deletedAt
		p.DeletedBy = deletedBy
		p.DeleteReason = deleteReason
		posts = append(posts, &p)
	}
	return posts, nil
}

// GetReplies returns all reply posts (回文) to a parent post
func (e *Engine) GetReplies(parentID uint64, limit, offset int) ([]*model.Post, error) {
	if limit <= 0 {
		limit = 50
	}
	rows, err := e.metaDB.Query(`
		SELECT id, parent_id, community, post_file, title, author, author_token, created_at, modified, filemode, upvotes, downvotes, num_comments, num_crossposts, encoding, is_deleted, deleted_at, deleted_by, delete_reason
		FROM posts WHERE parent_id = ? ORDER BY id ASC LIMIT ? OFFSET ?
	`, parentID, limit, offset)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var replies []*model.Post
	for rows.Next() {
		var p model.Post
		var isDeleted int
		var deletedAt int64
		var deletedBy, deleteReason string
		if err := rows.Scan(&p.ID, &p.ParentID, &p.Community, &p.PostFile, &p.Title, &p.Author, &p.AuthorToken, &p.CreatedAt, &p.Modified, &p.Filemode, &p.Upvotes, &p.Downvotes, &p.NumComments, &p.NumCrossposts, &p.Encoding, &isDeleted, &deletedAt, &deletedBy, &deleteReason); err != nil {
			return nil, err
		}
		if p.Modified == 0 {
			p.Modified = p.CreatedAt
		}
		p.IsDeleted = (isDeleted != 0)
		p.DeletedAt = deletedAt
		p.DeletedBy = deletedBy
		p.DeleteReason = deleteReason
		replies = append(replies, &p)
	}
	return replies, nil
}

// GetThread returns the root post and all its replies (完整討論串家族樹)
func (e *Engine) GetThread(rootID uint64) ([]*model.Post, error) {
	root, err := e.GetPost(rootID)
	if err != nil {
		return nil, err
	}
	replies, err := e.GetReplies(rootID, 1000, 0)
	if err != nil {
		return nil, err
	}
	return append([]*model.Post{root}, replies...), nil
}

// -----------------------------------------------------------------------------
// Crossposts (Shares / 轉錄追蹤)
// -----------------------------------------------------------------------------

// AddCrosspost records that a post was cross-posted to a target community
func (e *Engine) AddCrosspost(srcCommunity, srcPostFile, targetCommunity, targetPostFile, operator string, operatorToken uint32, createdAt int64) (*model.CrosspostRecord, int, error) {
	srcPost, err := e.GetPostByCommunityFile(srcCommunity, srcPostFile)
	if err != nil {
		return nil, 0, fmt.Errorf("source post not found: %w", err)
	}

	if createdAt == 0 {
		createdAt = time.Now().Unix()
	}

	// 1. Insert into SQLite crossposts table
	e.metaWriteMu.Lock()
	res, err := e.metaDB.Exec(`
		INSERT INTO crossposts (source_post_id, target_community, target_post_file, operator, operator_token, created_at)
		VALUES (?, ?, ?, ?, ?, ?)
	`, srcPost.ID, targetCommunity, targetPostFile, operator, operatorToken, createdAt)
	if err != nil {
		e.metaWriteMu.Unlock()
		return nil, 0, fmt.Errorf("insert crosspost failed: %w", err)
	}
	id, err := res.LastInsertId()
	if err != nil {
		e.metaWriteMu.Unlock()
		return nil, 0, err
	}

	// 2. Increment num_crossposts in posts table
	if _, err := e.metaDB.Exec(`UPDATE posts SET num_crossposts = num_crossposts + 1 WHERE id = ?`, srcPost.ID); err != nil {
		e.metaWriteMu.Unlock()
		return nil, 0, fmt.Errorf("update num_crossposts failed: %w", err)
	}
	e.metaWriteMu.Unlock()

	var newCount int
	_ = e.metaDB.QueryRow(`SELECT num_crossposts FROM posts WHERE id = ?`, srcPost.ID).Scan(&newCount)
	srcPost.NumCrossposts = newCount

	rec := &model.CrosspostRecord{
		ID:              uint64(id),
		SourcePostID:    srcPost.ID,
		TargetCommunity: targetCommunity,
		TargetPostFile:  targetPostFile,
		Operator:        operator,
		OperatorToken:   operatorToken,
		CreatedAt:       createdAt,
	}

	// 3. Write to Pebble (Source of truth)
	cKey := model.EncodeCrosspostKey(srcPost.ID, uint32(newCount))
	cVal := model.EncodeRFC822Crosspost(rec)
	if err := e.postsDB.Set(cKey, cVal, pebble.Sync); err != nil {
		return nil, 0, fmt.Errorf("set pebble crosspost failed: %w", err)
	}

	// Update source post in Pebble to reflect new NumCrossposts
	pVal := model.EncodeRFC822Post(srcPost)
	_ = e.postsDB.Set(model.EncodePostKey(srcPost.ID), pVal, pebble.NoSync)

	return rec, newCount, nil
}

// GetCrossposts returns all destinations where this post has been crossposted
func (e *Engine) GetCrossposts(community, postFile string) ([]*model.CrosspostRecord, error) {
	post, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return nil, err
	}

	rows, err := e.metaDB.Query(`
		SELECT id, source_post_id, target_community, target_post_file, operator, operator_token, created_at
		FROM crossposts WHERE source_post_id = ? ORDER BY id ASC
	`, post.ID)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var records []*model.CrosspostRecord
	for rows.Next() {
		var r model.CrosspostRecord
		if err := rows.Scan(&r.ID, &r.SourcePostID, &r.TargetCommunity, &r.TargetPostFile, &r.Operator, &r.OperatorToken, &r.CreatedAt); err != nil {
			return nil, err
		}
		records = append(records, &r)
	}
	return records, nil
}

// GetOriginPost finds the source post if target was crossposted from another board
func (e *Engine) GetOriginPost(targetCommunity, targetPostFile string) (*model.Post, *model.CrosspostRecord, error) {
	var r model.CrosspostRecord
	err := e.metaDB.QueryRow(`
		SELECT id, source_post_id, target_community, target_post_file, operator, operator_token, created_at
		FROM crossposts WHERE target_community = ? AND target_post_file = ?
	`, targetCommunity, targetPostFile).Scan(&r.ID, &r.SourcePostID, &r.TargetCommunity, &r.TargetPostFile, &r.Operator, &r.OperatorToken, &r.CreatedAt)
	if err != nil {
		if err == sql.ErrNoRows {
			return nil, nil, nil
		}
		return nil, nil, err
	}

	srcPost, err := e.GetPost(r.SourcePostID)
	if err != nil {
		return nil, nil, err
	}
	return srcPost, &r, nil
}

// -----------------------------------------------------------------------------
// Comments (Decoupled from Votes, RFC 822 formatted)
// -----------------------------------------------------------------------------

// AddComment appends a comment, storing it in Pebble (RFC 822) and updating dirty counts
func (e *Engine) AddComment(c *model.Comment) (*model.Comment, error) {
	if c.CreatedAt == 0 {
		c.CreatedAt = time.Now().Unix()
	}

	// 1. Get next sequence number
	seq := e.nextSequence(c.PostID)
	c.Sequence = seq

	// 2. Package RFC 822 for comment (Encoding is the first header!)
	val := model.EncodeRFC822Comment(c)
	key := model.EncodeCommentKey(c.PostID, c.Sequence)

	if err := e.commentsDB.Set(key, val, pebble.NoSync); err != nil {
		return nil, fmt.Errorf("write pebble comment failed: %w", err)
	}

	// 3. Write secondary author index: u:{author}:{created_at}:{post_id}:{sequence}
	secKey := model.EncodeAuthorCommentKey(c.Author, c.CreatedAt, c.PostID, c.Sequence)
	if err := e.commentsDB.Set(secKey, []byte{}, pebble.NoSync); err != nil {
		return nil, fmt.Errorf("write comment author index failed: %w", err)
	}

	// 4. Register dirty comments count for SQLite batch flush
	e.dirtyMu.Lock()
	d, ok := e.dirtyDeltas[c.PostID]
	if !ok {
		d = &postDelta{}
		e.dirtyDeltas[c.PostID] = d
	}
	d.CommentsDelta += 1
	e.dirtyMu.Unlock()

	// 5. Append to local cache text file
	_ = e.AppendCommentCache(c)

	return c, nil
}

func (e *Engine) nextSequence(postID uint64) uint32 {
	e.seqMu.Lock()
	defer e.seqMu.Unlock()

	ptr, ok := e.postSeqs[postID]
	if !ok {
		maxSeq := e.lookupMaxSequence(postID)
		v := maxSeq + 1
		e.postSeqs[postID] = &v
		return maxSeq + 1
	}
	return atomic.AddUint32(ptr, 1)
}

func (e *Engine) lookupMaxSequence(postID uint64) uint32 {
	prefix := model.EncodeCommentPrefix(postID)
	iter, err := e.commentsDB.NewIter(&pebble.IterOptions{
		LowerBound: prefix,
		UpperBound: append(prefix, 0xff),
	})
	if err != nil {
		return 0
	}
	defer iter.Close()

	if iter.Last() {
		_, seq, err := model.DecodeCommentKey(iter.Key())
		if err == nil {
			return seq
		}
	}
	return 0
}

func (e *Engine) GetComments(postID uint64, startFloor, limit uint32) ([]*model.Comment, error) {
	if limit == 0 {
		limit = 1000
	}
	prefix := model.EncodeCommentPrefix(postID)
	startKey := model.EncodeCommentKey(postID, startFloor)

	iter, err := e.commentsDB.NewIter(nil)
	if err != nil {
		return nil, err
	}
	defer iter.Close()

	var comments []*model.Comment
	for iter.SeekGE(startKey); iter.Valid() && bytes.HasPrefix(iter.Key(), prefix) && uint32(len(comments)) < limit; iter.Next() {
		_, seq, err := model.DecodeCommentKey(iter.Key())
		if err == nil {
			c, err := model.DecodeRFC822Comment(postID, seq, iter.Value())
			if err == nil {
				comments = append(comments, c)
			}
		}
	}
	return comments, nil
}

func (e *Engine) GetCommentCount(postID uint64) (int, error) {
	var count int
	err := e.metaDB.QueryRow("SELECT num_comments FROM posts WHERE id = ?", postID).Scan(&count)
	return count, err
}

func (e *Engine) GetCommentCountByCommunityFile(community, postFile string) (int, error) {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return 0, err
	}
	return e.GetCommentCount(p.ID)
}

func (e *Engine) GetCommentsByCommunityFile(community, postFile string, startFloor, limit uint32) ([]*model.Comment, error) {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return nil, err
	}
	return e.GetComments(p.ID, startFloor, limit)
}

// PurgeUserComments deletes all comments from an author created within maxAge (e.g. 7 days for anti-spam)
func (e *Engine) PurgeUserComments(author string, maxAge time.Duration) (purged int, err error) {
	minTimestamp := time.Now().Add(-maxAge).Unix()
	prefix := model.EncodeAuthorCommentPrefix(author)

	iter, err := e.commentsDB.NewIter(nil)
	if err != nil {
		return 0, err
	}
	defer iter.Close()

	var toPurgeKeys [][]byte
	var toPurgePostComments []struct {
		PostID   uint64
		Sequence uint32
	}

	for iter.SeekGE(prefix); iter.Valid() && bytes.HasPrefix(iter.Key(), prefix); iter.Next() {
		uAuthor, ctime, pID, seq, err := model.DecodeAuthorCommentKey(iter.Key())
		if err == nil && uAuthor == author {
			if ctime >= minTimestamp {
				toPurgeKeys = append(toPurgeKeys, append([]byte(nil), iter.Key()...))
				toPurgePostComments = append(toPurgePostComments, struct {
					PostID   uint64
					Sequence uint32
				}{pID, seq})
			}
		}
	}

	// Delete and mark comments as deleted
	batch := e.commentsDB.NewBatch()
	defer batch.Close()

	for i, k := range toPurgeKeys {
		batch.Delete(k, nil)
		// Mark comment as deleted in primary key
		cKey := model.EncodeCommentKey(toPurgePostComments[i].PostID, toPurgePostComments[i].Sequence)
		val, closer, err := e.commentsDB.Get(cKey)
		if err == nil {
			c, err := model.DecodeRFC822Comment(toPurgePostComments[i].PostID, toPurgePostComments[i].Sequence, val)
			closer.Close()
			if err == nil {
				c.IsDeleted = true
				batch.Set(cKey, model.EncodeRFC822Comment(c), nil)
			}
		}
		purged++
	}

	if err := batch.Commit(pebble.NoSync); err != nil {
		return 0, err
	}
	return purged, nil
}

// PurgeUserPosts deletes all posts created by an author within maxAge (anti-spam)
func (e *Engine) PurgeUserPosts(author string, maxAge time.Duration) (purged int, err error) {
	minTimestamp := time.Now().Add(-maxAge).Unix()
	rows, err := e.metaDB.Query(`
		SELECT id, community, post_file FROM posts WHERE author = ? AND created_at >= ?
	`, author, minTimestamp)
	if err != nil {
		return 0, err
	}
	defer rows.Close()

	type targetPost struct {
		ID        uint64
		Community string
		PostFile  string
	}
	var targets []targetPost
	for rows.Next() {
		var t targetPost
		if err := rows.Scan(&t.ID, &t.Community, &t.PostFile); err == nil {
			targets = append(targets, t)
		}
	}

	if len(targets) == 0 {
		return 0, nil
	}

	e.metaWriteMu.Lock()
	defer e.metaWriteMu.Unlock()

	tx, err := e.metaDB.Begin()
	if err != nil {
		return 0, err
	}
	defer tx.Rollback()

	delStmt, err := tx.Prepare("DELETE FROM posts WHERE id = ?")
	if err != nil {
		return 0, err
	}
	defer delStmt.Close()

	batch := e.postsDB.NewBatch()
	defer batch.Close()

	for _, t := range targets {
		delStmt.Exec(t.ID)

		// Mark as Deleted in Pebble posts.db (so Rebuild won't restore it)
		key := model.EncodePostKey(t.ID)
		val, closer, err := e.postsDB.Get(key)
		if err == nil {
			meta, body := model.DecodeRFC822Post(val)
			closer.Close()
			meta["Deleted"] = "true"
			var b strings.Builder
			b.WriteString("Encoding: utf-8\n")
			for k, v := range meta {
				if k == "Encoding" {
					continue
				}
				b.WriteString(fmt.Sprintf("%s: %s\n", k, v))
			}
			b.WriteString("\n")
			b.WriteString(body)
			batch.Set(key, []byte(b.String()), nil)
		}

		// Remove local cache file
		cachePath := filepath.Join(e.cfg.CacheDir, t.Community, t.PostFile)
		os.Remove(cachePath)
		purged++
	}

	if err := batch.Commit(pebble.NoSync); err != nil {
		return 0, err
	}
	if err := tx.Commit(); err != nil {
		return 0, err
	}

	return purged, nil
}

// DeleteCommunity completely removes all posts, comments, votes, history, and cache for a community.
func (e *Engine) DeleteCommunity(community string) (postsDeleted int, commentsDeleted int, err error) {
	if community == "" {
		return 0, 0, fmt.Errorf("community name required")
	}

	// 1. Flush any pending deltas first so SQLite is up to date
	e.FlushDirtyDeltas()

	e.metaWriteMu.Lock()
	defer e.metaWriteMu.Unlock()

	// 2. Query all post IDs in this community
	rows, err := e.metaDB.Query("SELECT id, post_file FROM posts WHERE community = ?", community)
	if err != nil {
		return 0, 0, fmt.Errorf("query posts for community %s failed: %w", community, err)
	}
	defer rows.Close()

	type postRecord struct {
		id       uint64
		postFile string
	}
	var posts []postRecord
	for rows.Next() {
		var pr postRecord
		if err := rows.Scan(&pr.id, &pr.postFile); err == nil {
			posts = append(posts, pr)
		}
	}
	rows.Close()

	// 3. Delete from SQLite
	tx, err := e.metaDB.Begin()
	if err != nil {
		return 0, 0, fmt.Errorf("begin transaction failed: %w", err)
	}
	defer tx.Rollback()

	if _, err := tx.Exec("DELETE FROM crossposts WHERE target_community = ? OR source_post_id IN (SELECT id FROM posts WHERE community = ?)", community, community); err != nil {
		return 0, 0, fmt.Errorf("delete crossposts failed: %w", err)
	}
	if _, err := tx.Exec("DELETE FROM posts WHERE community = ?", community); err != nil {
		return 0, 0, fmt.Errorf("delete posts failed: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return 0, 0, fmt.Errorf("commit sqlite delete failed: %w", err)
	}

	// 4. Delete from Pebble databases in chunks
	postBatch := e.postsDB.NewBatch()
	commentBatch := e.commentsDB.NewBatch()
	voteBatch := e.votesDB.NewBatch()
	historyBatch := e.historyDB.NewBatch()

	flushBatches := func() error {
		if err := postBatch.Commit(pebble.NoSync); err != nil {
			return err
		}
		postBatch.Close()
		postBatch = e.postsDB.NewBatch()

		if err := commentBatch.Commit(pebble.NoSync); err != nil {
			return err
		}
		commentBatch.Close()
		commentBatch = e.commentsDB.NewBatch()

		if err := voteBatch.Commit(pebble.NoSync); err != nil {
			return err
		}
		voteBatch.Close()
		voteBatch = e.votesDB.NewBatch()

		if err := historyBatch.Commit(pebble.NoSync); err != nil {
			return err
		}
		historyBatch.Close()
		historyBatch = e.historyDB.NewBatch()

		return nil
	}

	ops := 0

	for _, p := range posts {
		// Delete post from postsDB
		postBatch.Delete(model.EncodePostKey(p.id), nil)
		ops++

		// Delete crossposts associated with this source post
		cpPrefix := model.EncodeCrosspostPrefix(p.id)
		cpIter, _ := e.postsDB.NewIter(&pebble.IterOptions{
			LowerBound: cpPrefix,
			UpperBound: append(cpPrefix, 0xff),
		})
		if cpIter != nil {
			for cpIter.First(); cpIter.Valid(); cpIter.Next() {
				postBatch.Delete(cpIter.Key(), nil)
				ops++
			}
			cpIter.Close()
		}

		// Delete post history from historyDB
		pHistPrefix := model.EncodePostHistoryPrefix(p.id)
		pHistIter, _ := e.historyDB.NewIter(&pebble.IterOptions{
			LowerBound: pHistPrefix,
			UpperBound: append(pHistPrefix, 0xff),
		})
		if pHistIter != nil {
			for pHistIter.First(); pHistIter.Valid(); pHistIter.Next() {
				historyBatch.Delete(pHistIter.Key(), nil)
				ops++
			}
			pHistIter.Close()
		}

		// Delete comments and author index from commentsDB
		cPrefix := model.EncodeCommentPrefix(p.id)
		cIter, _ := e.commentsDB.NewIter(&pebble.IterOptions{
			LowerBound: cPrefix,
			UpperBound: append(cPrefix, 0xff),
		})
		if cIter != nil {
			for cIter.First(); cIter.Valid(); cIter.Next() {
				_, seq, dErr := model.DecodeCommentKey(cIter.Key())
				if dErr == nil {
					c, cErr := model.DecodeRFC822Comment(p.id, seq, cIter.Value())
					if cErr == nil && c != nil {
						authKey := model.EncodeAuthorCommentKey(c.Author, c.CreatedAt, p.id, seq)
						commentBatch.Delete(authKey, nil)
						ops++
					}
					// Delete comment history
					cHistPrefix := model.EncodeCommentHistoryPrefix(p.id, seq)
					cHistIter, _ := e.historyDB.NewIter(&pebble.IterOptions{
						LowerBound: cHistPrefix,
						UpperBound: append(cHistPrefix, 0xff),
					})
					if cHistIter != nil {
						for cHistIter.First(); cHistIter.Valid(); cHistIter.Next() {
							historyBatch.Delete(cHistIter.Key(), nil)
							ops++
						}
						cHistIter.Close()
					}
				}
				commentBatch.Delete(cIter.Key(), nil)
				commentsDeleted++
				ops++
			}
			cIter.Close()
		}

		// Delete votes from votesDB
		vPrefix := model.EncodeVotePrefix(p.id)
		vIter, _ := e.votesDB.NewIter(&pebble.IterOptions{
			LowerBound: vPrefix,
			UpperBound: append(vPrefix, 0xff),
		})
		if vIter != nil {
			for vIter.First(); vIter.Valid(); vIter.Next() {
				voteBatch.Delete(vIter.Key(), nil)
				ops++
			}
			vIter.Close()
		}

		// Clean in-memory sequence counters and deltas
		e.seqMu.Lock()
		delete(e.postSeqs, p.id)
		e.seqMu.Unlock()

		e.dirtyMu.Lock()
		delete(e.dirtyDeltas, p.id)
		e.dirtyMu.Unlock()

		postsDeleted++

		if ops >= 5000 {
			if err := flushBatches(); err != nil {
				return postsDeleted, commentsDeleted, err
			}
			ops = 0
		}
	}

	// Final flush
	if err := flushBatches(); err != nil {
		return postsDeleted, commentsDeleted, err
	}
	postBatch.Close()
	commentBatch.Close()
	voteBatch.Close()
	historyBatch.Close()

	// 5. Clean cache directory
	if e.cfg.CacheDir != "" {
		_ = os.RemoveAll(filepath.Join(e.cfg.CacheDir, community))
	}

	return postsDeleted, commentsDeleted, nil
}

// PurgePost permanently deletes a post and all its comments, votes, history, and crossposts.
func (e *Engine) PurgePost(postID uint64) error {
	p, err := e.GetPost(postID)
	if err != nil {
		return err
	}
	return e.purgePostRecord(p)
}

// PurgePostByCommunityFile permanently deletes a post by community and post_file.
func (e *Engine) PurgePostByCommunityFile(community, postFile string) error {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return err
	}
	return e.purgePostRecord(p)
}

func (e *Engine) purgePostRecord(p *model.Post) error {
	if p == nil {
		return fmt.Errorf("post is nil")
	}

	e.FlushDirtyDeltas()

	e.metaWriteMu.Lock()
	defer e.metaWriteMu.Unlock()

	tx, err := e.metaDB.Begin()
	if err != nil {
		return fmt.Errorf("begin transaction failed: %w", err)
	}
	defer tx.Rollback()

	if _, err := tx.Exec("DELETE FROM crossposts WHERE source_post_id = ? OR (target_community = ? AND target_post_file = ?)", p.ID, p.Community, p.PostFile); err != nil {
		return fmt.Errorf("delete crossposts failed: %w", err)
	}
	if _, err := tx.Exec("DELETE FROM posts WHERE id = ?", p.ID); err != nil {
		return fmt.Errorf("delete post failed: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return fmt.Errorf("commit sqlite delete failed: %w", err)
	}

	// Delete from Pebble databases
	postBatch := e.postsDB.NewBatch()
	commentBatch := e.commentsDB.NewBatch()
	voteBatch := e.votesDB.NewBatch()
	historyBatch := e.historyDB.NewBatch()

	// 1. Post key
	postBatch.Delete(model.EncodePostKey(p.ID), nil)

	// 2. Crossposts prefix
	cpPrefix := model.EncodeCrosspostPrefix(p.ID)
	cpIter, _ := e.postsDB.NewIter(&pebble.IterOptions{
		LowerBound: cpPrefix,
		UpperBound: append(cpPrefix, 0xff),
	})
	if cpIter != nil {
		for cpIter.First(); cpIter.Valid(); cpIter.Next() {
			postBatch.Delete(cpIter.Key(), nil)
		}
		cpIter.Close()
	}

	// 3. Post history
	pHistPrefix := model.EncodePostHistoryPrefix(p.ID)
	pHistIter, _ := e.historyDB.NewIter(&pebble.IterOptions{
		LowerBound: pHistPrefix,
		UpperBound: append(pHistPrefix, 0xff),
	})
	if pHistIter != nil {
		for pHistIter.First(); pHistIter.Valid(); pHistIter.Next() {
			historyBatch.Delete(pHistIter.Key(), nil)
		}
		pHistIter.Close()
	}

	// 4. Comments & comment history
	cPrefix := model.EncodeCommentPrefix(p.ID)
	cIter, _ := e.commentsDB.NewIter(&pebble.IterOptions{
		LowerBound: cPrefix,
		UpperBound: append(cPrefix, 0xff),
	})
	if cIter != nil {
		for cIter.First(); cIter.Valid(); cIter.Next() {
			_, seq, dErr := model.DecodeCommentKey(cIter.Key())
			if dErr == nil {
				c, cErr := model.DecodeRFC822Comment(p.ID, seq, cIter.Value())
				if cErr == nil && c != nil {
					authKey := model.EncodeAuthorCommentKey(c.Author, c.CreatedAt, p.ID, seq)
					commentBatch.Delete(authKey, nil)
				}
				cHistPrefix := model.EncodeCommentHistoryPrefix(p.ID, seq)
				cHistIter, _ := e.historyDB.NewIter(&pebble.IterOptions{
					LowerBound: cHistPrefix,
					UpperBound: append(cHistPrefix, 0xff),
				})
				if cHistIter != nil {
					for cHistIter.First(); cHistIter.Valid(); cHistIter.Next() {
						historyBatch.Delete(cHistIter.Key(), nil)
					}
					cHistIter.Close()
				}
			}
			commentBatch.Delete(cIter.Key(), nil)
		}
		cIter.Close()
	}

	// 5. Votes
	vPrefix := model.EncodeVotePrefix(p.ID)
	vIter, _ := e.votesDB.NewIter(&pebble.IterOptions{
		LowerBound: vPrefix,
		UpperBound: append(vPrefix, 0xff),
	})
	if vIter != nil {
		for vIter.First(); vIter.Valid(); vIter.Next() {
			voteBatch.Delete(vIter.Key(), nil)
		}
		vIter.Close()
	}

	// In-memory cleanup
	e.seqMu.Lock()
	delete(e.postSeqs, p.ID)
	e.seqMu.Unlock()

	e.dirtyMu.Lock()
	delete(e.dirtyDeltas, p.ID)
	e.dirtyMu.Unlock()

	_ = postBatch.Commit(pebble.NoSync)
	postBatch.Close()
	_ = commentBatch.Commit(pebble.NoSync)
	commentBatch.Close()
	_ = voteBatch.Commit(pebble.NoSync)
	voteBatch.Close()
	_ = historyBatch.Commit(pebble.NoSync)
	historyBatch.Close()

	// Clean cache file
	cachePath := e.CacheFilePath(p)
	_ = os.Remove(cachePath)

	return nil
}


// -----------------------------------------------------------------------------
// Votes (Independent DB: One Vote per User per Post)
// -----------------------------------------------------------------------------

// VotePost records a user's vote (1: Upvote, -1: Downvote, 0: Cancel vote)
func (e *Engine) VotePost(postID uint64, user string, authorToken uint32, newVote model.VoteType) (upDelta, downDelta int, err error) {
	key := model.EncodeVoteKey(postID, user)
	val, closer, err := e.votesDB.Get(key)

	var prevVote model.VoteType = model.VoteNeutral
	if err == nil {
		if len(val) > 0 {
			if val[0] == 'U' {
				prevVote = model.VoteUp
			} else if val[0] == 'D' {
				prevVote = model.VoteDown
			}
		}
		closer.Close()
	}

	if prevVote == newVote {
		return 0, 0, nil // Duplicate vote, no-op
	}

	// Calculate deltas
	if prevVote == model.VoteUp {
		upDelta -= 1
	} else if prevVote == model.VoteDown {
		downDelta -= 1
	}

	valBuf := make([]byte, 5)
	binary.BigEndian.PutUint32(valBuf[1:5], authorToken)

	if newVote == model.VoteUp {
		upDelta += 1
		valBuf[0] = 'U'
		e.votesDB.Set(key, valBuf, pebble.NoSync)
	} else if newVote == model.VoteDown {
		downDelta += 1
		valBuf[0] = 'D'
		e.votesDB.Set(key, valBuf, pebble.NoSync)
	} else {
		// Cancel vote
		e.votesDB.Delete(key, pebble.NoSync)
	}

	// Register dirty deltas for SQLite batch flush
	e.dirtyMu.Lock()
	d, ok := e.dirtyDeltas[postID]
	if !ok {
		d = &postDelta{}
		e.dirtyDeltas[postID] = d
	}
	d.UpvotesDelta += int32(upDelta)
	d.DownvotesDelta += int32(downDelta)
	e.dirtyMu.Unlock()

	return upDelta, downDelta, nil
}

// -----------------------------------------------------------------------------
// Background batch flusher for SQLite
// -----------------------------------------------------------------------------

func (e *Engine) flusherLoop() {
	for {
		select {
		case <-e.flushTicker.C:
			e.flushDirtyDeltas()
		case <-e.stopFlush:
			return
		}
	}
}

func (e *Engine) flushDirtyDeltas() {
	e.FlushDirtyDeltas()
}

func (e *Engine) FlushDirtyDeltas() {
	e.dirtyMu.Lock()
	if len(e.dirtyDeltas) == 0 {
		e.dirtyMu.Unlock()
		return
	}
	toFlush := e.dirtyDeltas
	e.dirtyDeltas = make(map[uint64]*postDelta)
	e.dirtyMu.Unlock()

	e.metaWriteMu.Lock()
	defer e.metaWriteMu.Unlock()

	tx, err := e.metaDB.Begin()
	if err != nil {
		return
	}
	defer tx.Rollback()

	stmt, err := tx.Prepare("UPDATE posts SET upvotes = upvotes + ?, downvotes = downvotes + ?, num_comments = num_comments + ? WHERE id = ?")
	if err != nil {
		return
	}
	defer stmt.Close()

	for postID, delta := range toFlush {
		stmt.Exec(delta.UpvotesDelta, delta.DownvotesDelta, delta.CommentsDelta, postID)
	}
	tx.Commit()
}

// -----------------------------------------------------------------------------
// Cache File Management
// -----------------------------------------------------------------------------

// BoardDir resolves the canonical directory for a board under $BBSHOME/boards
func BoardDir(bbsHome, community string) string {
	if community == "" {
		return filepath.Join(bbsHome, "boards")
	}
	c := string(community[0])
	candidates := []string{
		filepath.Join(bbsHome, "boards", strings.ToUpper(c), community),
		filepath.Join(bbsHome, "boards", strings.ToLower(c), community),
		filepath.Join(bbsHome, "boards", c, community),
		filepath.Join(bbsHome, "boards", community),
	}
	for _, cand := range candidates {
		if fi, err := os.Stat(cand); err == nil && fi.IsDir() {
			return cand
		}
	}
	return filepath.Join(bbsHome, "boards", strings.ToUpper(c), community)
}

func (e *Engine) BoardFilePath(p *model.Post) string {
	if p.PostFile == "" || p.PostFile == "-" {
		return ""
	}
	dir := BoardDir(e.cfg.BBSHome, p.Community)
	return filepath.Join(dir, p.PostFile)
}

func (e *Engine) CacheFilePath(p *model.Post) string {
	if e.cfg.CacheDir != "" && e.cfg.CacheDir != "boards" && e.cfg.CacheDir != filepath.Join(e.cfg.BBSHome, "boards") {
		return filepath.Join(e.cfg.CacheDir, p.Community, p.PostFile)
	}
	return e.BoardFilePath(p)
}

func (e *Engine) WritePostCache(p *model.Post) error {
	path := e.CacheFilePath(p)
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		return err
	}
	content := p.Content
	if len(content) > 0 && content[len(content)-1] != '\n' {
		content += "\n"
	}
	var data []byte
	if e.cfg.IsBig5() {
		data = big5uao.Encode(content)
	} else {
		data = []byte(content)
	}
	return os.WriteFile(path, data, 0644)
}

func (e *Engine) AppendCommentCache(c *model.Comment) error {
	p, err := e.GetPost(c.PostID)
	if err != nil {
		return err
	}
	path := e.CacheFilePath(p)
	f, err := os.OpenFile(path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
		return err
	}
	defer f.Close()

	t := time.Unix(c.CreatedAt, 0).Format("01/02/2006 15:04")
	var creation string
	if c.IP != "" && c.IP != "-" {
		creation = c.IP + " " + t
	} else {
		creation = t
	}
	var b strings.Builder
	b.WriteString(fmt.Sprintf("[%d] %s %s\n", c.Sequence, c.Author, creation))

	// Indent content lines by 2 spaces, without ANSI codes
	lines := strings.Split(c.Content, "\n")
	for _, l := range lines {
		trimmed := strings.TrimRight(l, "\r ")
		if trimmed != "" {
			clean := model.StripANSI(trimmed)
			if clean != "" {
				b.WriteString("  " + clean + "\n")
			}
		}
	}

	text := b.String()
	var data []byte
	if e.cfg.IsBig5() {
		data = big5uao.Encode(text)
	} else {
		data = []byte(text)
	}
	_, err = f.Write(data)
	return err
}

func (e *Engine) SetPostContent(postID uint64, content string) error {
	p, err := e.GetPost(postID)
	if err != nil {
		return err
	}
	p.Content = content
	val := model.EncodeRFC822Post(p)
	key := model.EncodePostKey(p.ID)
	return e.postsDB.Set(key, val, pebble.NoSync)
}

func (e *Engine) RenderFullPostText(postID uint64, asBig5 bool) ([]byte, error) {
	p, err := e.GetPost(postID)
	if err != nil {
		return nil, err
	}

	if p.IsDeleted {
		reason := p.DeleteReason
		if reason == "" {
			reason = "本文已被刪除"
		}
		deletedNotice := fmt.Sprintf("[%s]\n", reason)
		if asBig5 || e.cfg.IsBig5() {
			return big5uao.Encode(deletedNotice), nil
		}
		return []byte(deletedNotice), nil
	}

	if p.Content == "" && p.PostFile != "" && p.PostFile != "-" {
		// Fallback: reload content from BBS disk file if content was empty upon initial creation
		var paths []string
		if len(p.Community) > 0 {
			c := string(p.Community[0])
			paths = []string{
				filepath.Join(e.cfg.BBSHome, "boards", c, p.Community, p.PostFile),
				filepath.Join(e.cfg.BBSHome, "boards", strings.ToUpper(c), p.Community, p.PostFile),
				filepath.Join(e.cfg.BBSHome, "boards", strings.ToLower(c), p.Community, p.PostFile),
				filepath.Join(e.cfg.BBSHome, "boards", p.Community, p.PostFile),
			}
		} else {
			matches, _ := filepath.Glob(filepath.Join(e.cfg.BBSHome, "boards", "*", "*", p.PostFile))
			if len(matches) == 0 {
				matches, _ = filepath.Glob(filepath.Join(e.cfg.BBSHome, "boards", "*", p.PostFile))
			}
			paths = matches
		}
		for _, pth := range paths {
			if b, rErr := os.ReadFile(pth); rErr == nil && len(b) > 0 {
				if e.cfg.IsBig5() {
					p.Content = big5uao.DecodeSGR66(b)
				} else {
					p.Content = string(b)
				}
				_ = e.SetPostContent(p.ID, p.Content)
				break
			}
		}
	}

	comments, err := e.GetComments(postID, 1, 100000)
	if err != nil {
		return nil, err
	}

	var buf bytes.Buffer
	buf.WriteString(p.Content)
	if len(p.Content) > 0 && p.Content[len(p.Content)-1] != '\n' {
		buf.WriteByte('\n')
	}

	for _, c := range comments {
		t := time.Unix(c.CreatedAt, 0).Format("01/02/2006 15:04")
		var creation string
		if c.IP != "" && c.IP != "-" {
			creation = c.IP + " " + t
		} else {
			creation = t
		}
		buf.WriteString(fmt.Sprintf("[%d] %s %s\n", c.Sequence, c.Author, creation))
		if c.IsDeleted {
			reason := c.DeleteReason
			if reason == "" {
				reason = "[部份違規或廣告推文已被系統自動刪除]"
			}
			buf.WriteString("  " + reason + "\n")
			continue
		}
		lines := strings.Split(c.Content, "\n")
		for _, l := range lines {
			trimmed := strings.TrimRight(l, "\r ")
			if trimmed != "" {
				clean := model.StripANSI(trimmed)
				if clean != "" {
					buf.WriteString("  " + clean + "\n")
				}
			}
		}
	}

	utf8Bytes := buf.Bytes()
	if asBig5 || e.cfg.IsBig5() {
		return big5uao.Encode(string(utf8Bytes)), nil
	}
	return utf8Bytes, nil
}

// RenderAndSave renders the full post (content + active comments) and updates the canonical BBS disk file
func (e *Engine) RenderAndSave(p *model.Post) error {
	rendered, err := e.RenderFullPostText(p.ID, e.cfg.IsBig5())
	if err != nil {
		return err
	}

	// 1. Write directly to canonical BBS disk file under boards/<C>/<Community>/<PostFile>
	boardPath := e.BoardFilePath(p)
	if boardPath != "" {
		if err := os.MkdirAll(filepath.Dir(boardPath), 0755); err == nil {
			tmpBBS := fmt.Sprintf("%s.tmp.%d", boardPath, os.Getpid())
			if err := os.WriteFile(tmpBBS, rendered, 0644); err == nil {
				_ = os.Rename(tmpBBS, boardPath)
			}
		}
	}

	// 2. Also update external cache directory if explicitly configured (and different from boards)
	if e.cfg.CacheDir != "" && e.cfg.CacheDir != "boards" && e.cfg.CacheDir != filepath.Join(e.cfg.BBSHome, "boards") {
		cachePath := filepath.Join(e.cfg.CacheDir, p.Community, p.PostFile)
		if err := os.MkdirAll(filepath.Dir(cachePath), 0755); err == nil {
			tmpCache := fmt.Sprintf("%s.tmp.%d", cachePath, os.Getpid())
			if err := os.WriteFile(tmpCache, rendered, 0644); err == nil {
				_ = os.Rename(tmpCache, cachePath)
			}
		}
	}
	return nil
}

// -----------------------------------------------------------------------------
// Disaster Recovery: Rebuild SQLite from Pebble
// -----------------------------------------------------------------------------

func (e *Engine) RebuildSQLiteFromPebble() (postCount int, commentCount int, err error) {
	e.metaWriteMu.Lock()
	defer e.metaWriteMu.Unlock()

	tx, err := e.metaDB.Begin()
	if err != nil {
		return 0, 0, err
	}
	defer tx.Rollback()

	tx.Exec("DELETE FROM posts")
	tx.Exec("DELETE FROM crossposts")

	stmt, err := tx.Prepare(`
		INSERT INTO posts (id, parent_id, community, post_file, title, author, author_token, created_at, modified, filemode, upvotes, downvotes, num_comments, num_crossposts, encoding, is_deleted, deleted_at, deleted_by, delete_reason)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, 0, 0, 0, 'utf-8', ?, ?, ?, ?)
	`)
	if err != nil {
		return 0, 0, err
	}
	defer stmt.Close()

	crosspostStmt, err := tx.Prepare(`
		INSERT INTO crossposts (source_post_id, target_community, target_post_file, operator, operator_token, created_at)
		VALUES (?, ?, ?, ?, ?, ?)
	`)
	if err != nil {
		return 0, 0, err
	}
	defer crosspostStmt.Close()

	// 1. Scan posts.db (both posts 'p' and crossposts 'x')
	pIter, err := e.postsDB.NewIter(nil)
	if err != nil {
		return 0, 0, err
	}
	defer pIter.Close()

	postCrosspostCounts := make(map[uint64]int)
	for pIter.First(); pIter.Valid(); pIter.Next() {
		if len(pIter.Key()) > 0 && pIter.Key()[0] == 'x' {
			xRec, err := model.DecodeRFC822Crosspost(pIter.Value())
			if err == nil {
				crosspostStmt.Exec(xRec.SourcePostID, xRec.TargetCommunity, xRec.TargetPostFile, xRec.Operator, xRec.OperatorToken, xRec.CreatedAt)
				postCrosspostCounts[xRec.SourcePostID]++
			}
			continue
		}

		postID, err := model.DecodePostKey(pIter.Key())
		if err != nil {
			continue
		}
		meta, _ := model.DecodeRFC822Post(pIter.Value())
		isDeleted := 0
		if meta["Deleted"] == "true" {
			isDeleted = 1
		}
		deletedAt, _ := strconv.ParseInt(meta["DeletedAt"], 10, 64)
		deletedBy := meta["DeletedBy"]
		deleteReason := meta["DeleteReason"]
		parentID, _ := strconv.ParseUint(meta["ParentID"], 10, 64)
		ctime, _ := strconv.ParseInt(meta["CreatedAt"], 10, 64)
		mtime, _ := strconv.ParseInt(meta["Modified"], 10, 64)
		if mtime == 0 {
			mtime = ctime
		}
		filemode, _ := strconv.Atoi(meta["Filemode"])
		authorToken, _ := strconv.ParseUint(meta["AuthorToken"], 10, 32)
		stmt.Exec(postID, parentID, meta["Community"], meta["PostFile"], meta["Title"], meta["Author"], uint32(authorToken), ctime, mtime, filemode, isDeleted, deletedAt, deletedBy, deleteReason)
		if isDeleted == 0 {
			postCount++
		}
	}

	// 2. Scan comments.db to recount comments
	cIter, err := e.commentsDB.NewIter(nil)
	if err != nil {
		return postCount, 0, err
	}
	defer cIter.Close()

	postCommentCounts := make(map[uint64]int)
	for cIter.First(); cIter.Valid(); cIter.Next() {
		// Skip secondary index keys starting with 'u'
		if len(cIter.Key()) > 0 && cIter.Key()[0] == 'u' {
			continue
		}
		postID, seq, err := model.DecodeCommentKey(cIter.Key())
		if err != nil {
			continue
		}
		c, err := model.DecodeRFC822Comment(postID, seq, cIter.Value())
		if err == nil && c.IsDeleted {
			continue
		}
		postCommentCounts[postID]++
		commentCount++
	}

	// 3. Scan votes.db to recount upvotes and downvotes
	vIter, err := e.votesDB.NewIter(nil)
	if err != nil {
		return postCount, commentCount, err
	}
	defer vIter.Close()

	postUpvotes := make(map[uint64]int)
	postDownvotes := make(map[uint64]int)

	for vIter.First(); vIter.Valid(); vIter.Next() {
		postID, _, err := model.DecodeVoteKey(vIter.Key())
		if err != nil {
			continue
		}
		val := vIter.Value()
		if len(val) > 0 {
			if val[0] == 'U' {
				postUpvotes[postID]++
			} else if val[0] == 'D' {
				postDownvotes[postID]++
			}
		}
	}

	updateStmt, err := tx.Prepare("UPDATE posts SET upvotes = ?, downvotes = ?, num_comments = ?, num_crossposts = ? WHERE id = ?")
	if err != nil {
		return postCount, commentCount, err
	}
	defer updateStmt.Close()

	// Update all posts with aggregated counts
	allPostIDs := make(map[uint64]bool)
	for id := range postCommentCounts {
		allPostIDs[id] = true
	}
	for id := range postUpvotes {
		allPostIDs[id] = true
	}
	for id := range postDownvotes {
		allPostIDs[id] = true
	}
	for id := range postCrosspostCounts {
		allPostIDs[id] = true
	}

	for postID := range allPostIDs {
		updateStmt.Exec(postUpvotes[postID], postDownvotes[postID], postCommentCounts[postID], postCrosspostCounts[postID], postID)
	}

	return postCount, commentCount, tx.Commit()
}

// -----------------------------------------------------------------------------
// History and Editing (Old revisions stored in history.pebble)
// -----------------------------------------------------------------------------

func (e *Engine) UpdatePost(postID uint64, newTitle, newContent, editor string, expectedModified int64) (uint32, int64, error) {
	p, err := e.GetPost(postID)
	if err != nil {
		return 0, 0, err
	}
	if editor == "" {
		editor = p.Author
	}

	// Optimistic concurrency check: if expectedModified > 0 and doesn't match current modified timestamp, abort!
	if expectedModified > 0 && p.Modified > 0 && p.Modified != expectedModified {
		return 0, p.Modified, &model.ConflictError{LatestModified: p.Modified}
	}

	// Compare with current content and title in DB!
	isTitleChanged := newTitle != "" && newTitle != p.Title
	isContentChanged := newContent != p.Content

	if !isTitleChanged && !isContentChanged {
		// No changes made! Return revision 0
		return 0, p.Modified, nil
	}

	// 1. Determine next revision
	prefix := model.EncodePostHistoryPrefix(postID)
	iter, err := e.historyDB.NewIter(&pebble.IterOptions{
		LowerBound: prefix,
		UpperBound: append(prefix, 0xff),
	})
	var nextRev uint32 = 1
	if err == nil {
		if iter.Last() {
			_, lastRev, err := model.DecodePostHistoryKey(iter.Key())
			if err == nil {
				nextRev = lastRev + 1
			}
		}
		iter.Close()
	}

	// 2. Archive previous version into historyDB
	now := time.Now().Unix()
	if now <= p.Modified {
		now = p.Modified + 1
	}
	revRecord := &model.PostRevision{
		PostID:      p.ID,
		Revision:    nextRev,
		Community:   p.Community,
		PostFile:    p.PostFile,
		Title:       p.Title,
		Author:      p.Author,
		AuthorToken: p.AuthorToken,
		CreatedAt:   p.CreatedAt,
		EditedAt:    now,
		Editor:      editor,
		Content:     p.Content,
		Encoding:    "utf-8",
	}
	revKey := model.EncodePostHistoryKey(p.ID, nextRev)
	revVal := model.EncodeRFC822PostRevision(revRecord)
	if err := e.historyDB.Set(revKey, revVal, pebble.NoSync); err != nil {
		return 0, p.Modified, fmt.Errorf("write post history failed: %w", err)
	}

	// 3. Update post with new title / content / modified
	if newTitle != "" {
		p.Title = newTitle
	}
	p.Content = newContent
	p.Modified = now
	postKey := model.EncodePostKey(p.ID)
	postVal := model.EncodeRFC822Post(p)
	if err := e.postsDB.Set(postKey, postVal, pebble.NoSync); err != nil {
		return 0, p.Modified, fmt.Errorf("update post failed: %w", err)
	}

	// 4. Update SQLite title and modified
	e.metaWriteMu.Lock()
	_, _ = e.metaDB.Exec("UPDATE posts SET title = ?, modified = ? WHERE id = ?", p.Title, p.Modified, p.ID)
	e.metaWriteMu.Unlock()

	// 5. Re-render and update cache file and BBS disk file
	_ = e.RenderAndSave(p)

	return nextRev, p.Modified, nil
}

func (e *Engine) UpdatePostByCommunityFile(community, postFile, newTitle, newContent, editor string, expectedModified int64) (uint32, int64, error) {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return 0, 0, err
	}
	return e.UpdatePost(p.ID, newTitle, newContent, editor, expectedModified)
}

func (e *Engine) UpdatePostTitle(postID uint64, newTitle, editor string) (uint32, error) {
	p, err := e.GetPost(postID)
	if err != nil {
		return 0, err
	}
	newTitle = strings.TrimRight(newTitle, "\r\n")
	if newTitle == "" || newTitle == p.Title {
		return 0, nil
	}
	if editor == "" {
		editor = p.Author
	}

	// 1. Determine next revision
	prefix := model.EncodePostHistoryPrefix(postID)
	iter, err := e.historyDB.NewIter(&pebble.IterOptions{
		LowerBound: prefix,
		UpperBound: append(prefix, 0xff),
	})
	var nextRev uint32 = 1
	if err == nil {
		if iter.Last() {
			_, lastRev, err := model.DecodePostHistoryKey(iter.Key())
			if err == nil {
				nextRev = lastRev + 1
			}
		}
		iter.Close()
	}

	// 2. Archive previous version into historyDB with Action="update_title"
	now := time.Now().Unix()
	if now <= p.Modified {
		now = p.Modified + 1
	}
	revRecord := &model.PostRevision{
		PostID:      p.ID,
		Revision:    nextRev,
		Community:   p.Community,
		PostFile:    p.PostFile,
		Title:       p.Title,
		Author:      p.Author,
		AuthorToken: p.AuthorToken,
		CreatedAt:   p.CreatedAt,
		EditedAt:    now,
		Editor:      editor,
		Action:      "update_title",
		Content:     p.Content,
		Encoding:    "utf-8",
	}
	revKey := model.EncodePostHistoryKey(p.ID, nextRev)
	revVal := model.EncodeRFC822PostRevision(revRecord)
	if err := e.historyDB.Set(revKey, revVal, pebble.NoSync); err != nil {
		return 0, fmt.Errorf("write post history failed: %w", err)
	}

	// 3. Update post title in Pebble
	p.Title = newTitle
	p.Modified = now
	postKey := model.EncodePostKey(p.ID)
	postVal := model.EncodeRFC822Post(p)
	if err := e.postsDB.Set(postKey, postVal, pebble.NoSync); err != nil {
		return 0, fmt.Errorf("update post failed: %w", err)
	}

	// 4. Update SQLite title and modified
	e.metaWriteMu.Lock()
	_, _ = e.metaDB.Exec("UPDATE posts SET title = ?, modified = ? WHERE id = ?", p.Title, p.Modified, p.ID)
	e.metaWriteMu.Unlock()

	// 5. Re-render cache
	_ = e.RenderAndSave(p)

	return nextRev, nil
}

func (e *Engine) UpdatePostTitleByCommunityFile(community, postFile, newTitle, editor string) (uint32, error) {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return 0, err
	}
	return e.UpdatePostTitle(p.ID, newTitle, editor)
}

func (e *Engine) DeletePost(postID uint64, deleter, reason string) (uint32, error) {
	p, err := e.GetPost(postID)
	if err != nil {
		return 0, err
	}
	if p.IsDeleted {
		return 0, nil // Already deleted
	}
	if deleter == "" {
		deleter = p.Author
	}

	// 1. Determine next revision
	prefix := model.EncodePostHistoryPrefix(postID)
	iter, err := e.historyDB.NewIter(&pebble.IterOptions{
		LowerBound: prefix,
		UpperBound: append(prefix, 0xff),
	})
	var nextRev uint32 = 1
	if err == nil {
		if iter.Last() {
			_, lastRev, err := model.DecodePostHistoryKey(iter.Key())
			if err == nil {
				nextRev = lastRev + 1
			}
		}
		iter.Close()
	}

	// 2. Archive previous version into historyDB with Action="delete"
	now := time.Now().Unix()
	if now <= p.Modified {
		now = p.Modified + 1
	}
	revRecord := &model.PostRevision{
		PostID:      p.ID,
		Revision:    nextRev,
		Community:   p.Community,
		PostFile:    p.PostFile,
		Title:       p.Title,
		Author:      p.Author,
		AuthorToken: p.AuthorToken,
		CreatedAt:   p.CreatedAt,
		EditedAt:    now,
		Editor:      deleter,
		Action:      "delete",
		Reason:      reason,
		Content:     p.Content,
		Encoding:    "utf-8",
	}
	revKey := model.EncodePostHistoryKey(p.ID, nextRev)
	revVal := model.EncodeRFC822PostRevision(revRecord)
	if err := e.historyDB.Set(revKey, revVal, pebble.NoSync); err != nil {
		return 0, fmt.Errorf("write post history failed: %w", err)
	}

	// 3. Update post state
	p.IsDeleted = true
	p.DeletedAt = now
	p.DeletedBy = deleter
	p.DeleteReason = reason
	p.Modified = now
	postKey := model.EncodePostKey(p.ID)
	postVal := model.EncodeRFC822Post(p)
	if err := e.postsDB.Set(postKey, postVal, pebble.NoSync); err != nil {
		return 0, fmt.Errorf("update post failed: %w", err)
	}

	// 4. Update SQLite
	e.metaWriteMu.Lock()
	_, _ = e.metaDB.Exec("UPDATE posts SET is_deleted = 1, deleted_at = ?, deleted_by = ?, delete_reason = ?, modified = ? WHERE id = ?",
		now, deleter, reason, now, p.ID)
	e.metaWriteMu.Unlock()

	// 5. Update Cache and BBS disk file
	_ = e.RenderAndSave(p)

	return nextRev, nil
}

func (e *Engine) DeletePostByCommunityFile(community, postFile, deleter, reason string) (uint32, error) {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return 0, err
	}
	return e.DeletePost(p.ID, deleter, reason)
}

func (e *Engine) UndeletePost(postID uint64) error {
	p, err := e.GetPost(postID)
	if err != nil {
		return err
	}
	if !p.IsDeleted {
		return nil
	}
	now := time.Now().Unix()
	p.IsDeleted = false
	p.DeletedAt = 0
	p.DeletedBy = ""
	p.DeleteReason = ""
	p.Modified = now

	postKey := model.EncodePostKey(p.ID)
	postVal := model.EncodeRFC822Post(p)
	if err := e.postsDB.Set(postKey, postVal, pebble.NoSync); err != nil {
		return fmt.Errorf("undelete post failed: %w", err)
	}

	e.metaWriteMu.Lock()
	_, _ = e.metaDB.Exec("UPDATE posts SET is_deleted = 0, deleted_at = 0, deleted_by = '', delete_reason = '', modified = ? WHERE id = ?",
		now, p.ID)
	e.metaWriteMu.Unlock()

	_ = e.RenderAndSave(p)
	return nil
}

func (e *Engine) UndeletePostByCommunityFile(community, postFile string) error {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return err
	}
	return e.UndeletePost(p.ID)
}

func (e *Engine) UpdateComment(postID uint64, seq uint32, newContent, editor string) (uint32, error) {
	cKey := model.EncodeCommentKey(postID, seq)
	val, closer, err := e.commentsDB.Get(cKey)
	if err != nil {
		return 0, fmt.Errorf("comment %d/%d not found: %w", postID, seq, err)
	}
	currentComment, err := model.DecodeRFC822Comment(postID, seq, val)
	closer.Close()
	if err != nil {
		return 0, err
	}
	if editor == "" {
		editor = currentComment.Author
	}

	if newContent == currentComment.Content {
		// No changes made! Return revision 0
		return 0, nil
	}

	// 1. Determine next revision
	prefix := model.EncodeCommentHistoryPrefix(postID, seq)
	iter, err := e.historyDB.NewIter(&pebble.IterOptions{
		LowerBound: prefix,
		UpperBound: append(prefix, 0xff),
	})
	var nextRev uint32 = 1
	if err == nil {
		if iter.Last() {
			_, _, lastRev, err := model.DecodeCommentHistoryKey(iter.Key())
			if err == nil {
				nextRev = lastRev + 1
			}
		}
		iter.Close()
	}

	// 2. Archive previous version into historyDB
	now := time.Now().Unix()
	revRecord := &model.CommentRevision{
		PostID:      postID,
		Sequence:    seq,
		Revision:    nextRev,
		Author:      currentComment.Author,
		AuthorToken: currentComment.AuthorToken,
		CreatedAt:   currentComment.CreatedAt,
		EditedAt:    now,
		Editor:      editor,
		IP:          currentComment.IP,
		Content:     currentComment.Content,
		Encoding:    "utf-8",
	}
	revKey := model.EncodeCommentHistoryKey(postID, seq, nextRev)
	revVal := model.EncodeRFC822CommentRevision(revRecord)
	if err := e.historyDB.Set(revKey, revVal, pebble.NoSync); err != nil {
		return 0, fmt.Errorf("write comment history failed: %w", err)
	}

	// 3. Update current comment
	currentComment.Content = newContent
	newVal := model.EncodeRFC822Comment(currentComment)
	if err := e.commentsDB.Set(cKey, newVal, pebble.NoSync); err != nil {
		return 0, fmt.Errorf("update comment failed: %w", err)
	}

	return nextRev, nil
}

func (e *Engine) UpdateCommentByCommunityFile(community, postFile string, seq uint32, newContent, editor string) (uint32, error) {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return 0, err
	}
	return e.UpdateComment(p.ID, seq, newContent, editor)
}

func (e *Engine) DeleteComment(postID uint64, seq uint32, deleter, reason string) (uint32, error) {
	cKey := model.EncodeCommentKey(postID, seq)
	val, closer, err := e.commentsDB.Get(cKey)
	if err != nil {
		return 0, fmt.Errorf("comment %d/%d not found: %w", postID, seq, err)
	}
	currentComment, err := model.DecodeRFC822Comment(postID, seq, val)
	closer.Close()
	if err != nil {
		return 0, err
	}
	if currentComment.IsDeleted {
		return 0, nil // Already deleted
	}
	if deleter == "" {
		deleter = currentComment.Author
	}

	// 1. Determine next revision
	prefix := model.EncodeCommentHistoryPrefix(postID, seq)
	iter, err := e.historyDB.NewIter(&pebble.IterOptions{
		LowerBound: prefix,
		UpperBound: append(prefix, 0xff),
	})
	var nextRev uint32 = 1
	if err == nil {
		if iter.Last() {
			_, _, lastRev, err := model.DecodeCommentHistoryKey(iter.Key())
			if err == nil {
				nextRev = lastRev + 1
			}
		}
		iter.Close()
	}

	// 2. Archive previous version into historyDB with Action="delete"
	now := time.Now().Unix()
	revRecord := &model.CommentRevision{
		PostID:      postID,
		Sequence:    seq,
		Revision:    nextRev,
		Author:      currentComment.Author,
		AuthorToken: currentComment.AuthorToken,
		CreatedAt:   currentComment.CreatedAt,
		EditedAt:    now,
		Editor:      deleter,
		IP:          currentComment.IP,
		Action:      "delete",
		Reason:      reason,
		Content:     currentComment.Content,
		Encoding:    "utf-8",
	}
	revKey := model.EncodeCommentHistoryKey(postID, seq, nextRev)
	revVal := model.EncodeRFC822CommentRevision(revRecord)
	if err := e.historyDB.Set(revKey, revVal, pebble.NoSync); err != nil {
		return 0, fmt.Errorf("write comment history failed: %w", err)
	}

	// 3. Update current comment state
	currentComment.IsDeleted = true
	currentComment.DeletedAt = now
	currentComment.DeletedBy = deleter
	currentComment.DeleteReason = reason
	newVal := model.EncodeRFC822Comment(currentComment)
	if err := e.commentsDB.Set(cKey, newVal, pebble.NoSync); err != nil {
		return 0, fmt.Errorf("delete comment failed: %w", err)
	}

	// 4. Update SQLite num_comments count
	e.FlushDirtyDeltas()
	e.metaWriteMu.Lock()
	_, _ = e.metaDB.Exec("UPDATE posts SET num_comments = CASE WHEN num_comments > 0 THEN num_comments - 1 ELSE 0 END WHERE id = ?", postID)
	e.metaWriteMu.Unlock()

	// 5. Re-render and save
	if p, err := e.GetPost(postID); err == nil && p != nil {
		_ = e.RenderAndSave(p)
	}

	return nextRev, nil
}

func (e *Engine) DeleteCommentByCommunityFile(community, postFile string, seq uint32, deleter, reason string) (uint32, error) {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return 0, err
	}
	return e.DeleteComment(p.ID, seq, deleter, reason)
}

func (e *Engine) UndeleteComment(postID uint64, seq uint32) error {
	cKey := model.EncodeCommentKey(postID, seq)
	val, closer, err := e.commentsDB.Get(cKey)
	if err != nil {
		return fmt.Errorf("comment %d/%d not found: %w", postID, seq, err)
	}
	c, err := model.DecodeRFC822Comment(postID, seq, val)
	closer.Close()
	if err != nil {
		return err
	}
	if !c.IsDeleted {
		return nil
	}
	c.IsDeleted = false
	c.DeletedAt = 0
	c.DeletedBy = ""
	c.DeleteReason = ""

	newVal := model.EncodeRFC822Comment(c)
	if err := e.commentsDB.Set(cKey, newVal, pebble.NoSync); err != nil {
		return fmt.Errorf("undelete comment failed: %w", err)
	}

	e.FlushDirtyDeltas()
	e.metaWriteMu.Lock()
	_, _ = e.metaDB.Exec("UPDATE posts SET num_comments = num_comments + 1 WHERE id = ?", postID)
	e.metaWriteMu.Unlock()

	if p, err := e.GetPost(postID); err == nil && p != nil {
		_ = e.RenderAndSave(p)
	}
	return nil
}

func (e *Engine) UndeleteCommentByCommunityFile(community, postFile string, seq uint32) error {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return err
	}
	return e.UndeleteComment(p.ID, seq)
}

func (e *Engine) GetPostHistory(postID uint64, rev uint32) (*model.PostRevision, error) {
	key := model.EncodePostHistoryKey(postID, rev)
	val, closer, err := e.historyDB.Get(key)
	if err != nil {
		return nil, fmt.Errorf("post %d revision %d not found: %w", postID, rev, err)
	}
	defer closer.Close()
	return model.DecodeRFC822PostRevision(val)
}

func (e *Engine) ListPostHistory(postID uint64) ([]*model.PostRevision, error) {
	prefix := model.EncodePostHistoryPrefix(postID)
	iter, err := e.historyDB.NewIter(&pebble.IterOptions{
		LowerBound: prefix,
		UpperBound: append(prefix, 0xff),
	})
	if err != nil {
		return nil, err
	}
	defer iter.Close()

	var revs []*model.PostRevision
	for iter.First(); iter.Valid() && bytes.HasPrefix(iter.Key(), prefix); iter.Next() {
		r, err := model.DecodeRFC822PostRevision(iter.Value())
		if err == nil {
			revs = append(revs, r)
		}
	}
	return revs, nil
}

func (e *Engine) GetCommentHistory(postID uint64, seq uint32, rev uint32) (*model.CommentRevision, error) {
	key := model.EncodeCommentHistoryKey(postID, seq, rev)
	val, closer, err := e.historyDB.Get(key)
	if err != nil {
		return nil, fmt.Errorf("comment %d/%d revision %d not found: %w", postID, seq, rev, err)
	}
	defer closer.Close()
	return model.DecodeRFC822CommentRevision(val)
}

func (e *Engine) ListCommentHistory(postID uint64, seq uint32) ([]*model.CommentRevision, error) {
	prefix := model.EncodeCommentHistoryPrefix(postID, seq)
	iter, err := e.historyDB.NewIter(&pebble.IterOptions{
		LowerBound: prefix,
		UpperBound: append(prefix, 0xff),
	})
	if err != nil {
		return nil, err
	}
	defer iter.Close()

	var revs []*model.CommentRevision
	for iter.First(); iter.Valid() && bytes.HasPrefix(iter.Key(), prefix); iter.Next() {
		r, err := model.DecodeRFC822CommentRevision(iter.Value())
		if err == nil {
			revs = append(revs, r)
		}
	}
	return revs, nil
}

// ImportedPostData packages a post along with its comments and crossposts for batch import.
type ImportedPostData struct {
	Post       *model.Post
	Comments   []*model.Comment
	Crossposts []*model.CrosspostRecord
}

// ImportPostBatch imports a slice of posts and their comments atomically into SQLite and Pebble,
// and optionally renders them directly to renderTarget directory.
func (e *Engine) ImportPostBatch(batch []*ImportedPostData, renderTarget string, legacyFormat ...bool) error {
	if len(batch) == 0 {
		return nil
	}

	e.metaWriteMu.Lock()
	unlocked := false
	defer func() {
		if !unlocked {
			e.metaWriteMu.Unlock()
		}
	}()

	tx, err := e.metaDB.Begin()
	if err != nil {
		return fmt.Errorf("begin transaction failed: %w", err)
	}
	defer tx.Rollback()

	stmtPost, err := tx.Prepare(`
		INSERT INTO posts (parent_id, community, post_file, title, author, author_token, created_at, modified, filemode, upvotes, downvotes, num_comments, num_crossposts, encoding)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
	`)
	if err != nil {
		return fmt.Errorf("prepare insert post failed: %w", err)
	}
	defer stmtPost.Close()

	stmtCP, err := tx.Prepare(`
		INSERT INTO crossposts (source_post_id, target_community, target_post_file, operator, operator_token, created_at)
		VALUES (?, ?, ?, ?, ?, ?)
	`)
	if err != nil {
		return fmt.Errorf("prepare insert crosspost failed: %w", err)
	}
	defer stmtCP.Close()

	postBatch := e.postsDB.NewBatch()
	defer postBatch.Close()
	commentBatch := e.commentsDB.NewBatch()
	defer commentBatch.Close()

	for _, item := range batch {
		p := item.Post
		p.NumComments = len(item.Comments)
		p.NumCrossposts = len(item.Crossposts)
		if p.Encoding == "" {
			p.Encoding = "utf-8"
		}

		res, err := stmtPost.Exec(
			p.ParentID, p.Community, p.PostFile, p.Title, p.Author, p.AuthorToken,
			p.CreatedAt, p.Modified, p.Filemode, p.Upvotes, p.Downvotes,
			p.NumComments, p.NumCrossposts, p.Encoding,
		)
		if err != nil {
			return fmt.Errorf("insert post %s/%s failed: %w", p.Community, p.PostFile, err)
		}
		id, err := res.LastInsertId()
		if err != nil {
			return err
		}
		p.ID = uint64(id)

		// Set in postBatch
		postKey := model.EncodePostKey(p.ID)
		postVal := model.EncodeRFC822Post(p)
		if err := postBatch.Set(postKey, postVal, nil); err != nil {
			return fmt.Errorf("postBatch.Set failed: %w", err)
		}

		// Comments
		for i, c := range item.Comments {
			c.PostID = p.ID
			c.Sequence = uint32(i + 1)
			cKey := model.EncodeCommentKey(p.ID, c.Sequence)
			cVal := model.EncodeRFC822Comment(c)
			if err := commentBatch.Set(cKey, cVal, nil); err != nil {
				return fmt.Errorf("commentBatch.Set failed: %w", err)
			}
			authKey := model.EncodeAuthorCommentKey(c.Author, c.CreatedAt, p.ID, c.Sequence)
			if err := commentBatch.Set(authKey, []byte{}, nil); err != nil {
				return fmt.Errorf("commentBatch.Set author failed: %w", err)
			}
		}

		// Crossposts
		for i, cp := range item.Crossposts {
			cp.SourcePostID = p.ID
			cpRes, cpErr := stmtCP.Exec(p.ID, cp.TargetCommunity, cp.TargetPostFile, cp.Operator, cp.OperatorToken, cp.CreatedAt)
			if cpErr == nil {
				cpID, _ := cpRes.LastInsertId()
				cp.ID = uint64(cpID)
				cKey := model.EncodeCrosspostKey(p.ID, uint32(i+1))
				cVal := model.EncodeRFC822Crosspost(cp)
				_ = postBatch.Set(cKey, cVal, nil)
			}
		}
	}

	e.seqMu.Lock()
	for _, item := range batch {
		seqVal := uint32(len(item.Comments))
		e.postSeqs[item.Post.ID] = &seqVal
	}
	e.seqMu.Unlock()

	// Commit Pebble batches
	if err := postBatch.Commit(pebble.NoSync); err != nil {
		return fmt.Errorf("commit postBatch failed: %w", err)
	}
	if err := commentBatch.Commit(pebble.NoSync); err != nil {
		return fmt.Errorf("commit commentBatch failed: %w", err)
	}

	// Commit SQLite transaction
	if err := tx.Commit(); err != nil {
		return fmt.Errorf("commit sqlite batch failed: %w", err)
	}
	e.metaWriteMu.Unlock() // Unlock DB write mutex before rendering files
	unlocked = true

	// Render directly to renderTarget if requested (parallel file writers)
	if renderTarget != "" {
		if err := os.MkdirAll(renderTarget, 0755); err != nil {
			return fmt.Errorf("mkdir renderTarget failed: %w", err)
		}
		isLegacy := len(legacyFormat) > 0 && legacyFormat[0]
		var wg sync.WaitGroup
		sem := make(chan struct{}, 16)
		for _, item := range batch {
			wg.Add(1)
			sem <- struct{}{}
			go func(it *ImportedPostData) {
				defer wg.Done()
				defer func() { <-sem }()
				var rendered []byte
				if isLegacy {
					rendered = RenderLegacyPostWithComments(it.Post, it.Comments, e.cfg.IsBig5())
				} else {
					rendered = RenderPostWithComments(it.Post, it.Comments, e.cfg.IsBig5())
				}
				targetPath := filepath.Join(renderTarget, it.Post.PostFile)
				_ = os.WriteFile(targetPath, rendered, 0644)
			}(item)
		}
		wg.Wait()
	}

	return nil
}

// RenderPostWithComments renders a post and its comments directly in memory
func RenderPostWithComments(p *model.Post, comments []*model.Comment, asBig5 bool) []byte {
	var buf bytes.Buffer
	buf.WriteString(p.Content)
	if len(p.Content) > 0 && p.Content[len(p.Content)-1] != '\n' {
		buf.WriteByte('\n')
	}

	for _, c := range comments {
		t := time.Unix(c.CreatedAt, 0).Format("01/02/2006 15:04")
		var creation string
		if c.IP != "" && c.IP != "-" {
			creation = c.IP + " " + t
		} else {
			creation = t
		}
		buf.WriteString(fmt.Sprintf("[%d] %s %s\n", c.Sequence, c.Author, creation))
		if c.IsDeleted {
			reason := c.DeleteReason
			if reason == "" {
				reason = "[部份違規或廣告推文已被系統自動刪除]"
			}
			buf.WriteString("  " + reason + "\n")
			continue
		}
		lines := strings.Split(c.Content, "\n")
		for _, l := range lines {
			trimmed := strings.TrimRight(l, "\r ")
			if trimmed != "" {
				clean := model.StripANSI(trimmed)
				if clean != "" {
					buf.WriteString("  " + clean + "\n")
				}
			}
		}
	}

	if asBig5 {
		return big5uao.Encode(buf.String())
	}
	return buf.Bytes()
}

func visualWidth(s string) int {
	w := 0
	for _, r := range s {
		if r > 127 {
			w += 2
		} else {
			w += 1
		}
	}
	return w
}

// RenderLegacyPostWithComments renders a post and its comments directly in memory in legacy BBS push format
func RenderLegacyPostWithComments(p *model.Post, comments []*model.Comment, asBig5 bool) []byte {
	var buf bytes.Buffer
	buf.WriteString(p.Content)
	if len(p.Content) > 0 && p.Content[len(p.Content)-1] != '\n' {
		buf.WriteByte('\n')
	}

	for _, c := range comments {
		if c.IsDeleted {
			buf.WriteString("\x1b[1;30m[此推文已被刪除]\x1b[m\n")
			continue
		}

		lines := strings.Split(c.Content, "\n")
		t := time.Unix(c.CreatedAt, 0)
		dateStr := t.Format("01/02 15:04")

		for lineIdx, rawLine := range lines {
			line := strings.TrimRight(rawLine, "\r ")
			if line == "" && lineIdx > 0 {
				continue
			}

			// First line gets c.LegacyType; subsequent merged lines are arrows (or OLD_RECOMMEND)
			lineType := c.LegacyType
			if lineIdx > 0 {
				if c.LegacyType == model.LegacyTypeOldRecommend {
					lineType = model.LegacyTypeOldRecommend
				} else {
					lineType = model.LegacyTypeArrow
				}
			}

			var tag, tagColor string
			switch lineType {
			case model.LegacyTypeOldRecommend:
				tag = "→ "
				tagColor = "\x1b[1;31m"
			case model.LegacyTypePush:
				tag = "推 "
				tagColor = "\x1b[1;37m"
			case model.LegacyTypeBoo:
				tag = "噓 "
				tagColor = "\x1b[1;31m"
			case model.LegacyTypeArrow:
				fallthrough
			default:
				tag = "→ "
				tagColor = "\x1b[1;31m"
			}

			var tail string
			if lineType == model.LegacyTypeOldRecommend {
				if c.IP != "" && c.IP != "-" {
					tail = fmt.Sprintf("推 %s %s", c.IP, dateStr)
				} else {
					tail = fmt.Sprintf("推 %s", dateStr)
				}
			} else {
				if c.IP != "" && c.IP != "-" {
					tail = fmt.Sprintf(" %-15s %s", c.IP, dateStr)
				} else {
					tail = fmt.Sprintf(" %s", dateStr)
				}
			}

			// Calculate padding for 80-column alignment
			prefixWidth := 3 + len(c.Author) + 2 // tag(3) + author + ": "(2)
			msgWidth := visualWidth(line)
			tailWidth := len(tail)
			totalWidth := prefixWidth + msgWidth + tailWidth

			pad := 78 - totalWidth
			if pad < 1 {
				pad = 1
			}

			buf.WriteString(fmt.Sprintf("%s%s\x1b[33m%s\x1b[m\x1b[33m: %s%s\x1b[m%s\n",
				tagColor, tag, c.Author, line, strings.Repeat(" ", pad), tail))
		}
	}

	if asBig5 {
		return big5uao.Encode(buf.String())
	}
	return buf.Bytes()
}

// RenderLegacyPostText renders a post and its comments from storage in legacy BBS format
func (e *Engine) RenderLegacyPostText(postID uint64, asBig5 bool) ([]byte, error) {
	p, err := e.GetPost(postID)
	if err != nil {
		return nil, err
	}
	if p.IsDeleted {
		reason := p.DeleteReason
		if reason == "" {
			reason = "本文已被刪除"
		}
		deletedNotice := fmt.Sprintf("[%s]\n", reason)
		if asBig5 || e.cfg.IsBig5() {
			return big5uao.Encode(deletedNotice), nil
		}
		return []byte(deletedNotice), nil
	}

	comments, err := e.GetComments(postID, 1, 100000)
	if err != nil {
		return nil, err
	}

	return RenderLegacyPostWithComments(p, comments, asBig5 || e.cfg.IsBig5()), nil
}

// RenderCommunityOptions defines parameters for bulk community rendering
type RenderCommunityOptions struct {
	Community    string
	TargetDir    string
	LegacyFormat bool
	NumWorkers   int
	ProgressFn   func(current, total int)
}

// RenderCommunity renders all posts and comments of a community directly into TargetDir in parallel
func (e *Engine) RenderCommunity(opts RenderCommunityOptions) (int, error) {
	if opts.TargetDir == "" {
		return 0, fmt.Errorf("targetDir cannot be empty")
	}
	if err := os.MkdirAll(opts.TargetDir, 0755); err != nil {
		return 0, fmt.Errorf("mkdir targetDir failed: %w", err)
	}

	rows, err := e.metaDB.Query("SELECT id, post_file FROM posts WHERE community = ? AND is_deleted = 0 ORDER BY id ASC", opts.Community)
	if err != nil {
		return 0, fmt.Errorf("query posts failed: %w", err)
	}
	defer rows.Close()

	type renderItem struct {
		id       uint64
		postFile string
	}
	var items []renderItem
	for rows.Next() {
		var it renderItem
		if err := rows.Scan(&it.id, &it.postFile); err == nil && it.postFile != "" && it.postFile != "-" {
			items = append(items, it)
		}
	}

	total := len(items)
	if total == 0 {
		return 0, nil
	}

	numWorkers := opts.NumWorkers
	if numWorkers <= 0 {
		numWorkers = 32
	}
	if numWorkers > total {
		numWorkers = total
	}

	jobs := make(chan renderItem, 2000)
	var wg sync.WaitGroup
	var renderedCount atomic.Int64

	for w := 0; w < numWorkers; w++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for it := range jobs {
				var rendered []byte
				var rErr error
				if opts.LegacyFormat {
					rendered, rErr = e.RenderLegacyPostText(it.id, e.cfg.IsBig5())
				} else {
					rendered, rErr = e.RenderFullPostText(it.id, e.cfg.IsBig5())
				}
				if rErr == nil && len(rendered) > 0 {
					targetPath := filepath.Join(opts.TargetDir, it.postFile)
					_ = os.WriteFile(targetPath, rendered, 0644)
				}
				cur := int(renderedCount.Add(1))
				if opts.ProgressFn != nil && (cur%200 == 0 || cur == total) {
					opts.ProgressFn(cur, total)
				}
			}
		}()
	}

	for _, it := range items {
		jobs <- it
	}
	close(jobs)
	wg.Wait()

	return int(renderedCount.Load()), nil
}

