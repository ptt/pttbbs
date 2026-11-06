package model

import (
	"bytes"
	"fmt"
	"sort"
	"strconv"
	"strings"
)

// Board attributes (boardheader_t.brdattr, BRD_* in include/pttstruct.h)
// that post.svc exposes as booleans.
const (
	BrdGroupBoard       uint32 = 0x00000008 // BRD_GROUPBOARD: a group of boards
	BrdHide             uint32 = 0x00000010 // BRD_HIDE: only members can see it
	BrdAnonymous        uint32 = 0x00000040 // BRD_ANONYMOUS
	BrdNoComments       uint32 = 0x00001000 // BRD_NO_COMMENTS
	BrdSymbolic         uint32 = 0x00008000 // BRD_SYMBOLIC: a link to another board
	BrdNoDownvote       uint32 = 0x00010000 // BRD_NO_DOWNVOTE
	BrdRestrictedPost   uint32 = 0x00040000 // BRD_RESTRICTEDPOST: only members can post
	BrdOver18           uint32 = 0x01000000 // BRD_OVER18
	BrdNoReply          uint32 = 0x02000000 // BRD_NOREPLY
	BrdCommentReadOnly  uint32 = 0x40000000 // BRD_COMMENT_READONLY
	BrdNoSelfDeletePost uint32 = 0x08000000 // BRD_NOSELFDELPOST
)

// Community is a board, named and shaped after the Lemmy community
// (https://join-lemmy.org/api/interfaces/Community.html) and filled from
// boardheader_t (include/pttstruct.h), which stays the source of truth
// (.BRD, written by mbbsd).
//
// The ID is owned by post.svc and never changes; Name may be changed by
// renaming the board. Flags keeps boardheader_t.brdattr as is, and the
// booleans after it (NSFW, Hidden, ...) are derived from it by Derive.
type Community struct {
	ID          int64  `json:"id"`
	Name        string `json:"name"`        // brdname
	Title       string `json:"title"`       // desc
	Description string `json:"description"` // longer description (not in .BRD yet)
	Published   int64  `json:"published"`   // when post.svc first saw it
	Updated     int64  `json:"updated,omitempty"`
	Removed     bool   `json:"removed"` // taken down by the site (not used yet)
	Deleted     bool   `json:"deleted"` // the board was deleted (soft delete)
	DeletedAt   int64  `json:"deleted_at,omitempty"`

	// Derived from Flags (Lemmy names where Lemmy has the concept).
	NSFW   bool `json:"nsfw"`   // BRD_OVER18
	Hidden bool `json:"hidden"` // BRD_HIDE

	// PTT-specific.
	Category                   string `json:"category"`                      // bclass, the category of the board
	Flags                      uint32 `json:"flags"`                         // brdattr, raw
	PostingRestrictedToMembers bool   `json:"posting_restricted_to_members"` // BRD_RESTRICTEDPOST
	IsGroup                    bool   `json:"is_group"`                      // BRD_GROUPBOARD
	Anonymous                  bool   `json:"anonymous"`                     // BRD_ANONYMOUS
	CommentsDisabled           bool   `json:"comments_disabled"`             // BRD_NO_COMMENTS
	DownvotesDisabled          bool   `json:"downvotes_disabled"`            // BRD_NO_DOWNVOTE
	RepliesDisabled            bool   `json:"replies_disabled"`              // BRD_NOREPLY
	Shard                      int    `json:"shard"`                         // storage shard, fixed at creation
	BID                        int    `json:"bid,omitempty"`                 // index (1-based) in .BRD
	GroupBID                   int    `json:"group_bid,omitempty"`           // gid: bid of the group board
	Group                      string `json:"group,omitempty"`               // name of the group board
	Level                      uint32 `json:"level,omitempty"`               // permission needed to read
	PostLimitLogins            int    `json:"post_limit_logins,omitempty"`
	PostLimitBadpost           int    `json:"post_limit_badpost,omitempty"`
	PetitionLimitLogins        int    `json:"petition_limit_logins,omitempty"`
	PetitionLimitBadpost       int    `json:"petition_limit_badpost,omitempty"`
	FastCommentPause           int    `json:"fastcomment_pause,omitempty"` // seconds between comments
	ModeratorNotes             string `json:"moderator_notes,omitempty"`

	Moderators []CommunityModerator `json:"moderators,omitempty"`
}

// CommunityModerator is a moderator of a community (Lemmy's
// community_moderator), from boardheader_t.moderators.
type CommunityModerator struct {
	CommunityID int64    `json:"community_id"`
	PersonID    PersonID `json:"person_id"`
	PersonName  string   `json:"person_name,omitempty"` // userid, if known
	Published   int64    `json:"published"`             // when post.svc first saw it
}

// Derive sets the booleans derived from Flags.
func (c *Community) Derive() {
	has := func(f uint32) bool { return c.Flags&f != 0 }
	c.NSFW = has(BrdOver18)
	c.Hidden = has(BrdHide)
	c.PostingRestrictedToMembers = has(BrdRestrictedPost)
	c.IsGroup = has(BrdGroupBoard)
	c.Anonymous = has(BrdAnonymous)
	c.CommentsDisabled = has(BrdNoComments)
	c.DownvotesDisabled = has(BrdNoDownvote)
	c.RepliesDisabled = has(BrdNoReply)
}

// rfc822Value keeps a header value on one line.
func rfc822Value(s string) string {
	return strings.NewReplacer("\r", " ", "\n", " ").Replace(s)
}

// EncodeRFC822Community encodes the fields that mbbsd mirrors (and the
// ones post.svc owns) as RFC 822 headers, one "Moderator: <person_id>
// <name>" per moderator, followed by the description as the body. It is
// the payload of UPSERT_COMMUNITY and of the reply of GET_COMMUNITY.
func EncodeRFC822Community(c *Community) []byte {
	var b strings.Builder
	b.WriteString("Encoding: utf-8\n")
	w := func(k string, v any) { fmt.Fprintf(&b, "%s: %v\n", k, v) }
	if c.ID != 0 {
		w("ID", c.ID)
	}
	w("Name", rfc822Value(c.Name))
	w("Title", rfc822Value(c.Title))
	w("Category", rfc822Value(c.Category))
	w("Flags", c.Flags)
	w("BID", c.BID)
	w("GroupBID", c.GroupBID)
	if c.Group != "" {
		w("Group", rfc822Value(c.Group))
	}
	w("Level", c.Level)
	w("PostLimitLogins", c.PostLimitLogins)
	w("PostLimitBadpost", c.PostLimitBadpost)
	w("PetitionLimitLogins", c.PetitionLimitLogins)
	w("PetitionLimitBadpost", c.PetitionLimitBadpost)
	w("FastCommentPause", c.FastCommentPause)
	if c.ModeratorNotes != "" {
		w("ModeratorNotes", rfc822Value(c.ModeratorNotes))
	}
	if c.Shard != 0 {
		w("Shard", c.Shard)
	}
	if c.Published != 0 {
		w("Published", c.Published)
	}
	if c.Updated != 0 {
		w("Updated", c.Updated)
	}
	if c.Deleted {
		w("Deleted", "true")
		w("DeletedAt", c.DeletedAt)
	}
	if c.Removed {
		w("Removed", "true")
	}
	for _, m := range c.Moderators {
		if m.PersonName != "" {
			fmt.Fprintf(&b, "Moderator: %d %s\n", m.PersonID, rfc822Value(m.PersonName))
		} else {
			fmt.Fprintf(&b, "Moderator: %d\n", m.PersonID)
		}
	}
	b.WriteString("\n")
	b.WriteString(c.Description)
	return []byte(b.String())
}

// DecodeRFC822Community decodes what EncodeRFC822Community encodes. If
// decode is not nil, it converts the text values (which mbbsd may send in
// Big5) to UTF-8. Unknown headers are ignored.
func DecodeRFC822Community(val []byte, decode func(string) string) (*Community, error) {
	if decode == nil {
		decode = func(s string) string { return s }
	}
	header, body := val, []byte(nil)
	if idx := bytes.Index(val, []byte("\n\n")); idx >= 0 {
		header, body = val[:idx], val[idx+2:]
	}
	c := &Community{Description: decode(string(body))}
	atoi := func(v string) int { n, _ := strconv.Atoi(v); return n }
	for _, l := range strings.Split(string(header), "\n") {
		k, v, ok := strings.Cut(strings.TrimRight(l, "\r"), ":")
		if !ok {
			continue
		}
		v = strings.TrimPrefix(v, " ")
		switch k {
		case "ID":
			c.ID, _ = strconv.ParseInt(v, 10, 64)
		case "Name":
			c.Name = strings.TrimSpace(v)
		case "Title":
			c.Title = decode(v)
		case "Category":
			c.Category = decode(v)
		case "Flags":
			f, _ := strconv.ParseUint(v, 10, 32)
			c.Flags = uint32(f)
		case "BID":
			c.BID = atoi(v)
		case "GroupBID":
			c.GroupBID = atoi(v)
		case "Group":
			c.Group = strings.TrimSpace(v)
		case "Level":
			l, _ := strconv.ParseUint(v, 10, 32)
			c.Level = uint32(l)
		case "PostLimitLogins":
			c.PostLimitLogins = atoi(v)
		case "PostLimitBadpost":
			c.PostLimitBadpost = atoi(v)
		case "PetitionLimitLogins":
			c.PetitionLimitLogins = atoi(v)
		case "PetitionLimitBadpost":
			c.PetitionLimitBadpost = atoi(v)
		case "FastCommentPause":
			c.FastCommentPause = atoi(v)
		case "ModeratorNotes":
			c.ModeratorNotes = decode(v)
		case "Shard":
			c.Shard = atoi(v)
		case "Published":
			c.Published, _ = strconv.ParseInt(v, 10, 64)
		case "Updated":
			c.Updated, _ = strconv.ParseInt(v, 10, 64)
		case "Deleted":
			c.Deleted = v == "true"
		case "DeletedAt":
			c.DeletedAt, _ = strconv.ParseInt(v, 10, 64)
		case "Removed":
			c.Removed = v == "true"
		case "Moderator":
			id, name, _ := strings.Cut(v, " ")
			pid := ParsePersonID(id)
			if pid == 0 {
				return nil, fmt.Errorf("invalid moderator: %q", v)
			}
			c.Moderators = append(c.Moderators, CommunityModerator{PersonID: pid, PersonName: strings.TrimSpace(name)})
		}
	}
	if c.Name == "" {
		return nil, fmt.Errorf("community name required")
	}
	c.Derive()
	return c, nil
}

// SortModerators orders moderators by person ID, so that sets of them can
// be compared.
func SortModerators(mods []CommunityModerator) {
	sort.Slice(mods, func(i, j int) bool { return mods[i].PersonID < mods[j].PersonID })
}
