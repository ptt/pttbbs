package storage

import (
	"bytes"
	"encoding/binary"
	"fmt"

	"github.com/cockroachdb/pebble"
)

// VerifyReport is what Verify found.
type VerifyReport struct {
	Boards      int
	Listings    int // reachable
	Blobs       int // reachable
	Entries     uint64
	BadRefs     int // listings whose reference count is wrong
	BadBlobRefs int
	BadTotals   int
	Orphans     int // listings no one refers to (and not waiting to be freed)
	OrphanBlobs int
	OrphanTrash int    // what is kept of deleted entries no trash has
	Missing     int    // listings or blobs referred to but gone
	Pending     int    // listings waiting to be freed
	StoredBytes uint64 // content kept, each blob once
	Problems    []string
	Fixed       bool
}

func (r *VerifyReport) problem(format string, args ...any) {
	if len(r.Problems) < 100 {
		r.Problems = append(r.Problems, fmt.Sprintf(format, args...))
	}
}

// OK tells whether no problem was found.
func (r *VerifyReport) OK() bool {
	return r.BadRefs+r.BadBlobRefs+r.BadTotals+r.Orphans+r.OrphanBlobs+r.OrphanTrash+r.Missing == 0
}

// Verify checks reference counts and totals against what is reachable; fix
// rewrites the counts and drops the unreachable. Writes wait meanwhile, and
// it keeps a counter per blob in memory (some GB for all of PTT).
func (s *Store) Verify(fix bool) (*VerifyReport, error) {
	if fix && s.opts.ReadOnly {
		return nil, fmt.Errorf("storage: read only: cannot fix")
	}
	s.wmu.Lock()
	defer s.wmu.Unlock()
	rep := &VerifyReport{}
	db := s.db

	refs := map[ListingID]uint32{}
	blobs := map[BlobID]uint32{}
	visited := map[ListingID]bool{}
	var stack []ListingID
	visit := func(id ListingID) {
		if !visited[id] {
			visited[id] = true
			stack = append(stack, id)
		}
	}

	// Listings waiting to be freed still hold their references until the
	// reaper drops them: what they refer to is counted as if reachable.
	pending := map[ListingID]bool{}
	it, err := db.NewIter(&pebble.IterOptions{LowerBound: []byte{pFree}, UpperBound: []byte{pFree + 1}})
	if err != nil {
		return nil, err
	}
	for it.First(); it.Valid(); it.Next() {
		id := ListingID(binary.BigEndian.Uint64(it.Key()[1:]))
		pending[id] = true
		rep.Pending++
		visit(id)
	}
	if err := it.Close(); err != nil {
		return nil, err
	}

	it, err = db.NewIter(&pebble.IterOptions{LowerBound: []byte{pRoot}, UpperBound: []byte{pRoot + 1}})
	if err != nil {
		return nil, err
	}
	for it.First(); it.Valid(); it.Next() {
		id, n := binary.Uvarint(it.Value())
		if n <= 0 {
			rep.problem("bad root %q", it.Key()[1:])
			continue
		}
		rep.Boards++
		refs[ListingID(id)]++
		visit(ListingID(id))
	}
	if err := it.Close(); err != nil {
		return nil, err
	}

	// Each listing is visited once, however many refer to it; its totals
	// are checked against its entries, and its directory entries' copies of
	// the totals against the children's.
	totals := map[ListingID]Totals{}
	type subCheck struct {
		parent ListingID
		child  ListingID
		sub    Totals
	}
	var subs []subCheck
	for len(stack) > 0 {
		id := stack[len(stack)-1]
		stack = stack[:len(stack)-1]
		l, err := readListing(db, id)
		if err != nil {
			rep.Missing++
			rep.problem("listing %d: %v", id, err)
			continue
		}
		rep.Listings++
		rep.Entries += uint64(len(l.Entries))
		stored := l.Totals
		l.Recount()
		if stored != l.Totals {
			rep.BadTotals++
			rep.problem("listing %d: totals %+v, entries make %+v", id, stored, l.Totals)
		}
		totals[id] = stored
		for i := range l.Entries {
			e := &l.Entries[i]
			switch e.Kind {
			case KindDir:
				refs[e.Child]++
				visit(e.Child)
				subs = append(subs, subCheck{id, e.Child, e.Sub})
			case KindFile:
				blobs[e.Blob]++
			}
		}
	}
	for _, c := range subs {
		if t, ok := totals[c.child]; ok && t != c.sub {
			rep.BadTotals++
			rep.problem("listing %d: copy of the totals of %d is %+v, not %+v", c.parent, c.child, c.sub, t)
		}
	}
	rep.Blobs = len(blobs)

	b := db.NewBatch()
	defer b.Close()
	var drop []uint64 // serials of unreachable contents

	// Reference counts of listings.
	seen := map[ListingID]bool{}
	it, err = db.NewIter(&pebble.IterOptions{LowerBound: []byte{pRef}, UpperBound: []byte{pRef + 1}})
	if err != nil {
		return nil, err
	}
	for it.First(); it.Valid(); it.Next() {
		id := ListingID(binary.BigEndian.Uint64(it.Key()[1:]))
		n, _ := binary.Uvarint(it.Value())
		seen[id] = true
		if want := refs[id]; uint64(want) != n {
			rep.BadRefs++
			rep.problem("listing %d: %d references counted, %d found", id, n, want)
			if want > 0 {
				b.Set(idKey(pRef, id), binary.AppendUvarint(nil, uint64(want)), nil)
			} else {
				b.Delete(idKey(pRef, id), nil)
			}
		}
	}
	if err := it.Close(); err != nil {
		return nil, err
	}
	for id, want := range refs {
		if !seen[id] {
			rep.BadRefs++
			rep.problem("listing %d: no reference counted, %d found", id, want)
			b.Set(idKey(pRef, id), binary.AppendUvarint(nil, uint64(want)), nil)
		}
	}

	for id := range pending {
		if refs[id] > 0 {
			rep.BadRefs++
			rep.problem("listing %d is referred to but waits to be freed", id)
			b.Delete(idKey(pFree, id), nil)
		}
	}

	// Unreachable listings. Freeing one drops the references it holds,
	// which were not counted above: they are deleted as they are.
	it, err = db.NewIter(&pebble.IterOptions{LowerBound: []byte{pList}, UpperBound: []byte{pList + 1}})
	if err != nil {
		return nil, err
	}
	for it.First(); it.Valid(); it.Next() {
		id := ListingID(binary.BigEndian.Uint64(it.Key()[1:]))
		if !visited[id] {
			rep.Orphans++
			rep.problem("listing %d is unreachable", id)
			b.Delete(idKey(pList, id), nil)
		}
	}
	if err := it.Close(); err != nil {
		return nil, err
	}
	// Blobs.
	seenBlob := map[BlobID]bool{}
	it, err = db.NewIter(&pebble.IterOptions{LowerBound: []byte{pBlob}, UpperBound: []byte{pBlob + 1}})
	if err != nil {
		return nil, err
	}
	for it.First(); it.Valid(); it.Next() {
		var h BlobID
		copy(h[:], it.Key()[1:])
		m, _ := decodeBlobMeta(it.Value())
		n, size := m.refs, m.size
		seenBlob[h] = true
		want := blobs[h]
		if want > 0 {
			rep.StoredBytes += size
		}
		if uint64(want) == n {
			continue
		}
		if want == 0 {
			rep.OrphanBlobs++
			rep.problem("blob %s is unreachable", h)
			b.Delete(blobKey(h), nil)
			drop = append(drop, m.serial)
			continue
		}
		rep.BadBlobRefs++
		rep.problem("blob %s: %d references counted, %d found", h, n, want)
		m.refs = uint64(want)
		b.Set(blobKey(h), m.encode(), nil)
	}
	if err := it.Close(); err != nil {
		return nil, err
	}
	for h := range blobs {
		if !seenBlob[h] {
			rep.Missing++
			rep.problem("blob %s is referred to but gone", h)
		}
	}

	// What is kept of deleted entries (see TrashInfo) whose entry is not in
	// the top of the trash of its root.
	tops := map[string]*Listing{}
	it, err = db.NewIter(&pebble.IterOptions{LowerBound: []byte{pTrash}, UpperBound: []byte{pTrash + 1}})
	if err != nil {
		return nil, err
	}
	for it.First(); it.Valid(); it.Next() {
		k := it.Key()[1:]
		i := bytes.IndexByte(k, 0)
		if i < 0 {
			rep.OrphanTrash++
			rep.problem("bad trash key %q", k)
			b.Delete(bytes.Clone(it.Key()), nil)
			continue
		}
		root, name := string(k[:i]), string(k[i+1:])
		top, ok := tops[root]
		if !ok {
			if id, _, err := readRoot(db, TrashRoot(root)); err == nil {
				top, _ = readListing(db, id)
			}
			tops[root] = top
		}
		if top == nil || top.Find(-1, name) < 0 {
			rep.OrphanTrash++
			rep.problem("trash of %s: %s is gone", root, name)
			b.Delete(bytes.Clone(it.Key()), nil)
		}
	}
	if err := it.Close(); err != nil {
		return nil, err
	}

	if fix && !b.Empty() {
		if err := b.Commit(pebble.Sync); err != nil {
			return rep, err
		}
		rep.Fixed = true
	}
	if fix && len(drop) > 0 {
		db := s.data.NewBatch()
		defer db.Close()
		for _, serial := range drop {
			p := chunkPrefix(serial)
			db.DeleteRange(p, prefixEnd(p), nil)
		}
		if err := db.Commit(pebble.Sync); err != nil {
			return rep, err
		}
	}
	return rep, nil
}
