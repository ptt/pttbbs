package storage

import (
	"fmt"
	"sync"
	"sync/atomic"
	"time"

	"pttbbs/nest/config"
	"pttbbs/nest/model"
)

// ShardedEngine orchestrates multiple physical storage.Engine instances.
//
// All engines share one community registry, kept in the base DataDir (or
// the DataDir of the first engine if there is none). The registry stores
// the shard of each community, chosen once when it is created: the shard
// its name is pinned to, or else the shard with the least disk usage (see
// assignShard). Renaming a community, or changing the pins or the number
// of engines, never moves it; new engines simply take the new communities
// until they catch up.
//
// Post IDs are global, as in Lemmy: one sequence for all engines, so an ID
// names one post whichever engine holds it, and the post keeps its ID when
// it moves to another engine. The ID says nothing about the engine; a post
// is found by its community (through the registry) or, given only its ID,
// by asking every engine (there are only a few). A comment ID is its post
// ID and floor (model.MakeCommentID), so it is found as its post is.
type ShardedEngine struct {
	baseCfg Config
	engines []*Engine
	reg     *CommunityRegistry
	pins    map[string]int // lower-cased community name -> shard, for new communities
	lastID  atomic.Uint64  // the last post ID handed out

	// For assignShard: the disk usage of each engine measured at usageAt,
	// the average usage of a community then, and the communities added
	// to each engine since.
	diskUsage func(shard int) int64 // Engine.DiskUsage; replaced by tests
	usageMu   sync.Mutex
	usage     []int64
	usageAt   time.Time
	avgUsage  int64
	added     []int64
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

		MaxPostCommentLines: sc.MaxPostCommentLines,
	}

	if len(sc.Engines) <= 1 {
		dataDir := config.DataDir(sc.BBSHome)
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
	s := &ShardedEngine{baseCfg: baseCfg}

	regDir := baseCfg.DataDir
	if regDir == "" {
		regDir = locations[0].DataDir
	}
	reg, err := OpenCommunityRegistry(regDir)
	if err != nil {
		return nil, err
	}

	for i, loc := range locations {
		cfg := baseCfg
		cfg.Communities = reg
		cfg.DataDir = loc.DataDir
		cfg.CacheDir = loc.CacheDir
		cfg.NumShards = numShards
		cfg.NextPostID = s.nextPostID

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
			reg.Close()
			return nil, fmt.Errorf("failed to open shard engine %d (%s): %w", i, loc.DataDir, err)
		}
		engines[i] = eng
	}

	copiedPins := make(map[string]int)
	for k, v := range pins {
		if v >= 0 && v < numShards {
			copiedPins[communityKey(k)] = v
		}
	}

	s.engines, s.reg, s.pins = engines, reg, copiedPins
	// Continue after the largest ID any engine has used, so that the IDs
	// of a single engine stay valid when it becomes the first of several.
	for _, e := range engines {
		if id := e.MaxPostID(); id > s.lastID.Load() {
			s.lastID.Store(id)
		}
	}
	s.diskUsage = func(shard int) int64 { return s.engines[shard].DiskUsage() }
	reg.SetShardAssigner(s.assignShard)
	return s, nil
}

// nextPostID allocates the ID of a new post in any engine (checked to be
// within model.MaxPostID by the engine).
func (s *ShardedEngine) nextPostID() uint64 {
	return s.lastID.Add(1)
}

// usageTTL is how long assignShard uses one measure of the disk usage of
// the engines before measuring again.
const usageTTL = 10 * time.Minute

// assignShard chooses the shard of a new community: the one its name is
// pinned to, or else the one with the least disk usage. Boards are
// registered (by import-boards or the board headers) before their posts
// arrive, so each community put on an engine since its usage was measured
// counts as much as the average community then; until the next measure
// (after usageTTL), many boards registered at once are spread over the
// engines. An engine with a large community gets fewer new ones, and an
// engine added later takes the new ones until it catches up.
func (s *ShardedEngine) assignShard(name string, _ int64, communities map[int]int) int {
	s.usageMu.Lock()
	defer s.usageMu.Unlock()
	if now := time.Now(); len(s.usage) != len(s.engines) || now.Sub(s.usageAt) >= usageTTL {
		s.measureUsage(communities)
		s.usageAt = now
	}
	shard, ok := s.pins[communityKey(name)]
	if !ok {
		load := func(i int) int64 { return s.usage[i] + s.added[i]*s.avgUsage }
		for i := 1; i < len(s.engines); i++ {
			if load(i) < load(shard) {
				shard = i
			}
		}
	}
	s.added[shard]++
	return shard
}

// measureUsage measures the disk usage of each engine and the average
// usage of the communities (soft-deleted ones included, as their posts
// are still there); s.usageMu must be held.
func (s *ShardedEngine) measureUsage(communities map[int]int) {
	s.usage = make([]int64, len(s.engines))
	s.added = make([]int64, len(s.engines))
	var total, n int64
	for i := range s.engines {
		s.usage[i] = s.diskUsage(i)
		total += s.usage[i]
		n += int64(communities[i])
	}
	if n == 0 {
		n = 1
	}
	s.avgUsage = max(total/n, 1)
}

// Shards returns the number of active shards
func (s *ShardedEngine) Shards() int {
	return len(s.engines)
}

// Communities returns the community registry shared by all engines.
func (s *ShardedEngine) Communities() *CommunityRegistry {
	return s.reg
}

// GetEngineByCommunityID routes a community ID to the engine of the shard
// stored for it. Unknown IDs go to the first shard, which finds nothing.
func (s *ShardedEngine) GetEngineByCommunityID(id int64) *Engine {
	if shard, ok := s.reg.Shard(id); ok && shard >= 0 && shard < len(s.engines) {
		return s.engines[shard]
	}
	return s.engines[0]
}

// GetEngineByCommunity routes an existing community to its engine. Unknown
// (or deleted) names go to the first shard, which then simply finds
// nothing.
func (s *ShardedEngine) GetEngineByCommunity(community string) *Engine {
	if id, ok := s.reg.Lookup(community); ok {
		return s.GetEngineByCommunityID(id)
	}
	return s.engines[0]
}

// engineForWrite routes a community that is about to be written to,
// registering it (and so choosing its shard) if needed.
func (s *ShardedEngine) engineForWrite(community string) (*Engine, error) {
	if community == "" {
		return s.engines[0], nil
	}
	id, err := s.reg.Ensure(community)
	if err != nil {
		return nil, err
	}
	return s.GetEngineByCommunityID(id), nil
}

// GetEngineByPostID finds the engine that holds post postID by asking every
// engine (a primary key lookup in each). Unknown IDs go to the first
// engine, which then simply finds nothing.
func (s *ShardedEngine) GetEngineByPostID(postID uint64) *Engine {
	if len(s.engines) > 1 {
		for _, e := range s.engines {
			if e.HasPost(postID) {
				return e
			}
		}
	}
	return s.engines[0]
}

// -----------------------------------------------------------------------------
// Storage Interface Implementation
// -----------------------------------------------------------------------------

func (s *ShardedEngine) CreatePost(p *model.Post) (*model.Post, error) {
	if p.CommunityID != 0 {
		return s.GetEngineByCommunityID(p.CommunityID).CreatePost(p)
	}
	eng, err := s.engineForWrite(p.Community)
	if err != nil {
		return nil, err
	}
	return eng.CreatePost(p)
}

func (s *ShardedEngine) GetPost(postID uint64) (*model.Post, error) {
	return s.GetEngineByPostID(postID).GetPost(postID)
}

func (s *ShardedEngine) LockPost(postID uint64, locked bool) error {
	return s.GetEngineByPostID(postID).LockPost(postID, locked)
}

func (s *ShardedEngine) LockPostByCommunityFile(community, postFile string, locked bool) error {
	return s.GetEngineByCommunity(community).LockPostByCommunityFile(community, postFile, locked)
}

func (s *ShardedEngine) GetPostByCommunityFile(community, postFile string) (*model.Post, error) {
	return s.GetEngineByCommunity(community).GetPostByCommunityFile(community, postFile)
}

func (s *ShardedEngine) CountPosts(community string) (int, error) {
	return s.GetEngineByCommunity(community).CountPosts(community)
}

func (s *ShardedEngine) ListPostMeta(community string) ([]*model.Post, error) {
	return s.GetEngineByCommunity(community).ListPostMeta(community)
}

func (s *ShardedEngine) SetPostMeta(community, postFile string, filemode, money int) error {
	return s.GetEngineByCommunity(community).SetPostMeta(community, postFile, filemode, money)
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

// AddCrosspost records a crosspost with its source post, in the engine of
// the source community: the record is keyed by the source post (whose
// crosspost count it raises) and names the target by community and post
// file, which may be in any engine.
func (s *ShardedEngine) AddCrosspost(srcCommunity, srcPostFile, targetCommunity, targetPostFile, creatorName string, creatorID model.PersonID, createdAt int64) (*model.CrosspostRecord, int, error) {
	return s.GetEngineByCommunity(srcCommunity).AddCrosspost(srcCommunity, srcPostFile, targetCommunity, targetPostFile, creatorName, creatorID, createdAt)
}

// GetCrossposts lists the crossposts of a post, kept with it.
func (s *ShardedEngine) GetCrossposts(community, postFile string) ([]*model.CrosspostRecord, error) {
	return s.GetEngineByCommunity(community).GetCrossposts(community, postFile)
}

// GetOriginPost finds the source of a crosspost by asking every engine, as
// the record is kept with the source post.
func (s *ShardedEngine) GetOriginPost(targetCommunity, targetPostFile string) (*model.Post, *model.CrosspostRecord, error) {
	for _, e := range s.engines {
		p, rec, err := e.GetOriginPost(targetCommunity, targetPostFile)
		if err != nil || p != nil {
			return p, rec, err
		}
	}
	return nil, nil, nil
}

func (s *ShardedEngine) AddComment(c *model.Comment) (*model.Comment, error) {
	return s.GetEngineByPostID(c.PostID).AddComment(c)
}

// GetComment finds comment commentID in the engine of its post.
func (s *ShardedEngine) GetComment(commentID uint64) (*model.Comment, error) {
	postID, _, ok := model.SplitCommentID(commentID)
	if !ok {
		return nil, fmt.Errorf("%w: %d", ErrCommentNotFound, commentID)
	}
	return s.GetEngineByPostID(postID).GetComment(commentID)
}

func (s *ShardedEngine) GetComments(postID uint64, startFloor, limit uint32) ([]*model.Comment, error) {
	return s.GetEngineByPostID(postID).GetComments(postID, startFloor, limit)
}

func (s *ShardedEngine) GetCommentsFiltered(postID uint64, startFloor, limit uint32, creatorName string, creatorID model.PersonID) ([]*model.Comment, error) {
	return s.GetEngineByPostID(postID).GetCommentsFiltered(postID, startFloor, limit, creatorName, creatorID)
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

func (s *ShardedEngine) GetCommentsByCommunityFileFiltered(community, postFile string, startFloor, limit uint32, creatorName string, creatorID model.PersonID) ([]*model.Comment, error) {
	return s.GetEngineByCommunity(community).GetCommentsByCommunityFileFiltered(community, postFile, startFloor, limit, creatorName, creatorID)
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

func (s *ShardedEngine) ClearCommunity(community string) (int, int, error) {
	return s.GetEngineByCommunity(community).ClearCommunity(community)
}

// RenameCommunity renames a community, which stays on its shard.
func (s *ShardedEngine) RenameCommunity(oldName, newName string) (int64, error) {
	return s.reg.Rename(oldName, newName)
}

func (s *ShardedEngine) RemoveCommunity(name string, mod model.PersonID, reason string) (int64, error) {
	return s.reg.RemoveCommunity(name, mod, reason)
}

func (s *ShardedEngine) PurgeCommunity(id int64) (int, int, error) {
	return s.GetEngineByCommunityID(id).PurgeCommunity(id)
}

func (s *ShardedEngine) MergeCommunity(from, to string) (int, int, error) {
	return mergeCommunity(s.reg, from, to, s.GetEngineByCommunityID, s.engines)
}

// MoveCommunity moves community name, with its posts (which keep their
// IDs), to engine shard and returns the number of posts moved. Posts are
// copied while the community is still routed to its old engine, then the
// registry is switched, and posts written meanwhile are moved too. A
// comment or vote written to a post in the moment it is copied may be
// lost, so move a board while it is quiet (or closed).
func (s *ShardedEngine) MoveCommunity(name string, shard int) (int, error) {
	if shard < 0 || shard >= len(s.engines) {
		return 0, fmt.Errorf("no engine %d: there are %d engines", shard, len(s.engines))
	}
	id, ok := s.reg.Lookup(name)
	if !ok {
		return 0, fmt.Errorf("%w: %s", ErrCommunityNotFound, name)
	}
	src, dst := s.GetEngineByCommunityID(id), s.engines[shard]
	if src == dst {
		return 0, nil
	}
	moved, err := src.moveCommunityPosts(id, dst)
	if err != nil {
		return moved, err
	}
	if err := s.reg.SetShard(id, shard); err != nil {
		return moved, err
	}
	n, err := src.moveCommunityPosts(id, dst)
	return moved + n, err
}

func (s *ShardedEngine) PurgePost(postID uint64) error {
	return s.GetEngineByPostID(postID).PurgePost(postID)
}

func (s *ShardedEngine) PurgePostByCommunityFile(community, postFile string) error {
	return s.GetEngineByCommunity(community).PurgePostByCommunityFile(community, postFile)
}

func (s *ShardedEngine) VotePost(postID uint64, personName string, personID model.PersonID, newVote model.VoteType, createdAt int64, ip string) (int, int, error) {
	return s.GetEngineByPostID(postID).VotePost(postID, personName, personID, newVote, createdAt, ip)
}

func (s *ShardedEngine) GetVotes(postID uint64) ([]*model.VoteRecord, error) {
	return s.GetEngineByPostID(postID).GetVotes(postID)
}

func (s *ShardedEngine) GetVotesByCommunityFile(community, postFile string) ([]*model.VoteRecord, error) {
	p, err := s.GetPostByCommunityFile(community, postFile)
	if err != nil {
		return nil, err
	}
	if p == nil {
		return nil, fmt.Errorf("post not found: %s@%s", postFile, community)
	}
	return s.GetVotes(p.ID)
}

func (s *ShardedEngine) ResetVotes(postID uint64) error {
	return s.GetEngineByPostID(postID).ResetVotes(postID)
}

func (s *ShardedEngine) ResetVotesByCommunityFile(community, postFile string) error {
	return s.GetEngineByCommunity(community).ResetVotesByCommunityFile(community, postFile)
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

func (s *ShardedEngine) UpdatePost(postID uint64, newTitle, newContent, editorName string, editorID model.PersonID, expectedModified int64) (uint32, int64, error) {
	return s.GetEngineByPostID(postID).UpdatePost(postID, newTitle, newContent, editorName, editorID, expectedModified)
}

func (s *ShardedEngine) SetPostEditMarkByCommunityFile(community, postFile, mark string) error {
	return s.GetEngineByCommunity(community).SetPostEditMarkByCommunityFile(community, postFile, mark)
}

func (s *ShardedEngine) UpdatePostByCommunityFile(community, postFile, newTitle, newContent, editorName string, editorID model.PersonID, expectedModified int64) (uint32, int64, error) {
	return s.GetEngineByCommunity(community).UpdatePostByCommunityFile(community, postFile, newTitle, newContent, editorName, editorID, expectedModified)
}

func (s *ShardedEngine) UpdatePostTitle(postID uint64, newTitle, editorName string, editorID model.PersonID) (uint32, error) {
	return s.GetEngineByPostID(postID).UpdatePostTitle(postID, newTitle, editorName, editorID)
}

func (s *ShardedEngine) UpdatePostTitleByCommunityFile(community, postFile, newTitle, editorName string, editorID model.PersonID) (uint32, error) {
	return s.GetEngineByCommunity(community).UpdatePostTitleByCommunityFile(community, postFile, newTitle, editorName, editorID)
}

func (s *ShardedEngine) DeletePost(postID uint64, deleterName string, deleterID model.PersonID, reason string) (uint32, error) {
	return s.GetEngineByPostID(postID).DeletePost(postID, deleterName, deleterID, reason)
}

func (s *ShardedEngine) RemovePost(postID uint64, modName string, modID model.PersonID, reason string) (uint32, error) {
	return s.GetEngineByPostID(postID).RemovePost(postID, modName, modID, reason)
}

func (s *ShardedEngine) DeletePostByCommunityFile(community, postFile, deleterName string, deleterID model.PersonID, reason string) (uint32, error) {
	return s.GetEngineByCommunity(community).DeletePostByCommunityFile(community, postFile, deleterName, deleterID, reason)
}

func (s *ShardedEngine) UndeletePost(postID uint64) error {
	return s.GetEngineByPostID(postID).UndeletePost(postID)
}

func (s *ShardedEngine) UndeletePostByCommunityFile(community, postFile string) error {
	return s.GetEngineByCommunity(community).UndeletePostByCommunityFile(community, postFile)
}

func (s *ShardedEngine) UpdateComment(postID uint64, seq uint32, newContent, editorName string, editorID model.PersonID) (uint32, error) {
	return s.GetEngineByPostID(postID).UpdateComment(postID, seq, newContent, editorName, editorID)
}

func (s *ShardedEngine) UpdateCommentByCommunityFile(community, postFile string, seq uint32, newContent, editorName string, editorID model.PersonID) (uint32, error) {
	return s.GetEngineByCommunity(community).UpdateCommentByCommunityFile(community, postFile, seq, newContent, editorName, editorID)
}

func (s *ShardedEngine) DeleteComment(postID uint64, seq uint32, deleterName string, deleterID model.PersonID, reason string) (uint32, error) {
	return s.GetEngineByPostID(postID).DeleteComment(postID, seq, deleterName, deleterID, reason)
}

func (s *ShardedEngine) RemoveComment(postID uint64, seq uint32, modName string, modID model.PersonID, reason string) (uint32, error) {
	return s.GetEngineByPostID(postID).RemoveComment(postID, seq, modName, modID, reason)
}

func (s *ShardedEngine) DeleteCommentByCommunityFile(community, postFile string, seq uint32, deleterName string, deleterID model.PersonID, reason string) (uint32, error) {
	return s.GetEngineByCommunity(community).DeleteCommentByCommunityFile(community, postFile, seq, deleterName, deleterID, reason)
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
	p := batch[0].Post
	if p.CommunityID != 0 {
		return s.GetEngineByCommunityID(p.CommunityID).ImportPostBatch(batch, renderTarget, legacyFormat...)
	}
	eng, err := s.engineForWrite(p.Community)
	if err != nil {
		return err
	}
	return eng.ImportPostBatch(batch, renderTarget, legacyFormat...)
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
	if err := s.reg.Close(); err != nil && firstErr == nil {
		firstErr = err
	}
	return firstErr
}
