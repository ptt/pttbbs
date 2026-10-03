package model

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"regexp"
	"strconv"
	"strings"
)

var ansiRegex = regexp.MustCompile(`\x1b(\[[0-9;?]*[a-zA-Z]|].*?(\x07|\x1b\\)|[ -/]*[@-~])|\x1b`)

// StripANSI strips all ANSI and terminal escape sequences from a string
func StripANSI(s string) string {
	return ansiRegex.ReplaceAllString(s, "")
}

// VoteType represents Reddit-style upvoting/downvoting
type VoteType int8

const (
	VoteNeutral  VoteType = 0  // No vote / canceled
	VoteUp       VoteType = 1  // Upvote
	VoteDown     VoteType = -1 // Downvote
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
	ID               uint64 `json:"id"`
	ParentID         uint64 `json:"parent_id"`                    // 0: Root post, >0: Reply to ParentID (回文)
	Community        string `json:"community"`                    // Formerly Board
	PostFile         string `json:"post_file"`                    // Legacy filename (e.g. M.1417074209.A.393)
	Title            string `json:"title"`                        // UTF-8
	Author           string `json:"author"`                       // OP username
	AuthorToken      uint32 `json:"author_token"`                  // cuser.firstlogin
	CreatedAt        int64  `json:"created_at"`
	Filemode         int    `json:"filemode"`
	Upvotes          int    `json:"upvotes"`                       // Upvotes count
	Downvotes        int    `json:"downvotes"`                     // Downvotes count
	NumComments      int    `json:"num_comments"`                  // Total comments count
	Encoding         string `json:"encoding"`                      // Always "utf-8" in DB
	Content          string `json:"content"`                       // UTF-8 Post Body
	DemotedToComment bool   `json:"demoted_to_comment,omitempty"`  // True if content was too short and converted to comment
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

// Comment represents a reply / discussion item (decoupled from votes)
type Comment struct {
	PostID      uint64 `json:"post_id"`
	Sequence    uint32 `json:"sequence"` // 1-indexed sequential position
	Author      string `json:"author"`
	AuthorToken uint32 `json:"author_token"` // cuser.firstlogin / userref
	IP          string `json:"ip"`
	CreatedAt   int64  `json:"created_at"`
	IsDeleted   bool   `json:"is_deleted"`
	Content     string `json:"content"` // UTF-8 text
}

// VoteRecord represents a user's vote on a post (1 vote per user per post)
type VoteRecord struct {
	PostID      uint64   `json:"post_id"`
	User        string   `json:"user"`
	AuthorToken uint32   `json:"author_token"`
	Vote        VoteType `json:"vote"` // 1 (Upvote), -1 (Downvote)
	CreatedAt   int64    `json:"created_at"`
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

func DecodeVoteKey(k []byte) (postID uint64, user string, err error) {
	if len(k) < 10 || k[0] != 'v' {
		return 0, "", fmt.Errorf("invalid vote key")
	}
	postID = binary.BigEndian.Uint64(k[1:9])
	user = string(k[9:])
	return postID, user, nil
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
	b.WriteString(fmt.Sprintf("Author: %s\n", p.Author))
	b.WriteString(fmt.Sprintf("AuthorToken: %d\n", p.AuthorToken))
	b.WriteString(fmt.Sprintf("CreatedAt: %d\n", p.CreatedAt))
	b.WriteString(fmt.Sprintf("Filemode: %d\n", p.Filemode))
	b.WriteString(fmt.Sprintf("Upvotes: %d\n", p.Upvotes))
	b.WriteString(fmt.Sprintf("Downvotes: %d\n", p.Downvotes))
	b.WriteString(fmt.Sprintf("NumComments: %d\n\n", p.NumComments))
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
	b.WriteString(fmt.Sprintf("Author: %s\n", c.Author))
	b.WriteString(fmt.Sprintf("AuthorToken: %d\n", c.AuthorToken))
	b.WriteString(fmt.Sprintf("CreatedAt: %d\n", c.CreatedAt))
	b.WriteString(fmt.Sprintf("IP: %s\n", c.IP))
	b.WriteString(fmt.Sprintf("Deleted: %t\n\n", c.IsDeleted))
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
			case "Author":
				c.Author = v
			case "AuthorToken":
				val, _ := strconv.ParseUint(v, 10, 32)
				c.AuthorToken = uint32(val)
			case "CreatedAt":
				c.CreatedAt, _ = strconv.ParseInt(v, 10, 64)
			case "IP":
				c.IP = v
			case "Deleted":
				c.IsDeleted = (v == "true")
			}
		}
	}
	return c, nil
}
