// Package legacy moves gem areas between their files (a .DIR in each
// directory, one file per article: man/boards/<c>/<board>, man/ for the
// site, home/<c>/<id>/man for a person) and the storage of gem.svc.
package legacy

import (
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"sync"

	"pttbbs/gem/storage"
)

// RootDir is the man/ directory of the gem area root (as setapath(3) and
// sethomeman(3)), or "" for a bad root.
func RootDir(bbsHome, root string) string {
	kind, name, ok := storage.SplitRoot(root)
	switch {
	case !ok:
		return ""
	case kind == "board":
		return filepath.Join(bbsHome, "man", "boards", name[:1], name)
	case kind == "user":
		return filepath.Join(bbsHome, "home", name[:1], name, "man")
	}
	return filepath.Join(bbsHome, "man")
}

// IndexName is the table of contents util/mandex writes in the top of a
// gem area, which the entry titled for it names (or links to).
const IndexName = ".index"

// IndexEntryName names the table of contents entry (KindIndex) in place of
// mandex's S<hex time> link. A name starting with '.' would mark a deleted
// record in a .DIR.
const IndexEntryName = "S.index"

// The title and owner (Big5) of the entry of the table of contents, in
// place of what mandex wrote: "◎ 精華區目錄索引" without its size (which
// changes with every change, and is not kept), and "自動更新" (updated
// automatically) rather than "每天自動更新" (updated every day): it is made
// when read, and sent with the time it is read (protocol.FromEntry).
var (
	indexTitle = []byte("\xa1\xb7 \xba\xeb\xb5\xd8\xb0\xcf\xa5\xd8\xbf\xfd\xaf\xc1\xa4\xde")
	indexOwner = "\xa6\xdb\xb0\xca\xa7\xf3\xb7\x73"
)

// isIndexLink tells whether a link to target is the one mandex made to the
// table of contents (.index, in the same directory).
func isIndexLink(target string) bool {
	return filepath.Base(target) == IndexName
}

// indexEntry makes e (a record of a .DIR) the entry of the table of
// contents, named IndexEntryName unless taken.
func indexEntry(e *storage.Entry, taken map[string]bool) {
	e.Kind = storage.KindIndex
	setIndexHeader(&e.Header, taken)
}

// setIndexHeader names and titles the entry of the table of contents.
func setIndexHeader(h *storage.Header, taken map[string]bool) {
	if !taken[IndexEntryName] {
		h.SetName(IndexEntryName)
		taken[IndexEntryName] = true
	}
	h.SetTitle(indexTitle)
	h.SetOwner(indexOwner)
}

// MaxDepth is how deep an import goes; deeper directories are imported
// empty (PTT had trees 52 levels deep in 2026).
const MaxDepth = 256

// flushBytes is how much content of one directory is held in memory before
// it is stored.
var flushBytes int64 = 64 << 20

// ImportOptions control ImportRoot.
type ImportOptions struct {
	// Dir is the gem area to import (default RootDir(BBSHome, Root)).
	Dir     string
	BBSHome string
	Root    string // see storage.SplitRoot
	// Replace an area the root already has (else ErrRootExists).
	Replace bool
	// Attach, if set, adds Dir as a directory with this header at the end
	// of the area of Root, which must exist, instead of making the area.
	Attach *storage.Header
	// Workers read the files of a directory in parallel (default 8).
	Workers int
	// Logf reports problems (files that cannot be read); may be nil.
	Logf func(format string, args ...any)
	// Stop, when closed, ends the import at the next directory (with
	// ErrStopped; what was stored is freed by VERIFY fix).
	Stop <-chan struct{}
}

// ErrStopped is returned by an import stopped by ImportOptions.Stop.
var ErrStopped = errors.New("import stopped")

// ImportStats counts what an import found.
type ImportStats struct {
	Dirs     int
	Files    int
	Links    int
	Indexes  int   // entries of the table of contents (see IndexEntryName)
	Missing  int   // .DIR records whose file is gone
	Unlisted int   // files and directories no .DIR lists (not imported)
	Errors   int   // files that could not be read (imported as missing)
	Bytes    int64 // of the files
	TooDeep  int   // directories past MaxDepth (imported empty)
}

func (s ImportStats) String() string {
	return fmt.Sprintf("dirs=%d files=%d links=%d indexes=%d missing=%d unlisted=%d errors=%d bytes=%d too_deep=%d",
		s.Dirs, s.Files, s.Links, s.Indexes, s.Missing, s.Unlisted, s.Errors, s.Bytes, s.TooDeep)
}

type importer struct {
	st    *storage.Store
	opts  ImportOptions
	mu    sync.Mutex
	stats ImportStats
}

func (im *importer) logf(format string, args ...any) {
	if im.opts.Logf != nil {
		im.opts.Logf(format, args...)
	}
}

func (im *importer) count(fn func(s *ImportStats)) {
	im.mu.Lock()
	fn(&im.stats)
	im.mu.Unlock()
}

// ImportRoot imports a gem area from files into st, as the area of root.
// The files are left as they are. Quotas do not apply.
func ImportRoot(st *storage.Store, opts ImportOptions) (ImportStats, error) {
	if !storage.ValidRoot(opts.Root) {
		return ImportStats{}, fmt.Errorf("invalid root %q", opts.Root)
	}
	if opts.Dir == "" {
		opts.Dir = RootDir(opts.BBSHome, opts.Root)
	}
	if opts.Workers <= 0 {
		opts.Workers = 8
	}
	if opts.Attach != nil && !st.HasRoot(opts.Root) {
		return ImportStats{}, &storage.Error{Code: storage.ErrNoRoot.Code, Detail: opts.Root}
	}
	if opts.Attach == nil && !opts.Replace && st.HasRoot(opts.Root) {
		return ImportStats{}, &storage.Error{Code: storage.ErrRootExists.Code, Detail: opts.Root}
	}
	if fi, err := os.Stat(opts.Dir); err != nil {
		return ImportStats{}, err
	} else if !fi.IsDir() {
		return ImportStats{}, fmt.Errorf("%s is not a directory", opts.Dir)
	}
	im := &importer{st: st, opts: opts}
	id, err := im.importDir(opts.Dir, 0)
	if err != nil {
		return im.stats, err
	}
	if opts.Attach != nil {
		_, _, err = st.AttachDir(opts.Root, nil, -1, *opts.Attach, id)
		return im.stats, err
	}
	err = st.Update(func(t *storage.Tx) error {
		if !opts.Replace && st.HasRoot(opts.Root) {
			return &storage.Error{Code: storage.ErrRootExists.Code, Detail: opts.Root}
		}
		return t.SetRoot(opts.Root, id)
	})
	return im.stats, err
}

// readDIR returns the records of a .DIR (none when there is no .DIR).
func readDIR(path string) ([]storage.Header, error) {
	data, err := os.ReadFile(path)
	if errors.Is(err, fs.ErrNotExist) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	hs := make([]storage.Header, len(data)/storage.HeaderSize)
	for i := range hs {
		copy(hs[i][:], data[i*storage.HeaderSize:])
	}
	return hs, nil
}

type fileJob struct {
	idx  int
	path string
	size int64
	data []byte
	hash storage.BlobID
	err  error
}

// importDir imports directory dir and all below it, and returns the ID of
// its listing (no reference to it is held yet).
func (im *importer) importDir(dir string, depth int) (storage.ListingID, error) {
	select {
	case <-im.opts.Stop:
		return 0, ErrStopped
	default:
	}
	headers, err := readDIR(filepath.Join(dir, ".DIR"))
	if err != nil {
		return 0, err
	}
	entries := make([]storage.Entry, 0, len(headers))
	var files []*fileJob
	listed := map[string]bool{}
	// Names of the records, so that the table of contents is renamed only
	// to a name no other record has.
	taken := map[string]bool{}
	for i := range headers {
		taken[headers[i].Name()] = true
	}
	for _, h := range headers {
		name := h.Name()
		e := storage.Entry{Header: h, Kind: storage.KindMissing}
		// Records with no or bad names are kept as they are (missing), so
		// that the .DIR comes back the same.
		if !storage.ValidName(name) {
			im.count(func(s *ImportStats) { s.Missing++ })
			entries = append(entries, e)
			continue
		}
		listed[name] = true
		path := filepath.Join(dir, name)
		fi, err := os.Lstat(path)
		if err == nil {
			e.Mtime = fi.ModTime().Unix()
		}
		switch {
		case err != nil:
			im.count(func(s *ImportStats) { s.Missing++ })
		case fi.Mode()&fs.ModeSymlink != 0:
			target, err := os.Readlink(path)
			if err != nil {
				im.logf("readlink %s: %v", path, err)
				im.count(func(s *ImportStats) { s.Errors++ })
				break
			}
			if isIndexLink(target) {
				// The table of contents mandex made: not kept, gem.svc
				// makes it (INDEX).
				indexEntry(&e, taken)
				im.count(func(s *ImportStats) { s.Indexes++ })
				break
			}
			e.Kind = storage.KindLink
			e.Target = target
			im.count(func(s *ImportStats) { s.Links++ })
		case fi.IsDir():
			var child storage.ListingID
			var tot storage.Totals
			if depth+1 >= MaxDepth {
				im.count(func(s *ImportStats) { s.TooDeep++ })
				err = im.st.UpdateNoSync(func(t *storage.Tx) error {
					var err error
					child, tot, err = t.NewListing(nil)
					return err
				})
			} else {
				child, err = im.importDir(path, depth+1)
				if err == nil {
					tot, err = im.totals(child)
				}
			}
			if err != nil {
				return 0, err
			}
			e.Kind = storage.KindDir
			e.Child = child
			e.Sub = tot
			im.count(func(s *ImportStats) { s.Dirs++ })
		case fi.Mode().IsRegular() && name == IndexName:
			// The table of contents named as it is (before mandex linked
			// to it): not kept either.
			indexEntry(&e, taken)
			im.count(func(s *ImportStats) { s.Indexes++ })
		case fi.Mode().IsRegular():
			e.Kind = storage.KindFile
			files = append(files, &fileJob{idx: len(entries), path: path, size: fi.Size()})
		default:
			im.count(func(s *ImportStats) { s.Missing++ })
		}
		entries = append(entries, e)
	}
	im.countUnlisted(dir, listed)

	// A group of files stored before the listing is pinned by a listing of
	// its own, dropped once the directory's listing holds the contents.
	var pins []storage.ListingID
	for start := 0; start < len(files); {
		end := start
		var size int64
		for end < len(files) && (end == start || size < flushBytes) {
			size += files[end].size
			end++
		}
		group := files[start:end]
		im.readFiles(group)
		last := end == len(files)
		if !last {
			pin, err := im.store(group, entries, nil, nil, true)
			if err != nil {
				return 0, err
			}
			pins = append(pins, pin)
			for _, f := range group {
				f.data = nil
			}
		} else {
			return im.store(group, entries, pins, files, false)
		}
		start = end
	}
	return im.store(nil, entries, pins, files, false)
}

// store stores the contents of group, then either pins them (pin: returns
// the ID of a pinned listing of them) or stores the listing of entries
// (whose files are all; dropping pins) and returns its ID.
func (im *importer) store(group []*fileJob, entries []storage.Entry, pins []storage.ListingID, all []*fileJob, pin bool) (storage.ListingID, error) {
	var id storage.ListingID
	// The contents are written first, without the writer's lock, which
	// the Tx below then holds only to record them and the listing.
	var hashes []storage.BlobID
	var datas [][]byte
	for _, f := range group {
		if f.err == nil {
			hashes = append(hashes, f.hash)
			datas = append(datas, f.data)
		}
	}
	staged, err := im.st.StageBlobs(hashes, datas)
	if err != nil {
		return 0, err
	}
	err = im.st.UpdateNoSync(func(t *storage.Tx) error {
		var pinned []storage.Entry
		n := 0
		for _, f := range group {
			e := &entries[f.idx]
			if f.err != nil {
				continue
			}
			if _, err := t.PutStagedBlob(staged[n], f.data); err != nil {
				return err
			}
			n++
			e.Size = uint64(len(f.data))
			e.Blob = f.hash
			pinned = append(pinned, *e)
		}
		if pin {
			var err error
			if id, _, err = t.NewListing(pinned); err != nil {
				return err
			}
			return t.Pin(id)
		}
		for _, f := range all {
			if f.err != nil {
				entries[f.idx].Kind = storage.KindMissing
			}
		}
		var err error
		if id, _, err = t.NewListing(entries); err != nil {
			return err
		}
		for _, p := range pins {
			t.Unpin(p)
		}
		return nil
	})
	return id, err
}

func (im *importer) readFiles(group []*fileJob) {
	var wg sync.WaitGroup
	sem := make(chan struct{}, im.opts.Workers)
	for _, f := range group {
		wg.Add(1)
		sem <- struct{}{}
		go func(f *fileJob) {
			defer func() { <-sem; wg.Done() }()
			// One buffer of the size of the file (io.ReadAll grows and
			// copies its own, most of the garbage of an import).
			var err error
			f.data, err = os.ReadFile(f.path)
			if err != nil {
				f.err = err
				im.logf("read %s: %v", f.path, err)
				im.count(func(s *ImportStats) { s.Errors++ })
				return
			}
			f.hash = storage.HashContent(f.data)
			im.count(func(s *ImportStats) { s.Files++; s.Bytes += int64(len(f.data)) })
		}(f)
	}
	wg.Wait()
}

func (im *importer) totals(id storage.ListingID) (storage.Totals, error) {
	return im.st.ListingTotals(id)
}

// countUnlisted counts what is in dir but in no record of its .DIR.
func (im *importer) countUnlisted(dir string, listed map[string]bool) {
	des, err := os.ReadDir(dir)
	if err != nil {
		return
	}
	n := 0
	for _, de := range des {
		name := de.Name()
		if strings.HasPrefix(name, ".") || listed[name] {
			continue
		}
		n++
	}
	if n > 0 {
		im.count(func(s *ImportStats) { s.Unlisted += n })
	}
}
