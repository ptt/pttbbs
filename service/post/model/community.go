package model

// Community is a board, named and shaped after the Lemmy community
// (https://join-lemmy.org/api/interfaces/Community.html).
//
// The ID is owned by post.svc and never changes; Name may be changed by
// renaming the board.
type Community struct {
	ID        int64  `json:"id"`
	Name      string `json:"name"`      // brdname
	Published int64  `json:"published"` // when post.svc first saw it
	Updated   int64  `json:"updated,omitempty"`
	Deleted   bool   `json:"deleted"` // the board was deleted (soft delete)
	DeletedAt int64  `json:"deleted_at,omitempty"`

	// PTT-specific.
	Shard int `json:"shard"` // storage shard, fixed at creation
}
