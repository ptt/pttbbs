package storage

import (
	"time"

	"pttbbs/post/model"
)

// Storage defines the uniform interface for single-engine and multi-engine post storage
type Storage interface {
	CreatePost(p *model.Post) (*model.Post, error)
	GetPost(postID uint64) (*model.Post, error)
	GetPostByCommunityFile(community, postFile string) (*model.Post, error)
	// LockPost locks or unlocks a post (Lemmy's LockPost).
	LockPost(postID uint64, locked bool) error
	LockPostByCommunityFile(community, postFile string, locked bool) error
	CountPosts(community string) (int, error)
	ListPostMeta(community string) ([]*model.Post, error)
	SetPostMeta(community, postFile string, filemode, money int) error
	ListPosts(community string, limit, offset int) ([]*model.Post, error)
	ListDeletedPosts(community string, limit, offset int) ([]*model.Post, error)
	GetReplies(parentID uint64, limit, offset int) ([]*model.Post, error)
	GetThread(rootID uint64) ([]*model.Post, error)
	AddCrosspost(srcCommunity, srcPostFile, targetCommunity, targetPostFile, creatorName string, creatorID model.PersonID, createdAt int64) (*model.CrosspostRecord, int, error)
	GetCrossposts(community, postFile string) ([]*model.CrosspostRecord, error)
	GetOriginPost(targetCommunity, targetPostFile string) (*model.Post, *model.CrosspostRecord, error)
	AddComment(c *model.Comment) (*model.Comment, error)
	// GetComment returns the comment with the given (global) comment ID.
	GetComment(commentID uint64) (*model.Comment, error)
	GetComments(postID uint64, startFloor, limit uint32) ([]*model.Comment, error)
	GetCommentsFiltered(postID uint64, startFloor, limit uint32, creatorName string, creatorID model.PersonID) ([]*model.Comment, error)
	GetCommentCount(postID uint64) (int, error)
	GetCommentCountByCommunityFile(community, postFile string) (int, error)
	GetCommentsByCommunityFile(community, postFile string, startFloor, limit uint32) ([]*model.Comment, error)
	GetCommentsByCommunityFileFiltered(community, postFile string, startFloor, limit uint32, creatorName string, creatorID model.PersonID) ([]*model.Comment, error)
	PurgeUserComments(author string, maxAge time.Duration) (int, error)
	PurgeUserPosts(author string, maxAge time.Duration) (int, error)
	// ClearCommunity removes all posts of a community, which stays registered.
	ClearCommunity(community string) (int, int, error)
	// RenameCommunity changes the name of a community and returns its ID.
	RenameCommunity(oldName, newName string) (int64, error)
	// SoftDeleteCommunity marks a community as deleted (keeping its posts
	// under its ID) and returns its ID; the name becomes free.
	SoftDeleteCommunity(name string) (int64, error)
	// PurgeCommunity removes all posts of the community id and unregisters it.
	PurgeCommunity(id int64) (int, int, error)
	// MergeCommunity moves the posts of community from to community to,
	// except for those whose post file to already has, and returns the
	// numbers of posts moved and left behind.
	MergeCommunity(from, to string) (int, int, error)
	// MoveCommunity moves community name (with its posts, which keep their
	// IDs) to engine shard and returns the number of posts moved.
	MoveCommunity(name string, shard int) (int, error)
	// Communities returns the community registry.
	Communities() *CommunityRegistry
	PurgePost(postID uint64) error
	PurgePostByCommunityFile(community, postFile string) error
	VotePost(postID uint64, personName string, personID model.PersonID, newVote model.VoteType, createdAt int64, ip string) (int, int, error)
	GetVotes(postID uint64) ([]*model.VoteRecord, error)
	GetVotesByCommunityFile(community, postFile string) ([]*model.VoteRecord, error)
	ResetVotes(postID uint64) error
	ResetVotesByCommunityFile(community, postFile string) error
	SetPostContent(postID uint64, content string) error
	RenderFullPostText(postID uint64, asBig5 bool) ([]byte, error)
	RenderLegacyPostText(postID uint64, asBig5 bool) ([]byte, error)
	RenderCommunity(opts RenderCommunityOptions) (int, error)
	UpdatePost(postID uint64, newTitle, newContent, editorName string, editorID model.PersonID, expectedModified int64) (uint32, int64, error)
	UpdatePostByCommunityFile(community, postFile, newTitle, newContent, editorName string, editorID model.PersonID, expectedModified int64) (uint32, int64, error)
	UpdatePostTitle(postID uint64, newTitle, editorName string, editorID model.PersonID) (uint32, error)
	UpdatePostTitleByCommunityFile(community, postFile, newTitle, editorName string, editorID model.PersonID) (uint32, error)
	DeletePost(postID uint64, deleterName string, deleterID model.PersonID, reason string) (uint32, error)
	DeletePostByCommunityFile(community, postFile, deleterName string, deleterID model.PersonID, reason string) (uint32, error)
	UndeletePost(postID uint64) error
	UndeletePostByCommunityFile(community, postFile string) error
	UpdateComment(postID uint64, seq uint32, newContent, editorName string, editorID model.PersonID) (uint32, error)
	UpdateCommentByCommunityFile(community, postFile string, seq uint32, newContent, editorName string, editorID model.PersonID) (uint32, error)
	DeleteComment(postID uint64, seq uint32, deleterName string, deleterID model.PersonID, reason string) (uint32, error)
	DeleteCommentByCommunityFile(community, postFile string, seq uint32, deleterName string, deleterID model.PersonID, reason string) (uint32, error)
	UndeleteComment(postID uint64, seq uint32) error
	UndeleteCommentByCommunityFile(community, postFile string, seq uint32) error
	GetPostHistory(postID uint64, rev uint32) (*model.PostRevision, error)
	ListPostHistory(postID uint64) ([]*model.PostRevision, error)
	GetCommentHistory(postID uint64, seq uint32, rev uint32) (*model.CommentRevision, error)
	ListCommentHistory(postID uint64, seq uint32) ([]*model.CommentRevision, error)
	ImportPostBatch(batch []*ImportedPostData, renderTarget string, legacyFormat ...bool) error
	RebuildSQLiteFromPebble() (int, int, error)
	Backup(dest string) ([]string, error)
	Close() error
}
