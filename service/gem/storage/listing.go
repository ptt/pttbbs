// Package storage keeps the gem areas (精華區, man/) of all boards in one
// Pebble database, as copy-on-write trees.
//
// # Model
//
// A directory of a gem area is a listing: the entries of its .DIR, in order.
// Each entry keeps the 128 bytes of its fileheader_t as they are (title,
// owner, date, filemode: mbbsd shows and edits them as before) and what the
// entry is:
//
//   - a file: the content, kept once per distinct content (a blob, keyed by
//     its SHA-256) however many entries have it;
//   - a directory: the listing of the directory;
//   - a symbolic link: its target, as readlink(2) gave it;
//   - the table of contents of the gem area (util/mandex wrote it to .index
//     and linked to it): nothing is kept, gem.svc makes it when read;
//   - missing: a .DIR record whose file was gone when it was imported.
//
// Listings and blobs are reference counted. Copying an entry, a whole
// directory tree included, only adds a reference (O(1)): the copy and the
// original share the listing until one of them is changed. A change copies
// the listings from the first shared one down to the changed one (path
// copying) and leaves the other side as it was. A listing or blob whose
// last reference goes away is freed, the listings in the background.
//
// Each listing also keeps the totals of its subtree (files, directories,
// bytes, depth), as the copy sees them: a shared tree counts in every place
// that has it. The totals of a board are those of its root listing, which
// the quotas check.
//
// # Identity
//
// Lemmy, Reddit and Discourse have no gem area, so this is PTT specific.
// A gem area is a root: "board:<name>", "user:<id>" (a person's mail folder)
// or "site" (man/ itself). An entry is known by its root and the path of
// names from it; names are unique only within a directory. Listing IDs are
// internal and never exposed.
package storage

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
)

// Kind is what an entry of a listing is.
type Kind uint8

const (
	KindFile    Kind = 1
	KindDir     Kind = 2
	KindLink    Kind = 3
	KindMissing Kind = 4
	KindIndex   Kind = 5
)

func (k Kind) String() string {
	switch k {
	case KindFile:
		return "file"
	case KindDir:
		return "dir"
	case KindLink:
		return "link"
	case KindMissing:
		return "missing"
	case KindIndex:
		return "index"
	}
	return fmt.Sprintf("kind(%d)", uint8(k))
}

// HeaderSize is sizeof(fileheader_t).
const HeaderSize = 128

// Offsets in fileheader_t (include/pttstruct.h).
const (
	hdrFilenameLen = 22 // FNSIZE; the 6 bytes after are reserved (0)
	hdrOwnerOff    = 34
	hdrOwnerLen    = 14
	hdrTitleOff    = 54
	hdrTitleLen    = 65 // TTLEN + 1
	hdrFilemodeOff = 124
)

// Filemode bits of the gem area (include/pttstruct.h).
const (
	FileHide = 0x0020 // FILE_HIDE
	FileBM   = 0x0040 // FILE_BM
)

// MaxNameLen is the longest name: fileheader_t.filename holds FNSIZE (22)
// bytes with its NUL.
const MaxNameLen = hdrFilenameLen - 1

// Header is a fileheader_t as kept in .DIR.
type Header [HeaderSize]byte

func cstr(b []byte) []byte {
	if i := bytes.IndexByte(b, 0); i >= 0 {
		return b[:i]
	}
	return b
}

// Name is fileheader_t.filename.
func (h *Header) Name() string { return string(cstr(h[:hdrFilenameLen])) }

// SetName sets fileheader_t.filename (truncated to MaxNameLen bytes).
func (h *Header) SetName(name string) {
	clear(h[:hdrFilenameLen])
	copy(h[:MaxNameLen], name)
}

// Title is fileheader_t.title, in the encoding of the site (Big5).
func (h *Header) Title() []byte { return cstr(h[hdrTitleOff : hdrTitleOff+hdrTitleLen]) }

// SetTitle sets fileheader_t.title (truncated to TTLEN bytes).
func (h *Header) SetTitle(title []byte) {
	clear(h[hdrTitleOff : hdrTitleOff+hdrTitleLen])
	copy(h[hdrTitleOff:hdrTitleOff+hdrTitleLen-1], title)
}

// Owner is fileheader_t.owner.
func (h *Header) Owner() string { return string(cstr(h[hdrOwnerOff : hdrOwnerOff+hdrOwnerLen])) }

// SetOwner sets fileheader_t.owner.
func (h *Header) SetOwner(owner string) {
	clear(h[hdrOwnerOff : hdrOwnerOff+hdrOwnerLen])
	copy(h[hdrOwnerOff:hdrOwnerOff+hdrOwnerLen-1], owner)
}

// Filemode is fileheader_t.filemode.
func (h *Header) Filemode() uint16 {
	return binary.LittleEndian.Uint16(h[hdrFilemodeOff:])
}

// SetFilemode sets fileheader_t.filemode.
func (h *Header) SetFilemode(m uint16) {
	binary.LittleEndian.PutUint16(h[hdrFilemodeOff:], m)
}

// ValidName tells whether name may name an entry: what a .DIR record may
// hold and a path component may be.
func ValidName(name string) bool {
	if name == "" || name == "." || name == ".." || len(name) > MaxNameLen {
		return false
	}
	for i := 0; i < len(name); i++ {
		c := name[i]
		if c <= ' ' || c == '/' || c >= 0x7f {
			return false
		}
	}
	return true
}

// BlobID is the SHA-256 of a content.
type BlobID [32]byte

func (b BlobID) String() string { return fmt.Sprintf("%x", b[:]) }

// ListingID identifies a listing within the database (never exposed).
type ListingID uint64

// Totals counts a subtree as copies see it (shared trees count in each
// place).
type Totals struct {
	Files   uint64
	Dirs    uint64
	Links   uint64
	Missing uint64
	Bytes   uint64 // of the files
	Depth   uint64 // levels of directories below (0: no subdirectory)
}

// Nodes is the number of entries in the subtree.
func (t Totals) Nodes() uint64 { return t.Files + t.Dirs + t.Links + t.Missing }

func (t *Totals) add(o Totals) {
	t.Files += o.Files
	t.Dirs += o.Dirs
	t.Links += o.Links
	t.Missing += o.Missing
	t.Bytes += o.Bytes
}

// Entry is one record of a listing.
type Entry struct {
	Header Header
	Kind   Kind
	Size   uint64    // KindFile: length of the content
	Blob   BlobID    // KindFile
	Child  ListingID // KindDir
	Sub    Totals    // KindDir: the totals of Child
	Target string    // KindLink
	Mtime  int64     // when it was made or last changed (Unix time)
}

// Name is the name of the entry (fileheader_t.filename).
func (e *Entry) Name() string { return e.Header.Name() }

// Totals is what the entry adds to the totals of its listing.
func (e *Entry) Totals() Totals {
	switch e.Kind {
	case KindFile:
		return Totals{Files: 1, Bytes: e.Size}
	case KindDir:
		t := e.Sub
		t.Dirs++
		t.Depth++
		return t
	case KindLink:
		return Totals{Links: 1}
	case KindIndex:
		// Made from the others when read: not content of its own.
		return Totals{}
	}
	return Totals{Missing: 1}
}

// Listing is a directory: its entries in order, and their totals.
type Listing struct {
	Totals  Totals
	Entries []Entry
}

// Recount computes Totals from the entries.
func (l *Listing) Recount() {
	var t Totals
	for i := range l.Entries {
		et := l.Entries[i].Totals()
		t.add(et)
		if et.Depth > t.Depth {
			t.Depth = et.Depth
		}
	}
	l.Totals = t
}

// Find returns the index of the entry named name, preferring hint when it
// names it (the index a client last saw); -1 when there is none.
func (l *Listing) Find(hint int, name string) int {
	if hint >= 0 && hint < len(l.Entries) && l.Entries[hint].Name() == name {
		return hint
	}
	for i := range l.Entries {
		if l.Entries[i].Name() == name {
			return i
		}
	}
	return -1
}

func (l *Listing) clone() *Listing {
	n := &Listing{Totals: l.Totals, Entries: make([]Entry, len(l.Entries))}
	copy(n.Entries, l.Entries)
	return n
}

const listingVersion = 2

var errCorrupt = errors.New("corrupt listing")

func appendTotals(b []byte, t Totals) []byte {
	b = binary.AppendUvarint(b, t.Files)
	b = binary.AppendUvarint(b, t.Dirs)
	b = binary.AppendUvarint(b, t.Links)
	b = binary.AppendUvarint(b, t.Missing)
	b = binary.AppendUvarint(b, t.Bytes)
	b = binary.AppendUvarint(b, t.Depth)
	return b
}

type decoder struct {
	b   []byte
	err error
}

func (d *decoder) uvarint() uint64 {
	if d.err != nil {
		return 0
	}
	v, n := binary.Uvarint(d.b)
	if n <= 0 {
		d.err = errCorrupt
		return 0
	}
	d.b = d.b[n:]
	return v
}

func (d *decoder) varint() int64 {
	if d.err != nil {
		return 0
	}
	v, n := binary.Varint(d.b)
	if n <= 0 {
		d.err = errCorrupt
		return 0
	}
	d.b = d.b[n:]
	return v
}

func (d *decoder) bytes(n int) []byte {
	if d.err != nil {
		return nil
	}
	if n < 0 || n > len(d.b) {
		d.err = errCorrupt
		return nil
	}
	v := d.b[:n]
	d.b = d.b[n:]
	return v
}

func (d *decoder) totals() Totals {
	return Totals{
		Files: d.uvarint(), Dirs: d.uvarint(), Links: d.uvarint(),
		Missing: d.uvarint(), Bytes: d.uvarint(), Depth: d.uvarint(),
	}
}

// encodeListing serializes l: version, totals, count, then per entry its
// kind, header and what the kind needs.
func encodeListing(l *Listing) []byte {
	b := make([]byte, 0, 16+len(l.Entries)*(HeaderSize+40))
	b = append(b, listingVersion)
	b = appendTotals(b, l.Totals)
	b = binary.AppendUvarint(b, uint64(len(l.Entries)))
	for i := range l.Entries {
		e := &l.Entries[i]
		b = append(b, byte(e.Kind))
		b = append(b, e.Header[:]...)
		b = binary.AppendVarint(b, e.Mtime)
		switch e.Kind {
		case KindFile:
			b = binary.AppendUvarint(b, e.Size)
			b = append(b, e.Blob[:]...)
		case KindDir:
			b = binary.AppendUvarint(b, uint64(e.Child))
			b = appendTotals(b, e.Sub)
		case KindLink:
			b = binary.AppendUvarint(b, uint64(len(e.Target)))
			b = append(b, e.Target...)
		}
	}
	return b
}

func decodeListing(b []byte) (*Listing, error) {
	if len(b) == 0 || b[0] != listingVersion {
		return nil, errCorrupt
	}
	d := &decoder{b: b[1:]}
	l := &Listing{Totals: d.totals()}
	n := d.uvarint()
	if d.err != nil || n > uint64(len(d.b)/(HeaderSize+1)) {
		return nil, errCorrupt
	}
	l.Entries = make([]Entry, n)
	for i := range l.Entries {
		e := &l.Entries[i]
		k := d.bytes(1)
		h := d.bytes(HeaderSize)
		if d.err != nil {
			return nil, d.err
		}
		e.Kind = Kind(k[0])
		copy(e.Header[:], h)
		e.Mtime = d.varint()
		switch e.Kind {
		case KindFile:
			e.Size = d.uvarint()
			copy(e.Blob[:], d.bytes(32))
		case KindDir:
			e.Child = ListingID(d.uvarint())
			e.Sub = d.totals()
		case KindLink:
			e.Target = string(d.bytes(int(d.uvarint())))
		case KindMissing, KindIndex:
		default:
			return nil, errCorrupt
		}
	}
	if d.err != nil {
		return nil, d.err
	}
	return l, nil
}

// decodeTotals reads only the totals of an encoded listing.
func decodeTotals(b []byte) (Totals, error) {
	if len(b) == 0 || b[0] != listingVersion {
		return Totals{}, errCorrupt
	}
	d := &decoder{b: b[1:]}
	t := d.totals()
	return t, d.err
}
