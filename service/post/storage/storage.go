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
	CountPosts(community string) (int, error)
	ListPosts(community string, limit, offset int) ([]*model.Post, error)
	ListDeletedPosts(community string, limit, offset int) ([]*model.Post, error)
	GetReplies(parentID uint64, limit, offset int) ([]*model.Post, error)
	GetThread(rootID uint64) ([]*model.Post, error)
	AddCrosspost(srcCommunity, srcPostFile, targetCommunity, targetPostFile, operator string, operatorToken uint32, createdAt int64) (*model.CrosspostRecord, int, error)
	GetCrossposts(community, postFile string) ([]*model.CrosspostRecord, error)
	GetOriginPost(targetCommunity, targetPostFile string) (*model.Post, *model.CrosspostRecord, error)
	AddComment(c *model.Comment) (*model.Comment, error)
	GetComments(postID uint64, startFloor, limit uint32) ([]*model.Comment, error)
	GetCommentCount(postID uint64) (int, error)
	GetCommentCountByCommunityFile(community, postFile string) (int, error)
	GetCommentsByCommunityFile(community, postFile string, startFloor, limit uint32) ([]*model.Comment, error)
	PurgeUserComments(author string, maxAge time.Duration) (int, error)
	PurgeUserPosts(author string, maxAge time.Duration) (int, error)
	DeleteCommunity(community string) (int, int, error)
	PurgePost(postID uint64) error
	PurgePostByCommunityFile(community, postFile string) error
	VotePost(postID uint64, user string, authorToken uint32, newVote model.VoteType) (int, int, error)
	SetPostContent(postID uint64, content string) error
	RenderFullPostText(postID uint64, asBig5 bool) ([]byte, error)
	RenderLegacyPostText(postID uint64, asBig5 bool) ([]byte, error)
	RenderCommunity(opts RenderCommunityOptions) (int, error)
	UpdatePost(postID uint64, newTitle, newContent, editor string, expectedModified int64) (uint32, int64, error)
	UpdatePostByCommunityFile(community, postFile, newTitle, newContent, editor string, expectedModified int64) (uint32, int64, error)
	UpdatePostTitle(postID uint64, newTitle, editor string) (uint32, error)
	UpdatePostTitleByCommunityFile(community, postFile, newTitle, editor string) (uint32, error)
	DeletePost(postID uint64, deleter, reason string) (uint32, error)
	DeletePostByCommunityFile(community, postFile, deleter, reason string) (uint32, error)
	UndeletePost(postID uint64) error
	UndeletePostByCommunityFile(community, postFile string) error
	UpdateComment(postID uint64, seq uint32, newContent, editor string) (uint32, error)
	UpdateCommentByCommunityFile(community, postFile string, seq uint32, newContent, editor string) (uint32, error)
	DeleteComment(postID uint64, seq uint32, deleter, reason string) (uint32, error)
	DeleteCommentByCommunityFile(community, postFile string, seq uint32, deleter, reason string) (uint32, error)
	UndeleteComment(postID uint64, seq uint32) error
	UndeleteCommentByCommunityFile(community, postFile string, seq uint32) error
	GetPostHistory(postID uint64, rev uint32) (*model.PostRevision, error)
	ListPostHistory(postID uint64) ([]*model.PostRevision, error)
	GetCommentHistory(postID uint64, seq uint32, rev uint32) (*model.CommentRevision, error)
	ListCommentHistory(postID uint64, seq uint32) ([]*model.CommentRevision, error)
	ImportPostBatch(batch []*ImportedPostData, renderTarget string, legacyFormat ...bool) error
	RebuildSQLiteFromPebble() (int, int, error)
	Close() error
}
