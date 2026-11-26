package storage

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"math/rand/v2"
	"slices"
	"strings"
	"time"

	"github.com/cockroachdb/pebble"
)

// # Deleted entries
//
// As Lemmy's deleted/removed posts, an entry deleted by moderators goes to
// the trash of its area (with where it was, who and when) until restored or
// purged (gem.ctl purge). It is moved out of its directory so that indexes
// (which mbbsd numbers entries by), totals and quotas exclude it.
//
// The trash of root is the root TrashRoot(root), whose top lists deleted
// entries (last first) under unique names. It is readable as any area but
// changed only by Delete, Restore and Purge.

// TrashPrefix starts the name of the trash of a root.
const TrashPrefix = "trash:"

// TrashRoot is the root of the trash of root.
func TrashRoot(root string) string { return TrashPrefix + root }

// SplitTrashRoot returns the root whose trash s is; ok is false when s is
// not the trash of a root.
func SplitTrashRoot(s string) (root string, ok bool) {
	r, found := strings.CutPrefix(s, TrashPrefix)
	return r, found && ValidRoot(r)
}

// ValidReadRoot tells whether s may be read: a root or the trash of one.
func ValidReadRoot(s string) bool {
	_, ok := SplitTrashRoot(s)
	return ok || ValidRoot(s)
}

// TrashInfo is what is kept of a deleted entry besides the entry.
type TrashInfo struct {
	Name  string   // the name it had (fileheader_t.filename)
	Path  []string // the directory it was in
	Index int      // its index there
	By    string   // who deleted it
	At    int64    // when (Unix time)
}

// pTrash + lower(root) + 0 + name in the trash: encoded TrashInfo.
const pTrash = 'T'

func trashKey(root, name string) []byte {
	k := append([]byte{pTrash}, strings.ToLower(root)...)
	k = append(k, 0)
	return append(k, name...)
}

func trashPrefix(root string) []byte {
	k := append([]byte{pTrash}, strings.ToLower(root)...)
	return append(k, 0)
}

func appendString(b []byte, s string) []byte {
	b = binary.AppendUvarint(b, uint64(len(s)))
	return append(b, s...)
}

func (d *decoder) string() string { return string(d.bytes(int(d.uvarint()))) }

func (ti *TrashInfo) encode() []byte {
	var b []byte
	b = appendString(b, ti.Name)
	b = binary.AppendUvarint(b, uint64(len(ti.Path)))
	for _, p := range ti.Path {
		b = appendString(b, p)
	}
	b = binary.AppendUvarint(b, uint64(max(ti.Index, 0)))
	b = appendString(b, ti.By)
	return binary.AppendVarint(b, ti.At)
}

func decodeTrashInfo(b []byte) (TrashInfo, error) {
	d := &decoder{b: b}
	var ti TrashInfo
	ti.Name = d.string()
	n := d.uvarint()
	if d.err == nil && n > uint64(len(d.b)) {
		return ti, errCorrupt
	}
	for i := uint64(0); i < n && d.err == nil; i++ {
		ti.Path = append(ti.Path, d.string())
	}
	ti.Index = int(d.uvarint())
	ti.By = d.string()
	ti.At = d.varint()
	return ti, d.err
}

func (t *Tx) trashInfo(root, name string) (TrashInfo, error) {
	v, err := getCopy(t.s.db, trashKey(root, name))
	if errors.Is(err, pebble.ErrNotFound) {
		return TrashInfo{}, errorf(ErrNotFound, "%s/%s", TrashRoot(root), name)
	}
	if err != nil {
		return TrashInfo{}, err
	}
	ti, err := decodeTrashInfo(v)
	if err != nil {
		return ti, errorf(ErrCorruptRecord, "trash of %s: %s", root, name)
	}
	return ti, nil
}

// trashName returns a name for an entry of the trash l, in the style of
// stampfile(3): T.<time>.<hex>.
func trashName(l *Listing, now int64) string {
	for {
		n := fmt.Sprintf("T.%d.%03X", now, rand.Uint32()&0xfff)
		if l.Find(-1, n) < 0 {
			return n
		}
		now++
	}
}

// Delete moves the entry r names (a directory with all below it) to the
// trash of its area, deleted by by, and returns its name there.
func (s *Store) Delete(r Ref, by string) (string, error) {
	if !ValidRoot(r.Root) {
		return "", errorf(ErrInvalidName, "root %q", r.Root)
	}
	var name string
	err := s.Update(func(t *Tx) error {
		var e Entry
		at := -1
		_, _, err := t.modifyEntry(r, func(l *Listing, i int) error {
			e, at = l.Entries[i], i
			if err := t.decEntry(&l.Entries[i]); err != nil {
				return err
			}
			l.Entries = slices.Delete(l.Entries, i, i+1)
			return nil
		})
		if err != nil {
			return err
		}
		_, rname, err := t.root(r.Root)
		if err != nil {
			return err
		}
		tr := TrashRoot(rname)
		if _, _, err := t.root(tr); isCode(err, ErrNoRoot) {
			id, _, err := t.NewListing(nil)
			if err != nil {
				return err
			}
			if err := t.SetRoot(tr, id); err != nil {
				return err
			}
		} else if err != nil {
			return err
		}
		now := time.Now().Unix()
		ti := TrashInfo{Name: e.Name(), Path: slices.Clone(r.Path), Index: at, By: by, At: now}
		if err := t.incEntry(&e); err != nil {
			return err
		}
		_, _, err = t.Modify(tr, nil, func(l *Listing) error {
			name = trashName(l, now)
			e.Header.SetName(name)
			l.Entries = slices.Insert(l.Entries, 0, e)
			return nil
		})
		if err != nil {
			return err
		}
		return t.batch.Set(trashKey(rname, name), ti.encode(), nil)
	})
	return name, err
}

// Trash returns the top of the trash of root (an empty one when nothing was
// deleted) and what is kept of each entry, in order.
func (s *Store) Trash(root string) (*Listing, []TrashInfo, error) {
	if !ValidRoot(root) {
		return nil, nil, errorf(ErrInvalidName, "root %q", root)
	}
	snap := s.db.NewSnapshot()
	defer snap.Close()
	l, err := walk(snap, TrashRoot(root), nil)
	if isCode(err, ErrNoRoot) {
		if _, _, err := readRoot(snap, root); err != nil {
			return nil, nil, err
		}
		return &Listing{}, nil, nil
	}
	if err != nil {
		return nil, nil, err
	}
	infos := make([]TrashInfo, len(l.Entries))
	for i := range l.Entries {
		v, err := getCopy(snap, trashKey(root, l.Entries[i].Name()))
		if err != nil {
			continue
		}
		if ti, err := decodeTrashInfo(v); err == nil {
			infos[i] = ti
		}
	}
	return l, infos, nil
}

// TrashEntry returns the entry name of the trash of root and what is kept
// of it.
func (s *Store) TrashEntry(root, name string) (Entry, TrashInfo, error) {
	l, infos, err := s.Trash(root)
	if err != nil {
		return Entry{}, TrashInfo{}, err
	}
	i := l.Find(-1, name)
	if i < 0 {
		return Entry{}, TrashInfo{}, errorf(ErrNotFound, "%s/%s", TrashRoot(root), name)
	}
	return l.Entries[i], infos[i], nil
}

// Restore moves the entry name of the trash of root back to its index in
// its directory (or the end of the top when that is gone), renamed if its
// name is taken, and returns where it went.
func (s *Store) Restore(root, name string) (Ref, error) {
	var out Ref
	if !ValidRoot(root) {
		return out, errorf(ErrInvalidName, "root %q", root)
	}
	err := s.Update(func(t *Tx) error {
		_, rname, err := t.root(root)
		if err != nil {
			return err
		}
		tr := TrashRoot(rname)
		ti, err := t.trashInfo(rname, name)
		if err != nil {
			return err
		}
		var e Entry
		_, _, err = t.modifyAny(Ref{Root: tr, Index: -1, Name: name}, func(l *Listing, i int) error {
			e = l.Entries[i]
			if err := t.decEntry(&l.Entries[i]); err != nil {
				return err
			}
			l.Entries = slices.Delete(l.Entries, i, i+1)
			return nil
		})
		if err != nil {
			return err
		}
		path := ti.Path
		if _, err := t.chain(rname, path); err != nil {
			path = nil
		}
		e.Header.SetName(ti.Name)
		if err := t.incEntry(&e); err != nil {
			return err
		}
		lim := t.s.limits(rname)
		lim.Copy = 0 // back where it was: not a copy
		at := -1
		before, after, err := t.Modify(rname, path, func(l *Listing) error {
			if !ValidName(ti.Name) || l.Find(-1, ti.Name) >= 0 {
				e.Header.SetName("")
			}
			pos := ti.Index
			if len(path) != len(ti.Path) {
				pos = -1
			}
			var err error
			at, err = insert(l, pos, e, lim)
			if err == nil {
				e = l.Entries[at]
			}
			return err
		})
		if err != nil {
			return err
		}
		if err := lim.check(before, after); err != nil {
			return err
		}
		out = Ref{Root: rname, Path: path, Index: at, Name: e.Name()}
		return t.batch.Delete(trashKey(rname, name), nil)
	})
	return out, err
}

// PurgeEntry removes the entry name of the trash of root for good.
func (s *Store) PurgeEntry(root, name string) error {
	if !ValidRoot(root) {
		return errorf(ErrInvalidName, "root %q", root)
	}
	return s.Update(func(t *Tx) error {
		_, rname, err := t.root(root)
		if err != nil {
			return err
		}
		_, _, err = t.modifyAny(Ref{Root: TrashRoot(rname), Index: -1, Name: name}, func(l *Listing, i int) error {
			if err := t.decEntry(&l.Entries[i]); err != nil {
				return err
			}
			l.Entries = slices.Delete(l.Entries, i, i+1)
			return nil
		})
		if err != nil {
			return err
		}
		return t.batch.Delete(trashKey(rname, name), nil)
	})
}

// Purge removes for good the entries of every trash deleted before cutoff
// (Unix time), and returns how many.
func (s *Store) Purge(cutoff int64) (int, error) {
	roots, err := s.trashRoots()
	if err != nil {
		return 0, err
	}
	total := 0
	for _, root := range roots {
		err := s.Update(func(t *Tx) error {
			tr := TrashRoot(root)
			if _, _, err := t.root(tr); err != nil {
				return err
			}
			// Nothing to purge: the trash is left as it is.
			steps, err := t.chain(tr, nil)
			if err != nil {
				return err
			}
			old := false
			for i := range steps[0].l.Entries {
				ti, err := t.trashInfo(root, steps[0].l.Entries[i].Name())
				if err != nil || ti.At < cutoff {
					old = true
					break
				}
			}
			if !old {
				return nil
			}
			var gone []string
			_, _, err = t.Modify(tr, nil, func(l *Listing) error {
				kept := l.Entries[:0]
				for i := range l.Entries {
					e := &l.Entries[i]
					ti, err := t.trashInfo(root, e.Name())
					if err == nil && ti.At >= cutoff {
						kept = append(kept, *e)
						continue
					}
					if err := t.decEntry(e); err != nil {
						return err
					}
					gone = append(gone, e.Name())
				}
				l.Entries = kept
				return nil
			})
			if err != nil {
				return err
			}
			for _, n := range gone {
				if err := t.batch.Delete(trashKey(root, n), nil); err != nil {
					return err
				}
			}
			total += len(gone)
			return nil
		})
		if err != nil && !isCode(err, ErrNoRoot) {
			return total, err
		}
	}
	return total, nil
}

// trashRoots lists the roots that have a trash.
func (s *Store) trashRoots() ([]string, error) {
	it, err := s.db.NewIter(&pebble.IterOptions{LowerBound: []byte{pRoot}, UpperBound: []byte{pRoot + 1}})
	if err != nil {
		return nil, err
	}
	defer it.Close()
	var out []string
	for it.First(); it.Valid(); it.Next() {
		v := it.Value()
		if _, n := binary.Uvarint(v); n > 0 {
			if r, ok := SplitTrashRoot(string(v[n:])); ok {
				out = append(out, r)
			}
		}
	}
	return out, it.Error()
}

// moveTrash moves the trash of root from to root to (RenameRoot), with
// what is kept of its entries.
func (t *Tx) moveTrash(from, to string) error {
	id, _, err := t.root(TrashRoot(from))
	if isCode(err, ErrNoRoot) {
		return nil
	}
	if err != nil {
		return err
	}
	t.roots[strings.ToLower(TrashRoot(from))] = rootState{}
	t.roots[strings.ToLower(TrashRoot(to))] = rootState{id: id, name: TrashRoot(to)}
	return t.rekeyTrash(from, to)
}

// rekeyTrash moves what is kept of the entries of the trash of from to the
// keys of to ("": drops it).
func (t *Tx) rekeyTrash(from, to string) error {
	p := trashPrefix(from)
	it, err := t.s.db.NewIter(&pebble.IterOptions{LowerBound: p, UpperBound: prefixEnd(p)})
	if err != nil {
		return err
	}
	defer it.Close()
	for it.First(); it.Valid(); it.Next() {
		name := string(bytes.Clone(it.Key()[len(p):]))
		if to != "" {
			if err := t.batch.Set(trashKey(to, name), bytes.Clone(it.Value()), nil); err != nil {
				return err
			}
		}
		if strings.EqualFold(from, to) {
			continue
		}
		if err := t.batch.Delete(bytes.Clone(it.Key()), nil); err != nil {
			return err
		}
	}
	return it.Error()
}

// dropTrash removes the trash of root (DeleteRoot).
func (t *Tx) dropTrash(root string) error {
	if _, _, err := t.root(TrashRoot(root)); isCode(err, ErrNoRoot) {
		return nil
	} else if err != nil {
		return err
	}
	if err := t.DropRoot(TrashRoot(root)); err != nil {
		return err
	}
	return t.rekeyTrash(root, "")
}
