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
}

func (c Config) IsBig5() bool {
	enc := strings.ToLower(strings.TrimSpace(c.FilterEncoding))
	return enc == "" || enc == "big5" || enc == "big5-uao" || enc == "uao"
}

type Engine struct {
	cfg        Config
	postsDB    *pebble.DB
	commentsDB *pebble.DB
	votesDB    *pebble.DB
	metaDB     *sql.DB

	// In-memory atomic sequence counters per post
	seqMu       sync.RWMutex
	postSeqs    map[uint64]*uint32
	dirtyDeltas    map[uint64]*postDelta
	dirtyMu        sync.Mutex
	flushTicker    *time.Ticker
	stopFlush      chan struct{}
	closeOnce      sync.Once
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
		cfg.CacheDir = "/home/bbs/cache"
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
	if err := os.MkdirAll(cfg.CacheDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to create cache dir: %w", err)
	}

	// 1. Posts Pebble DB
	postsPath := filepath.Join(cfg.DataDir, "posts.pebble")
	postsOpts := &pebble.Options{
		Cache:        pebble.NewCache(int64(cfg.CacheSizeMB/3) * 1024 * 1024),
		MemTableSize: 16 * 1024 * 1024,
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
	}
	votesDB, err := pebble.Open(votesPath, votesOpts)
	if err != nil {
		postsDB.Close()
		commentsDB.Close()
		return nil, fmt.Errorf("open votes pebble failed: %w", err)
	}

	// 4. SQLite Metadata DB
	sqlitePath := filepath.Join(cfg.DataDir, "meta.sqlite3")
	metaDB, err := sql.Open("sqlite", sqlitePath+"?_pragma=journal_mode(WAL)&_pragma=synchronous(NORMAL)&_pragma=temp_store(MEMORY)&_pragma=cache_size(-64000)")
	if err != nil {
		postsDB.Close()
		commentsDB.Close()
		votesDB.Close()
		return nil, fmt.Errorf("open sqlite meta failed: %w", err)
	}

	if err := initSQLiteSchema(metaDB); err != nil {
		postsDB.Close()
		commentsDB.Close()
		votesDB.Close()
		metaDB.Close()
		return nil, fmt.Errorf("init sqlite schema failed: %w", err)
	}

	e := &Engine{
		cfg:         cfg,
		postsDB:     postsDB,
		commentsDB:  commentsDB,
		votesDB:     votesDB,
		metaDB:      metaDB,
		postSeqs:    make(map[uint64]*uint32),
		dirtyDeltas: make(map[uint64]*postDelta),
		flushTicker: time.NewTicker(time.Duration(cfg.FlushSeconds) * time.Second),
		stopFlush:   make(chan struct{}),
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
		filemode INTEGER DEFAULT 0,
		upvotes INTEGER DEFAULT 0,
		downvotes INTEGER DEFAULT 0,
		num_comments INTEGER DEFAULT 0,
		encoding TEXT DEFAULT 'utf-8'
	);
	CREATE INDEX IF NOT EXISTS idx_posts_comm_id ON posts(community, id DESC);
	CREATE UNIQUE INDEX IF NOT EXISTS idx_posts_comm_file ON posts(community, post_file);
	CREATE INDEX IF NOT EXISTS idx_posts_author ON posts(author);
	CREATE INDEX IF NOT EXISTS idx_posts_parent_id ON posts(parent_id);
	`
	_, err := db.Exec(schema)
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
	res, err := e.metaDB.Exec(`
		INSERT INTO posts (parent_id, community, post_file, title, author, author_token, created_at, filemode, upvotes, downvotes, num_comments, encoding)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
	`, p.ParentID, p.Community, p.PostFile, p.Title, p.Author, p.AuthorToken, p.CreatedAt, p.Filemode, p.Upvotes, p.Downvotes, p.NumComments, p.Encoding)
	if err != nil {
		return nil, fmt.Errorf("insert sqlite post failed: %w", err)
	}

	id, err := res.LastInsertId()
	if err != nil {
		return nil, err
	}
	p.ID = uint64(id)

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
	err := e.metaDB.QueryRow(`
		SELECT id, parent_id, community, post_file, title, author, author_token, created_at, filemode, upvotes, downvotes, num_comments, encoding
		FROM posts WHERE id = ?
	`, postID).Scan(&p.ID, &p.ParentID, &p.Community, &p.PostFile, &p.Title, &p.Author, &p.AuthorToken, &p.CreatedAt, &p.Filemode, &p.Upvotes, &p.Downvotes, &p.NumComments, &p.Encoding)
	if err != nil {
		return nil, err
	}

	// Read content from Pebble
	key := model.EncodePostKey(postID)
	val, closer, err := e.postsDB.Get(key)
	if err != nil {
		return nil, fmt.Errorf("get pebble post %d: %w", postID, err)
	}
	defer closer.Close()

	_, body := model.DecodeRFC822Post(val)
	p.Content = body
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
		SELECT count(*) FROM posts WHERE community = ?
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
		SELECT id, parent_id, community, post_file, title, author, author_token, created_at, filemode, upvotes, downvotes, num_comments, encoding
		FROM posts WHERE community = ? ORDER BY id DESC LIMIT ? OFFSET ?
	`, community, limit, offset)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var posts []*model.Post
	for rows.Next() {
		var p model.Post
		if err := rows.Scan(&p.ID, &p.ParentID, &p.Community, &p.PostFile, &p.Title, &p.Author, &p.AuthorToken, &p.CreatedAt, &p.Filemode, &p.Upvotes, &p.Downvotes, &p.NumComments, &p.Encoding); err != nil {
			return nil, err
		}
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
		SELECT id, parent_id, community, post_file, title, author, author_token, created_at, filemode, upvotes, downvotes, num_comments, encoding
		FROM posts WHERE parent_id = ? ORDER BY id ASC LIMIT ? OFFSET ?
	`, parentID, limit, offset)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var replies []*model.Post
	for rows.Next() {
		var p model.Post
		if err := rows.Scan(&p.ID, &p.ParentID, &p.Community, &p.PostFile, &p.Title, &p.Author, &p.AuthorToken, &p.CreatedAt, &p.Filemode, &p.Upvotes, &p.Downvotes, &p.NumComments, &p.Encoding); err != nil {
			return nil, err
		}
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
	e.dirtyMu.Lock()
	if len(e.dirtyDeltas) == 0 {
		e.dirtyMu.Unlock()
		return
	}
	toFlush := e.dirtyDeltas
	e.dirtyDeltas = make(map[uint64]*postDelta)
	e.dirtyMu.Unlock()

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

func (e *Engine) CacheFilePath(p *model.Post) string {
	return filepath.Join(e.cfg.CacheDir, p.Community, p.PostFile)
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

	t := time.Unix(c.CreatedAt, 0).Format("01/02/2006 15:04:05")
	var creation string
	if c.IP != "" {
		creation = t + " " + c.IP
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

	var activeComments []*model.Comment
	for _, c := range comments {
		if !c.IsDeleted {
			activeComments = append(activeComments, c)
		}
	}

	for _, c := range activeComments {
		t := time.Unix(c.CreatedAt, 0).Format("01/02/2006 15:04:05")
		var creation string
		if c.IP != "" {
			creation = t + " " + c.IP
		} else {
			creation = t
		}
		buf.WriteString(fmt.Sprintf("[%d] %s %s\n", c.Sequence, c.Author, creation))
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

// -----------------------------------------------------------------------------
// Disaster Recovery: Rebuild SQLite from Pebble
// -----------------------------------------------------------------------------

func (e *Engine) RebuildSQLiteFromPebble() (postCount int, commentCount int, err error) {
	tx, err := e.metaDB.Begin()
	if err != nil {
		return 0, 0, err
	}
	defer tx.Rollback()

	tx.Exec("DELETE FROM posts")

	stmt, err := tx.Prepare(`
		INSERT INTO posts (id, parent_id, community, post_file, title, author, author_token, created_at, filemode, upvotes, downvotes, num_comments, encoding)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 0, 0, 0, 'utf-8')
	`)
	if err != nil {
		return 0, 0, err
	}
	defer stmt.Close()

	// 1. Scan posts.db
	pIter, err := e.postsDB.NewIter(nil)
	if err != nil {
		return 0, 0, err
	}
	defer pIter.Close()

	for pIter.First(); pIter.Valid(); pIter.Next() {
		postID, err := model.DecodePostKey(pIter.Key())
		if err != nil {
			continue
		}
		meta, _ := model.DecodeRFC822Post(pIter.Value())
		if meta["Deleted"] == "true" {
			continue
		}
		parentID, _ := strconv.ParseUint(meta["ParentID"], 10, 64)
		ctime, _ := strconv.ParseInt(meta["CreatedAt"], 10, 64)
		filemode, _ := strconv.Atoi(meta["Filemode"])
		authorToken, _ := strconv.ParseUint(meta["AuthorToken"], 10, 32)
		stmt.Exec(postID, parentID, meta["Community"], meta["PostFile"], meta["Title"], meta["Author"], uint32(authorToken), ctime, filemode)
		postCount++
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

	updateStmt, err := tx.Prepare("UPDATE posts SET upvotes = ?, downvotes = ?, num_comments = ? WHERE id = ?")
	if err != nil {
		return postCount, commentCount, err
	}
	defer updateStmt.Close()

	// Update all posts with aggregated counts
	for postID, count := range postCommentCounts {
		updateStmt.Exec(postUpvotes[postID], postDownvotes[postID], count, postID)
	}

	return postCount, commentCount, tx.Commit()
}
