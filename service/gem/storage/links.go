package storage

import (
	"slices"
	"strings"
)

// LinkTarget says where the link target (as readlink(2) gave it), in
// directory dir of root, points: a root and the path of names from it (none:
// the root itself). ok is false when it points out of the gem areas.
type LinkTarget func(root string, dir []string, target string) (troot string, path []string, ok bool)

// ResolveStats counts what ResolveLinks made of the links.
type ResolveStats struct {
	Dirs    int // a directory, now shared with the link (copy on write)
	Files   int // a file, now sharing its content
	Indexes int // a table of contents (KindIndex)
	Broken  int // pointing nowhere, out of the gem areas, through a link
	Loops   int // to a directory the link is in (or a loop of links)
}

// Total is the number of links resolved.
func (s ResolveStats) Total() int { return s.Dirs + s.Files + s.Indexes + s.Broken + s.Loops }

// maxLinkHops bounds a link to a link to a link...
const maxLinkHops = 8

type resolved struct {
	id  ListingID
	tot Totals
}

// ResolveOptions control ResolveLinksWith.
type ResolveOptions struct {
	Target LinkTarget
	// IsIndex tells a link to the table of contents, made a KindIndex
	// entry whose header IndexHeader sets (it may rename it away from
	// taken). Both may be nil.
	IsIndex     func(target string) bool
	IndexHeader func(h *Header, taken map[string]bool)
	Logf        func(format string, args ...any)
}

type resolver struct {
	t      *Tx
	opts   ResolveOptions
	target LinkTarget
	logf   func(format string, args ...any)
	stats  ResolveStats
	memo   map[ListingID]resolved
	open   map[ListingID]bool // being resolved (an ancestor)
	roots  map[string]ListingID
}

// ResolveLinks replaces every symbolic link of every gem area by what it
// points to (a shared listing, the same content, or a KindIndex entry),
// keeping its header. Broken, outside, chained or self-containing links
// become missing.
//
// Only listings with links below them are rewritten, in one Tx. A shared
// listing resolves relative links from the first place it is found in.
func (s *Store) ResolveLinks(target LinkTarget, logf func(format string, args ...any)) (ResolveStats, error) {
	return s.ResolveLinksWith(ResolveOptions{Target: target, Logf: logf})
}

// ResolveLinksWith is ResolveLinks with the links to the table of contents
// told apart (see ResolveOptions).
func (s *Store) ResolveLinksWith(opts ResolveOptions) (ResolveStats, error) {
	logf := opts.Logf
	if logf == nil {
		logf = func(string, ...any) {}
	}
	var stats ResolveStats
	err := s.Update(func(t *Tx) error {
		r := &resolver{t: t, opts: opts, target: opts.Target, logf: logf, memo: map[ListingID]resolved{},
			open: map[ListingID]bool{}, roots: map[string]ListingID{}}
		roots, err := s.Roots()
		if err != nil {
			return err
		}
		for _, root := range roots {
			id, _, err := t.root(root)
			if err != nil {
				return err
			}
			r.roots[strings.ToLower(root)] = id
		}
		for _, root := range roots {
			id := r.roots[strings.ToLower(root)]
			res, err := r.listing(id, root, nil)
			if err != nil {
				return err
			}
			if res.id != id {
				if err := t.SetRoot(root, res.id); err != nil {
					return err
				}
			}
		}
		stats = r.stats
		return nil
	})
	return stats, err
}

// listing returns listing id (directory path of root) with its links
// resolved: itself when it has none below it.
func (r *resolver) listing(id ListingID, root string, path []string) (resolved, error) {
	if res, ok := r.memo[id]; ok {
		return res, nil
	}
	// The committed state, read without keeping it in the Tx (most of it
	// is not changed).
	l, err := readListing(r.t.s.db, id)
	if err != nil {
		return resolved{}, err
	}
	if l.Totals.Links == 0 {
		res := resolved{id, l.Totals}
		r.memo[id] = res
		return res, nil
	}
	r.open[id] = true
	entries := slices.Clone(l.Entries)
	var taken map[string]bool
	for i := range entries {
		e := &entries[i]
		switch e.Kind {
		case KindDir:
			if e.Sub.Links == 0 {
				continue
			}
			sub, err := r.listing(e.Child, root, append(slices.Clone(path), e.Name()))
			if err != nil {
				return resolved{}, err
			}
			e.Child, e.Sub = sub.id, sub.tot
		case KindLink:
			if r.opts.IsIndex != nil && r.opts.IsIndex(e.Target) {
				if taken == nil {
					taken = map[string]bool{}
					for j := range entries {
						taken[entries[j].Name()] = true
					}
				}
				*e = Entry{Header: e.Header, Kind: KindIndex, Mtime: e.Mtime}
				if r.opts.IndexHeader != nil {
					r.opts.IndexHeader(&e.Header, taken)
				}
				r.stats.Indexes++
				continue
			}
			if err := r.link(e, root, path); err != nil {
				return resolved{}, err
			}
		}
	}
	delete(r.open, id)
	nid, tot, err := r.t.NewListing(entries)
	if err != nil {
		return resolved{}, err
	}
	res := resolved{nid, tot}
	r.memo[id] = res
	return res, nil
}

// link replaces the link e, in directory dir of root, by what it points to.
func (r *resolver) link(e *Entry, root string, dir []string) error {
	where := Ref{Root: root, Path: dir, Name: e.Name()}.String()
	broken := func(why string) {
		r.logf("%s -> %s: %s", where, e.Target, why)
		*e = Entry{Header: e.Header, Kind: KindMissing, Mtime: e.Mtime}
	}
	lroot, ldir, target := root, dir, e.Target
	for hop := 0; ; hop++ {
		if hop == maxLinkHops {
			r.stats.Loops++
			broken("too many links")
			return nil
		}
		troot, tpath, ok := r.target(lroot, ldir, target)
		if !ok {
			r.stats.Broken++
			broken("out of the gem areas")
			return nil
		}
		id, ok := r.roots[strings.ToLower(troot)]
		if !ok {
			r.stats.Broken++
			broken("no gem area " + troot)
			return nil
		}
		if len(tpath) == 0 {
			return r.toDir(e, id, troot, nil, broken)
		}
		parent, err := walkTo(r.t, id, tpath[:len(tpath)-1])
		if err != nil {
			r.stats.Broken++
			broken(err.Error())
			return nil
		}
		j := parent.Find(-1, tpath[len(tpath)-1])
		if j < 0 {
			r.stats.Broken++
			broken("not found")
			return nil
		}
		te := parent.Entries[j]
		switch te.Kind {
		case KindDir:
			return r.toDir(e, te.Child, troot, tpath, broken)
		case KindFile:
			*e = Entry{Header: e.Header, Kind: KindFile, Size: te.Size, Blob: te.Blob, Mtime: e.Mtime}
			r.stats.Files++
			return nil
		case KindIndex:
			*e = Entry{Header: e.Header, Kind: KindIndex, Mtime: e.Mtime}
			r.stats.Indexes++
			return nil
		case KindLink:
			lroot, ldir, target = troot, tpath[:len(tpath)-1], te.Target
			continue
		}
		r.stats.Broken++
		broken("missing")
		return nil
	}
}

// toDir makes e the directory id (path of root), resolved.
func (r *resolver) toDir(e *Entry, id ListingID, root string, path []string, broken func(string)) error {
	if r.open[id] {
		r.stats.Loops++
		broken("a directory it is in")
		return nil
	}
	res, err := r.listing(id, root, path)
	if err != nil {
		return err
	}
	*e = Entry{Header: e.Header, Kind: KindDir, Child: res.id, Sub: res.tot, Mtime: e.Mtime}
	r.stats.Dirs++
	return nil
}

// walkTo follows the directories path from listing id, as committed.
func walkTo(t *Tx, id ListingID, path []string) (*Listing, error) {
	l, err := readListing(t.s.db, id)
	if err != nil {
		return nil, err
	}
	for i, name := range path {
		j := l.Find(-1, name)
		if j < 0 {
			return nil, errorf(ErrNotFound, "%s", strings.Join(path[:i+1], "/"))
		}
		e := &l.Entries[j]
		if e.Kind != KindDir {
			return nil, errorf(ErrNotDir, "%s is a %s", strings.Join(path[:i+1], "/"), e.Kind)
		}
		if l, err = readListing(t.s.db, e.Child); err != nil {
			return nil, err
		}
	}
	return l, nil
}
