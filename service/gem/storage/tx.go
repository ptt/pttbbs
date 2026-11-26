package storage

import (
	"bytes"
	"encoding/binary"
	"errors"
	"strings"

	"github.com/cockroachdb/pebble"
	"github.com/cockroachdb/pebble/bloom"
)

var bloomFilter = bloom.FilterPolicy(10)

// Tx is a set of changes committed at once. Only one Tx runs at a time
// (Store.Update); it reads the committed state and keeps its own changes,
// and the reference counts it changes, until Commit.
type Tx struct {
	s     *Store
	batch *pebble.Batch

	listings map[ListingID]*Listing // read or written by this Tx
	dirty    map[ListingID]bool     // to write at commit
	refs     map[ListingID]int64    // reference count changes
	baseRefs map[ListingID]uint64
	blobs    map[BlobID]*blobState
	roots    map[string]rootState // lower(board)
	maxID    uint64
	blobIter *pebble.Iterator // finds contents (see blob)
	// Contents written (committed before the index) and to remove (after).
	dataBatch   *pebble.Batch
	dropSerials []uint64
	newBlobs    bool
	freed       bool
}

type blobState struct {
	base    uint64 // references before this Tx
	size    uint64
	serial  uint64 // of its chunks
	exists  bool   // before this Tx, or written by it
	delta   int64
	written bool
}

type rootState struct {
	id   ListingID // 0: removed
	name string
}

// Update runs fn in a Tx and commits it if fn returns nil.
func (s *Store) Update(fn func(t *Tx) error) error {
	s.wmu.Lock()
	defer s.wmu.Unlock()
	if s.closed.Load() {
		return errors.New("storage: closed")
	}
	if s.opts.ReadOnly {
		return errors.New("storage: read only")
	}
	t := s.newTx()
	defer t.close()
	if err := fn(t); err != nil {
		return err
	}
	return t.commit(s.writeOpts())
}

// UpdateNoSync is Update without syncing the WAL (bulk imports, which flush
// at the end).
func (s *Store) UpdateNoSync(fn func(t *Tx) error) error {
	s.wmu.Lock()
	defer s.wmu.Unlock()
	if s.closed.Load() {
		return errors.New("storage: closed")
	}
	if s.opts.ReadOnly {
		return errors.New("storage: read only")
	}
	t := s.newTx()
	defer t.close()
	if err := fn(t); err != nil {
		return err
	}
	return t.commit(pebble.NoSync)
}

func (s *Store) newTx() *Tx {
	return &Tx{
		s:        s,
		batch:    s.db.NewBatch(),
		listings: map[ListingID]*Listing{},
		dirty:    map[ListingID]bool{},
		refs:     map[ListingID]int64{},
		baseRefs: map[ListingID]uint64{},
		blobs:    map[BlobID]*blobState{},
		roots:    map[string]rootState{},
	}
}

func (t *Tx) close() {
	if t.blobIter != nil {
		t.blobIter.Close()
	}
	if t.dataBatch != nil {
		t.dataBatch.Close()
	}
	t.batch.Close()
}

// lookupBlob reads B + h with an iterator using UseL6Filters: DB.Get skips
// L6 bloom filters, where in-order contents land, so every miss (common in
// imports) would read a data block. One per Tx (one writer).
func (t *Tx) lookupBlob(h BlobID) ([]byte, bool, error) {
	if t.blobIter == nil {
		it, err := t.s.db.NewIter(&pebble.IterOptions{UseL6Filters: true})
		if err != nil {
			return nil, false, err
		}
		t.blobIter = it
	}
	k := blobKey(h)
	if !t.blobIter.SeekPrefixGE(k) {
		return nil, false, t.blobIter.Error()
	}
	if !bytes.Equal(t.blobIter.Key(), k) {
		return nil, false, nil
	}
	return t.blobIter.Value(), true, nil
}

func (t *Tx) newID() ListingID {
	id := t.s.nextID.Add(1) - 1
	if id+1 > t.maxID {
		t.maxID = id + 1
	}
	return ListingID(id)
}

func (t *Tx) root(board string) (ListingID, string, error) {
	if r, ok := t.roots[strings.ToLower(board)]; ok {
		if r.id == 0 {
			return 0, "", errorf(ErrNoRoot, "%s", board)
		}
		return r.id, r.name, nil
	}
	return readRoot(t.s.db, board)
}

func (t *Tx) listing(id ListingID) (*Listing, error) {
	if l, ok := t.listings[id]; ok {
		return l, nil
	}
	l, err := readListing(t.s.db, id)
	if err != nil {
		return nil, err
	}
	t.listings[id] = l
	return l, nil
}

func (t *Tx) putListing(id ListingID, l *Listing) {
	t.listings[id] = l
	t.dirty[id] = true
}

func (t *Tx) refCount(id ListingID) (uint64, error) {
	base, ok := t.baseRefs[id]
	if !ok {
		var err error
		if base, err = readUvarint(t.s.db, idKey(pRef, id)); err != nil {
			return 0, err
		}
		t.baseRefs[id] = base
	}
	n := int64(base) + t.refs[id]
	if n < 0 {
		return 0, errorf(ErrCorruptRecord, "listing %d has %d references", id, n)
	}
	return uint64(n), nil
}

func (t *Tx) blob(h BlobID) (*blobState, error) {
	if b, ok := t.blobs[h]; ok {
		return b, nil
	}
	b := &blobState{}
	v, found, err := t.lookupBlob(h)
	if err != nil {
		return nil, err
	}
	if found {
		m, derr := decodeBlobMeta(v)
		if derr != nil {
			return nil, errorf(ErrCorruptRecord, "blob %s", h)
		}
		b.base, b.size, b.serial = m.refs, m.size, m.serial
		b.exists = true
	}
	t.blobs[h] = b
	return b, nil
}

// incEntry adds the references an entry holds (a copy of it is made).
func (t *Tx) incEntry(e *Entry) error {
	switch e.Kind {
	case KindDir:
		t.refs[e.Child]++
	case KindFile:
		b, err := t.blob(e.Blob)
		if err != nil {
			return err
		}
		if !b.exists {
			return errorf(ErrCorruptRecord, "blob %s is gone", e.Blob)
		}
		b.delta++
	}
	return nil
}

// decEntry drops the references an entry holds (it is removed).
func (t *Tx) decEntry(e *Entry) error {
	switch e.Kind {
	case KindDir:
		t.refs[e.Child]--
	case KindFile:
		b, err := t.blob(e.Blob)
		if err != nil {
			return err
		}
		b.delta--
	}
	return nil
}

// PutBlob stores data (once per content) and returns its ID. It holds no
// reference: an entry made in the same Tx must refer to it, or it is
// dropped at commit.
func (t *Tx) PutBlob(data []byte) (BlobID, error) {
	return t.PutBlobHashed(HashContent(data), data)
}

// PutBlobHashed is PutBlob with the hash already computed.
func (t *Tx) PutBlobHashed(h BlobID, data []byte) (BlobID, error) {
	b, err := t.blob(h)
	if err != nil {
		return h, err
	}
	if !b.exists {
		b.serial = t.s.nextBlob.Add(1) - 1
		t.newBlobs = true
		if t.dataBatch == nil {
			t.dataBatch = t.s.data.NewBatch()
		}
		if err := writeChunks(t.dataBatch, b.serial, data); err != nil {
			return h, err
		}
		b.exists = true
		b.written = true
		b.size = uint64(len(data))
	}
	return h, nil
}

// FileEntry makes the entry of a file with content data (stored in t).
func (t *Tx) FileEntry(h Header, data []byte) (Entry, error) {
	id, err := t.PutBlob(data)
	if err != nil {
		return Entry{}, err
	}
	return Entry{Header: h, Kind: KindFile, Size: uint64(len(data)), Blob: id}, nil
}

// NewListing stores a listing of entries made in this Tx or before (their
// children and blobs gain a reference) and returns its ID. The listing has
// no reference yet: an entry or a root must refer to it.
func (t *Tx) NewListing(entries []Entry) (ListingID, Totals, error) {
	l := &Listing{Entries: entries}
	for i := range l.Entries {
		if err := t.incEntry(&l.Entries[i]); err != nil {
			return 0, Totals{}, err
		}
	}
	l.Recount()
	id := t.newID()
	t.putListing(id, l)
	return id, l.Totals, nil
}

// SetRoot makes id the root listing of board's gem area, replacing (and
// dropping) any previous one.
func (t *Tx) SetRoot(board string, id ListingID) error {
	if old, _, err := t.root(board); err == nil {
		t.refs[old]--
	} else if !errors.Is(err, ErrNoRoot) {
		return err
	}
	t.refs[id]++
	t.roots[strings.ToLower(board)] = rootState{id: id, name: board}
	return nil
}

// DropRoot removes board's gem area.
func (t *Tx) DropRoot(board string) error {
	old, _, err := t.root(board)
	if err != nil {
		return err
	}
	t.refs[old]--
	t.roots[strings.ToLower(board)] = rootState{}
	return nil
}

// cloneListing copies l: everything it refers to gains a reference.
func (t *Tx) cloneListing(l *Listing) (*Listing, error) {
	n := l.clone()
	for i := range n.Entries {
		if err := t.incEntry(&n.Entries[i]); err != nil {
			return nil, err
		}
	}
	return n, nil
}

type step struct {
	id  ListingID
	l   *Listing
	idx int // index of the entry in the parent listing
}

// chain loads the listings from the root of board to directory path.
func (t *Tx) chain(board string, path []string) ([]step, error) {
	id, _, err := t.root(board)
	if err != nil {
		return nil, err
	}
	l, err := t.listing(id)
	if err != nil {
		return nil, err
	}
	steps := []step{{id: id, l: l, idx: -1}}
	for i, name := range path {
		j := l.Find(-1, name)
		if j < 0 {
			return nil, errorf(ErrNotFound, "%s/%s", board, strings.Join(path[:i+1], "/"))
		}
		e := &l.Entries[j]
		if e.Kind != KindDir {
			return nil, errorf(ErrNotDir, "%s/%s", board, strings.Join(path[:i+1], "/"))
		}
		if l, err = t.listing(e.Child); err != nil {
			return nil, err
		}
		steps = append(steps, step{id: e.Child, l: l, idx: j})
	}
	return steps, nil
}

// Modify changes the directory path of board's gem area with fn, copying
// shared listings on the way first, and returns the totals before and
// after. fn must keep references right (incEntry, decEntry).
func (t *Tx) Modify(board string, path []string, fn func(l *Listing) error) (before, after Totals, err error) {
	steps, err := t.chain(board, path)
	if err != nil {
		return before, after, err
	}
	before = steps[0].l.Totals
	shared := make([]bool, len(steps))
	for i := range steps {
		n, err := t.refCount(steps[i].id)
		if err != nil {
			return before, after, err
		}
		shared[i] = n > 1 || (i > 0 && shared[i-1])
	}

	last := len(steps) - 1
	l, id := steps[last].l, steps[last].id
	if shared[last] {
		if l, err = t.cloneListing(l); err != nil {
			return before, after, err
		}
		id = t.newID()
	}
	if err := fn(l); err != nil {
		return before, after, err
	}
	l.Recount()
	t.putListing(id, l)

	for i := last; i >= 1; i-- {
		p := steps[i-1]
		pl, pid := p.l, p.id
		if shared[i-1] {
			if pl, err = t.cloneListing(pl); err != nil {
				return before, after, err
			}
			pid = t.newID()
		}
		e := &pl.Entries[steps[i].idx]
		if e.Child != id {
			t.refs[e.Child]--
			t.refs[id]++
			e.Child = id
		}
		e.Sub = l.Totals
		pl.Recount()
		t.putListing(pid, pl)
		l, id = pl, pid
	}
	if id != steps[0].id {
		_, name, err := t.root(board)
		if err != nil {
			return before, after, err
		}
		t.refs[steps[0].id]--
		t.refs[id]++
		t.roots[strings.ToLower(board)] = rootState{id: id, name: name}
	}
	return before, l.Totals, nil
}

func (t *Tx) commit(wo *pebble.WriteOptions) error {
	b := t.batch
	for id := range t.dirty {
		if err := b.Set(idKey(pList, id), encodeListing(t.listings[id]), nil); err != nil {
			return err
		}
	}
	for id, delta := range t.refs {
		if delta == 0 {
			continue
		}
		n, err := t.refCount(id)
		if err != nil {
			return err
		}
		switch {
		case n > 0:
			if err := b.Set(idKey(pRef, id), binary.AppendUvarint(nil, n), nil); err != nil {
				return err
			}
		case delta < 0:
			// Its last reference went away: the reaper frees it and
			// drops what it refers to.
			if err := b.Delete(idKey(pRef, id), nil); err != nil {
				return err
			}
			if err := b.Set(idKey(pFree, id), nil, nil); err != nil {
				return err
			}
			t.freed = true
		}
	}
	for h, st := range t.blobs {
		if st.delta == 0 && !st.written {
			continue
		}
		n := int64(st.base) + st.delta
		if n < 0 {
			return errorf(ErrCorruptRecord, "blob %s has %d references", h, n)
		}
		if n == 0 {
			if err := b.Delete(blobKey(h), nil); err != nil {
				return err
			}
			t.dropSerials = append(t.dropSerials, st.serial)
			continue
		}
		v := blobMeta{refs: uint64(n), size: st.size, serial: st.serial}.encode()
		if err := b.Set(blobKey(h), v, nil); err != nil {
			return err
		}
	}
	for board, r := range t.roots {
		k := append([]byte{pRoot}, board...)
		if r.id == 0 {
			if err := b.Delete(k, nil); err != nil {
				return err
			}
			continue
		}
		v := binary.AppendUvarint(nil, uint64(r.id))
		v = append(v, r.name...)
		if err := b.Set(k, v, nil); err != nil {
			return err
		}
	}
	if t.newBlobs {
		if err := b.Set(keyNextBlob, binary.AppendUvarint(nil, t.s.nextBlob.Load()), nil); err != nil {
			return err
		}
	}
	if t.maxID > 0 {
		if err := b.Set(keyNextID, binary.AppendUvarint(nil, t.s.nextID.Load()), nil); err != nil {
			return err
		}
	}
	// The contents first, then the index that refers to them, then the
	// removal of contents nothing refers to any more.
	if t.dataBatch != nil && !t.dataBatch.Empty() {
		if err := t.dataBatch.Commit(wo); err != nil {
			return err
		}
	}
	if !b.Empty() {
		if err := b.Commit(wo); err != nil {
			return err
		}
	}
	if len(t.dropSerials) > 0 {
		db := t.s.data.NewBatch()
		defer db.Close()
		for _, serial := range t.dropSerials {
			p := chunkPrefix(serial)
			if err := db.DeleteRange(p, prefixEnd(p), nil); err != nil {
				return err
			}
		}
		if err := db.Commit(pebble.NoSync); err != nil {
			return err
		}
	}
	if t.freed {
		t.s.wakeReaper()
	}
	return nil
}

// Pin holds a reference to listing id (made by NewListing) without any
// entry or root referring to it, e.g. to keep contents stored by an import
// in pieces; Unpin drops it.
func (t *Tx) Pin(id ListingID) error {
	t.refs[id]++
	return nil
}

// Unpin drops a reference taken by Pin.
func (t *Tx) Unpin(id ListingID) { t.refs[id]-- }
