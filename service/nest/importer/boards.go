package importer

import (
	"encoding/binary"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
	"unicode/utf8"

	"pttbbs/big5uao"
	"pttbbs/nest/model"
	"pttbbs/nest/storage"
)

// BoardHeaderSize is sizeof(boardheader_t), the size of a record in .BRD.
const BoardHeaderSize = 256

// maxBMs is MAX_BMs, the number of boardheader_t.moderators.
const maxBMs = 4

// Offsets of the fields of boardheader_t (include/pttstruct.h) that nest.svc
// mirrors.
const (
	bhBrdname              = 0   // char brdname[BRDNAME_SIZE = 13]
	bhBclass               = 13  // char bclass[BCLEN + 1 = 5]
	bhDesc                 = 20  // char desc[BTLEN + 1 = 42]
	bhModeratorNotes       = 62  // char moderator_notes[IDLEN * 3 + 3 = 39]
	bhBrdattr              = 104 // uint32_t brdattr
	bhPetitionLimitLogins  = 110 // uint8_t
	bhPetitionLimitBadpost = 111 // uint8_t
	bhFastcommentPause     = 116 // uint8_t
	bhPostLimitLogins      = 117 // uint8_t
	bhPostLimitBadpost     = 118 // uint8_t
	bhLevel                = 124 // uint32_t
	bhGid                  = 132 // int32_t
	bhModerators           = 172 // user_t moderators[MAX_BMs]: { int unum; time4_t firstlogin; }
)

// BoardHeader is a record of .BRD (boardheader_t), with its text fields
// still in the encoding of .BRD.
type BoardHeader struct {
	BID                  int // 1-based index in .BRD
	Name                 string
	Class                []byte
	Desc                 []byte
	ModeratorNotes       []byte
	Attr                 uint32
	PetitionLimitLogins  uint8
	PetitionLimitBadpost uint8
	FastCommentPause     uint8
	PostLimitLogins      uint8
	PostLimitBadpost     uint8
	Level                uint32
	GID                  int32
	Moderators           [maxBMs]model.PersonID
}

func cBytes(b []byte) []byte {
	for i, c := range b {
		if c == 0 {
			return b[:i]
		}
	}
	return b
}

// ParseBoardHeader parses a boardheader_t record.
func ParseBoardHeader(buf []byte, bid int) (*BoardHeader, error) {
	if len(buf) < BoardHeaderSize {
		return nil, fmt.Errorf("short board header: %d bytes", len(buf))
	}
	le := binary.LittleEndian
	bh := &BoardHeader{
		BID:                  bid,
		Name:                 string(cBytes(buf[bhBrdname : bhBrdname+13])),
		Class:                cBytes(buf[bhBclass : bhBclass+5]),
		Desc:                 cBytes(buf[bhDesc : bhDesc+42]),
		ModeratorNotes:       cBytes(buf[bhModeratorNotes : bhModeratorNotes+39]),
		Attr:                 le.Uint32(buf[bhBrdattr:]),
		PetitionLimitLogins:  buf[bhPetitionLimitLogins],
		PetitionLimitBadpost: buf[bhPetitionLimitBadpost],
		FastCommentPause:     buf[bhFastcommentPause],
		PostLimitLogins:      buf[bhPostLimitLogins],
		PostLimitBadpost:     buf[bhPostLimitBadpost],
		Level:                le.Uint32(buf[bhLevel:]),
		GID:                  int32(le.Uint32(buf[bhGid:])),
	}
	for i := 0; i < maxBMs; i++ {
		off := bhModerators + 8*i
		unum := int32(le.Uint32(buf[off:]))
		firstlogin := le.Uint32(buf[off+4:])
		bh.Moderators[i] = model.MakePersonID(int(unum), firstlogin)
	}
	return bh, nil
}

// ReadBoardHeaders reads all records of a .BRD file.
func ReadBoardHeaders(path string) ([]*BoardHeader, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var headers []*BoardHeader
	for i := 0; i+BoardHeaderSize <= len(data); i += BoardHeaderSize {
		bh, err := ParseBoardHeader(data[i:i+BoardHeaderSize], i/BoardHeaderSize+1)
		if err != nil {
			return nil, err
		}
		headers = append(headers, bh)
	}
	return headers, nil
}

// IsCommunity reports whether the record is a board of its own: not an
// empty (deleted) slot, and not a link to another board.
func (bh *BoardHeader) IsCommunity() bool {
	return bh.Name != "" && bh.Attr&model.BrdSymbolic == 0
}

func decodeBoardText(b []byte, isBig5 bool) string {
	s := strings.TrimRight(string(b), " ")
	if isBig5 && !utf8.ValidString(s) {
		s = big5uao.Decode([]byte(s))
	}
	return s
}

// Community converts the record to a model.Community. headers (all of
// .BRD, may be nil) gives the name of the group; users (may be nil) gives
// the names of the moderators.
func (bh *BoardHeader) Community(headers []*BoardHeader, users UserResolver, isBig5 bool) *model.Community {
	c := &model.Community{
		Name:                 bh.Name,
		Title:                decodeBoardText(bh.Desc, isBig5),
		Category:             decodeBoardText(bh.Class, isBig5),
		ModeratorNotes:       decodeBoardText(bh.ModeratorNotes, isBig5),
		Flags:                bh.Attr,
		BID:                  bh.BID,
		GroupBID:             int(bh.GID),
		Level:                bh.Level,
		PostLimitLogins:      int(bh.PostLimitLogins),
		PostLimitBadpost:     int(bh.PostLimitBadpost),
		PetitionLimitLogins:  int(bh.PetitionLimitLogins),
		PetitionLimitBadpost: int(bh.PetitionLimitBadpost),
		FastCommentPause:     int(bh.FastCommentPause),
	}
	if g := int(bh.GID); g >= 1 && g <= len(headers) {
		c.Group = headers[g-1].Name
	}
	for _, pid := range bh.Moderators {
		if pid == 0 {
			continue
		}
		m := model.CommunityModerator{PersonID: pid}
		if users != nil {
			if userid, firstlogin, ok := users.LookupUsernum(pid.Usernum()); ok && firstlogin == pid.Firstlogin() {
				m.PersonName = userid
			}
		}
		c.Moderators = append(c.Moderators, m)
	}
	c.Derive()
	return c
}

// ImportBoardsOptions configures ImportBoards.
type ImportBoardsOptions struct {
	BBSHome string
	IsBig5  bool
	Users   UserResolver
	DryRun  bool
}

// ImportBoardsStats tells what ImportBoards did.
type ImportBoardsStats struct {
	Records  int // records in .BRD
	Upserted int // communities upserted (or that would be, in a dry run)
	Skipped  int // empty slots and links
	Failed   int // boards that could not be upserted (see Errors)
	Errors   []string
}

// ImportBoards mirrors every board of the legacy $BBSHOME/.BRD into the
// community registry.
func ImportBoards(st storage.Storage, opts ImportBoardsOptions) (*ImportBoardsStats, error) {
	headers, err := ReadBoardHeaders(filepath.Join(opts.BBSHome, ".BRD"))
	if err != nil {
		return nil, err
	}
	users := NewCachedResolver(opts.Users)
	stats := &ImportBoardsStats{Records: len(headers)}
	for _, bh := range headers {
		if !bh.IsCommunity() {
			stats.Skipped++
			continue
		}
		if !opts.DryRun {
			if _, err := st.Communities().Upsert(bh.Community(headers, users, opts.IsBig5)); err != nil {
				// e.g. a board renamed without nest.svc: the others go on.
				stats.Failed++
				stats.Errors = append(stats.Errors, fmt.Sprintf("%s: %v", bh.Name, err))
				continue
			}
		}
		stats.Upserted++
	}
	return stats, nil
}

// readBoardHeaderAt reads record bid (1-based) of .BRD; nil if there is
// none.
func readBoardHeaderAt(f *os.File, bid int) (*BoardHeader, error) {
	buf := make([]byte, BoardHeaderSize)
	n, err := f.ReadAt(buf, int64(bid-1)*BoardHeaderSize)
	if n < BoardHeaderSize {
		if err == nil || err == io.EOF {
			return nil, nil
		}
		return nil, err
	}
	return ParseBoardHeader(buf, bid)
}

// ImportBoardHeader mirrors record bid of $BBSHOME/.BRD (as mbbsd has just
// written it, through BoF) into the community registry, and returns the ID
// of its community; 0 for an empty slot or a link, which are not
// communities.
func ImportBoardHeader(st storage.Storage, opts ImportBoardsOptions, bid int) (int64, error) {
	if bid < 1 {
		return 0, fmt.Errorf("invalid board slot %d", bid)
	}
	f, err := os.Open(filepath.Join(opts.BBSHome, ".BRD"))
	if err != nil {
		return 0, err
	}
	defer f.Close()
	bh, err := readBoardHeaderAt(f, bid)
	if err != nil || bh == nil || !bh.IsCommunity() {
		return 0, err
	}
	c := bh.Community(nil, opts.Users, opts.IsBig5)
	// The name of its group, from the slot of the group.
	if bh.GID >= 1 {
		if gh, err := readBoardHeaderAt(f, int(bh.GID)); err == nil && gh != nil {
			c.Group = gh.Name
		}
	}
	return st.Communities().Upsert(c)
}

// importCommunity mirrors the record of board in $BBSHOME/.BRD into the
// community registry, if there is one.
func importBoardHeader(st storage.Storage, bbsHome, board string, users UserResolver, isBig5 bool) error {
	headers, err := ReadBoardHeaders(filepath.Join(bbsHome, ".BRD"))
	if err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return err
	}
	for _, bh := range headers {
		if bh.IsCommunity() && strings.EqualFold(bh.Name, board) {
			_, err := st.Communities().Upsert(bh.Community(headers, users, isBig5))
			return err
		}
	}
	return nil
}

// brdNames caches the names of the boards in a .BRD, for boardExists.
var brdNames struct {
	sync.Mutex
	path  string
	mtime time.Time
	size  int64
	names map[string]bool // lower case; nil if .BRD cannot be read
}

// boardNamesInBRD returns the names (in lower case) of the boards in
// $BBSHOME/.BRD, or nil if it cannot be read.
func boardNamesInBRD(bbsHome string) map[string]bool {
	path := filepath.Join(bbsHome, ".BRD")
	fi, err := os.Stat(path)
	brdNames.Lock()
	defer brdNames.Unlock()
	if err != nil {
		return nil
	}
	if brdNames.path == path && brdNames.mtime.Equal(fi.ModTime()) && brdNames.size == fi.Size() {
		return brdNames.names
	}
	headers, err := ReadBoardHeaders(path)
	var names map[string]bool
	if err == nil {
		names = make(map[string]bool, len(headers))
		for _, bh := range headers {
			if bh.IsCommunity() {
				names[strings.ToLower(bh.Name)] = true
			}
		}
	}
	brdNames.path, brdNames.mtime, brdNames.size, brdNames.names = path, fi.ModTime(), fi.Size(), names
	return names
}

// boardExists tells whether name is a board: in .BRD, or a community
// mirrored from it. Without a .BRD every name is taken as one.
func boardExists(st storage.Storage, bbsHome, name string) bool {
	names := boardNamesInBRD(bbsHome)
	if names == nil || names[strings.ToLower(name)] {
		return true
	}
	c, ok := st.Communities().GetByName(name)
	return ok && c.BID != 0
}
