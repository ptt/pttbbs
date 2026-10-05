package storage

import (
	"fmt"
	"hash/crc32"
	"strings"
	"time"

	"pttbbs/post/config"
	"pttbbs/post/model"
)

// ShardedEngine orchestrates multiple physical storage.Engine instances
type ShardedEngine struct {
	baseCfg Config
	engines []*Engine
	pins    map[string]int
}

// OpenStorage creates either a single Engine or a ShardedEngine based on ServiceConfig
func OpenStorage(sc *config.ServiceConfig) (Storage, error) {
	if sc == nil {
		sc = config.DefaultConfig("/home/bbs")
	}

	baseCfg := Config{
		BBSHome:        sc.BBSHome,
		CacheSizeMB:    sc.CacheSizeMB,
		FlushSeconds:   sc.FlushSeconds,
		MinReplyRunes:  sc.MinReplyRunes,
		FilterEncoding: sc.FilterEncoding,
		MaxOpenFiles:   sc.MaxOpenFiles,
	}

	if len(sc.Engines) <= 1 {
		dataDir := sc.BBSHome + "/db"
		cacheDir := "boards"
		if len(sc.Engines) == 1 {
			if sc.Engines[0].DataDir != "" {
				dataDir = sc.Engines[0].DataDir
			}
			if sc.Engines[0].CacheDir != "" {
				cacheDir = sc.Engines[0].CacheDir
			}
		}
		baseCfg.DataDir = dataDir
		baseCfg.CacheDir = cacheDir
		baseCfg.ShardID = 0
		baseCfg.NumShards = 1
		return OpenEngine(baseCfg)
	}

	return OpenShardedEngine(baseCfg, sc.Engines, sc.Pins)
}

// OpenShardedEngine initializes multiple physical storage engines
func OpenShardedEngine(baseCfg Config, locations []config.EngineLocation, pins map[string]int) (*ShardedEngine, error) {
	if len(locations) == 0 {
		return nil, fmt.Errorf("no engine locations specified for sharding")
	}

	numShards := len(locations)
	engines := make([]*Engine, numShards)

	for i, loc := range locations {
		cfg := baseCfg
		cfg.DataDir = loc.DataDir
		cfg.CacheDir = loc.CacheDir
		cfg.ShardID = i
		cfg.NumShards = numShards

		// Distribute cache memory proportionally across shards
		if cfg.CacheSizeMB > 0 {
			cfg.CacheSizeMB = baseCfg.CacheSizeMB / numShards
			if cfg.CacheSizeMB < 32 {
				cfg.CacheSizeMB = 32
			}
		}

		eng, err := OpenEngine(cfg)
		if err != nil {
			// Rollback already opened engines
			for j := 0; j < i; j++ {
				_ = engines[j].Close()
			}
			return nil, fmt.Errorf("failed to open shard engine %d (%s): %w", i, loc.DataDir, err)
		}
		engines[i] = eng
	}

	copiedPins := make(map[string]int)
	for k, v := range pins {
		if v >= 0 && v < numShards {
			copiedPins[strings.TrimSpace(k)] = v
		}
	}

	return &ShardedEngine{
		baseCfg: baseCfg,
		engines: engines,
		pins:    copiedPins,
	}, nil
}

// Shards returns the number of active shards
func (s *ShardedEngine) Shards() int {
	return len(s.engines)
}

// GetEngineByCommunity routes a community to its target engine (pinned or consistent hash)
func (s *ShardedEngine) GetEngineByCommunity(community string) *Engine {
	norm := strings.TrimSpace(community)
	if idx, ok := s.pins[norm]; ok && idx >= 0 && idx < len(s.engines) {
		return s.engines[idx]
	}
	if len(s.engines) == 1 {
		return s.engines[0]
	}
	h := crc32.ChecksumIEEE([]byte(strings.ToLower(norm)))
	return s.engines[int(h)%len(s.engines)]
}

// GetEngineByPostID routes a post ID to its origin engine shard via modulo
func (s *ShardedEngine) GetEngineByPostID(postID uint64) *Engine {
	if len(s.engines) == 1 {
		return s.engines[0]
	}
	idx := int(postID % uint64(len(s.engines)))
	return s.engines[idx]
}

// -----------------------------------------------------------------------------
// Storage Interface Implementation
// -----------------------------------------------------------------------------

func (s *ShardedEngine) CreatePost(p *model.Post) (*model.Post, error) {
	return s.GetEngineByCommunity(p.Community).CreatePost(p)
}

func (s *ShardedEngine) GetPost(postID uint64) (*model.Post, error) {
	return s.GetEngineByPostID(postID).GetPost(postID)
}

func (s *ShardedEngine) GetPostByCommunityFile(community, postFile string) (*model.Post, error) {
	return s.GetEngineByCommunity(community).GetPostByCommunityFile(community, postFile)
}

func (s *ShardedEngine) CountPosts(community string) (int, error) {
	return s.GetEngineByCommunity(community).CountPosts(community)
}

func (s *ShardedEngine) ListPosts(community string, limit, offset int) ([]*model.Post, error) {
	return s.GetEngineByCommunity(community).ListPosts(community, limit, offset)
}

func (s *ShardedEngine) ListDeletedPosts(community string, limit, offset int) ([]*model.Post, error) {
	return s.GetEngineByCommunity(community).ListDeletedPosts(community, limit, offset)
}

func (s *ShardedEngine) GetReplies(parentID uint64, limit, offset int) ([]*model.Post, error) {
	return s.GetEngineByPostID(parentID).GetReplies(parentID, limit, offset)
}

func (s *ShardedEngine) GetThread(rootID uint64) ([]*model.Post, error) {
	return s.GetEngineByPostID(rootID).GetThread(rootID)
}

func (s *ShardedEngine) AddCrosspost(srcCommunity, srcPostFile, targetCommunity, targetPostFile, operator string, operatorToken uint32, createdAt int64) (*model.CrosspostRecord, int, error) {
	return s.GetEngineByCommunity(targetCommunity).AddCrosspost(srcCommunity, srcPostFile, targetCommunity, targetPostFile, operator, operatorToken, createdAt)
}

func (s *ShardedEngine) GetCrossposts(community, postFile string) ([]*model.CrosspostRecord, error) {
	return s.GetEngineByCommunity(community).GetCrossposts(community, postFile)
}

func (s *ShardedEngine) GetOriginPost(targetCommunity, targetPostFile string) (*model.Post, *model.CrosspostRecord, error) {
	return s.GetEngineByCommunity(targetCommunity).GetOriginPost(targetCommunity, targetPostFile)
}

func (s *ShardedEngine) AddComment(c *model.Comment) (*model.Comment, error) {
	return s.GetEngineByPostID(c.PostID).AddComment(c)
}

func (s *ShardedEngine) GetComments(postID uint64, startFloor, limit uint32) ([]*model.Comment, error) {
	return s.GetEngineByPostID(postID).GetComments(postID, startFloor, limit)
}

func (s *ShardedEngine) GetCommentsFiltered(postID uint64, startFloor, limit uint32, author, authorToken string) ([]*model.Comment, error) {
	return s.GetEngineByPostID(postID).GetCommentsFiltered(postID, startFloor, limit, author, authorToken)
}

func (s *ShardedEngine) GetCommentCount(postID uint64) (int, error) {
	return s.GetEngineByPostID(postID).GetCommentCount(postID)
}

func (s *ShardedEngine) GetCommentCountByCommunityFile(community, postFile string) (int, error) {
	return s.GetEngineByCommunity(community).GetCommentCountByCommunityFile(community, postFile)
}

func (s *ShardedEngine) GetCommentsByCommunityFile(community, postFile string, startFloor, limit uint32) ([]*model.Comment, error) {
	return s.GetEngineByCommunity(community).GetCommentsByCommunityFile(community, postFile, startFloor, limit)
}

func (s *ShardedEngine) GetCommentsByCommunityFileFiltered(community, postFile string, startFloor, limit uint32, author, authorToken string) ([]*model.Comment, error) {
	return s.GetEngineByCommunity(community).GetCommentsByCommunityFileFiltered(community, postFile, startFloor, limit, author, authorToken)
}

func (s *ShardedEngine) PurgeUserComments(author string, maxAge time.Duration) (int, error) {
	total := 0
	for _, e := range s.engines {
		n, err := e.PurgeUserComments(author, maxAge)
		if err != nil {
			return total, err
		}
		total += n
	}
	return total, nil
}

func (s *ShardedEngine) PurgeUserPosts(author string, maxAge time.Duration) (int, error) {
	total := 0
	for _, e := range s.engines {
		n, err := e.PurgeUserPosts(author, maxAge)
		if err != nil {
			return total, err
		}
		total += n
	}
	return total, nil
}

func (s *ShardedEngine) DeleteCommunity(community string) (int, int, error) {
	return s.GetEngineByCommunity(community).DeleteCommunity(community)
}

func (s *ShardedEngine) PurgePost(postID uint64) error {
	return s.GetEngineByPostID(postID).PurgePost(postID)
}

func (s *ShardedEngine) PurgePostByCommunityFile(community, postFile string) error {
	return s.GetEngineByCommunity(community).PurgePostByCommunityFile(community, postFile)
}

func (s *ShardedEngine) VotePost(postID uint64, user string, authorToken uint32, newVote model.VoteType) (int, int, error) {
	return s.GetEngineByPostID(postID).VotePost(postID, user, authorToken, newVote)
}

func (s *ShardedEngine) SetPostContent(postID uint64, content string) error {
	return s.GetEngineByPostID(postID).SetPostContent(postID, content)
}

func (s *ShardedEngine) RenderFullPostText(postID uint64, asBig5 bool) ([]byte, error) {
	return s.GetEngineByPostID(postID).RenderFullPostText(postID, asBig5)
}

func (s *ShardedEngine) RenderLegacyPostText(postID uint64, asBig5 bool) ([]byte, error) {
	return s.GetEngineByPostID(postID).RenderLegacyPostText(postID, asBig5)
}

func (s *ShardedEngine) RenderCommunity(opts RenderCommunityOptions) (int, error) {
	return s.GetEngineByCommunity(opts.Community).RenderCommunity(opts)
}

func (s *ShardedEngine) UpdatePost(postID uint64, newTitle, newContent, editor string, expectedModified int64) (uint32, int64, error) {
	return s.GetEngineByPostID(postID).UpdatePost(postID, newTitle, newContent, editor, expectedModified)
}

func (s *ShardedEngine) UpdatePostByCommunityFile(community, postFile, newTitle, newContent, editor string, expectedModified int64) (uint32, int64, error) {
	return s.GetEngineByCommunity(community).UpdatePostByCommunityFile(community, postFile, newTitle, newContent, editor, expectedModified)
}

func (s *ShardedEngine) UpdatePostTitle(postID uint64, newTitle, editor string) (uint32, error) {
	return s.GetEngineByPostID(postID).UpdatePostTitle(postID, newTitle, editor)
}

func (s *ShardedEngine) UpdatePostTitleByCommunityFile(community, postFile, newTitle, editor string) (uint32, error) {
	return s.GetEngineByCommunity(community).UpdatePostTitleByCommunityFile(community, postFile, newTitle, editor)
}

func (s *ShardedEngine) DeletePost(postID uint64, deleter, reason string) (uint32, error) {
	return s.GetEngineByPostID(postID).DeletePost(postID, deleter, reason)
}

func (s *ShardedEngine) DeletePostByCommunityFile(community, postFile, deleter, reason string) (uint32, error) {
	return s.GetEngineByCommunity(community).DeletePostByCommunityFile(community, postFile, deleter, reason)
}

func (s *ShardedEngine) UndeletePost(postID uint64) error {
	return s.GetEngineByPostID(postID).UndeletePost(postID)
}

func (s *ShardedEngine) UndeletePostByCommunityFile(community, postFile string) error {
	return s.GetEngineByCommunity(community).UndeletePostByCommunityFile(community, postFile)
}

func (s *ShardedEngine) UpdateComment(postID uint64, seq uint32, newContent, editor string) (uint32, error) {
	return s.GetEngineByPostID(postID).UpdateComment(postID, seq, newContent, editor)
}

func (s *ShardedEngine) UpdateCommentByCommunityFile(community, postFile string, seq uint32, newContent, editor string) (uint32, error) {
	return s.GetEngineByCommunity(community).UpdateCommentByCommunityFile(community, postFile, seq, newContent, editor)
}

func (s *ShardedEngine) DeleteComment(postID uint64, seq uint32, deleter, reason string) (uint32, error) {
	return s.GetEngineByPostID(postID).DeleteComment(postID, seq, deleter, reason)
}

func (s *ShardedEngine) DeleteCommentByCommunityFile(community, postFile string, seq uint32, deleter, reason string) (uint32, error) {
	return s.GetEngineByCommunity(community).DeleteCommentByCommunityFile(community, postFile, seq, deleter, reason)
}

func (s *ShardedEngine) UndeleteComment(postID uint64, seq uint32) error {
	return s.GetEngineByPostID(postID).UndeleteComment(postID, seq)
}

func (s *ShardedEngine) UndeleteCommentByCommunityFile(community, postFile string, seq uint32) error {
	return s.GetEngineByCommunity(community).UndeleteCommentByCommunityFile(community, postFile, seq)
}

func (s *ShardedEngine) GetPostHistory(postID uint64, rev uint32) (*model.PostRevision, error) {
	return s.GetEngineByPostID(postID).GetPostHistory(postID, rev)
}

func (s *ShardedEngine) ListPostHistory(postID uint64) ([]*model.PostRevision, error) {
	return s.GetEngineByPostID(postID).ListPostHistory(postID)
}

func (s *ShardedEngine) GetCommentHistory(postID uint64, seq uint32, rev uint32) (*model.CommentRevision, error) {
	return s.GetEngineByPostID(postID).GetCommentHistory(postID, seq, rev)
}

func (s *ShardedEngine) ListCommentHistory(postID uint64, seq uint32) ([]*model.CommentRevision, error) {
	return s.GetEngineByPostID(postID).ListCommentHistory(postID, seq)
}

func (s *ShardedEngine) ImportPostBatch(batch []*ImportedPostData, renderTarget string, legacyFormat ...bool) error {
	if len(batch) == 0 {
		return nil
	}
	return s.GetEngineByCommunity(batch[0].Post.Community).ImportPostBatch(batch, renderTarget, legacyFormat...)
}

func (s *ShardedEngine) RebuildSQLiteFromPebble() (int, int, error) {
	totalPosts, totalComments := 0, 0
	for _, e := range s.engines {
		p, c, err := e.RebuildSQLiteFromPebble()
		if err != nil {
			return totalPosts, totalComments, err
		}
		totalPosts += p
		totalComments += c
	}
	return totalPosts, totalComments, nil
}

func (s *ShardedEngine) Close() error {
	var firstErr error
	for _, e := range s.engines {
		if err := e.Close(); err != nil && firstErr == nil {
			firstErr = err
		}
	}
	return firstErr
}
