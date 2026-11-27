package storage

import (
	"bytes"
	"database/sql"
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
	"unicode/utf8"

	"github.com/cockroachdb/pebble"
	"golang.org/x/sys/unix"
	_ "modernc.org/sqlite"

	"pttbbs/big5uao"
	"pttbbs/nest/config"
	"pttbbs/nest/model"
)

type Config struct {
	BBSHome        string
	DataDir        string
	CacheDir       string
	CacheSizeMB    int
	FlushSeconds   int
	MinReplyRunes  int
	FilterEncoding string // "big5" (default) or "utf-8"
	NumShards      int    // engines sharing the file descriptors
	MaxOpenFiles   int
	// MaxPostCommentLines is how many lines the comments of a post may
	// have in all (0: no limit); see ErrPostTooLarge.
	MaxPostCommentLines int

	// NextPostID allocates the ID of a new post. Post IDs are global: a
	// sharded storage hands out one sequence to all its engines, so an ID
	// names one post wherever it lives (and keeps naming it when the post
	// moves to another engine). If nil, SQLite assigns the next ID of this
	// engine (AUTOINCREMENT).
	NextPostID func() uint64

	// Communities is the community registry shared by all engines. If nil,
	// OpenEngine opens one in DataDir and closes it with the engine.
	Communities *CommunityRegistry
}

// writeHeavyOptions tunes the options of a Pebble DB that imports write in
// bulk (the comments of a board are tens of MB per batch): with Pebble's
// defaults, one compaction at a time and writes stopped at 12 files in L0,
// an import into one engine spent much of its time waiting for compactions.
func writeHeavyOptions(o *pebble.Options) *pebble.Options {
	compactions := min(4, max(1, runtime.NumCPU()/4))
	o.MaxConcurrentCompactions = func() int { return compactions }
	o.MemTableStopWritesThreshold = 4
	o.L0CompactionThreshold = 4
	o.L0StopWritesThreshold = 32
	o.LBaseMaxBytes = 256 << 20
	return o
}

// zstdOptions compresses text (post bodies, comments) with zstd in 16 KB
// blocks: a third smaller than Snappy in 4 KB, for twice the read CPU. Every
// level, as imports write in key order and Pebble moves those tables down
// without rewriting them, so the flush decides how most data is kept.
func zstdOptions(o *pebble.Options) *pebble.Options {
	o.Levels = make([]pebble.LevelOptions, 7)
	for i := range o.Levels {
		o.Levels[i].BlockSize = 16 << 10
		o.Levels[i].Compression = pebble.ZstdCompression
	}
	return o
}

func (c Config) IsBig5() bool {
	enc := strings.ToLower(strings.TrimSpace(c.FilterEncoding))
	return enc == "" || enc == "big5" || enc == "big5-uao" || enc == "uao"
}

type Engine struct {
	cfg         Config
	postsDB     *pebble.DB
	commentsDB  *pebble.DB
	votesDB     *pebble.DB
	historyDB   *pebble.DB
	metaDB      *sql.DB
	metaWriteMu sync.Mutex
	// voteMu makes reading and writing a vote atomic, or two concurrent
	// votes of one person would both count.
	voteMu  sync.Mutex
	reg     *CommunityRegistry
	ownsReg bool

	// In-memory atomic sequence counters per post
	seqMu    sync.RWMutex
	postSeqs map[uint64]*uint32

	// linesMu guards postLines, the lines of the comments of the posts
	// commented on since nest.svc started (see reserveCommentLines).
	linesMu     sync.Mutex
	postLines   map[uint64]int
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
		cfg.DataDir = config.DataDir(cfg.BBSHome)
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

	reg := cfg.Communities
	ownsReg := false
	if reg == nil {
		var err error
		if reg, err = OpenCommunityRegistry(cfg.DataDir); err != nil {
			return nil, err
		}
		ownsReg = true
	}
	opened := false
	defer func() {
		if !opened && ownsReg {
			reg.Close()
		}
	}()
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
	postsOpts := zstdOptions(writeHeavyOptions(&pebble.Options{
		Cache:        pebble.NewCache(int64(cfg.CacheSizeMB/3) * 1024 * 1024),
		MemTableSize: 16 * 1024 * 1024,
		MaxOpenFiles: maxOpenFiles,
	}))
	postsDB, err := pebble.Open(postsPath, postsOpts)
	if err != nil {
		return nil, fmt.Errorf("open posts pebble failed: %w", err)
	}

	// 2. Comments Pebble DB
	commentsPath := filepath.Join(cfg.DataDir, "comments.pebble")
	commentsOpts := zstdOptions(writeHeavyOptions(&pebble.Options{
		Cache:        pebble.NewCache(int64(cfg.CacheSizeMB/3) * 1024 * 1024),
		MemTableSize: 64 * 1024 * 1024,
		MaxOpenFiles: maxOpenFiles,
	}))
	commentsDB, err := pebble.Open(commentsPath, commentsOpts)
	if err != nil {
		postsDB.Close()
		return nil, fmt.Errorf("open comments pebble failed: %w", err)
	}

	// 3. Votes Pebble DB (Separate DB to enforce 1 vote per user without write amplification)
	votesPath := filepath.Join(cfg.DataDir, "votes.pebble")
	votesOpts := writeHeavyOptions(&pebble.Options{
		Cache:        pebble.NewCache(int64(cfg.CacheSizeMB/3) * 1024 * 1024),
		MemTableSize: 16 * 1024 * 1024,
		MaxOpenFiles: maxOpenFiles,
	})
	votesDB, err := pebble.Open(votesPath, votesOpts)
	if err != nil {
		postsDB.Close()
		commentsDB.Close()
		return nil, fmt.Errorf("open votes pebble failed: %w", err)
	}

	// 4. SQLite Metadata DB
	metaPath := filepath.Join(cfg.DataDir, "meta.sqlite3")
	_, statErr := os.Stat(metaPath)
	metaMissing := os.IsNotExist(statErr)
	metaDB, err := openMetaDB(metaPath)
	if err != nil {
		postsDB.Close()
		commentsDB.Close()
		votesDB.Close()
		return nil, err
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
		reg:         reg,
		ownsReg:     ownsReg,
		postSeqs:    make(map[uint64]*uint32),
		postLines:   make(map[uint64]int),
		dirtyDeltas: make(map[uint64]*postDelta),
		flushTicker: time.NewTicker(time.Duration(cfg.FlushSeconds) * time.Second),
		stopFlush:   make(chan struct{}),
	}

	// A data dir restored from a backup has no meta.sqlite3 (see backupTo).
	if metaMissing && e.hasPostRecords() {
		log.Printf("[nest.svc] %s: no meta.sqlite3; rebuilding it from Pebble...", cfg.DataDir)
		start := time.Now()
		if _, _, err := e.RebuildSQLiteFromPebble(); err != nil {
			opened = true // e.Close closes the registry.
			e.Close()
			return nil, fmt.Errorf("rebuild meta.sqlite3: %w", err)
		}
		log.Printf("[nest.svc] %s: meta.sqlite3 rebuilt in %s", cfg.DataDir, time.Since(start).Round(time.Second))
	}

	go e.flusherLoop()
	opened = true
	return e, nil
}

// hasPostRecords reports whether posts.pebble holds any post.
func (e *Engine) hasPostRecords() bool {
	it, err := e.postsDB.NewIter(&pebble.IterOptions{LowerBound: []byte{'m'}, UpperBound: []byte{'n'}})
	if err != nil {
		return false
	}
	defer it.Close()
	return it.First()
}

// openMetaDB opens (or creates) meta.sqlite3 at path.
func openMetaDB(path string) (*sql.DB, error) {
	db, err := sql.Open("sqlite", path+"?_pragma=journal_mode(WAL)&_pragma=busy_timeout(10000)&_pragma=synchronous(NORMAL)&_pragma=temp_store(MEMORY)&_pragma=cache_size(-64000)")
	if err != nil {
		return nil, fmt.Errorf("open sqlite meta failed: %w", err)
	}
	db.SetMaxOpenConns(4)
	db.SetMaxIdleConns(4)
	if err := initSQLiteSchema(db); err != nil {
		db.Close()
		return nil, fmt.Errorf("init sqlite schema failed: %w", err)
	}
	return db, nil
}

// sqlExecer is a *sql.DB or *sql.Tx.
type sqlExecer interface {
	Exec(query string, args ...any) (sql.Result, error)
}

func initSQLiteSchema(db sqlExecer) error {
	schema := `
	CREATE TABLE IF NOT EXISTS posts (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		parent_id INTEGER DEFAULT 0,
		community_id INTEGER NOT NULL,
		post_file TEXT NOT NULL,
		title TEXT NOT NULL,
		creator_name TEXT NOT NULL,
		creator_id INTEGER NOT NULL DEFAULT 0,
		created_at INTEGER NOT NULL,
		modified INTEGER NOT NULL DEFAULT 0,
		filemode INTEGER DEFAULT 0,
		upvotes INTEGER DEFAULT 0,
		downvotes INTEGER DEFAULT 0,
		num_comments INTEGER DEFAULT 0,
		num_crossposts INTEGER DEFAULT 0,
		deleted INTEGER DEFAULT 0,
		removed INTEGER DEFAULT 0,
		deleted_at INTEGER DEFAULT 0,
		mod_person_name TEXT DEFAULT '',
		mod_person_id INTEGER DEFAULT 0,
		reason TEXT DEFAULT '',
		money INTEGER DEFAULT 0,
		locked INTEGER DEFAULT 0
	);
	CREATE INDEX IF NOT EXISTS idx_posts_comm_id ON posts(community_id, id DESC);
	CREATE INDEX IF NOT EXISTS idx_posts_comm_del ON posts(community_id, deleted, removed, id DESC);
	CREATE UNIQUE INDEX IF NOT EXISTS idx_posts_comm_file ON posts(community_id, post_file);
	CREATE INDEX IF NOT EXISTS idx_posts_creator ON posts(creator_name);
	CREATE INDEX IF NOT EXISTS idx_posts_parent_id ON posts(parent_id);

	CREATE TABLE IF NOT EXISTS crossposts (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		source_post_id INTEGER NOT NULL,
		target_community_id INTEGER NOT NULL,
		target_post_file TEXT NOT NULL,
		creator_name TEXT NOT NULL,
		creator_id INTEGER NOT NULL DEFAULT 0,
		created_at INTEGER NOT NULL,
		bbs_exempt INTEGER NOT NULL DEFAULT 0
	);
	CREATE INDEX IF NOT EXISTS idx_crossposts_source ON crossposts(source_post_id);
	CREATE INDEX IF NOT EXISTS idx_crossposts_target ON crossposts(target_community_id, target_post_file);

	-- Where the copies of this engine were copied from (model.CrosspostOrigin).
	CREATE TABLE IF NOT EXISTS bbs_crosspost_origins (
		target_post_id INTEGER PRIMARY KEY,
		source_community TEXT NOT NULL,
		source_post_file TEXT NOT NULL,
		creator_name TEXT NOT NULL,
		creator_id INTEGER NOT NULL DEFAULT 0,
		created_at INTEGER NOT NULL
	);
	CREATE INDEX IF NOT EXISTS idx_bbs_crosspost_origins_source ON bbs_crosspost_origins(source_community COLLATE NOCASE, source_post_file);
	`
	if _, err := db.Exec(schema); err != nil {
		return err
	}
	return addColumn(db, "crossposts", "bbs_exempt", "INTEGER NOT NULL DEFAULT 0")
}

// addColumn adds a column to a table of an older database.
func addColumn(db sqlExecer, table, column, decl string) error {
	_, err := db.Exec("ALTER TABLE " + table + " ADD COLUMN " + column + " " + decl)
	if err != nil && strings.Contains(err.Error(), "duplicate column") {
		return nil
	}
	return err
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
		if e.ownsReg {
			if err := e.reg.Close(); err != nil && firstErr == nil {
				firstErr = err
			}
		}
	})
	return firstErr
}

// Communities returns the community registry used by the engine.
func (e *Engine) Communities() *CommunityRegistry {
	return e.reg
}

// fillCommunity sets the name of the community of p from the registry.
func (e *Engine) fillCommunity(p *model.Post) {
	p.Community = e.reg.Name(p.CommunityID)
}

// resolveCommunity sets p.CommunityID from p.Community (registering the
// community if needed) and p.Community to the registered name.
func (e *Engine) resolveCommunity(p *model.Post) error {
	if p.CommunityID == 0 && p.Community != "" {
		id, err := e.reg.Ensure(p.Community)
		if err != nil {
			return err
		}
		p.CommunityID = id
	}
	if p.CommunityID != 0 {
		e.fillCommunity(p)
	}
	return nil
}

// separateCache reports whether CacheDir is a directory of its own rather
// than the BBS boards.
func (e *Engine) separateCache() bool {
	return e.cfg.CacheDir != "" && e.cfg.CacheDir != "boards" && e.cfg.CacheDir != filepath.Join(e.cfg.BBSHome, "boards")
}

// communityCacheDir returns the cache directory of a community, which is
// named after its ID so renaming the community keeps the cache.
func (e *Engine) communityCacheDir(communityID int64) string {
	return filepath.Join(e.cfg.CacheDir, strconv.FormatInt(communityID, 10))
}

// CreatePost creates a new submission, storing UTF-8 content in Pebble and metadata in SQLite
func (e *Engine) CreatePost(p *model.Post) (*model.Post, error) {
	if p.CreatedAt == 0 {
		p.CreatedAt = time.Now().Unix()
	}
	if p.Modified == 0 {
		p.Modified = p.CreatedAt
	}
	p.Title = model.HeaderValue(p.Title)
	p.PostFile = model.CanonicalPostFile(p.PostFile)
	if err := e.resolveCommunity(p); err != nil {
		return nil, err
	}

	// Auto-demotion: if a reply (ParentID > 0) has too little net new content, convert it to a comment on parent!
	if p.ParentID > 0 && e.cfg.MinReplyRunes > 0 {
		netText := model.ExtractNetNewContent(p.Content)
		if len([]rune(netText)) < e.cfg.MinReplyRunes {
			comment := &model.Comment{
				PostID:      p.ParentID,
				CreatorName: p.CreatorName,
				Content:     netText,
				CreatedAt:   p.CreatedAt,
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

	// Check if post already exists in SQLite
	if p.ID == 0 && p.CommunityID != 0 && p.PostFile != "" {
		var existingID uint64
		err := e.metaDB.QueryRow(`SELECT id FROM posts WHERE community_id = ? AND post_file = ?`, p.CommunityID, p.PostFile).Scan(&existingID)
		if err == nil && existingID > 0 {
			p.ID = existingID
			_ = putPost(e.postsDB, p)
			return p, nil
		}
	}

	// 1. Insert into SQLite to get unique ID if not provided
	if p.ID == 0 {
		if e.cfg.NextPostID != nil {
			p.ID = e.cfg.NextPostID()
			if err := checkPostID(p.ID); err != nil {
				return nil, err
			}
			e.metaWriteMu.Lock()
			_, err := e.metaDB.Exec(`
				INSERT INTO posts (id, parent_id, community_id, post_file, title, creator_name, creator_id, created_at, modified, filemode, upvotes, downvotes, num_comments, num_crossposts, money)
				VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
			`, p.ID, p.ParentID, p.CommunityID, p.PostFile, p.Title, p.CreatorName, p.CreatorID, p.CreatedAt, p.Modified, p.Filemode, p.Upvotes, p.Downvotes, p.NumComments, p.NumCrossposts, p.Money)
			e.metaWriteMu.Unlock()
			if err != nil {
				return nil, fmt.Errorf("insert sqlite post with allocated id failed: %w", err)
			}
		} else {
			e.metaWriteMu.Lock()
			res, err := e.metaDB.Exec(`
				INSERT INTO posts (parent_id, community_id, post_file, title, creator_name, creator_id, created_at, modified, filemode, upvotes, downvotes, num_comments, num_crossposts, money)
				VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
			`, p.ParentID, p.CommunityID, p.PostFile, p.Title, p.CreatorName, p.CreatorID, p.CreatedAt, p.Modified, p.Filemode, p.Upvotes, p.Downvotes, p.NumComments, p.NumCrossposts, p.Money)
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
			if err := checkPostID(p.ID); err != nil {
				return nil, err
			}
		}
	} else {
		e.metaWriteMu.Lock()
		_, err := e.metaDB.Exec(`
			INSERT INTO posts (id, parent_id, community_id, post_file, title, creator_name, creator_id, created_at, modified, filemode, upvotes, downvotes, num_comments, num_crossposts, money)
			VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
		`, p.ID, p.ParentID, p.CommunityID, p.PostFile, p.Title, p.CreatorName, p.CreatorID, p.CreatedAt, p.Modified, p.Filemode, p.Upvotes, p.Downvotes, p.NumComments, p.NumCrossposts, p.Money)
		e.metaWriteMu.Unlock()
		if err != nil {
			return nil, fmt.Errorf("insert sqlite post with explicit id failed: %w", err)
		}
	}

	// 2. Write the post records to posts.pebble
	if err := putPost(e.postsDB, p); err != nil {
		return nil, fmt.Errorf("write pebble post failed: %w", err)
	}
	if p.Upvotes != 0 || p.Downvotes != 0 || p.NumComments != 0 {
		_ = putPostCounts(e.postsDB, p.ID, model.PostCounts{Upvotes: p.Upvotes, Downvotes: p.Downvotes, NumComments: p.NumComments, Modified: p.Modified})
	}

	// 3. Render and save canonical BBS file (and cache file)
	_ = e.RenderAndSave(p)
	_ = e.WritePostCache(p)

	return p, nil
}

// DiskUsage returns the bytes this engine takes on its disk: its Pebble
// databases (with their WAL) and its SQLite database (with its WAL). The
// rendered files in CacheDir are not counted, as they can be rebuilt.
func (e *Engine) DiskUsage() int64 {
	var n uint64
	for _, db := range []*pebble.DB{e.postsDB, e.commentsDB, e.votesDB, e.historyDB} {
		n += db.Metrics().DiskSpaceUsage()
	}
	sqlitePath := filepath.Join(e.cfg.DataDir, "meta.sqlite3")
	for _, path := range []string{sqlitePath, sqlitePath + "-wal"} {
		if fi, err := os.Stat(path); err == nil {
			n += uint64(fi.Size())
		}
	}
	return int64(n)
}

// MaxPostID returns the largest post ID this engine has ever used: the
// largest in SQLite or Pebble, or that SQLite AUTOINCREMENT has handed out
// (which also covers posts purged since).
func (e *Engine) MaxPostID() uint64 {
	var maxID, seq uint64
	_ = e.metaDB.QueryRow("SELECT COALESCE(MAX(id), 0) FROM posts").Scan(&maxID)
	_ = e.metaDB.QueryRow("SELECT COALESCE(MAX(seq), 0) FROM sqlite_sequence WHERE name = 'posts'").Scan(&seq)
	if seq > maxID {
		maxID = seq
	}
	if it, err := e.postsDB.NewIter(&pebble.IterOptions{LowerBound: []byte{'m'}, UpperBound: []byte{'n'}}); err == nil {
		for ok := it.Last(); ok; ok = it.Prev() {
			if id, err := model.DecodePostIDKey(it.Key()); err == nil {
				if id > maxID {
					maxID = id
				}
				break
			}
		}
		it.Close()
	}
	return maxID
}

// HasPost reports whether post postID is in this engine.
func (e *Engine) HasPost(postID uint64) bool {
	var one int
	return e.metaDB.QueryRow("SELECT 1 FROM posts WHERE id = ?", postID).Scan(&one) == nil
}

func (e *Engine) GetPost(postID uint64) (*model.Post, error) {
	var p model.Post
	err := e.metaDB.QueryRow(`
		SELECT id, parent_id, community_id, post_file, title, creator_name, creator_id, created_at, modified, filemode, upvotes, downvotes, num_comments, num_crossposts, deleted, removed, deleted_at, mod_person_name, mod_person_id, reason, money, locked
		FROM posts WHERE id = ?
	`, postID).Scan(&p.ID, &p.ParentID, &p.CommunityID, &p.PostFile, &p.Title, &p.CreatorName, &p.CreatorID, &p.CreatedAt, &p.Modified, &p.Filemode, &p.Upvotes, &p.Downvotes, &p.NumComments, &p.NumCrossposts, &p.Deleted, &p.Removed, &p.DeletedAt, &p.ModPersonName, &p.ModPersonID, &p.Reason, &p.Money, &p.Locked)
	if err != nil {
		return nil, err
	}
	if p.Modified == 0 {
		p.Modified = p.CreatedAt
	}
	e.fillCommunity(&p)

	// Read content from Pebble
	meta, body, err := e.readPostRecord(postID)
	if err != nil {
		if errors.Is(err, pebble.ErrNotFound) {
			boardPath := e.BoardFilePath(&p)
			if boardPath != "" {
				if rawBytes, errR := os.ReadFile(boardPath); errR == nil && len(rawBytes) > 0 {
					body := string(rawBytes)
					if e.cfg.IsBig5() && !utf8.Valid(rawBytes) {
						body = big5uao.DecodeSGR66(rawBytes)
					}
					p.Content = body
					_ = putPost(e.postsDB, &p)
					return &p, nil
				}
			}
		}
		return nil, fmt.Errorf("get pebble post %d: %w", postID, err)
	}
	p.Content = body
	p.BBSEditMark = meta["BBSEditMark"]
	if !p.DeletedOrRemoved() {
		p.Deletion = model.DeletionFromRFC822(meta)
	}
	return &p, nil
}

// LockPost locks or unlocks post postID, as Lemmy's LockPost: a locked
// post takes no new comments and, as PTT locks posts, its board file is
// renamed to X.<stamp> (which mbbsd does not show) while it is locked.
func (e *Engine) LockPost(postID uint64, locked bool) error {
	p, err := e.GetPost(postID)
	if err != nil {
		return err
	}
	if p.Locked == locked {
		return nil
	}
	from := e.BoardFilePath(p)
	p.Locked = locked
	e.metaWriteMu.Lock()
	_, err = e.metaDB.Exec(`UPDATE posts SET locked = ? WHERE id = ?`, boolToInt(locked), postID)
	e.metaWriteMu.Unlock()
	if err != nil {
		return fmt.Errorf("lock post %d failed: %w", postID, err)
	}
	if err := putPostMeta(e.postsDB, p); err != nil {
		return fmt.Errorf("lock post %d failed: %w", postID, err)
	}
	// mbbsd renames the file itself; do it here for other clients.
	if to := e.BoardFilePath(p); from != "" && to != "" && from != to {
		if _, err := os.Stat(to); os.IsNotExist(err) {
			_ = os.Rename(from, to)
		}
	}
	return nil
}

// LockPostByCommunityFile locks or unlocks the post postFile of community.
func (e *Engine) LockPostByCommunityFile(community, postFile string, locked bool) error {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return fmt.Errorf("post not found: %s/%s: %w", community, postFile, err)
	}
	return e.LockPost(p.ID, locked)
}

func (e *Engine) GetPostByCommunityFile(community, postFile string) (*model.Post, error) {
	cid, ok := e.reg.Lookup(community)
	if !ok {
		return nil, sql.ErrNoRows
	}
	var id uint64
	err := e.metaDB.QueryRow(`
		SELECT id FROM posts WHERE community_id = ? AND post_file = ?
	`, cid, model.CanonicalPostFile(postFile)).Scan(&id)
	if err != nil {
		return nil, err
	}
	return e.GetPost(id)
}

func (e *Engine) CountPosts(community string) (int, error) {
	cid, ok := e.reg.Lookup(community)
	if !ok {
		return 0, nil
	}
	var count int
	err := e.metaDB.QueryRow(`
		SELECT count(*) FROM posts WHERE community_id = ? AND deleted = 0 AND removed = 0
	`, cid).Scan(&count)
	return count, err
}

// ListPostMeta returns the metadata (without content) of every post,
// including deleted ones, in community, ordered by ID.
func (e *Engine) ListPostMeta(community string) ([]*model.Post, error) {
	cid, ok := e.reg.Lookup(community)
	if !ok {
		return nil, nil
	}
	// Counts of recent votes and comments may still be pending in memory.
	e.FlushDirtyDeltas()

	rows, err := e.metaDB.Query(`
		SELECT id, parent_id, community_id, post_file, title, creator_name, creator_id, created_at, modified, filemode, upvotes, downvotes, num_comments, num_crossposts, deleted, removed, deleted_at, mod_person_name, mod_person_id, reason, money, locked
		FROM posts WHERE community_id = ? ORDER BY id
	`, cid)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var posts []*model.Post
	for rows.Next() {
		var p model.Post
		if err := rows.Scan(&p.ID, &p.ParentID, &p.CommunityID, &p.PostFile, &p.Title, &p.CreatorName, &p.CreatorID, &p.CreatedAt, &p.Modified, &p.Filemode, &p.Upvotes, &p.Downvotes, &p.NumComments, &p.NumCrossposts, &p.Deleted, &p.Removed, &p.DeletedAt, &p.ModPersonName, &p.ModPersonID, &p.Reason, &p.Money, &p.Locked); err != nil {
			return nil, err
		}
		if p.Modified == 0 {
			p.Modified = p.CreatedAt
		}
		e.fillCommunity(&p)
		posts = append(posts, &p)
	}
	return posts, rows.Err()
}

// SetPostMeta updates the filemode and money of a post, as mbbsd changes
// them in .DIR.
func (e *Engine) SetPostMeta(community, postFile string, filemode, money int) error {
	cid, ok := e.reg.Lookup(community)
	if !ok {
		return fmt.Errorf("post not found: %s/%s", community, postFile)
	}
	var id uint64
	if err := e.metaDB.QueryRow(`SELECT id FROM posts WHERE community_id = ? AND post_file = ?`, cid, model.CanonicalPostFile(postFile)).Scan(&id); err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return fmt.Errorf("post not found: %s/%s", community, postFile)
		}
		return err
	}

	e.metaWriteMu.Lock()
	_, err := e.metaDB.Exec(`
		UPDATE posts SET filemode = ?, money = ? WHERE id = ?
	`, filemode, money, id)
	e.metaWriteMu.Unlock()
	if err != nil {
		return err
	}

	// Keep the copy in Pebble (used by RebuildSQLiteFromPebble) in sync.
	p, err := e.GetPost(id)
	if err != nil {
		return err
	}
	return putPostMeta(e.postsDB, p)
}

func boolToInt(b bool) int {
	if b {
		return 1
	}
	return 0
}

func (e *Engine) ListPosts(community string, limit, offset int) ([]*model.Post, error) {
	if limit <= 0 {
		limit = 20
	}
	if offset < 0 {
		offset = 0
	}
	cid, ok := e.reg.Lookup(community)
	if !ok {
		return nil, nil
	}
	rows, err := e.metaDB.Query(`
		SELECT id, parent_id, community_id, post_file, title, creator_name, creator_id, created_at, modified, filemode, upvotes, downvotes, num_comments, num_crossposts, deleted, removed, deleted_at, mod_person_name, mod_person_id, reason, money, locked
		FROM posts WHERE community_id = ? AND deleted = 0 AND removed = 0 ORDER BY id DESC LIMIT ? OFFSET ?
	`, cid, limit, offset)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var posts []*model.Post
	for rows.Next() {
		var p model.Post
		if err := rows.Scan(&p.ID, &p.ParentID, &p.CommunityID, &p.PostFile, &p.Title, &p.CreatorName, &p.CreatorID, &p.CreatedAt, &p.Modified, &p.Filemode, &p.Upvotes, &p.Downvotes, &p.NumComments, &p.NumCrossposts, &p.Deleted, &p.Removed, &p.DeletedAt, &p.ModPersonName, &p.ModPersonID, &p.Reason, &p.Money, &p.Locked); err != nil {
			return nil, err
		}
		if p.Modified == 0 {
			p.Modified = p.CreatedAt
		}
		e.fillCommunity(&p)
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
	cid, ok := e.reg.Lookup(community)
	if !ok {
		return nil, nil
	}
	rows, err := e.metaDB.Query(`
		SELECT id, parent_id, community_id, post_file, title, creator_name, creator_id, created_at, modified, filemode, upvotes, downvotes, num_comments, num_crossposts, deleted, removed, deleted_at, mod_person_name, mod_person_id, reason, money, locked
		FROM posts WHERE community_id = ? AND (deleted = 1 OR removed = 1) ORDER BY id DESC LIMIT ? OFFSET ?
	`, cid, limit, offset)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var posts []*model.Post
	for rows.Next() {
		var p model.Post
		if err := rows.Scan(&p.ID, &p.ParentID, &p.CommunityID, &p.PostFile, &p.Title, &p.CreatorName, &p.CreatorID, &p.CreatedAt, &p.Modified, &p.Filemode, &p.Upvotes, &p.Downvotes, &p.NumComments, &p.NumCrossposts, &p.Deleted, &p.Removed, &p.DeletedAt, &p.ModPersonName, &p.ModPersonID, &p.Reason, &p.Money, &p.Locked); err != nil {
			return nil, err
		}
		if p.Modified == 0 {
			p.Modified = p.CreatedAt
		}
		e.fillCommunity(&p)
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
		SELECT id, parent_id, community_id, post_file, title, creator_name, creator_id, created_at, modified, filemode, upvotes, downvotes, num_comments, num_crossposts, deleted, removed, deleted_at, mod_person_name, mod_person_id, reason, money, locked
		FROM posts WHERE parent_id = ? ORDER BY id ASC LIMIT ? OFFSET ?
	`, parentID, limit, offset)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var replies []*model.Post
	for rows.Next() {
		var p model.Post
		if err := rows.Scan(&p.ID, &p.ParentID, &p.CommunityID, &p.PostFile, &p.Title, &p.CreatorName, &p.CreatorID, &p.CreatedAt, &p.Modified, &p.Filemode, &p.Upvotes, &p.Downvotes, &p.NumComments, &p.NumCrossposts, &p.Deleted, &p.Removed, &p.DeletedAt, &p.ModPersonName, &p.ModPersonID, &p.Reason, &p.Money, &p.Locked); err != nil {
			return nil, err
		}
		if p.Modified == 0 {
			p.Modified = p.CreatedAt
		}
		e.fillCommunity(&p)
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

// AddCrosspost records that a post was cross-posted to a target community.
// The record is kept with the original: a crosspost of a crosspost is one
// of the original (see resolveCrosspostRoot).
func (e *Engine) AddCrosspost(srcCommunity, srcPostFile, targetCommunity, targetPostFile, creatorName string, creatorID model.PersonID, createdAt int64, exempt bool) (*model.CrosspostRecord, int, error) {
	return addCrosspost(e, srcCommunity, srcPostFile, targetCommunity, targetPostFile, creatorName, creatorID, createdAt, exempt)
}

// addCrosspostAt records a crosspost of post srcID of this engine and
// returns it with the crosspost count of the post.
func (e *Engine) addCrosspostAt(srcID uint64, targetCommunity, targetPostFile, creatorName string, creatorID model.PersonID, createdAt int64, exempt bool) (*model.CrosspostRecord, int, error) {
	if createdAt == 0 {
		createdAt = time.Now().Unix()
	}
	targetID, err := e.reg.Ensure(targetCommunity)
	if err != nil {
		return nil, 0, err
	}
	targetPostFile = model.CanonicalPostFile(targetPostFile)

	// 1. Insert into SQLite crossposts table
	e.metaWriteMu.Lock()
	defer e.metaWriteMu.Unlock()
	existing, err := e.crosspostEntries(srcID)
	if err != nil {
		return nil, 0, err
	}
	res, err := e.metaDB.Exec(`
		INSERT INTO crossposts (source_post_id, target_community_id, target_post_file, creator_name, creator_id, created_at, bbs_exempt)
		VALUES (?, ?, ?, ?, ?, ?, ?)
	`, srcID, targetID, targetPostFile, creatorName, creatorID, createdAt, boolToInt(exempt))
	if err != nil {
		return nil, 0, fmt.Errorf("insert crosspost failed: %w", err)
	}
	id, err := res.LastInsertId()
	if err != nil {
		return nil, 0, err
	}

	// 2. Increment num_crossposts in posts table
	if _, err := e.metaDB.Exec(`UPDATE posts SET num_crossposts = num_crossposts + 1 WHERE id = ?`, srcID); err != nil {
		return nil, 0, fmt.Errorf("update num_crossposts failed: %w", err)
	}

	var newCount int
	_ = e.metaDB.QueryRow(`SELECT num_crossposts FROM posts WHERE id = ?`, srcID).Scan(&newCount)

	rec := &model.CrosspostRecord{
		ID:                uint64(id),
		SourcePostID:      srcID,
		TargetCommunityID: targetID,
		TargetCommunity:   e.reg.Name(targetID),
		TargetPostFile:    targetPostFile,
		CreatorName:       creatorName,
		CreatorID:         creatorID,
		CreatedAt:         createdAt,
		BBSExempt:         exempt,
	}

	// 3. Write to Pebble (Source of truth), after the last record: the
	// count is not a free sequence number once records are merged.
	cKey := model.EncodeCrosspostKey(srcID, lastCrosspostSeq(existing)+1)
	cVal := model.EncodeRFC822Crosspost(rec)
	if err := e.postsDB.Set(cKey, cVal, pebble.Sync); err != nil {
		return nil, 0, fmt.Errorf("set pebble crosspost failed: %w", err)
	}

	return rec, newCount, nil
}

// GetCrossposts returns all destinations where this post has been crossposted
func (e *Engine) GetCrossposts(community, postFile string) ([]*model.CrosspostRecord, error) {
	post, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return nil, err
	}

	rows, err := e.metaDB.Query(`
		SELECT id, source_post_id, target_community_id, target_post_file, creator_name, creator_id, created_at, bbs_exempt
		FROM crossposts WHERE source_post_id = ? ORDER BY id ASC
	`, post.ID)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var records []*model.CrosspostRecord
	for rows.Next() {
		var r model.CrosspostRecord
		if err := rows.Scan(&r.ID, &r.SourcePostID, &r.TargetCommunityID, &r.TargetPostFile, &r.CreatorName, &r.CreatorID, &r.CreatedAt, &r.BBSExempt); err != nil {
			return nil, err
		}
		r.TargetCommunity = e.reg.Name(r.TargetCommunityID)
		records = append(records, &r)
	}
	return records, nil
}

// GetOriginPost finds the source post if target was crossposted from another board
func (e *Engine) GetOriginPost(targetCommunity, targetPostFile string) (*model.Post, *model.CrosspostRecord, error) {
	targetID, ok := e.reg.Lookup(targetCommunity)
	if !ok {
		return nil, nil, nil
	}
	var r model.CrosspostRecord
	err := e.metaDB.QueryRow(`
		SELECT id, source_post_id, target_community_id, target_post_file, creator_name, creator_id, created_at, bbs_exempt
		FROM crossposts WHERE target_community_id = ? AND target_post_file = ?
	`, targetID, model.CanonicalPostFile(targetPostFile)).Scan(&r.ID, &r.SourcePostID, &r.TargetCommunityID, &r.TargetPostFile, &r.CreatorName, &r.CreatorID, &r.CreatedAt, &r.BBSExempt)
	if err != nil {
		if err == sql.ErrNoRows {
			return nil, nil, nil
		}
		return nil, nil, err
	}
	r.TargetCommunity = e.reg.Name(r.TargetCommunityID)

	srcPost, err := e.GetPost(r.SourcePostID)
	if err != nil {
		return nil, nil, err
	}
	return srcPost, &r, nil
}

// -----------------------------------------------------------------------------
// Comments (Decoupled from Votes, RFC 822 formatted)
// -----------------------------------------------------------------------------

// AddComment appends a comment, storing it in Pebble (RFC 822) and updating dirty counts.
// Consecutive comments by the same author without intervening comments within 3 minutes (< 180s)
// are automatically merged into the same comment floor.
func (e *Engine) AddComment(c *model.Comment) (added *model.Comment, err error) {
	if c.CreatedAt == 0 {
		c.CreatedAt = time.Now().Unix()
	}
	if e.cfg.MaxPostCommentLines > 0 {
		lines := model.CountCommentLines(model.TruncateCommentLines(c.Content, model.MaxCommentLines))
		if err := e.reserveCommentLines(c.PostID, lines); err != nil {
			return nil, err
		}
		defer func() {
			if err != nil {
				e.forgetCommentLines(c.PostID) // counted again when needed
			}
		}()
	}

	// 1. Check if previous comment is from the same author within 3 minutes (< 180s)
	e.seqMu.Lock()
	ptr, ok := e.postSeqs[c.PostID]
	var lastSeq uint32
	if ok {
		lastSeq = *ptr
	} else {
		lastSeq = e.lookupMaxSequence(c.PostID)
	}

	var prev *model.Comment
	if lastSeq > 0 {
		cKey := model.EncodeCommentKey(c.PostID, lastSeq)
		if val, closer, err := e.commentsDB.Get(cKey); err == nil {
			prev, _ = model.DecodeRFC822Comment(c.PostID, lastSeq, val)
			closer.Close()
		}
	}

	c.Content = model.TruncateCommentLines(c.Content, model.MaxCommentLines)

	var lastTime int64
	if prev != nil {
		lastTime = prev.LastCommentAt
		if lastTime == 0 {
			lastTime = prev.CreatedAt
		}
	}
	diff := c.CreatedAt - lastTime
	canMerge := prev != nil && !prev.DeletedOrRemoved() &&
		strings.EqualFold(prev.CreatorName, c.CreatorName) &&
		(c.CreatorID == 0 || prev.CreatorID == 0 || c.CreatorID == prev.CreatorID) &&
		diff >= 0 && diff <= 180

	if canMerge {
		prevLines := model.CountCommentLines(prev.Content)
		curLines := model.CountCommentLines(c.Content)
		if prevLines+curLines > model.MaxCommentLines {
			canMerge = false
		}
	}

	if canMerge {
		if !ok {
			e.postSeqs[c.PostID] = &lastSeq
		}
		e.seqMu.Unlock()

		prev.Content = strings.TrimRight(prev.Content, "\r\n") + "\n" + c.Content
		prev.LastCommentAt = c.CreatedAt
		// The merged comment keeps the ID of the floor.
		if err := e.commentsDB.Set(model.EncodeCommentKey(prev.PostID, prev.Sequence), model.EncodeRFC822Comment(prev), pebble.NoSync); err != nil {
			return nil, fmt.Errorf("update pebble comment failed: %w", err)
		}

		_ = e.AppendMergedCommentCache(prev, c.Content)

		prev.IsMerged = true
		return prev, nil
	}

	if lastSeq >= model.MaxFloor {
		e.seqMu.Unlock()
		return nil, ErrTooManyComments
	}
	var seq uint32
	if ok {
		seq = atomic.AddUint32(ptr, 1)
	} else {
		seq = lastSeq + 1
		e.postSeqs[c.PostID] = &seq
	}
	e.seqMu.Unlock()

	c.Sequence = seq
	c.ID = model.MakeCommentID(c.PostID, seq)

	// 2. Write the comment (RFC 822) and its author index u:{author}:{created_at}:{post_id}:{sequence} together.
	batch := e.commentsDB.NewBatch()
	_ = batch.Set(model.EncodeCommentKey(c.PostID, c.Sequence), model.EncodeRFC822Comment(c), nil)
	_ = batch.Set(model.EncodeAuthorCommentKey(c.CreatorName, c.CreatedAt, c.PostID, c.Sequence), []byte{}, nil)
	err = batch.Commit(pebble.NoSync)
	batch.Close()
	if err != nil {
		return nil, fmt.Errorf("write pebble comment failed: %w", err)
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

// reserveCommentLines adds lines to the comment line count of postID, or
// returns ErrPostTooLarge past MaxPostCommentLines. The count is made from
// the comments on first use; forgetCommentLines drops it after other changes.
func (e *Engine) reserveCommentLines(postID uint64, lines int) error {
	e.linesMu.Lock()
	defer e.linesMu.Unlock()
	total, ok := e.postLines[postID]
	if !ok {
		total = e.countCommentLines(postID)
	}
	if total+lines > e.cfg.MaxPostCommentLines {
		e.postLines[postID] = total
		return ErrPostTooLarge
	}
	e.postLines[postID] = total + lines
	return nil
}

// countCommentLines returns how many lines the comments of postID have.
func (e *Engine) countCommentLines(postID uint64) int {
	prefix := model.EncodeCommentPrefix(postID)
	iter, err := e.commentsDB.NewIter(&pebble.IterOptions{
		LowerBound: prefix,
		UpperBound: append(prefix, 0xff),
	})
	if err != nil {
		return 0
	}
	defer iter.Close()
	total := 0
	for ok := iter.First(); ok; ok = iter.Next() {
		_, seq, err := model.DecodeCommentKey(iter.Key())
		if err != nil {
			continue
		}
		c, err := model.DecodeRFC822Comment(postID, seq, iter.Value())
		if err != nil {
			continue
		}
		if c.DeletedOrRemoved() {
			total++
		} else {
			total += model.CountCommentLines(c.Content)
		}
	}
	return total
}

// forgetCommentLines drops the count of lines of the comments of the
// posts given, or of every post with none.
func (e *Engine) forgetCommentLines(postIDs ...uint64) {
	e.linesMu.Lock()
	defer e.linesMu.Unlock()
	if len(postIDs) == 0 {
		clear(e.postLines)
		return
	}
	for _, id := range postIDs {
		delete(e.postLines, id)
	}
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
	return e.GetCommentsFiltered(postID, startFloor, limit, "", 0)
}

func (e *Engine) GetCommentsFiltered(postID uint64, startFloor, limit uint32, creatorName string, creatorID model.PersonID) ([]*model.Comment, error) {
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
				if creatorName != "" && !strings.EqualFold(c.CreatorName, creatorName) {
					continue
				}
				if creatorID != 0 && c.CreatorID != 0 && c.CreatorID != creatorID {
					continue
				}
				comments = append(comments, c)
			}
		}
	}
	return comments, nil
}

func (e *Engine) GetCommentCount(postID uint64) (int, error) {
	var count int
	if err := e.metaDB.QueryRow("SELECT num_comments FROM posts WHERE id = ?", postID).Scan(&count); err != nil {
		return 0, err
	}
	// Include comments added since the last background flush to SQLite.
	e.dirtyMu.Lock()
	if d, ok := e.dirtyDeltas[postID]; ok {
		count += int(d.CommentsDelta)
	}
	e.dirtyMu.Unlock()
	return count, nil
}

func (e *Engine) GetCommentCountByCommunityFile(community, postFile string) (int, error) {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return 0, err
	}
	if p == nil {
		return 0, fmt.Errorf("post not found: %s/%s", community, postFile)
	}
	return e.GetCommentCount(p.ID)
}

func (e *Engine) GetCommentsByCommunityFile(community, postFile string, startFloor, limit uint32) ([]*model.Comment, error) {
	return e.GetCommentsByCommunityFileFiltered(community, postFile, startFloor, limit, "", 0)
}

func (e *Engine) GetCommentsByCommunityFileFiltered(community, postFile string, startFloor, limit uint32, creatorName string, creatorID model.PersonID) ([]*model.Comment, error) {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return nil, err
	}
	if p == nil {
		return nil, fmt.Errorf("post not found: %s/%s", community, postFile)
	}
	return e.GetCommentsFiltered(p.ID, startFloor, limit, creatorName, creatorID)
}

// PurgeUserComments deletes all comments from an author created within maxAge (e.g. 7 days for anti-spam)
func (e *Engine) PurgeUserComments(author string, maxAge time.Duration) (purged int, err error) {
	defer e.forgetCommentLines()
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
				c.Removed = true
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
		SELECT id, community_id, post_file FROM posts WHERE creator_name = ? AND created_at >= ?
	`, author, minTimestamp)
	if err != nil {
		return 0, err
	}
	defer rows.Close()

	type targetPost struct {
		ID          uint64
		CommunityID int64
		PostFile    string
	}
	var targets []targetPost
	for rows.Next() {
		var t targetPost
		if err := rows.Scan(&t.ID, &t.CommunityID, &t.PostFile); err == nil {
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
		if _, err := delStmt.Exec(t.ID); err != nil {
			return 0, err
		}

		// Mark as Deleted in Pebble posts.db (so Rebuild won't restore it)
		key := model.EncodePostMetaKey(t.ID)
		val, closer, err := e.postsDB.Get(key)
		if err == nil {
			meta := model.DecodeRFC822PostMeta(val)
			closer.Close()
			meta["Deleted"] = "true"
			var b strings.Builder
			for k, v := range meta {
				if k == "Encoding" { // obsolete header of old records
					continue
				}
				b.WriteString(fmt.Sprintf("%s: %s\n", k, v))
			}
			batch.Set(key, []byte(b.String()), nil)
		}

		// Remove local cache file
		if e.separateCache() {
			os.Remove(filepath.Join(e.communityCacheDir(t.CommunityID), t.PostFile))
		}
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

// ClearCommunity completely removes all posts, comments, votes, history,
// and cache of the active community, which keeps its ID and name.
func (e *Engine) ClearCommunity(community string) (postsDeleted int, commentsDeleted int, err error) {
	if community == "" {
		return 0, 0, fmt.Errorf("community name required")
	}
	cid, ok := e.reg.Lookup(community)
	if !ok {
		return 0, 0, nil
	}
	return e.clearCommunity(cid)
}

// PurgeCommunity completely removes all posts, comments, votes, history, and
// cache of the community id (which may be taken down), and then removes it
// from the registry.
func (e *Engine) PurgeCommunity(id int64) (postsDeleted int, commentsDeleted int, err error) {
	if _, ok := e.reg.Get(id); !ok {
		return 0, 0, fmt.Errorf("%w: #%d", ErrCommunityNotFound, id)
	}
	postsDeleted, commentsDeleted, err = e.clearCommunity(id)
	if err != nil {
		return postsDeleted, commentsDeleted, err
	}
	return postsDeleted, commentsDeleted, e.reg.Purge(id)
}

// RenameCommunity changes the name of a community and returns its ID.
// Nothing on disk is keyed by the name (the cache is named after the ID, and
// mbbsd moves the BBS board itself), so only the registry changes.
func (e *Engine) RenameCommunity(oldName, newName string) (int64, error) {
	return e.reg.Rename(oldName, newName)
}

// RemoveCommunity takes a community down as the site does (Lemmy's
// RemoveCommunity) and returns its ID. Its posts are kept (and can be purged
// with PurgeCommunity), and the name can be used by a new community.
func (e *Engine) RemoveCommunity(name string, mod model.PersonID, reason string) (int64, error) {
	return e.reg.RemoveCommunity(name, mod, reason)
}

func (e *Engine) clearCommunity(community int64) (postsDeleted int, commentsDeleted int, err error) {
	defer e.forgetCommentLines()
	// 1. Flush any pending deltas first so SQLite is up to date
	e.FlushDirtyDeltas()

	e.metaWriteMu.Lock()
	defer e.metaWriteMu.Unlock()

	// 2. Query all post IDs in this community
	rows, err := e.metaDB.Query("SELECT id, post_file FROM posts WHERE community_id = ?", community)
	if err != nil {
		return 0, 0, fmt.Errorf("query posts for community #%d failed: %w", community, err)
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

	if _, err := tx.Exec("DELETE FROM crossposts WHERE target_community_id = ? OR source_post_id IN (SELECT id FROM posts WHERE community_id = ?)", community, community); err != nil {
		return 0, 0, fmt.Errorf("delete crossposts failed: %w", err)
	}
	if _, err := tx.Exec("DELETE FROM bbs_crosspost_origins WHERE target_post_id IN (SELECT id FROM posts WHERE community_id = ?)", community); err != nil {
		return 0, 0, fmt.Errorf("delete crosspost origins failed: %w", err)
	}
	if _, err := tx.Exec("DELETE FROM posts WHERE community_id = ?", community); err != nil {
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
		_ = deletePostRecords(postBatch, p.id)
		_ = postBatch.Delete(model.EncodeCrosspostOriginKey(p.id), nil)
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
						authKey := model.EncodeAuthorCommentKey(c.CreatorName, c.CreatedAt, p.id, seq)
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
	if e.separateCache() {
		_ = os.RemoveAll(e.communityCacheDir(community))
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
	return e.removePostRecord(p, false, false)
}

// removePostRecord removes post p and everything keyed by its ID from this
// engine. Moving the post to another engine keeps the records of the
// crossposts to it (keepIncoming; they belong to their source posts, which
// stay here) and its cache file if the other engine uses the same one
// (keepCache).
func (e *Engine) removePostRecord(p *model.Post, keepIncoming, keepCache bool) error {
	defer e.forgetCommentLines(p.ID)
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

	if keepIncoming {
		if _, err := tx.Exec("DELETE FROM crossposts WHERE source_post_id = ?", p.ID); err != nil {
			return fmt.Errorf("delete crossposts failed: %w", err)
		}
	} else if _, err := tx.Exec("DELETE FROM crossposts WHERE source_post_id = ? OR (target_community_id = ? AND target_post_file = ?)", p.ID, p.CommunityID, p.PostFile); err != nil {
		return fmt.Errorf("delete crossposts failed: %w", err)
	}
	if _, err := tx.Exec("DELETE FROM posts WHERE id = ?", p.ID); err != nil {
		return fmt.Errorf("delete post failed: %w", err)
	}
	if _, err := tx.Exec("DELETE FROM bbs_crosspost_origins WHERE target_post_id = ?", p.ID); err != nil {
		return fmt.Errorf("delete crosspost origin failed: %w", err)
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
	_ = deletePostRecords(postBatch, p.ID)
	_ = postBatch.Delete(model.EncodeCrosspostOriginKey(p.ID), nil)

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
					authKey := model.EncodeAuthorCommentKey(c.CreatorName, c.CreatedAt, p.ID, seq)
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
	if !keepCache {
		_ = os.Remove(e.CacheFilePath(p))
	}

	return nil
}

// -----------------------------------------------------------------------------
// Votes (Independent DB: One Vote per User per Post)
// -----------------------------------------------------------------------------

// VotePost records a user's vote (1: Upvote, -1: Downvote, 0: Cancel vote).
// A person (personID) has one vote per post, whatever name it is shown
// with; only votes of unknown persons (personID 0) go by personName. A
// known person takes over the vote recorded under its name for an unknown
// person (legacy votes imported without resolving their voters).
func (e *Engine) VotePost(postID uint64, personName string, personID model.PersonID, newVote model.VoteType, createdAt int64, ip string) (upDelta, downDelta int, err error) {
	e.voteMu.Lock()
	defer e.voteMu.Unlock()
	key := model.EncodeVoteKey(postID, personID, personName)
	val, closer, err := e.votesDB.Get(key)
	var legacyKey []byte
	if err != nil && personID != 0 && personName != "" {
		legacyKey = model.EncodeVoteKey(postID, 0, personName)
		val, closer, err = e.votesDB.Get(legacyKey)
		if err != nil {
			legacyKey = nil
		}
	}

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
	if legacyKey != nil {
		_ = e.votesDB.Delete(legacyKey, pebble.NoSync)
	}

	// Calculate deltas
	if prevVote == model.VoteUp {
		upDelta -= 1
	} else if prevVote == model.VoteDown {
		downDelta -= 1
	}

	if newVote == model.VoteUp || newVote == model.VoteDown {
		if createdAt == 0 {
			createdAt = time.Now().Unix()
		}
		if newVote == model.VoteUp {
			upDelta += 1
		} else {
			downDelta += 1
		}
		valBuf := model.EncodeVoteValue(newVote, personID, personName, createdAt, ip)
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

// GetVotes returns all active votes for a post
func (e *Engine) GetVotes(postID uint64) ([]*model.VoteRecord, error) {
	vPrefix := model.EncodeVotePrefix(postID)
	iter, err := e.votesDB.NewIter(&pebble.IterOptions{
		LowerBound: vPrefix,
		UpperBound: append(vPrefix, 0xff),
	})
	if err != nil {
		return nil, err
	}
	defer iter.Close()

	var votes []*model.VoteRecord
	for iter.First(); iter.Valid() && bytes.HasPrefix(iter.Key(), vPrefix); iter.Next() {
		pID, _, _, err := model.DecodeVoteKey(iter.Key())
		if err != nil || pID != postID {
			continue
		}
		vType, personID, user, ctime, ip, err := model.DecodeVoteValue(iter.Value())
		if err != nil || (vType != model.VoteUp && vType != model.VoteDown) {
			continue
		}
		votes = append(votes, &model.VoteRecord{
			PostID:     pID,
			PersonName: user,
			PersonID:   personID,
			Vote:       vType,
			CreatedAt:  ctime,
			IP:         ip,
		})
	}
	return votes, nil
}

// GetVotesByCommunityFile returns all active votes for a post identified by community and postFile
func (e *Engine) GetVotesByCommunityFile(community, postFile string) ([]*model.VoteRecord, error) {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return nil, err
	}
	if p == nil {
		return nil, fmt.Errorf("post not found: %s@%s", postFile, community)
	}
	return e.GetVotes(p.ID)
}

// ResetVotes clears all user votes and resets upvotes and downvotes to 0
func (e *Engine) ResetVotes(postID uint64) error {
	e.voteMu.Lock()
	defer e.voteMu.Unlock()
	vPrefix := model.EncodeVotePrefix(postID)
	iter, err := e.votesDB.NewIter(&pebble.IterOptions{
		LowerBound: vPrefix,
		UpperBound: append(vPrefix, 0xff),
	})
	if err == nil {
		batch := e.votesDB.NewBatch()
		for iter.First(); iter.Valid() && bytes.HasPrefix(iter.Key(), vPrefix); iter.Next() {
			_ = batch.Delete(iter.Key(), pebble.NoSync)
		}
		iter.Close()
		_ = batch.Commit(pebble.NoSync)
	}

	e.dirtyMu.Lock()
	if d, ok := e.dirtyDeltas[postID]; ok {
		d.UpvotesDelta = 0
		d.DownvotesDelta = 0
	}
	e.dirtyMu.Unlock()

	e.metaWriteMu.Lock()
	defer e.metaWriteMu.Unlock()
	if _, err = e.metaDB.Exec("UPDATE posts SET upvotes = 0, downvotes = 0 WHERE id = ?", postID); err != nil {
		return err
	}
	return e.syncCounts(postID)
}

func (e *Engine) ResetVotesByCommunityFile(community, postFile string) error {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return err
	}
	return e.ResetVotes(p.ID)
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

	ids := make([]uint64, 0, len(toFlush))
	for postID, delta := range toFlush {
		if _, err := stmt.Exec(delta.UpvotesDelta, delta.DownvotesDelta, delta.CommentsDelta, postID); err != nil {
			log.Printf("[nest.svc] Warning: failed to flush score delta for post %d: %v", postID, err)
		}
		ids = append(ids, postID)
	}
	if err := tx.Commit(); err != nil {
		log.Printf("[nest.svc] Warning: failed to commit flush transaction: %v", err)
		return
	}
	if err := e.syncCounts(ids...); err != nil {
		log.Printf("[nest.svc] Warning: %v", err)
	}
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

// BoardFilePath returns the path of the post in the BBS board, which is
// named after the current name of the community. It returns "" for posts of
// a soft-deleted community, whose name may now belong to another board.
func (e *Engine) BoardFilePath(p *model.Post) string {
	if p.PostFile == "" || p.PostFile == "-" {
		return ""
	}
	if p.CommunityID != 0 && !e.reg.Active(p.CommunityID) {
		return ""
	}
	dir := BoardDir(e.cfg.BBSHome, p.Community)
	if p.Locked {
		return filepath.Join(dir, model.LockedFileName(p.PostFile))
	}
	return filepath.Join(dir, p.PostFile)
}

// CacheFilePath returns the path of the cache file of the post, which is
// CacheDir/<community_id>/<post_file> (or the BBS board file if there is no
// separate cache).
func (e *Engine) CacheFilePath(p *model.Post) string {
	if e.separateCache() {
		return filepath.Join(e.communityCacheDir(p.CommunityID), p.PostFile)
	}
	return e.BoardFilePath(p)
}

func (e *Engine) WritePostCache(p *model.Post) error {
	path := e.CacheFilePath(p)
	if path == "" {
		return nil
	}
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

// IsSubmitter is Reddit's is_submitter: whether c is by the creator of p as
// readers see it. Never for anonymous posts, whose creator_id is the real
// account behind the nickname.
func IsSubmitter(p *model.Post, c *model.Comment) bool {
	if p == nil || c == nil || p.Filemode&model.FileAnonymous != 0 {
		return false
	}
	return model.IsCreator(p.CreatorName, p.CreatorID, c.CreatorName, c.CreatorID)
}

// Marks of the comment lines FormatCommentBlock writes.
const (
	commentLineMark    = "\x1e" // opens every line: ASCII RS, which editors drop
	commentOPMark      = "*"    // by the OP
	commentDeletedMark = "-"    // deleted by its creator (Lemmy's deleted)
	commentRemovedMark = "!"    // removed by a moderator (Lemmy's removed)
)

// commentMoreLine formats a further line of the comment by name, marked if
// by the OP, so each line can be drawn alone and aligned with the first.
func commentMoreLine(text, name string, op bool) string {
	mark := ""
	if op {
		mark = commentOPMark
	}
	return commentLineMark + mark + strings.Repeat(" ", visualWidth(name)) + "|" + text + "\n"
}

// commentAuthor returns the author field of a comment line: never empty, no
// "|" or leading mark. Non-userids (e.g. "[系統安全局]") come from imports.
func commentAuthor(name string) string {
	name = strings.TrimSpace(strings.Map(func(r rune) rune {
		// Nor a character whose Big5 trail byte is '|', for mbbsd.
		if r == '|' || (r >= 0x80 && bytes.IndexByte(big5uao.Encode(string(r)), '|') >= 0) {
			return '?'
		}
		return r
	}, name))
	if name == "" {
		return "?"
	}
	if strings.ContainsRune(commentOPMark+commentDeletedMark+commentRemovedMark, rune(name[0])) {
		name = "_" + name[1:]
	}
	return name
}

// FormatCommentBlock formats comment c of post p as PTT's own text form
// (Lemmy and Reddit write no comment files); each line can be drawn alone:
//
//	Line 1:  RS[*][-|!]userid|[~]No|MM/DD/YYYY HH:MM|[ip]|comment
//	Line 2+: RS[*]<spaces>|comment
//
// RS (0x1e) cannot be typed, so no article line is taken for a comment.
// "*" is the OP (IsSubmitter; Reddit "[S]", Lemmy "OP"), "-" deleted, "!"
// removed, "~" edited. The fields before the comment never have "|", so
// readers parse them forward and print the comment as it comes. Further
// lines have as many spaces as the userid is wide, to align with the first.
func FormatCommentBlock(p *model.Post, c *model.Comment) string {
	t := time.Unix(c.CreatedAt, 0).Format("01/02/2006 15:04")
	ip := c.IP
	if ip == "-" {
		ip = ""
	}

	op := IsSubmitter(p, c)
	marks := ""
	if op {
		marks = commentOPMark
	}
	author := commentAuthor(c.CreatorName)
	modMark := ""
	if c.ModifiedAt > 0 {
		modMark = "~"
	}
	head := func(marks, text string) string {
		return fmt.Sprintf("%s%s%s|%s%d|%s|%s|%s\n", commentLineMark, marks,
			author, modMark, c.Sequence, t, ip, text)
	}

	if c.DeletedOrRemoved() {
		cleanReason := c.Reason
		if !utf8.ValidString(cleanReason) {
			cleanReason = big5uao.Decode([]byte(cleanReason))
		}
		cleanReason = strings.TrimSpace(cleanReason)
		// Lemmy's words: deleted by its creator, removed by a moderator.
		if c.Deleted || cleanReason == "自刪" {
			return head(marks+commentDeletedMark, "留言已刪除")
		}
		msg := "留言已移除"
		if cleanReason != "" {
			cleanReason = strings.TrimRight(cleanReason, "\r\n")
			cleanReason = strings.ReplaceAll(cleanReason, "--", "-")
			msg = fmt.Sprintf("留言已移除 (%s)", cleanReason)
		}
		return head(marks+commentRemovedMark, msg)
	}

	rawLines := strings.Split(c.Content, "\n")
	var cleanLines []string
	for _, l := range rawLines {
		trimmed := strings.TrimRight(l, "\r ")
		if trimmed != "" {
			clean := model.StripANSI(trimmed)
			clean = strings.ReplaceAll(clean, "\t", "  ")
			if clean != "" {
				cleanLines = append(cleanLines, clean)
			}
		}
	}

	firstLine := ""
	if len(cleanLines) > 0 {
		firstLine = cleanLines[0]
	}

	var b strings.Builder
	b.WriteString(head(marks, firstLine))
	for i := 1; i < len(cleanLines); i++ {
		b.WriteString(commentMoreLine(cleanLines[i], author, op))
	}

	return b.String()
}

func (e *Engine) AppendCommentCache(c *model.Comment) error {
	p, err := e.GetPost(c.PostID)
	if err != nil {
		return err
	}
	boardPath := e.BoardFilePath(p)
	if boardPath != "" {
		if _, statErr := os.Stat(boardPath); os.IsNotExist(statErr) {
			return e.RenderAndSave(p)
		}
	}

	path := e.CacheFilePath(p)
	f, err := os.OpenFile(path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
		return err
	}
	defer f.Close()

	formatted := FormatCommentBlock(p, c)
	var data []byte
	if e.cfg.IsBig5() {
		data = big5uao.Encode(formatted)
	} else {
		data = []byte(formatted)
	}

	_, err = f.Write(data)
	if boardPath != "" && boardPath != path {
		if fb, errB := os.OpenFile(boardPath, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644); errB == nil {
			_, _ = fb.Write(data)
			_ = fb.Close()
		}
	}
	return err
}

// FormatMergedCommentLines formats content merged into floor of post p as
// its further lines (see FormatCommentBlock).
func FormatMergedCommentLines(p *model.Post, floor *model.Comment, content string) string {
	op := IsSubmitter(p, floor)
	rawLines := strings.Split(content, "\n")
	var b strings.Builder
	for _, l := range rawLines {
		trimmed := strings.TrimRight(l, "\r ")
		if trimmed != "" {
			clean := model.StripANSI(trimmed)
			clean = strings.ReplaceAll(clean, "\t", "  ")
			if clean != "" {
				b.WriteString(commentMoreLine(clean, commentAuthor(floor.CreatorName), op))
			}
		}
	}
	return b.String()
}

// AppendMergedCommentCache appends content merged into floor to the files
// of its post.
func (e *Engine) AppendMergedCommentCache(floor *model.Comment, content string) error {
	p, err := e.GetPost(floor.PostID)
	if err != nil {
		return err
	}
	boardPath := e.BoardFilePath(p)
	if boardPath != "" {
		if _, statErr := os.Stat(boardPath); os.IsNotExist(statErr) {
			return e.RenderAndSave(p)
		}
	}

	formatted := FormatMergedCommentLines(p, floor, content)
	if formatted == "" {
		return nil
	}
	var data []byte
	if e.cfg.IsBig5() {
		data = big5uao.Encode(formatted)
	} else {
		data = []byte(formatted)
	}

	path := e.CacheFilePath(p)
	if f, err := os.OpenFile(path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644); err == nil {
		_, _ = f.Write(data)
		_ = f.Close()
	}
	if boardPath != "" && boardPath != path {
		if fb, errB := os.OpenFile(boardPath, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644); errB == nil {
			_, _ = fb.Write(data)
			_ = fb.Close()
		}
	}
	return nil
}

func (e *Engine) SetPostContent(postID uint64, content string) error {
	p, err := e.GetPost(postID)
	if err != nil {
		return err
	}
	p.Content = content
	return putPostBody(e.postsDB, p)
}

func (e *Engine) RenderFullPostText(postID uint64, asBig5 bool) ([]byte, error) {
	p, err := e.GetPost(postID)
	if err != nil {
		return nil, err
	}

	if p.DeletedOrRemoved() {
		reason := p.Reason
		if reason == "" {
			if p.Removed {
				reason = "本文已被移除"
			} else {
				reason = "本文已被刪除"
			}
		}
		deletedNotice := fmt.Sprintf("[%s]\n", reason)
		if asBig5 {
			return big5uao.Encode(deletedNotice), nil
		}
		return []byte(deletedNotice), nil
	}

	if p.Content == "" && p.PostFile != "" && p.PostFile != "-" {
		// Fallback: reload content from BBS disk file if content was empty upon initial creation
		var paths []string
		switch {
		case p.CommunityID != 0 && !e.reg.Active(p.CommunityID):
			// The name of a deleted community may belong to another board.
		case len(p.Community) > 0:
			c := string(p.Community[0])
			paths = []string{
				filepath.Join(e.cfg.BBSHome, "boards", c, p.Community, p.PostFile),
				filepath.Join(e.cfg.BBSHome, "boards", strings.ToUpper(c), p.Community, p.PostFile),
				filepath.Join(e.cfg.BBSHome, "boards", strings.ToLower(c), p.Community, p.PostFile),
				filepath.Join(e.cfg.BBSHome, "boards", p.Community, p.PostFile),
			}
		default:
			matches, _ := filepath.Glob(filepath.Join(e.cfg.BBSHome, "boards", "*", "*", p.PostFile))
			if len(matches) == 0 {
				matches, _ = filepath.Glob(filepath.Join(e.cfg.BBSHome, "boards", "*", p.PostFile))
			}
			paths = matches
		}
		for _, pth := range paths {
			if b, rErr := readArticleFileLimited(pth); rErr == nil && len(b) > 0 {
				if e.cfg.IsBig5() && !utf8.Valid(b) {
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

	return RenderPostWithComments(p, comments, asBig5), nil
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
	if e.separateCache() {
		cachePath := e.CacheFilePath(p)
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
// History and Editing (Old revisions stored in history.pebble)
// -----------------------------------------------------------------------------

func (e *Engine) UpdatePost(postID uint64, newTitle, newContent, editorName string, editorID model.PersonID, expectedModified int64) (uint32, int64, error) {
	p, err := e.GetPost(postID)
	if err != nil {
		return 0, 0, err
	}
	if editorName == "" {
		editorName, editorID = p.CreatorName, p.CreatorID
	}
	newTitle = model.HeaderValue(newTitle)

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
		CommunityID: p.CommunityID,
		Community:   p.Community,
		PostFile:    p.PostFile,
		Title:       p.Title,
		CreatorName: p.CreatorName,
		CreatorID:   p.CreatorID,
		CreatedAt:   p.CreatedAt,
		EditedAt:    now,
		EditorName:  editorName,
		EditorID:    editorID,
		Content:     p.Content,
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
	if err := putPost(e.postsDB, p); err != nil {
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

func (e *Engine) UpdatePostByCommunityFile(community, postFile, newTitle, newContent, editorName string, editorID model.PersonID, expectedModified int64) (uint32, int64, error) {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return 0, 0, err
	}
	return e.UpdatePost(p.ID, newTitle, newContent, editorName, editorID, expectedModified)
}

func (e *Engine) UpdatePostTitle(postID uint64, newTitle, editorName string, editorID model.PersonID) (uint32, error) {
	p, err := e.GetPost(postID)
	if err != nil {
		return 0, err
	}
	newTitle = model.HeaderValue(newTitle)
	if newTitle == "" || newTitle == p.Title {
		return 0, nil
	}
	if editorName == "" {
		editorName, editorID = p.CreatorName, p.CreatorID
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
		CommunityID: p.CommunityID,
		Community:   p.Community,
		PostFile:    p.PostFile,
		Title:       p.Title,
		CreatorName: p.CreatorName,
		CreatorID:   p.CreatorID,
		CreatedAt:   p.CreatedAt,
		EditedAt:    now,
		EditorName:  editorName,
		EditorID:    editorID,
		Action:      "update_title",
		Content:     p.Content,
	}
	revKey := model.EncodePostHistoryKey(p.ID, nextRev)
	revVal := model.EncodeRFC822PostRevision(revRecord)
	if err := e.historyDB.Set(revKey, revVal, pebble.NoSync); err != nil {
		return 0, fmt.Errorf("write post history failed: %w", err)
	}

	p.Title = newTitle
	p.Modified = now

	// 3. Update the post metadata: the revision above keeps the old title,
	// so RebuildSQLiteFromPebble can only learn the new one from here.
	if err := putPostMeta(e.postsDB, p); err != nil {
		return 0, fmt.Errorf("update post failed: %w", err)
	}

	// 4. Update SQLite title and modified (metaDB)
	e.metaWriteMu.Lock()
	_, _ = e.metaDB.Exec("UPDATE posts SET title = ?, modified = ? WHERE id = ?", p.Title, p.Modified, p.ID)
	e.metaWriteMu.Unlock()

	// 4. Re-render cache
	_ = e.RenderAndSave(p)

	return nextRev, nil
}

func (e *Engine) UpdatePostTitleByCommunityFile(community, postFile, newTitle, editorName string, editorID model.PersonID) (uint32, error) {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return 0, err
	}
	return e.UpdatePostTitle(p.ID, newTitle, editorName, editorID)
}

// DeletePost takes the post down on behalf of deleterName / deleterID: as
// deleted if that is its creator, else as removed (model.Deletion.Delete).
func (e *Engine) DeletePost(postID uint64, deleterName string, deleterID model.PersonID, reason string) (uint32, error) {
	return e.takeDownPost(postID, deleterName, deleterID, reason, false)
}

// RemovePost takes the post down as removed by the moderator modName /
// modID for reason (Lemmy's RemovePost), even if that is its creator.
func (e *Engine) RemovePost(postID uint64, modName string, modID model.PersonID, reason string) (uint32, error) {
	return e.takeDownPost(postID, modName, modID, reason, true)
}

func (e *Engine) takeDownPost(postID uint64, deleterName string, deleterID model.PersonID, reason string, remove bool) (uint32, error) {
	p, err := e.GetPost(postID)
	if err != nil {
		return 0, err
	}
	if p.DeletedOrRemoved() {
		return 0, nil // Already deleted
	}
	if deleterName == "" {
		deleterName, deleterID = p.CreatorName, p.CreatorID
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
		CommunityID: p.CommunityID,
		Community:   p.Community,
		PostFile:    p.PostFile,
		Title:       p.Title,
		CreatorName: p.CreatorName,
		CreatorID:   p.CreatorID,
		CreatedAt:   p.CreatedAt,
		EditedAt:    now,
		EditorName:  deleterName,
		EditorID:    deleterID,
		Action:      "delete",
		Reason:      reason,
		Content:     p.Content,
	}
	revKey := model.EncodePostHistoryKey(p.ID, nextRev)
	revVal := model.EncodeRFC822PostRevision(revRecord)
	if err := e.historyDB.Set(revKey, revVal, pebble.NoSync); err != nil {
		return 0, fmt.Errorf("write post history failed: %w", err)
	}

	// 3. Update post state
	if remove {
		p.Deletion.Remove(deleterName, deleterID, reason, now)
	} else {
		p.Deletion.Delete(p.CreatorName, p.CreatorID, deleterName, deleterID, reason, now)
	}
	p.Modified = now
	if err := putPostMeta(e.postsDB, p); err != nil {
		return 0, fmt.Errorf("update post failed: %w", err)
	}

	// 4. Update SQLite
	e.metaWriteMu.Lock()
	_, _ = e.metaDB.Exec("UPDATE posts SET deleted = ?, removed = ?, deleted_at = ?, mod_person_name = ?, mod_person_id = ?, reason = ?, modified = ? WHERE id = ?",
		boolToInt(p.Deleted), boolToInt(p.Removed), p.DeletedAt, p.ModPersonName, p.ModPersonID, p.Reason, now, p.ID)
	e.metaWriteMu.Unlock()

	// 5. Update Cache and BBS disk file
	_ = e.RenderAndSave(p)

	return nextRev, nil
}

func (e *Engine) DeletePostByCommunityFile(community, postFile, deleterName string, deleterID model.PersonID, reason string) (uint32, error) {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return 0, err
	}
	return e.DeletePost(p.ID, deleterName, deleterID, reason)
}

func (e *Engine) UndeletePost(postID uint64) error {
	p, err := e.GetPost(postID)
	if err != nil {
		return err
	}
	if !p.DeletedOrRemoved() {
		return nil
	}
	now := time.Now().Unix()
	p.Deletion.Restore()
	p.Modified = now

	if err := putPostMeta(e.postsDB, p); err != nil {
		return fmt.Errorf("undelete post failed: %w", err)
	}

	e.metaWriteMu.Lock()
	_, _ = e.metaDB.Exec("UPDATE posts SET deleted = 0, removed = 0, deleted_at = 0, mod_person_name = '', mod_person_id = 0, reason = '', modified = ? WHERE id = ?",
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

func (e *Engine) UpdateComment(postID uint64, seq uint32, newContent, editorName string, editorID model.PersonID) (uint32, error) {
	defer e.forgetCommentLines(postID)
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
	if editorName == "" {
		editorName, editorID = currentComment.CreatorName, currentComment.CreatorID
	}

	newContent = model.TruncateCommentLines(newContent, model.MaxCommentLines)

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
		CreatorName: currentComment.CreatorName,
		CreatorID:   currentComment.CreatorID,
		CreatedAt:   currentComment.CreatedAt,
		EditedAt:    now,
		EditorName:  editorName,
		EditorID:    editorID,
		IP:          currentComment.IP,
		Action:      "edit",
		Content:     currentComment.Content,
	}
	revKey := model.EncodeCommentHistoryKey(postID, seq, nextRev)
	revVal := model.EncodeRFC822CommentRevision(revRecord)
	if err := e.historyDB.Set(revKey, revVal, pebble.NoSync); err != nil {
		return 0, fmt.Errorf("write comment history failed: %w", err)
	}

	// 3. Update current comment
	currentComment.Content = newContent
	currentComment.ModifiedAt = now
	newVal := model.EncodeRFC822Comment(currentComment)
	if err := e.commentsDB.Set(cKey, newVal, pebble.NoSync); err != nil {
		return 0, fmt.Errorf("update comment failed: %w", err)
	}

	// 4. Re-render and save canonical file
	if p, err := e.GetPost(postID); err == nil && p != nil {
		p.Modified = now
		e.metaWriteMu.Lock()
		if _, err := e.metaDB.Exec("UPDATE posts SET modified = ? WHERE id = ?", now, p.ID); err == nil {
			_ = e.syncCounts(p.ID) // the new modified time goes to the counters, not the body
		}
		e.metaWriteMu.Unlock()
		_ = e.RenderAndSave(p)
	}

	return nextRev, nil
}

func (e *Engine) UpdateCommentByCommunityFile(community, postFile string, seq uint32, newContent, editorName string, editorID model.PersonID) (uint32, error) {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return 0, err
	}
	return e.UpdateComment(p.ID, seq, newContent, editorName, editorID)
}

// DeleteComment takes the comment down on behalf of deleterName /
// deleterID: as deleted if that is its creator, else as removed.
func (e *Engine) DeleteComment(postID uint64, seq uint32, deleterName string, deleterID model.PersonID, reason string) (uint32, error) {
	return e.takeDownComment(postID, seq, deleterName, deleterID, reason, false)
}

// RemoveComment takes the comment down as removed by the moderator modName
// / modID for reason (Lemmy's RemoveComment), even if that is its creator.
func (e *Engine) RemoveComment(postID uint64, seq uint32, modName string, modID model.PersonID, reason string) (uint32, error) {
	return e.takeDownComment(postID, seq, modName, modID, reason, true)
}

func (e *Engine) takeDownComment(postID uint64, seq uint32, deleterName string, deleterID model.PersonID, reason string, remove bool) (uint32, error) {
	defer e.forgetCommentLines(postID)
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
	if currentComment.DeletedOrRemoved() {
		return 0, nil // Already deleted
	}
	if deleterName == "" {
		deleterName, deleterID = currentComment.CreatorName, currentComment.CreatorID
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
		CreatorName: currentComment.CreatorName,
		CreatorID:   currentComment.CreatorID,
		CreatedAt:   currentComment.CreatedAt,
		EditedAt:    now,
		EditorName:  deleterName,
		EditorID:    deleterID,
		IP:          currentComment.IP,
		Action:      "delete",
		Reason:      reason,
		Content:     currentComment.Content,
	}
	revKey := model.EncodeCommentHistoryKey(postID, seq, nextRev)
	revVal := model.EncodeRFC822CommentRevision(revRecord)
	if err := e.historyDB.Set(revKey, revVal, pebble.NoSync); err != nil {
		return 0, fmt.Errorf("write comment history failed: %w", err)
	}

	// 3. Update current comment state
	if remove {
		currentComment.Deletion.Remove(deleterName, deleterID, reason, now)
	} else {
		currentComment.Deletion.Delete(currentComment.CreatorName, currentComment.CreatorID, deleterName, deleterID, reason, now)
	}
	newVal := model.EncodeRFC822Comment(currentComment)
	if err := e.commentsDB.Set(cKey, newVal, pebble.NoSync); err != nil {
		return 0, fmt.Errorf("delete comment failed: %w", err)
	}

	// num_comments is unchanged: the comment stays as a tombstone line.

	// 4. Re-render and save
	if p, err := e.GetPost(postID); err == nil && p != nil {
		_ = e.RenderAndSave(p)
	}

	return nextRev, nil
}

func (e *Engine) DeleteCommentByCommunityFile(community, postFile string, seq uint32, deleterName string, deleterID model.PersonID, reason string) (uint32, error) {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return 0, err
	}
	return e.DeleteComment(p.ID, seq, deleterName, deleterID, reason)
}

func (e *Engine) UndeleteComment(postID uint64, seq uint32) error {
	defer e.forgetCommentLines(postID)
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
	if !c.DeletedOrRemoved() {
		return nil
	}
	c.Deletion.Restore()

	newVal := model.EncodeRFC822Comment(c)
	if err := e.commentsDB.Set(cKey, newVal, pebble.NoSync); err != nil {
		return fmt.Errorf("undelete comment failed: %w", err)
	}

	// num_comments is unchanged: tombstones were counted all along.

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
	r, err := model.DecodeRFC822PostRevision(val)
	if err == nil {
		r.Community = e.reg.Name(r.CommunityID)
	}
	return r, err
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
			r.Community = e.reg.Name(r.CommunityID)
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
	Votes      []*model.VoteRecord
	// Origin is where the post was copied from, if it is a copy; the
	// crosspost record is then added to the source post (see crosspost.go).
	Origin *model.CrosspostOrigin
}

// ImportPostBatch imports a slice of posts and their comments atomically into SQLite and Pebble,
// and optionally renders them directly to renderTarget directory. The
// copies among them and the copies of them are then linked to their
// sources, as far as this engine has them.
func (e *Engine) ImportPostBatch(batch []*ImportedPostData, renderTarget string, legacyFormat ...bool) error {
	if err := e.importPostBatch(batch, renderTarget, legacyFormat...); err != nil {
		return err
	}
	linkImported(e, e, batch)
	return nil
}

func (e *Engine) importPostBatch(batch []*ImportedPostData, renderTarget string, legacyFormat ...bool) error {
	if len(batch) == 0 {
		return nil
	}

	// What needs no post ID is done before taking the write lock, so that
	// several imports into this engine (import-board --all --jobs) only
	// take turns for what must be serial.
	for _, item := range batch {
		item.Comments = foldComments(item.Comments)
		p := item.Post
		p.PostFile = model.CanonicalPostFile(p.PostFile)
		if err := e.resolveCommunity(p); err != nil {
			return err
		}
		if len(item.Comments) > 0 || !p.DeletedOrRemoved() {
			// A deleted post imported from .DIR has no comments, only a count.
			p.NumComments = len(item.Comments)
		}
		p.NumCrossposts = len(item.Crossposts)
		p.Title = model.HeaderValue(p.Title)
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

	stmtCP, err := tx.Prepare(`
		INSERT INTO crossposts (source_post_id, target_community_id, target_post_file, creator_name, creator_id, created_at, bbs_exempt)
		VALUES (?, ?, ?, ?, ?, ?, ?)
	`)
	if err != nil {
		return fmt.Errorf("prepare insert crosspost failed: %w", err)
	}
	defer stmtCP.Close()

	postBatch := e.postsDB.NewBatch()
	defer postBatch.Close()
	voteBatch := e.votesDB.NewBatch()
	defer voteBatch.Close()

	if err := insertImportedPosts(tx, batch, e.cfg.NextPostID); err != nil {
		return err
	}
	batchCrossposts := make(map[uint64][]crosspostEntry)
	for _, item := range batch {
		p := item.Post
		if err := checkPostID(p.ID); err != nil {
			return err
		}

		// Set in postBatch
		if err := putPost(postBatch, p); err != nil {
			return fmt.Errorf("postBatch.Set failed: %w", err)
		}
		if err := putPostCounts(postBatch, p.ID, model.PostCounts{Upvotes: p.Upvotes, Downvotes: p.Downvotes, NumComments: p.NumComments, Modified: p.Modified}); err != nil {
			return fmt.Errorf("postBatch.Set failed: %w", err)
		}

		// User Votes
		for _, v := range item.Votes {
			if v.Vote != model.VoteUp && v.Vote != model.VoteDown {
				continue
			}
			key := model.EncodeVoteKey(p.ID, v.PersonID, v.PersonName)
			_ = voteBatch.Set(key, model.EncodeVoteValue(v.Vote, v.PersonID, v.PersonName, v.CreatedAt, v.IP), nil)
		}

		// Crossposts. A post imported again keeps the records it has (of
		// copies linked to it, or added live) and only gets the new ones.
		existing, ok := batchCrossposts[p.ID]
		if !ok {
			if existing, err = e.crosspostEntries(p.ID); err != nil {
				return err
			}
		}
		seq := lastCrosspostSeq(existing)
		for _, cp := range item.Crossposts {
			cp.SourcePostID = p.ID
			if cp.TargetCommunityID == 0 && cp.TargetCommunity != "" {
				targetID, err := e.reg.Ensure(cp.TargetCommunity)
				if err != nil {
					return err
				}
				cp.TargetCommunityID = targetID
			}
			cp.TargetCommunity = e.reg.Name(cp.TargetCommunityID)
			if knownCrosspost(existing, cp) {
				continue
			}
			cpRes, cpErr := stmtCP.Exec(p.ID, cp.TargetCommunityID, cp.TargetPostFile, cp.CreatorName, cp.CreatorID, cp.CreatedAt, boolToInt(cp.BBSExempt))
			if cpErr == nil {
				cpID, _ := cpRes.LastInsertId()
				cp.ID = uint64(cpID)
				seq++
				existing = append(existing, crosspostEntry{seq: seq, rec: cp})
				_ = postBatch.Set(model.EncodeCrosspostKey(p.ID, seq), model.EncodeRFC822Crosspost(cp), nil)
			}
		}
		batchCrossposts[p.ID] = existing
		if count := len(existing); count != p.NumCrossposts {
			p.NumCrossposts = count
			if _, err := tx.Exec(`UPDATE posts SET num_crossposts = ? WHERE id = ?`, count, p.ID); err != nil {
				return fmt.Errorf("update num_crossposts failed: %w", err)
			}
		}

		if o := item.Origin; o != nil {
			o.TargetPostID = p.ID
			if _, err := tx.Exec(insertOriginSQL, originArgs(o)...); err != nil {
				return fmt.Errorf("insert crosspost origin failed: %w", err)
			}
			_ = postBatch.Set(model.EncodeCrosspostOriginKey(p.ID), model.EncodeRFC822CrosspostOrigin(o), nil)
		}
	}

	e.seqMu.Lock()
	for _, item := range batch {
		seqVal := uint32(len(item.Comments))
		e.postSeqs[item.Post.ID] = &seqVal
	}
	e.seqMu.Unlock()

	// Commit Pebble batches
	if err := e.writeImportedComments(batch); err != nil {
		return err
	}
	if err := postBatch.Commit(pebble.NoSync); err != nil {
		return fmt.Errorf("commit postBatch failed: %w", err)
	}
	if err := voteBatch.Commit(pebble.NoSync); err != nil {
		return fmt.Errorf("commit voteBatch failed: %w", err)
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
			if item.Post.DeletedOrRemoved() {
				continue // deleted posts have no file on the board
			}
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
				name := it.Post.PostFile
				if it.Post.Locked {
					name = model.LockedFileName(name)
				}
				targetPath := filepath.Join(renderTarget, name)
				_ = os.WriteFile(targetPath, rendered, 0644)
			}(item)
		}
		wg.Wait()
	}

	return nil
}

// importPostColumns are the columns of posts that ImportPostBatch writes,
// and importPostUpsert what it does with a post that is there already.
const (
	importPostColumns = `id, parent_id, community_id, post_file, title, creator_name, creator_id, created_at, modified, filemode, upvotes, downvotes, num_comments, num_crossposts,
		deleted, removed, deleted_at, mod_person_name, mod_person_id, reason, money, locked`
	importPostUpsert = `ON CONFLICT(community_id, post_file) DO UPDATE SET
			locked = excluded.locked,
			title = excluded.title,
			creator_name = excluded.creator_name,
			creator_id = excluded.creator_id,
			created_at = excluded.created_at,
			modified = excluded.modified,
			filemode = excluded.filemode,
			upvotes = excluded.upvotes,
			downvotes = excluded.downvotes,
			num_comments = excluded.num_comments,
			num_crossposts = excluded.num_crossposts,
			money = excluded.money`
	importPostParams = 22
	// importPostsPerInsert is how many posts one INSERT writes: the SQLite
	// driver prepares every statement it runs again, which took as long as
	// the insert itself when it was one statement per post.
	importPostsPerInsert = 64
)

// insertImportedPosts writes the posts of batch into SQLite in tx, a few
// dozen per statement, and sets the ID of each: the one it had, or a new
// one (from nextID, if not nil), or the ID of the post that was already
// there with the same file in the community.
func insertImportedPosts(tx *sql.Tx, batch []*ImportedPostData, nextID func() uint64) error {
	args := func(p *model.Post) []any {
		var candidateID any
		if p.ID > 0 {
			candidateID = p.ID
		} else if nextID != nil {
			candidateID = nextID()
		}
		return []any{candidateID, p.ParentID, p.CommunityID, p.PostFile, p.Title, p.CreatorName, p.CreatorID,
			p.CreatedAt, p.Modified, p.Filemode, p.Upvotes, p.Downvotes,
			p.NumComments, p.NumCrossposts,
			boolToInt(p.Deleted), boolToInt(p.Removed), p.DeletedAt, p.ModPersonName, p.ModPersonID, p.Reason,
			p.Money, boolToInt(p.Locked)}
	}
	row := "(" + strings.TrimSuffix(strings.Repeat("?, ", importPostParams), ", ") + ")"
	type fileKey struct {
		community int64
		file      string
	}
	for off := 0; off < len(batch); off += importPostsPerInsert {
		chunk := batch[off:min(off+importPostsPerInsert, len(batch))]
		byFile := make(map[fileKey]*model.Post, len(chunk))
		values := make([]any, 0, len(chunk)*importPostParams)
		for _, item := range chunk {
			p := item.Post
			k := fileKey{p.CommunityID, p.PostFile}
			if byFile[k] != nil {
				// The same file twice: the upsert cannot update a row
				// that its own statement inserted, so one at a time.
				byFile = nil
				break
			}
			byFile[k] = p
			values = append(values, args(p)...)
		}
		if byFile == nil {
			for _, item := range chunk {
				p := item.Post
				q := `INSERT INTO posts (` + importPostColumns + `) VALUES ` + row + ` ` + importPostUpsert + ` RETURNING id`
				if err := tx.QueryRow(q, args(p)...).Scan(&p.ID); err != nil {
					return fmt.Errorf("insert post %s/%s failed: %w", p.Community, p.PostFile, err)
				}
			}
			continue
		}
		q := `INSERT INTO posts (` + importPostColumns + `) VALUES ` +
			strings.TrimSuffix(strings.Repeat(row+", ", len(chunk)), ", ") + ` ` + importPostUpsert +
			` RETURNING id, community_id, post_file`
		rows, err := tx.Query(q, values...)
		if err != nil {
			p := chunk[0].Post
			return fmt.Errorf("insert posts %s/%s... failed: %w", p.Community, p.PostFile, err)
		}
		n := 0
		for rows.Next() {
			var id uint64
			var k fileKey
			if err := rows.Scan(&id, &k.community, &k.file); err != nil {
				rows.Close()
				return err
			}
			// RETURNING gives the rows in no particular order.
			if p := byFile[k]; p != nil {
				p.ID = id
				n++
			}
		}
		if err := rows.Close(); err != nil {
			return err
		}
		if err := rows.Err(); err != nil {
			return err
		}
		if n != len(chunk) {
			p := chunk[0].Post
			return fmt.Errorf("insert posts %s/%s...: %d of %d IDs returned", p.Community, p.PostFile, n, len(chunk))
		}
	}
	return nil
}

// importCommentsPerWorker is how many comments a batch of imported posts
// needs for each goroutine that writeImportedComments uses.
const importCommentsPerWorker = 4096

// writeImportedComments writes the comments of a batch of imported posts
// (whose IDs are known by then) into Pebble, by floor, with the index by
// author. Encoding them takes most of the time of an import that the write
// lock serializes, so a large batch is split by the number of comments
// among several goroutines, each writing a Pebble batch of its own.
func (e *Engine) writeImportedComments(batch []*ImportedPostData) error {
	defer func() {
		ids := make([]uint64, len(batch))
		for i, item := range batch {
			ids[i] = item.Post.ID
		}
		e.forgetCommentLines(ids...)
	}()
	total := 0
	for _, item := range batch {
		total += len(item.Comments)
	}
	if total == 0 {
		return nil
	}
	workers := min(runtime.GOMAXPROCS(0), 8, (total+importCommentsPerWorker-1)/importCommentsPerWorker)

	write := func(items []*ImportedPostData) error {
		b := e.commentsDB.NewBatch()
		defer b.Close()
		for _, item := range items {
			p := item.Post
			for i, c := range item.Comments {
				c.PostID = p.ID
				c.Sequence = uint32(i + 1)
				c.ID = model.MakeCommentID(p.ID, c.Sequence)
				if err := b.Set(model.EncodeCommentKey(p.ID, c.Sequence), model.EncodeRFC822Comment(c), nil); err != nil {
					return fmt.Errorf("commentBatch.Set failed: %w", err)
				}
				authKey := model.EncodeAuthorCommentKey(c.CreatorName, c.CreatedAt, p.ID, c.Sequence)
				if err := b.Set(authKey, []byte{}, nil); err != nil {
					return fmt.Errorf("commentBatch.Set author failed: %w", err)
				}
			}
		}
		if err := b.Commit(pebble.NoSync); err != nil {
			return fmt.Errorf("commit commentBatch failed: %w", err)
		}
		return nil
	}
	if workers <= 1 {
		return write(batch)
	}

	// Contiguous runs of posts with about total/workers comments each.
	var chunks [][]*ImportedPostData
	start, n := 0, 0
	for i, item := range batch {
		n += len(item.Comments)
		if n*workers >= total*(len(chunks)+1) || i == len(batch)-1 {
			chunks = append(chunks, batch[start:i+1])
			start = i + 1
		}
	}
	errs := make([]error, len(chunks))
	var wg sync.WaitGroup
	for i, items := range chunks {
		wg.Add(1)
		go func() {
			defer wg.Done()
			errs[i] = write(items)
		}()
	}
	wg.Wait()
	return errors.Join(errs...)
}

// SetPostEditMark sets the line the BBS shows after the body of post
// postID for its last edit (model.Post.BBSEditMark).
func (e *Engine) SetPostEditMark(postID uint64, mark string) error {
	p, err := e.GetPost(postID)
	if err != nil {
		return err
	}
	p.BBSEditMark = mark
	return putPostMeta(e.postsDB, p)
}

// SetPostEditMarkByCommunityFile is SetPostEditMark for post postFile of
// community.
func (e *Engine) SetPostEditMarkByCommunityFile(community, postFile, mark string) error {
	p, err := e.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return err
	}
	return e.SetPostEditMark(p.ID, mark)
}

// RenderPostWithComments renders a post and its comments directly in memory
func RenderPostWithComments(p *model.Post, comments []*model.Comment, asBig5 bool) []byte {
	var buf bytes.Buffer
	buf.WriteString(p.Content)
	if len(p.Content) > 0 && p.Content[len(p.Content)-1] != '\n' {
		buf.WriteByte('\n')
	}
	if p.BBSEditMark != "" {
		buf.WriteString(p.BBSEditMark + "\n")
	}

	for _, c := range comments {
		buf.WriteString(FormatCommentBlock(p, c))
	}

	if asBig5 {
		return big5uao.Encode(buf.String())
	}
	return buf.Bytes()
}

const maxArticleFileSize = 32 * 1024 * 1024 // 32MB

// readArticleFileLimited reads an article file with sparse detection, NUL truncation, and 32MB limit.
func readArticleFileLimited(path string) ([]byte, error) {
	fi, err := os.Stat(path)
	if err != nil {
		return nil, err
	}

	maxRead := fi.Size()
	if stat, ok := fi.Sys().(*unix.Stat_t); ok {
		allocated := int64(stat.Blocks) * 512
		if allocated < maxRead && !(allocated == 0 && maxRead <= 4096) {
			maxRead = allocated
		}
	}

	if maxRead > maxArticleFileSize {
		maxRead = maxArticleFileSize
	}

	if maxRead <= 0 {
		return []byte{}, nil
	}

	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	rawBytes, err := io.ReadAll(io.LimitReader(f, maxRead))
	if err != nil {
		return nil, err
	}

	if idx := bytes.IndexByte(rawBytes, 0); idx >= 0 {
		rawBytes = rawBytes[:idx]
	}

	return rawBytes, nil
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

// RenderLegacyComment renders a single comment in legacy BBS push format
func RenderLegacyComment(c *model.Comment, asBig5 bool) []byte {
	if c.DeletedOrRemoved() {
		cleanReason := c.Reason
		if !utf8.ValidString(cleanReason) {
			cleanReason = big5uao.Decode([]byte(cleanReason))
		}
		cleanReason = strings.TrimSpace(cleanReason)
		isSelf := c.Deleted || cleanReason == "自刪"
		var s string
		if isSelf {
			if cleanReason != "" && cleanReason != "自刪" {
				s = fmt.Sprintf("\x1b[1;30m[此推文已被刪除: %s]\x1b[m\n", cleanReason)
			} else {
				s = "\x1b[1;30m[此推文已被刪除]\x1b[m\n"
			}
		} else {
			if cleanReason != "" {
				s = fmt.Sprintf("\x1b[1;30m[此推文已被隱藏: %s]\x1b[m\n", cleanReason)
			} else {
				s = "\x1b[1;30m[此推文已被隱藏]\x1b[m\n"
			}
		}
		if asBig5 {
			return big5uao.Encode(s)
		}
		return []byte(s)
	}

	var buf bytes.Buffer
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
		prefixWidth := 3 + visualWidth(c.CreatorName) + 2 // tag(3) + author + ": "(2)
		msgWidth := visualWidth(line)
		tailWidth := len(tail)
		totalWidth := prefixWidth + msgWidth + tailWidth

		pad := 78 - totalWidth
		if pad < 1 {
			pad = 1
		}

		buf.WriteString(fmt.Sprintf("%s%s\x1b[33m%s\x1b[m\x1b[33m: %s%s\x1b[m%s\n",
			tagColor, tag, c.CreatorName, line, strings.Repeat(" ", pad), tail))
	}

	if asBig5 {
		return big5uao.Encode(buf.String())
	}
	return buf.Bytes()
}

// RenderLegacyPostWithComments renders a post and its comments directly in memory in legacy BBS push format
func RenderLegacyPostWithComments(p *model.Post, comments []*model.Comment, asBig5 bool) []byte {
	var buf bytes.Buffer
	buf.WriteString(p.Content)
	if len(p.Content) > 0 && p.Content[len(p.Content)-1] != '\n' {
		buf.WriteByte('\n')
	}
	if p.BBSEditMark != "" {
		buf.WriteString(p.BBSEditMark + "\n")
	}

	for _, c := range comments {
		buf.Write(RenderLegacyComment(c, false))
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
	if p.DeletedOrRemoved() {
		reason := p.Reason
		if reason == "" {
			if p.Removed {
				reason = "本文已被移除"
			} else {
				reason = "本文已被刪除"
			}
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
	Community string
	TargetDir string
	// BoardFiles renders into the board directory of the community
	// (boards/<c>/<name>, the cache that mbbsd reads) instead of
	// TargetDir: each file is replaced atomically, and a locked post gets
	// its locked file name, as RenderAndSave writes them.
	BoardFiles   bool
	LegacyFormat bool
	NumWorkers   int
	ProgressFn   func(current, total int)
}

// RenderCommunity renders all posts and comments of a community directly into TargetDir in parallel
func (e *Engine) RenderCommunity(opts RenderCommunityOptions) (int, error) {
	cid, ok := e.reg.Lookup(opts.Community)
	if !ok {
		return 0, nil
	}
	targetDir := opts.TargetDir
	if opts.BoardFiles {
		targetDir = BoardDir(e.cfg.BBSHome, e.reg.Name(cid))
	}
	if targetDir == "" {
		return 0, fmt.Errorf("targetDir cannot be empty")
	}
	if err := os.MkdirAll(targetDir, 0755); err != nil {
		return 0, fmt.Errorf("mkdir targetDir failed: %w", err)
	}

	rows, err := e.metaDB.Query("SELECT id, post_file, COALESCE(locked, 0) FROM posts WHERE community_id = ? AND deleted = 0 AND removed = 0 ORDER BY id ASC", cid)
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
		var locked bool
		if err := rows.Scan(&it.id, &it.postFile, &locked); err == nil && it.postFile != "" && it.postFile != "-" {
			if opts.BoardFiles && locked {
				it.postFile = model.LockedFileName(it.postFile)
			}
			items = append(items, it)
		}
	}
	if err := rows.Err(); err != nil {
		return 0, fmt.Errorf("query posts failed: %w", err)
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
					targetPath := filepath.Join(targetDir, it.postFile)
					if opts.BoardFiles {
						tmp := fmt.Sprintf("%s.tmp.%d", targetPath, os.Getpid())
						if os.WriteFile(tmp, rendered, 0644) == nil && os.Rename(tmp, targetPath) != nil {
							os.Remove(tmp)
						}
					} else {
						_ = os.WriteFile(targetPath, rendered, 0644)
					}
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

// -----------------------------------------------------------------------------
