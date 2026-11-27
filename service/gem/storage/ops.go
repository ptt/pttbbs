package storage

import (
	"fmt"
	"math/rand/v2"
	"slices"
	"strings"
	"time"
)

// Ref names an entry: its board, directory and name, and the index it had
// when the client last saw it (a hint; -1 for none).
type Ref struct {
	Root  string // see SplitRoot
	Path  []string
	Index int
	Name  string
}

func (r Ref) String() string {
	return r.Root + "/" + strings.Join(append(slices.Clone(r.Path), r.Name), "/")
}

// CreateRoot makes an empty gem area for root (see SplitRoot).
func (s *Store) CreateRoot(board string) error {
	if !ValidRoot(board) {
		return errorf(ErrInvalidName, "root %q", board)
	}
	return s.Update(func(t *Tx) error {
		if _, _, err := t.root(board); err == nil {
			return errorf(ErrRootExists, "%s", board)
		}
		id, _, err := t.NewListing(nil)
		if err != nil {
			return err
		}
		return t.SetRoot(board, id)
	})
}

// EnsureRoot makes an empty gem area for board unless it has one.
func (s *Store) EnsureRoot(board string) error {
	if s.HasRoot(board) {
		return nil
	}
	if err := s.CreateRoot(board); err != nil && !isCode(err, ErrRootExists) {
		return err
	}
	return nil
}

// DeleteRoot removes board's gem area (freed in the background), with its
// trash.
func (s *Store) DeleteRoot(board string) error {
	if !ValidRoot(board) {
		return errorf(ErrInvalidName, "root %q", board)
	}
	return s.Update(func(t *Tx) error {
		if err := t.DropRoot(board); err != nil {
			return err
		}
		return t.dropTrash(board)
	})
}

// RenameRoot moves the gem area of board from to board to.
func (s *Store) RenameRoot(from, to string) error {
	if !ValidRoot(to) {
		return errorf(ErrInvalidName, "root %q", to)
	}
	return s.Update(func(t *Tx) error {
		id, _, err := t.root(from)
		if err != nil {
			return err
		}
		if strings.EqualFold(from, to) {
			t.roots[strings.ToLower(to)] = rootState{id: id, name: to}
			return t.moveTrash(from, to)
		}
		if _, _, err := t.root(to); err == nil {
			return errorf(ErrRootExists, "%s", to)
		}
		t.roots[strings.ToLower(from)] = rootState{}
		t.roots[strings.ToLower(to)] = rootState{id: id, name: to}
		return t.moveTrash(from, to)
	})
}

func isCode(err error, base *Error) bool {
	e, ok := err.(*Error)
	return ok && e.Code == base.Code
}

// uniqueName returns a new name for an entry of kind in l, in the style of
// stampfile(3) / stampadir(3): M.<time>.A.<hex> for files, D<hex>
// for directories.
func uniqueName(l *Listing, kind Kind) string {
	now := time.Now().Unix()
	for {
		var n string
		if kind == KindDir {
			n = fmt.Sprintf("D%X", rand.Uint32()&0xffff)
		} else {
			n = fmt.Sprintf("M.%d.A.%03X", now, rand.Uint32()&0xfff)
		}
		if l.Find(-1, n) < 0 {
			return n
		}
		now++
	}
}

// insert puts e at pos (-1 or past the end: appended) in l, naming it when
// its header has no name, and returns where it went.
func insert(l *Listing, pos int, e Entry, lim Limits) (int, error) {
	name := e.Name()
	if name == "" {
		e.Header.SetName(uniqueName(l, e.Kind))
	} else if !ValidName(name) {
		return -1, errorf(ErrInvalidName, "%q", name)
	} else if l.Find(-1, name) >= 0 {
		return -1, errorf(ErrExists, "%s", name)
	}
	if err := lim.checkWidth(len(l.Entries) + 1); err != nil {
		return -1, err
	}
	if pos < 0 || pos > len(l.Entries) {
		pos = len(l.Entries)
	}
	l.Entries = slices.Insert(l.Entries, pos, e)
	return pos, nil
}

// add puts entry e (whose references t already holds) into directory path
// of board, checking the quotas.
func (t *Tx) add(board string, path []string, pos int, e Entry) (Entry, int, error) {
	if !ValidRoot(board) {
		return e, -1, errorf(ErrInvalidName, "root %q", board)
	}
	lim := t.s.limits(board)
	if err := lim.checkCopy(e.Totals()); err != nil {
		return e, -1, err
	}
	if e.Mtime == 0 {
		e.Mtime = time.Now().Unix()
	}
	at := -1
	before, after, err := t.Modify(board, path, func(l *Listing) error {
		var err error
		at, err = insert(l, pos, e, lim)
		if err == nil {
			e = l.Entries[at]
		}
		return err
	})
	if err != nil {
		return e, -1, err
	}
	if err := lim.check(before, after); err != nil {
		return e, -1, err
	}
	return e, at, nil
}

// AddFile adds a file with header h (an empty name is made up) and content
// data at pos (-1: at the end) of directory path. It returns the entry and
// its index.
func (s *Store) AddFile(board string, path []string, pos int, h Header, data []byte) (Entry, int, error) {
	var e Entry
	at := -1
	err := s.Update(func(t *Tx) error {
		fe, err := t.FileEntry(h, data)
		if err != nil {
			return err
		}
		if err := t.incEntry(&fe); err != nil {
			return err
		}
		e, at, err = t.add(board, path, pos, fe)
		return err
	})
	return e, at, err
}

// AttachDir adds listing id (made by an import, not referred to yet) as a
// directory with header h at pos of directory path. Quotas do not apply,
// as to an import.
func (s *Store) AttachDir(board string, path []string, pos int, h Header, id ListingID) (Entry, int, error) {
	var e Entry
	at := -1
	if !ValidRoot(board) {
		return e, at, errorf(ErrInvalidName, "root %q", board)
	}
	err := s.Update(func(t *Tx) error {
		l, err := t.listing(id)
		if err != nil {
			return err
		}
		de := Entry{Header: h, Kind: KindDir, Child: id, Sub: l.Totals, Mtime: time.Now().Unix()}
		t.refs[id]++
		_, _, err = t.Modify(board, path, func(dl *Listing) error {
			var err error
			if at, err = insert(dl, pos, de, Limits{}); err == nil {
				e = dl.Entries[at]
			}
			return err
		})
		return err
	})
	return e, at, err
}

// AddDir adds an empty directory with header h at pos of directory path.
func (s *Store) AddDir(board string, path []string, pos int, h Header) (Entry, int, error) {
	var e Entry
	at := -1
	err := s.Update(func(t *Tx) error {
		id, tot, err := t.NewListing(nil)
		if err != nil {
			return err
		}
		de := Entry{Header: h, Kind: KindDir, Child: id, Sub: tot}
		t.refs[id]++
		e, at, err = t.add(board, path, pos, de)
		return err
	})
	return e, at, err
}

// modifyEntry runs fn on the entry r names, in its listing; r is in an
// area (a trash is changed only by its own calls, see modifyAny).
func (t *Tx) modifyEntry(r Ref, fn func(l *Listing, i int) error) (before, after Totals, err error) {
	if !ValidRoot(r.Root) {
		return before, after, errorf(ErrInvalidName, "root %q", r.Root)
	}
	return t.modifyAny(r, fn)
}

// modifyAny is modifyEntry in an area or a trash.
func (t *Tx) modifyAny(r Ref, fn func(l *Listing, i int) error) (before, after Totals, err error) {
	return t.Modify(r.Root, r.Path, func(l *Listing) error {
		i := l.Find(r.Index, r.Name)
		if i < 0 {
			return errorf(ErrNotFound, "%s", r)
		}
		return fn(l, i)
	})
}

// SetHeader replaces the header of the entry r names with h. The name may
// change (to one not taken); an empty name keeps it.
func (s *Store) SetHeader(r Ref, h Header) (Entry, error) {
	var e Entry
	err := s.Update(func(t *Tx) error {
		_, _, err := t.modifyEntry(r, func(l *Listing, i int) error {
			name := h.Name()
			if name == "" {
				h.SetName(r.Name)
			} else if name != l.Entries[i].Name() {
				if !ValidName(name) {
					return errorf(ErrInvalidName, "%q", name)
				}
				if l.Find(-1, name) >= 0 {
					return errorf(ErrExists, "%s", name)
				}
			}
			l.Entries[i].Header = h
			e = l.Entries[i]
			return nil
		})
		return err
	})
	return e, err
}

// PutFile replaces the content of the file r names with data.
func (s *Store) PutFile(r Ref, data []byte) (Entry, error) {
	var e Entry
	err := s.Update(func(t *Tx) error {
		id, err := t.PutBlob(data)
		if err != nil {
			return err
		}
		before, after, err := t.modifyEntry(r, func(l *Listing, i int) error {
			old := &l.Entries[i]
			if old.Kind != KindFile && old.Kind != KindMissing {
				return errorf(ErrNotFile, "%s is a %s", r, old.Kind)
			}
			ne := Entry{Header: old.Header, Kind: KindFile, Size: uint64(len(data)), Blob: id, Mtime: time.Now().Unix()}
			if err := t.incEntry(&ne); err != nil {
				return err
			}
			if err := t.decEntry(old); err != nil {
				return err
			}
			l.Entries[i] = ne
			e = ne
			return nil
		})
		if err != nil {
			return err
		}
		return t.s.limits(r.Root).check(before, after)
	})
	return e, err
}

// Remove removes the entry r names (a directory with all below it).
func (s *Store) Remove(r Ref) (Entry, error) {
	var e Entry
	err := s.Update(func(t *Tx) error {
		_, _, err := t.modifyEntry(r, func(l *Listing, i int) error {
			e = l.Entries[i]
			if err := t.decEntry(&l.Entries[i]); err != nil {
				return err
			}
			l.Entries = slices.Delete(l.Entries, i, i+1)
			return nil
		})
		return err
	})
	return e, err
}

// Reorder moves the entry r names to index to within its directory.
func (s *Store) Reorder(r Ref, to int) (int, error) {
	at := -1
	err := s.Update(func(t *Tx) error {
		_, _, err := t.modifyEntry(r, func(l *Listing, i int) error {
			if to < 0 || to >= len(l.Entries) {
				return errorf(ErrInvalidPos, "%d", to)
			}
			e := l.Entries[i]
			l.Entries = slices.Delete(l.Entries, i, i+1)
			l.Entries = slices.Insert(l.Entries, to, e)
			at = to
			return nil
		})
		return err
	})
	return at, err
}

// source finds the entry r names, within t.
func (t *Tx) source(r Ref) (Entry, []step, error) {
	steps, err := t.chain(r.Root, r.Path)
	if err != nil {
		return Entry{}, nil, err
	}
	l := steps[len(steps)-1].l
	i := l.Find(r.Index, r.Name)
	if i < 0 {
		return Entry{}, nil, errorf(ErrNotFound, "%s", r)
	}
	return l.Entries[i], steps, nil
}

// Copy copies the entry src names into directory dstPath of dstBoard at
// pos, with header h (nil: the header of src). A directory is copied with
// all below it in O(1): the copy shares it until either side changes.
func (s *Store) Copy(src Ref, dstBoard string, dstPath []string, pos int, h *Header) (Entry, int, error) {
	var e Entry
	at := -1
	err := s.Update(func(t *Tx) error {
		se, _, err := t.source(src)
		if err != nil {
			return err
		}
		if h != nil {
			se.Header = *h
		}
		se.Mtime = 0 // the copy is new (add stamps it)
		lim := t.s.limits(dstBoard)
		if lim.Depth > 0 && se.Kind == KindDir && int64(len(dstPath))+int64(se.Totals().Depth) > lim.Depth {
			return errorf(ErrQuota, "depth %d > %d", len(dstPath)+int(se.Totals().Depth), lim.Depth)
		}
		// The reference is taken first, so that a copy into its own tree
		// finds that tree shared and copies the path to it instead of
		// changing it under the copy.
		if err := t.incEntry(&se); err != nil {
			return err
		}
		e, at, err = t.add(dstBoard, dstPath, pos, se)
		return err
	})
	return e, at, err
}

// Move moves the entry src names into directory dstPath of dstBoard at pos
// (-1: the end), keeping its header, or h when not nil. Within a directory,
// use Reorder.
func (s *Store) Move(src Ref, dstBoard string, dstPath []string, pos int, h *Header) (Entry, int, error) {
	var e Entry
	at := -1
	err := s.Update(func(t *Tx) error {
		if !ValidRoot(src.Root) || !ValidRoot(dstBoard) {
			return errorf(ErrInvalidName, "root %q or %q", src.Root, dstBoard)
		}
		if strings.EqualFold(src.Root, dstBoard) && slices.Equal(src.Path, dstPath) {
			return errorf(ErrInvalidPos, "%s is already there; reorder it instead", src)
		}
		se, _, err := t.source(src)
		if err != nil {
			return err
		}
		if se.Kind == KindDir {
			dsteps, err := t.chain(dstBoard, dstPath)
			if err != nil {
				return err
			}
			for _, d := range dsteps {
				if d.id == se.Child {
					return errorf(ErrMoveIntoSelf, "%s", src)
				}
			}
		}
		if h != nil {
			se.Header = *h
		}
		lim := t.s.limits(dstBoard)
		if lim.Depth > 0 && se.Kind == KindDir && int64(len(dstPath))+int64(se.Totals().Depth) > lim.Depth {
			return errorf(ErrQuota, "depth %d > %d", len(dstPath)+int(se.Totals().Depth), lim.Depth)
		}
		// Within the same board the entries only change place: no copy
		// quota (moving a large tree is fine), totals unchanged.
		if strings.EqualFold(src.Root, dstBoard) {
			lim.Copy = 0
		}
		if err := t.incEntry(&se); err != nil {
			return err
		}
		at = -1
		before, after, err := t.Modify(dstBoard, dstPath, func(l *Listing) error {
			var err error
			if at, err = insert(l, pos, se, lim); err == nil {
				e = l.Entries[at]
			}
			return err
		})
		if err != nil {
			return err
		}
		if err := lim.checkCopy(se.Totals()); err != nil {
			return err
		}
		if !strings.EqualFold(src.Root, dstBoard) {
			if err := lim.check(before, after); err != nil {
				return err
			}
		}
		// The source is found again: the change above may have copied
		// its directory (when it shared listings with the destination).
		_, _, err = t.modifyEntry(Ref{Root: src.Root, Path: src.Path, Index: -1, Name: src.Name}, func(l *Listing, i int) error {
			if err := t.decEntry(&l.Entries[i]); err != nil {
				return err
			}
			l.Entries = slices.Delete(l.Entries, i, i+1)
			return nil
		})
		return err
	})
	return e, at, err
}

// Expire removes for good the files of directory path of root made or last
// changed before cutoff (Unix time), e.g. the songs ordered (Note SONGO)
// after some days, and returns how many. Directories and other entries are
// left.
func (s *Store) Expire(root string, path []string, cutoff int64) (int, error) {
	n := 0
	err := s.Update(func(t *Tx) error {
		if !ValidRoot(root) {
			return errorf(ErrInvalidName, "root %q", root)
		}
		steps, err := t.chain(root, path)
		if err != nil {
			return err
		}
		old := func(e *Entry) bool { return e.Kind == KindFile && e.Mtime < cutoff }
		if !slices.ContainsFunc(steps[len(steps)-1].l.Entries, func(e Entry) bool { return old(&e) }) {
			return nil
		}
		_, _, err = t.Modify(root, path, func(l *Listing) error {
			kept := l.Entries[:0]
			for i := range l.Entries {
				e := &l.Entries[i]
				if !old(e) {
					kept = append(kept, *e)
					continue
				}
				if err := t.decEntry(e); err != nil {
					return err
				}
				n++
			}
			l.Entries = kept
			return nil
		})
		return err
	})
	return n, err
}
