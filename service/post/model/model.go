package model

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"regexp"
	"strconv"
	"strings"
	"time"
)

var ansiRegex = regexp.MustCompile(`\x1b(\[[0-9;?]*[a-zA-Z]|].*?(\x07|\x1b\\)|[ -/]*[@-~])|\x1b`)

// StripANSI strips all ANSI and terminal escape sequences from a string
func StripANSI(s string) string {
	return ansiRegex.ReplaceAllString(s, "")
}

// VoteType represents Reddit-style upvoting/downvoting
type VoteType int8

const (
	VoteNeutral VoteType = 0  // No vote / canceled
	VoteUp      VoteType = 1  // Upvote
	VoteDown    VoteType = -1 // Downvote
)

func (v VoteType) String() string {
	switch v {
	case VoteUp:
		return "upvote"
	case VoteDown:
		return "downvote"
	default:
		return "neutral"
	}
}

// Post represents a submission in a Community
type Post struct {
	ID            uint64   `json:"id"`
	ParentID      uint64   `json:"parent_id"`    // 0: Root post, >0: Reply to ParentID (回文)
	Community     string   `json:"community"`    // Formerly Board
	PostFile      string   `json:"post_file"`    // Legacy filename (e.g. M.1417074209.A.393)
	Title         string   `json:"title"`        // UTF-8
	CreatorName   string   `json:"creator_name"` // shown name of the OP (a nickname for anonymous posts)
	CreatorID     PersonID `json:"creator_id"`   // who the OP really is
	CreatedAt     int64    `json:"created_at"`
	Modified      int64    `json:"modified"`
	Filemode      int      `json:"filemode"`
	Money         int      `json:"money,omitempty"` // fileheader_t.multi.money
	Upvotes       int      `json:"upvotes"`         // Upvotes count
	Downvotes     int      `json:"downvotes"`       // Downvotes count
	NumComments   int      `json:"num_comments"`    // Total comments count
	NumCrossposts int      `json:"num_crossposts"`  // Total crossposts count (shares)
	Encoding      string   `json:"encoding"`        // Always "utf-8" in DB
	Deletion
	Content          string `json:"content"`                      // UTF-8 Post Body
	DemotedToComment bool   `json:"demoted_to_comment,omitempty"` // True if content was too short and converted to comment
}

// CrosspostRecord represents a cross-post to another community (share / 轉錄)
type CrosspostRecord struct {
	ID              uint64   `json:"id"`
	SourcePostID    uint64   `json:"source_post_id"`
	TargetCommunity string   `json:"target_community"`
	TargetPostFile  string   `json:"target_post_file"`
	CreatorName     string   `json:"creator_name"`
	CreatorID       PersonID `json:"creator_id"`
	CreatedAt       int64    `json:"created_at"`
}

// NetScore calculates upvotes - downvotes
func (p *Post) NetScore() int {
	return p.Upvotes - p.Downvotes
}

// ExtractNetNewContent extracts genuine user-written content from a post body,
// stripping standard PTT headers (作者:, 標題:), quote lines (: , > ), and signatures (-- , ※).
func ExtractNetNewContent(body string) string {
	lines := strings.Split(body, "\n")
	var netLines []string

	for _, rawLine := range lines {
		line := strings.TrimSpace(rawLine)
		if line == "" {
			continue
		}
		// Strip quotes
		if strings.HasPrefix(line, ":") || strings.HasPrefix(line, ">") || strings.HasPrefix(line, "：") {
			continue
		}
		// Strip standard BBS headers
		if strings.HasPrefix(line, "作者:") || strings.HasPrefix(line, "標題:") ||
			strings.HasPrefix(line, "時間:") || strings.HasPrefix(line, "看板:") {
			continue
		}
		// Strip signatures
		if strings.HasPrefix(line, "--") || strings.HasPrefix(line, "※ 發信站:") ||
			strings.HasPrefix(line, "※ 文章網址:") || strings.HasPrefix(line, "※ 編輯:") {
			continue
		}
		netLines = append(netLines, line)
	}
	return strings.Join(netLines, "\n")
}

// Legacy push types for backward compatibility and fallback rendering
const (
	LegacyTypeOldRecommend uint8 = 0 // PTT2 OLD_RECOMMEND: →  author: msg 推 date
	LegacyTypePush         uint8 = 1 // PTT1 推
	LegacyTypeBoo          uint8 = 2 // PTT1 噓
	LegacyTypeArrow        uint8 = 3 // PTT1 箭頭 (→)
)

// MaxCommentLines defines the maximum number of lines allowed per comment (consistent with COMMENT_LINES in mbbsd)
const MaxCommentLines = 5

// CountCommentLines returns the number of non-empty content lines in a comment
func CountCommentLines(content string) int {
	rawLines := strings.Split(content, "\n")
	cnt := 0
	for _, l := range rawLines {
		trimmed := strings.TrimRight(l, "\r ")
		if trimmed != "" {
			cnt++
		}
	}
	if cnt == 0 && strings.TrimSpace(content) != "" {
		return 1
	}
	return cnt
}

// TruncateCommentLines limits the comment to at most maxLines non-empty lines
func TruncateCommentLines(content string, maxLines int) string {
	if maxLines <= 0 {
		return content
	}
	rawLines := strings.Split(content, "\n")
	var validLines []string
	for _, l := range rawLines {
		trimmed := strings.TrimRight(l, "\r ")
		if trimmed != "" {
			validLines = append(validLines, trimmed)
			if len(validLines) >= maxLines {
				break
			}
		}
	}
	return strings.Join(validLines, "\n")
}

// Comment represents a reply / discussion item (decoupled from votes)
type Comment struct {
	PostID        uint64   `json:"post_id"`
	Sequence      uint32   `json:"sequence"` // 1-indexed sequential position
	CreatorName   string   `json:"creator_name"`
	CreatorID     PersonID `json:"creator_id"`
	IP            string   `json:"ip"`
	CreatedAt     int64    `json:"created_at"`
	ModifiedAt    int64    `json:"modified_at,omitempty"`
	LastCommentAt int64    `json:"last_comment_at,omitempty"`
	Deletion
	LegacyType uint8  `json:"legacy_type"` // 0: OLD_RECOMMEND, 1: 推, 2: 噓, 3: 箭頭
	Content    string `json:"content"`     // UTF-8 text
	IsMerged   bool   `json:"-"`           // Transient flag indicating comment was merged with previous
}

// VoteRecord represents a user's vote on a post (1 vote per user per post)
type VoteRecord struct {
	PostID     uint64   `json:"post_id"`
	PersonName string   `json:"person_name"` // not in Lemmy, which joins person
	PersonID   PersonID `json:"person_id"`
	Vote       VoteType `json:"vote"` // 1 (Upvote), -1 (Downvote)
	CreatedAt  int64    `json:"created_at"`
	IP         string   `json:"ip,omitempty"`
}

// PostRevision represents an archived previous version of an edited post
type PostRevision struct {
	PostID      uint64   `json:"post_id"`
	Revision    uint32   `json:"revision"`
	Community   string   `json:"community"`
	PostFile    string   `json:"post_file"`
	Title       string   `json:"title"`
	CreatorName string   `json:"creator_name"`
	CreatorID   PersonID `json:"creator_id"`
	CreatedAt   int64    `json:"created_at"`
	EditedAt    int64    `json:"edited_at"`
	EditorName  string   `json:"editor_name"` // who made this revision; Lemmy keeps no revisions
	EditorID    PersonID `json:"editor_id"`
	Action      string   `json:"action,omitempty"` // "update" or "delete"
	Reason      string   `json:"reason,omitempty"`
	Content     string   `json:"content"`
	Encoding    string   `json:"encoding"`
}

// CommentRevision represents an archived previous version of an edited comment
type CommentRevision struct {
	PostID      uint64   `json:"post_id"`
	Sequence    uint32   `json:"sequence"`
	Revision    uint32   `json:"revision"`
	CreatorName string   `json:"creator_name"`
	CreatorID   PersonID `json:"creator_id"`
	CreatedAt   int64    `json:"created_at"`
	EditedAt    int64    `json:"edited_at"`
	EditorName  string   `json:"editor_name"` // who made this revision; Lemmy keeps no revisions
	EditorID    PersonID `json:"editor_id"`
	IP          string   `json:"ip"`
	LegacyType  uint8    `json:"legacy_type"`
	Action      string   `json:"action,omitempty"` // "update" or "delete"
	Reason      string   `json:"reason,omitempty"`
	Content     string   `json:"content"`
	Encoding    string   `json:"encoding"`
}

// ConflictError indicates an optimistic concurrency check failure when editing a post
type ConflictError struct {
	LatestModified int64
}

func (e *ConflictError) Error() string {
	return fmt.Sprintf("conflict: post modified at %d", e.LatestModified)
}

// -----------------------------------------------------------------------------
// Pebble Key Encoders
// -----------------------------------------------------------------------------

// Post Key: 'p' (1 byte) + 8-byte uint64 PostID = 9 bytes
func EncodePostKey(postID uint64) []byte {
	k := make([]byte, 9)
	k[0] = 'p'
	binary.BigEndian.PutUint64(k[1:9], postID)
	return k
}

func DecodePostKey(k []byte) (uint64, error) {
	if len(k) != 9 || k[0] != 'p' {
		return 0, fmt.Errorf("invalid post key: %x", k)
	}
	return binary.BigEndian.Uint64(k[1:9]), nil
}

// Post History Key: 'p' (1 byte) + 8-byte uint64 PostID + 'v' (1 byte) + 4-byte uint32 Revision = 14 bytes
// Example: p0001 => p0001v0001
func EncodePostHistoryKey(postID uint64, rev uint32) []byte {
	k := make([]byte, 14)
	k[0] = 'p'
	binary.BigEndian.PutUint64(k[1:9], postID)
	k[9] = 'v'
	binary.BigEndian.PutUint32(k[10:14], rev)
	return k
}

func DecodePostHistoryKey(k []byte) (postID uint64, rev uint32, err error) {
	if len(k) != 14 || k[0] != 'p' || k[9] != 'v' {
		return 0, 0, fmt.Errorf("invalid post history key: %x", k)
	}
	postID = binary.BigEndian.Uint64(k[1:9])
	rev = binary.BigEndian.Uint32(k[10:14])
	return postID, rev, nil
}

func EncodePostHistoryPrefix(postID uint64) []byte {
	k := make([]byte, 10)
	k[0] = 'p'
	binary.BigEndian.PutUint64(k[1:9], postID)
	k[9] = 'v'
	return k
}

// Comment Primary Key: 'c' (1 byte) + 8-byte PostID + 4-byte Sequence = 13 bytes
func EncodeCommentKey(postID uint64, seq uint32) []byte {
	k := make([]byte, 13)
	k[0] = 'c'
	binary.BigEndian.PutUint64(k[1:9], postID)
	binary.BigEndian.PutUint32(k[9:13], seq)
	return k
}

func DecodeCommentKey(k []byte) (postID uint64, seq uint32, err error) {
	if len(k) != 13 || k[0] != 'c' {
		return 0, 0, fmt.Errorf("invalid comment key: %x", k)
	}
	postID = binary.BigEndian.Uint64(k[1:9])
	seq = binary.BigEndian.Uint32(k[9:13])
	return postID, seq, nil
}

func EncodeCommentPrefix(postID uint64) []byte {
	k := make([]byte, 9)
	k[0] = 'c'
	binary.BigEndian.PutUint64(k[1:9], postID)
	return k
}

// Comment History Key: 'c' (1 byte) + 8-byte uint64 PostID + 4-byte uint32 Sequence + 'v' (1 byte) + 4-byte uint32 Revision = 18 bytes
func EncodeCommentHistoryKey(postID uint64, seq uint32, rev uint32) []byte {
	k := make([]byte, 18)
	k[0] = 'c'
	binary.BigEndian.PutUint64(k[1:9], postID)
	binary.BigEndian.PutUint32(k[9:13], seq)
	k[13] = 'v'
	binary.BigEndian.PutUint32(k[14:18], rev)
	return k
}

func DecodeCommentHistoryKey(k []byte) (postID uint64, seq uint32, rev uint32, err error) {
	if len(k) != 18 || k[0] != 'c' || k[13] != 'v' {
		return 0, 0, 0, fmt.Errorf("invalid comment history key: %x", k)
	}
	postID = binary.BigEndian.Uint64(k[1:9])
	seq = binary.BigEndian.Uint32(k[9:13])
	rev = binary.BigEndian.Uint32(k[14:18])
	return postID, seq, rev, nil
}

func EncodeCommentHistoryPrefix(postID uint64, seq uint32) []byte {
	k := make([]byte, 14)
	k[0] = 'c'
	binary.BigEndian.PutUint64(k[1:9], postID)
	binary.BigEndian.PutUint32(k[9:13], seq)
	k[13] = 'v'
	return k
}

// Comment Author Secondary Index Key:
// 'u' (1 byte) + author (16 bytes padded) + created_at (8 bytes) + post_id (8 bytes) + sequence (4 bytes) = 37 bytes
// Used to query / delete all comments by an author within a time range (anti-spam)
func EncodeAuthorCommentKey(author string, createdAt int64, postID uint64, seq uint32) []byte {
	k := make([]byte, 37)
	k[0] = 'u'
	copy(k[1:17], []byte(author))
	binary.BigEndian.PutUint64(k[17:25], uint64(createdAt))
	binary.BigEndian.PutUint64(k[25:33], postID)
	binary.BigEndian.PutUint32(k[33:37], seq)
	return k
}

func DecodeAuthorCommentKey(k []byte) (author string, createdAt int64, postID uint64, seq uint32, err error) {
	if len(k) != 37 || k[0] != 'u' {
		return "", 0, 0, 0, fmt.Errorf("invalid author comment key: %x", k)
	}
	author = strings.TrimRight(string(k[1:17]), "\x00")
	createdAt = int64(binary.BigEndian.Uint64(k[17:25]))
	postID = binary.BigEndian.Uint64(k[25:33])
	seq = binary.BigEndian.Uint32(k[33:37])
	return author, createdAt, postID, seq, nil
}

func EncodeAuthorCommentPrefix(author string) []byte {
	k := make([]byte, 17)
	k[0] = 'u'
	copy(k[1:17], []byte(author))
	return k
}

// Vote Key: 'v' (1 byte) + 8-byte PostID + user string
func EncodeVoteKey(postID uint64, user string) []byte {
	var buf bytes.Buffer
	buf.WriteByte('v')
	var idBytes [8]byte
	binary.BigEndian.PutUint64(idBytes[:], postID)
	buf.Write(idBytes[:])
	buf.WriteString(user)
	return buf.Bytes()
}

func EncodeVotePrefix(postID uint64) []byte {
	k := make([]byte, 9)
	k[0] = 'v'
	binary.BigEndian.PutUint64(k[1:9], postID)
	return k
}

func DecodeVoteKey(k []byte) (postID uint64, user string, err error) {
	if len(k) < 10 || k[0] != 'v' {
		return 0, "", fmt.Errorf("invalid vote key")
	}
	postID = binary.BigEndian.Uint64(k[1:9])
	user = string(k[9:])
	return postID, user, nil
}

// EncodeVoteValue encodes a vote record value:
// Byte 0: 'U' (Upvote) or 'D' (Downvote)
// Bytes 1..8: PersonID of the voter (uint64, BigEndian)
// Bytes 9..16: CreatedAt (int64, BigEndian)
// Bytes 17..: IP (UTF-8 string)
func EncodeVoteValue(vote VoteType, personID PersonID, createdAt int64, ip string) []byte {
	ipBytes := []byte(ip)
	buf := make([]byte, 17+len(ipBytes))
	if vote == VoteUp {
		buf[0] = 'U'
	} else if vote == VoteDown {
		buf[0] = 'D'
	} else {
		buf[0] = '0'
	}
	binary.BigEndian.PutUint64(buf[1:9], uint64(personID))
	binary.BigEndian.PutUint64(buf[9:17], uint64(createdAt))
	copy(buf[17:], ipBytes)
	return buf
}

func DecodeVoteValue(val []byte) (vote VoteType, personID PersonID, createdAt int64, ip string, err error) {
	if len(val) == 0 {
		return VoteNeutral, 0, 0, "", fmt.Errorf("empty vote value")
	}
	if val[0] == 'U' {
		vote = VoteUp
	} else if val[0] == 'D' {
		vote = VoteDown
	} else {
		vote = VoteNeutral
	}
	if len(val) >= 9 {
		personID = PersonID(binary.BigEndian.Uint64(val[1:9]))
	}
	if len(val) >= 17 {
		createdAt = int64(binary.BigEndian.Uint64(val[9:17]))
		if len(val) > 17 {
			ip = string(val[17:])
		}
	}
	return vote, personID, createdAt, ip, nil
}

// Crosspost Key: 'x' (1 byte) + 8-byte uint64 SourcePostID + 4-byte uint32 Seq = 13 bytes
func EncodeCrosspostKey(sourcePostID uint64, seq uint32) []byte {
	k := make([]byte, 13)
	k[0] = 'x'
	binary.BigEndian.PutUint64(k[1:9], sourcePostID)
	binary.BigEndian.PutUint32(k[9:13], seq)
	return k
}

func DecodeCrosspostKey(k []byte) (sourcePostID uint64, seq uint32, err error) {
	if len(k) != 13 || k[0] != 'x' {
		return 0, 0, fmt.Errorf("invalid crosspost key: %x", k)
	}
	sourcePostID = binary.BigEndian.Uint64(k[1:9])
	seq = binary.BigEndian.Uint32(k[9:13])
	return sourcePostID, seq, nil
}

func EncodeCrosspostPrefix(sourcePostID uint64) []byte {
	k := make([]byte, 9)
	k[0] = 'x'
	binary.BigEndian.PutUint64(k[1:9], sourcePostID)
	return k
}

// -----------------------------------------------------------------------------
// RFC 822 Encoders and Decoders
// -----------------------------------------------------------------------------

// EncodeRFC822Post packages the post into RFC 822 format:
// Encoding MUST be the very first header!
func EncodeRFC822Post(p *Post) []byte {
	var b strings.Builder
	b.WriteString("Encoding: utf-8\n")
	b.WriteString(fmt.Sprintf("Community: %s\n", p.Community))
	b.WriteString(fmt.Sprintf("ParentID: %d\n", p.ParentID))
	b.WriteString(fmt.Sprintf("PostFile: %s\n", p.PostFile))
	b.WriteString(fmt.Sprintf("Title: %s\n", p.Title))
	b.WriteString(fmt.Sprintf("CreatorName: %s\n", p.CreatorName))
	b.WriteString(fmt.Sprintf("CreatorID: %d\n", p.CreatorID))
	b.WriteString(fmt.Sprintf("CreatedAt: %d\n", p.CreatedAt))
	m := p.Modified
	if m == 0 {
		m = p.CreatedAt
	}
	b.WriteString(fmt.Sprintf("Modified: %d\n", m))
	b.WriteString(fmt.Sprintf("Filemode: %d\n", p.Filemode))
	if p.Money != 0 {
		b.WriteString(fmt.Sprintf("Money: %d\n", p.Money))
	}
	p.Deletion.writeRFC822(&b)
	b.WriteString("\n")
	b.WriteString(p.Content)
	return []byte(b.String())
}

func DecodeRFC822Post(val []byte) (meta map[string]string, body string) {
	meta = make(map[string]string)
	idx := bytes.Index(val, []byte("\n\n"))
	if idx == -1 {
		return meta, string(val)
	}
	headerText := string(val[:idx])
	body = string(val[idx+2:])
	lines := strings.Split(headerText, "\n")
	for _, l := range lines {
		if k, v, ok := strings.Cut(l, ": "); ok {
			meta[k] = v
		}
	}
	return meta, body
}

// EncodeRFC822Comment packages a comment into RFC 822 format
// Encoding is the first header!
func EncodeRFC822Comment(c *Comment) []byte {
	var b strings.Builder
	b.WriteString("Encoding: utf-8\n")
	b.WriteString(fmt.Sprintf("CreatorName: %s\n", c.CreatorName))
	b.WriteString(fmt.Sprintf("CreatorID: %d\n", c.CreatorID))
	b.WriteString(fmt.Sprintf("CreatedAt: %d\n", c.CreatedAt))
	if c.ModifiedAt > 0 {
		b.WriteString(fmt.Sprintf("ModifiedAt: %d\n", c.ModifiedAt))
	}
	if c.LastCommentAt > 0 {
		b.WriteString(fmt.Sprintf("LastCommentAt: %d\n", c.LastCommentAt))
	}
	b.WriteString(fmt.Sprintf("IP: %s\n", c.IP))
	b.WriteString(fmt.Sprintf("LegacyType: %d\n", c.LegacyType))
	c.Deletion.writeRFC822(&b)
	b.WriteString("\n")
	b.WriteString(c.Content)
	return []byte(b.String())
}

func DecodeRFC822Comment(postID uint64, seq uint32, val []byte) (*Comment, error) {
	idx := bytes.Index(val, []byte("\n\n"))
	if idx == -1 {
		return nil, fmt.Errorf("invalid comment format")
	}
	headerText := string(val[:idx])
	content := string(val[idx+2:])

	c := &Comment{
		PostID:   postID,
		Sequence: seq,
		Content:  content,
	}

	lines := strings.Split(headerText, "\n")
	for _, l := range lines {
		if k, v, ok := strings.Cut(l, ": "); ok {
			switch k {
			case "CreatorName":
				c.CreatorName = v
			case "CreatorID":
				c.CreatorID = ParsePersonID(v)
			case "CreatedAt":
				c.CreatedAt, _ = strconv.ParseInt(v, 10, 64)
			case "ModifiedAt":
				c.ModifiedAt, _ = strconv.ParseInt(v, 10, 64)
			case "LastCommentAt":
				c.LastCommentAt, _ = strconv.ParseInt(v, 10, 64)
			case "IP":
				c.IP = v
			case "LegacyType":
				val, _ := strconv.ParseUint(v, 10, 8)
				c.LegacyType = uint8(val)
			case "Deleted":
				c.Deleted = v == "true"
			case "Removed":
				c.Removed = v == "true"
			case "DeletedAt":
				c.DeletedAt, _ = strconv.ParseInt(v, 10, 64)
			case "ModPersonName":
				c.ModPersonName = v
			case "ModPersonID":
				c.ModPersonID = ParsePersonID(v)
			case "Reason":
				c.Reason = v
			}
		}
	}
	return c, nil
}

// EncodeRFC822PostRevision encodes a post revision into RFC 822 text
func EncodeRFC822PostRevision(r *PostRevision) []byte {
	var b strings.Builder
	b.WriteString("Encoding: utf-8\n")
	b.WriteString(fmt.Sprintf("PostID: %d\n", r.PostID))
	b.WriteString(fmt.Sprintf("Revision: %d\n", r.Revision))
	b.WriteString(fmt.Sprintf("Community: %s\n", r.Community))
	b.WriteString(fmt.Sprintf("PostFile: %s\n", r.PostFile))
	b.WriteString(fmt.Sprintf("Title: %s\n", r.Title))
	b.WriteString(fmt.Sprintf("CreatorName: %s\n", r.CreatorName))
	b.WriteString(fmt.Sprintf("CreatorID: %d\n", r.CreatorID))
	b.WriteString(fmt.Sprintf("CreatedAt: %d\n", r.CreatedAt))
	b.WriteString(fmt.Sprintf("EditedAt: %d\n", r.EditedAt))
	b.WriteString(fmt.Sprintf("EditorName: %s\n", r.EditorName))
	b.WriteString(fmt.Sprintf("EditorID: %d\n", r.EditorID))
	if r.Action != "" {
		b.WriteString(fmt.Sprintf("Action: %s\n", r.Action))
	}
	if r.Reason != "" {
		b.WriteString(fmt.Sprintf("Reason: %s\n", r.Reason))
	}
	b.WriteString("\n")
	b.WriteString(r.Content)
	return []byte(b.String())
}

func DecodeRFC822PostRevision(val []byte) (*PostRevision, error) {
	idx := bytes.Index(val, []byte("\n\n"))
	if idx == -1 {
		return nil, fmt.Errorf("invalid post revision format")
	}
	headerText := string(val[:idx])
	content := string(val[idx+2:])

	r := &PostRevision{
		Content: content,
	}

	lines := strings.Split(headerText, "\n")
	for _, l := range lines {
		if k, v, ok := strings.Cut(l, ": "); ok {
			switch k {
			case "PostID":
				val, _ := strconv.ParseUint(v, 10, 64)
				r.PostID = val
			case "Revision":
				val, _ := strconv.ParseUint(v, 10, 32)
				r.Revision = uint32(val)
			case "Community":
				r.Community = v
			case "PostFile":
				r.PostFile = v
			case "Title":
				r.Title = v
			case "CreatorName":
				r.CreatorName = v
			case "CreatorID":
				r.CreatorID = ParsePersonID(v)
			case "CreatedAt":
				r.CreatedAt, _ = strconv.ParseInt(v, 10, 64)
			case "EditedAt":
				r.EditedAt, _ = strconv.ParseInt(v, 10, 64)
			case "EditorName":
				r.EditorName = v
			case "EditorID":
				r.EditorID = ParsePersonID(v)
			case "Action":
				r.Action = v
			case "Reason":
				r.Reason = v
			}
		}
	}
	return r, nil
}

// EncodeRFC822CommentRevision encodes a comment revision into RFC 822 text
func EncodeRFC822CommentRevision(r *CommentRevision) []byte {
	var b strings.Builder
	b.WriteString("Encoding: utf-8\n")
	b.WriteString(fmt.Sprintf("PostID: %d\n", r.PostID))
	b.WriteString(fmt.Sprintf("Sequence: %d\n", r.Sequence))
	b.WriteString(fmt.Sprintf("Revision: %d\n", r.Revision))
	b.WriteString(fmt.Sprintf("CreatorName: %s\n", r.CreatorName))
	b.WriteString(fmt.Sprintf("CreatorID: %d\n", r.CreatorID))
	b.WriteString(fmt.Sprintf("CreatedAt: %d\n", r.CreatedAt))
	b.WriteString(fmt.Sprintf("EditedAt: %d\n", r.EditedAt))
	b.WriteString(fmt.Sprintf("EditorName: %s\n", r.EditorName))
	b.WriteString(fmt.Sprintf("EditorID: %d\n", r.EditorID))
	b.WriteString(fmt.Sprintf("IP: %s\n", r.IP))
	b.WriteString(fmt.Sprintf("LegacyType: %d\n", r.LegacyType))
	if r.Action != "" {
		b.WriteString(fmt.Sprintf("Action: %s\n", r.Action))
	}
	if r.Reason != "" {
		b.WriteString(fmt.Sprintf("Reason: %s\n", r.Reason))
	}
	b.WriteString("\n")
	b.WriteString(r.Content)
	return []byte(b.String())
}

func DecodeRFC822CommentRevision(val []byte) (*CommentRevision, error) {
	idx := bytes.Index(val, []byte("\n\n"))
	if idx == -1 {
		return nil, fmt.Errorf("invalid comment revision format")
	}
	headerText := string(val[:idx])
	content := string(val[idx+2:])

	r := &CommentRevision{
		Content: content,
	}

	lines := strings.Split(headerText, "\n")
	for _, l := range lines {
		if k, v, ok := strings.Cut(l, ": "); ok {
			switch k {
			case "PostID":
				val, _ := strconv.ParseUint(v, 10, 64)
				r.PostID = val
			case "Sequence":
				val, _ := strconv.ParseUint(v, 10, 32)
				r.Sequence = uint32(val)
			case "Revision":
				val, _ := strconv.ParseUint(v, 10, 32)
				r.Revision = uint32(val)
			case "CreatorName":
				r.CreatorName = v
			case "CreatorID":
				r.CreatorID = ParsePersonID(v)
			case "CreatedAt":
				r.CreatedAt, _ = strconv.ParseInt(v, 10, 64)
			case "EditedAt":
				r.EditedAt, _ = strconv.ParseInt(v, 10, 64)
			case "EditorName":
				r.EditorName = v
			case "EditorID":
				r.EditorID = ParsePersonID(v)
			case "IP":
				r.IP = v
			case "LegacyType":
				val, _ := strconv.ParseUint(v, 10, 8)
				r.LegacyType = uint8(val)
			case "Action":
				r.Action = v
			case "Reason":
				r.Reason = v
			}
		}
	}
	return r, nil
}

// EncodeRFC822Crosspost packages a crosspost into RFC 822 format
func EncodeRFC822Crosspost(x *CrosspostRecord) []byte {
	var b strings.Builder
	b.WriteString("Encoding: utf-8\n")
	b.WriteString(fmt.Sprintf("SourcePostID: %d\n", x.SourcePostID))
	b.WriteString(fmt.Sprintf("TargetCommunity: %s\n", x.TargetCommunity))
	b.WriteString(fmt.Sprintf("TargetPostFile: %s\n", x.TargetPostFile))
	b.WriteString(fmt.Sprintf("CreatorName: %s\n", x.CreatorName))
	b.WriteString(fmt.Sprintf("CreatorID: %d\n", x.CreatorID))
	b.WriteString(fmt.Sprintf("CreatedAt: %d\n\n", x.CreatedAt))
	return []byte(b.String())
}

func DecodeRFC822Crosspost(val []byte) (*CrosspostRecord, error) {
	idx := bytes.Index(val, []byte("\n\n"))
	if idx == -1 {
		idx = len(val)
	}
	headerText := string(val[:idx])
	lines := strings.Split(headerText, "\n")
	rec := &CrosspostRecord{}
	for _, l := range lines {
		if k, v, ok := strings.Cut(l, ": "); ok {
			switch k {
			case "SourcePostID":
				val, _ := strconv.ParseUint(v, 10, 64)
				rec.SourcePostID = val
			case "TargetCommunity":
				rec.TargetCommunity = v
			case "TargetPostFile":
				rec.TargetPostFile = v
			case "CreatorName":
				rec.CreatorName = v
			case "CreatorID":
				rec.CreatorID = ParsePersonID(v)
			case "CreatedAt":
				rec.CreatedAt, _ = strconv.ParseInt(v, 10, 64)
			}
		}
	}
	return rec, nil
}

// RenderLegacyComment formats a Comment back into legacy BBS push line format
func RenderLegacyComment(c *Comment) string {
	if c.DeletedOrRemoved() {
		cleanReason := strings.TrimSpace(c.Reason)
		isSelf := c.Deleted || cleanReason == "自刪"
		if isSelf {
			if cleanReason != "" && cleanReason != "自刪" {
				return fmt.Sprintf("\x1b[1;30m[此推文已被刪除: %s]\x1b[m\n", cleanReason)
			}
			return "\x1b[1;30m[此推文已被刪除]\x1b[m\n"
		}
		if cleanReason != "" {
			return fmt.Sprintf("\x1b[1;30m[此推文已被隱藏: %s]\x1b[m\n", cleanReason)
		}
		return "\x1b[1;30m[此推文已被隱藏]\x1b[m\n"
	}
	t := time.Unix(c.CreatedAt, 0)
	dateStr := t.Format("01/02 15:04")
	var tail string
	if c.IP != "" && c.IP != "-" {
		tail = fmt.Sprintf("%-15s %s", c.IP, dateStr)
	} else {
		tail = dateStr
	}

	content := strings.ReplaceAll(c.Content, "\n", " ")

	switch c.LegacyType {
	case LegacyTypeOldRecommend:
		// PTT2 OLD_RECOMMEND: → author: msg pad 推 tail
		return fmt.Sprintf("→ %-12s: %s 推 %s", c.CreatorName, content, dateStr)
	case LegacyTypePush:
		return fmt.Sprintf("推 %-12s: %s %s", c.CreatorName, content, tail)
	case LegacyTypeBoo:
		return fmt.Sprintf("噓 %-12s: %s %s", c.CreatorName, content, tail)
	case LegacyTypeArrow:
		fallthrough
	default:
		return fmt.Sprintf("→ %-12s: %s %s", c.CreatorName, content, tail)
	}
}
