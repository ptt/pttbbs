// Package protocol is the wire format of gem.svc, shared by the server and
// its clients (gem.ctl here; the gem backend of bof for mbbsd).
//
// One command per connection: a header line of words separated by single
// spaces, then the payload the command announces; the reply is a line
// "OK ..." (and its payload) or "ERR <code> [detail]". Codes follow Lemmy's
// error names, with bbs_ for what Lemmy has not (see package storage).
//
// A directory is named by its root (a gem area: "board:<name>",
// "user:<id>" or "site"; a bare name is a board) and path: names separated
// by '/', "." for the top of the area. An entry is named by its directory,
// the index the client last saw it at (-1 for none) and its name.
//
// Entries go over the wire as fixed records of RecordSize bytes, so that C
// reads them into a struct:
//
//	struct gem_entry {          // 152 bytes, little endian
//	    fileheader_t header;    // as in .DIR
//	    uint8_t  kind;          // 1 file, 2 dir, 3 link, 4 missing
//	    uint8_t  pad[3];
//	    uint32_t nodes;         // dir: entries below it (saturated)
//	    uint64_t bytes;         // file: its size; dir: bytes of files below
//	    int64_t  mtime;         // made or last changed (Unix time)
//	};
package protocol

import (
	"encoding/binary"
	"math"
	"time"

	"pttbbs/gem/storage"
)

// RecordSize is sizeof(struct gem_entry).
const RecordSize = storage.HeaderSize + 24

// MaxPayload bounds what a request may send (MAX_FILE_SIZE of mbbsd is
// 32 MB).
const MaxPayload = 64 << 20

// Record is one entry as sent.
type Record struct {
	Header storage.Header
	Kind   storage.Kind
	Nodes  uint32
	Bytes  uint64
	Mtime  int64
}

// FromEntry makes the record of e.
func FromEntry(e *storage.Entry) Record {
	r := Record{Header: e.Header, Kind: e.Kind, Mtime: e.Mtime}
	switch e.Kind {
	case storage.KindFile:
		r.Bytes = e.Size
	case storage.KindDir:
		r.Nodes = uint32(min(e.Sub.Nodes(), math.MaxUint32))
		r.Bytes = e.Sub.Bytes
	case storage.KindIndex:
		// Made when read: as of now (mbbsd shows it as the date).
		r.Mtime = time.Now().Unix()
	}
	return r
}

// Append appends the wire form of r to b.
func (r *Record) Append(b []byte) []byte {
	b = append(b, r.Header[:]...)
	b = append(b, byte(r.Kind), 0, 0, 0)
	b = binary.LittleEndian.AppendUint32(b, r.Nodes)
	b = binary.LittleEndian.AppendUint64(b, r.Bytes)
	b = binary.LittleEndian.AppendUint64(b, uint64(r.Mtime))
	return b
}

// Decode reads a record from b (RecordSize bytes).
func Decode(b []byte) Record {
	var r Record
	copy(r.Header[:], b[:storage.HeaderSize])
	r.Kind = storage.Kind(b[storage.HeaderSize])
	r.Nodes = binary.LittleEndian.Uint32(b[storage.HeaderSize+4:])
	r.Bytes = binary.LittleEndian.Uint64(b[storage.HeaderSize+8:])
	r.Mtime = int64(binary.LittleEndian.Uint64(b[storage.HeaderSize+16:]))
	return r
}

// Path renders a directory path for the wire.
func Path(p []string) string {
	if len(p) == 0 {
		return "."
	}
	s := p[0]
	for _, n := range p[1:] {
		s += "/" + n
	}
	return s
}
