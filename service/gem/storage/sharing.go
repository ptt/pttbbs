package storage

import (
	"crypto/sha256"
	"encoding/binary"

	"github.com/cockroachdb/pebble"
)

// SharingReport is what Sharing found: what the gem areas have as they are
// seen (a shared tree counts in each place), what is stored, and what could
// be.
type SharingReport struct {
	Areas int
	// As seen: the totals of all areas.
	Files, Dirs, Bytes uint64
	// Stored: listings and contents, each once.
	Listings, Blobs uint64
	StoredBytes     uint64
	// Listings referred to by more than one place (copies, resolved links),
	// and the places beyond the first.
	SharedListings, SharedListingRefs uint64
	// Contents kept once for more than one file, the files beyond the first,
	// and the bytes they would have taken.
	SharedBlobs, SharedBlobRefs, SavedBytes uint64
	// Identical non-empty directories kept as separate listings: listings
	// saved by sharing them, the entries they hold (nested copies counted
	// once), and the empty directories (which could all be one listing).
	SameDirs, SameDirEntries, EmptyDirs uint64
}

// Sharing walks every area (in a snapshot, each listing once) and reports
// what is shared. Directories are told the same by a hash of their entries
// and of the hashes below (one per listing in memory).
func (s *Store) Sharing() (*SharingReport, error) {
	snap := s.db.NewSnapshot()
	defer snap.Close()
	rep := &SharingReport{}

	roots, err := s.Roots()
	if err != nil {
		return nil, err
	}
	rep.Areas = len(roots)
	for _, r := range roots {
		id, _, err := readRoot(snap, r)
		if err != nil {
			return nil, err
		}
		v, err := getCopy(snap, idKey(pList, id))
		if err != nil {
			return nil, err
		}
		t, err := decodeTotals(v)
		if err != nil {
			return nil, err
		}
		rep.Files += t.Files
		rep.Dirs += t.Dirs
		rep.Bytes += t.Bytes
	}

	// Reference counts, as kept.
	it, err := snap.NewIter(&pebble.IterOptions{LowerBound: []byte{pRef}, UpperBound: []byte{pRef + 1}})
	if err != nil {
		return nil, err
	}
	for it.First(); it.Valid(); it.Next() {
		rep.Listings++
		if n, _ := binary.Uvarint(it.Value()); n > 1 {
			rep.SharedListings++
			rep.SharedListingRefs += n - 1
		}
	}
	if err := it.Close(); err != nil {
		return nil, err
	}
	it, err = snap.NewIter(&pebble.IterOptions{LowerBound: []byte{pBlob}, UpperBound: []byte{pBlob + 1}})
	if err != nil {
		return nil, err
	}
	for it.First(); it.Valid(); it.Next() {
		m, err := decodeBlobMeta(it.Value())
		if err != nil {
			continue
		}
		rep.Blobs++
		rep.StoredBytes += m.size
		if m.refs > 1 {
			rep.SharedBlobs++
			rep.SharedBlobRefs += m.refs - 1
			rep.SavedBytes += (m.refs - 1) * m.size
		}
	}
	if err := it.Close(); err != nil {
		return nil, err
	}

	// Directories alike: a hash per listing, bottom up.
	type info struct {
		hash    [32]byte
		entries uint64
	}
	seen := map[ListingID]info{}
	count := map[[32]byte]uint64{}
	var walk func(id ListingID) (info, error)
	walk = func(id ListingID) (info, error) {
		if in, ok := seen[id]; ok {
			return in, nil
		}
		l, err := readListing(snap, id)
		if err != nil {
			return info{}, err
		}
		h := sha256.New()
		var n uint64
		for i := range l.Entries {
			e := &l.Entries[i]
			n++
			h.Write(e.Header[:])
			h.Write([]byte{byte(e.Kind)})
			switch e.Kind {
			case KindFile:
				h.Write(e.Blob[:])
			case KindDir:
				sub, err := walk(e.Child)
				if err != nil {
					return info{}, err
				}
				h.Write(sub.hash[:])
				n += sub.entries
			case KindLink:
				h.Write([]byte(e.Target))
			}
		}
		var in info
		copy(in.hash[:], h.Sum(nil))
		in.entries = n
		seen[id] = in
		count[in.hash]++
		if n == 0 {
			rep.EmptyDirs++
		} else if count[in.hash] > 1 {
			rep.SameDirs++
			rep.SameDirEntries += uint64(len(l.Entries))
		}
		return in, nil
	}
	for _, r := range roots {
		id, _, err := readRoot(snap, r)
		if err != nil {
			return nil, err
		}
		if _, err := walk(id); err != nil {
			return nil, err
		}
	}
	return rep, nil
}
