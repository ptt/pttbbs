package storage

import (
	"errors"
	"fmt"
	"strings"

	"pttbbs/nest/model"
)

// Comment IDs.
//
// A comment ID is its post ID and floor (model.MakeCommentID), as PTT (and
// Discourse) knows a comment by its post and floor: it needs no sequence or
// index of its own, never changes, and is not used again, as post IDs are
// not and floors only grow. Comments are keyed by (post ID, floor) in
// comments.pebble, so that the comments of a post are read in one range
// scan.

// ErrCommentNotFound is returned for an unknown comment ID.
var ErrCommentNotFound = errors.New("comment not found")

// ErrTooManyComments is returned when a post has no floor left.
var ErrTooManyComments = fmt.Errorf("a post has at most %d floors", model.MaxFloor)

// commentAt returns the comment at (postID, seq), or nil.
func (e *Engine) commentAt(postID uint64, seq uint32) *model.Comment {
	val, closer, err := e.commentsDB.Get(model.EncodeCommentKey(postID, seq))
	if err != nil {
		return nil
	}
	defer closer.Close()
	c, err := model.DecodeRFC822Comment(postID, seq, val)
	if err != nil {
		return nil
	}
	return c
}

// GetComment returns comment commentID (with its post ID and floor).
func (e *Engine) GetComment(commentID uint64) (*model.Comment, error) {
	postID, seq, ok := model.SplitCommentID(commentID)
	if !ok {
		return nil, fmt.Errorf("%w: %d", ErrCommentNotFound, commentID)
	}
	c := e.commentAt(postID, seq)
	if c == nil {
		return nil, fmt.Errorf("%w: %d", ErrCommentNotFound, commentID)
	}
	return c, nil
}

// checkPostID checks that a new post ID is within int32 (model.MaxPostID).
func checkPostID(id uint64) error {
	if id == 0 || id > model.MaxPostID {
		return fmt.Errorf("post ID %d out of range (1..%d)", id, model.MaxPostID)
	}
	return nil
}

// foldComments keeps the comments of an imported post within MaxFloor
// floors: the comments beyond the last floor are appended to it, as
// consecutive comments are merged into a floor, so that none are lost.
func foldComments(comments []*model.Comment) []*model.Comment {
	if len(comments) <= model.MaxFloor {
		return comments
	}
	last := comments[model.MaxFloor-1]
	for _, c := range comments[model.MaxFloor:] {
		last.Content = strings.TrimRight(last.Content, "\r\n") + "\n" + c.CreatorName + ": " + c.Content
		if c.CreatedAt > last.LastCommentAt {
			last.LastCommentAt = c.CreatedAt
		}
	}
	return comments[:model.MaxFloor]
}
