package storage

import (
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"sync/atomic"

	"github.com/cockroachdb/pebble"
	"golang.org/x/sys/unix"
)

// Error is an error a client may act on; Code follows Lemmy's error names
// (couldnt_find_community), with bbs_ for what Lemmy does not have.
type Error struct {
	Code   string
	Detail string
}

func (e *Error) Error() string {
	if e.Detail == "" {
		return e.Code
	}
	return e.Code + " " + e.Detail
}

// Is matches errors by code, so errors.Is(err, ErrNotFound) works whatever
// the detail.
func (e *Error) Is(target error) bool {
	t, ok := target.(*Error)
	return ok && t.Code == e.Code
}

func errorf(base *Error, format string, args ...any) error {
	return &Error{Code: base.Code, Detail: fmt.Sprintf(format, args...)}
}

var (
	// A board without a gem area is not a missing community: these are
	// BBS errors (bbs_), not Lemmy's couldnt_find_community.
	ErrNoRoot        = &Error{Code: "bbs_couldnt_find_gem_area"}
	ErrRootExists    = &Error{Code: "bbs_gem_area_already_exists"}
	ErrNotFound      = &Error{Code: "bbs_couldnt_find_gem"}
	ErrNotDir        = &Error{Code: "bbs_gem_not_a_dir"}
	ErrNotFile       = &Error{Code: "bbs_gem_not_a_file"}
	ErrExists        = &Error{Code: "bbs_gem_already_exists"}
	ErrInvalidName   = &Error{Code: "bbs_gem_invalid_name"}
	ErrMoveIntoSelf  = &Error{Code: "bbs_gem_move_into_itself"}
	ErrQuota         = &Error{Code: "bbs_gem_quota_exceeded"}
	ErrInvalidPos    = &Error{Code: "bbs_gem_invalid_position"}
	ErrCorruptRecord = &Error{Code: "bbs_gem_corrupt"}
)

// Keys. Big-endian IDs keep listings in creation order.
const (
	pMeta  = 'M' // "Mnext": next listing ID
	pRoot  = 'R' // R + lower(root): uvarint root listing ID + root name
	pList  = 'L' // L + id: encoded listing
	pRef   = 'N' // N + id: uvarint references to the listing
	pBlob  = 'B' // B + sha256: uvarint references, size, serial
	pChunk = 'C' // C + be64 serial + be32 index: content (contents DB)
	pFree  = 'Q' // Q + id: listing with no reference left, to free
)

// ChunkSize is how much content one key holds.
const ChunkSize = 256 << 10

var (
	keyNextID   = []byte{pMeta, 'n', 'e', 'x', 't'}
	keyNextBlob = []byte{pMeta, 'b', 'l', 'o', 'b'}
	keyVersion  = []byte{pMeta, 'v', 'e', 'r', 's', 'i', 'o', 'n'}
)

// schemaVersion is the layout of the keys. 2: contents keyed by a serial,
// so imports write them in order and Pebble does not keep re-merging them.
//
// 3: contents in their own Pebble (contents.pebble), so their in-order
// tables never overlap the index keys and move down whole.
const schemaVersion = 3

func idKey(p byte, id ListingID) []byte {
	k := make([]byte, 9)
	k[0] = p
	binary.BigEndian.PutUint64(k[1:], uint64(id))
	return k
}

func rootKey(root string) []byte {
	return append([]byte{pRoot}, strings.ToLower(root)...)
}

// Roots: the gem area of a board, of a person (their mail folder), or of
// the whole site.
const (
	RootSite        = "site"
	RootBoardPrefix = "board:"
	RootUserPrefix  = "user:"
)

// BoardRoot is the root of the gem area of board.
func BoardRoot(board string) string { return RootBoardPrefix + board }

// UserRoot is the root of the mail folder of userid.
func UserRoot(userid string) string { return RootUserPrefix + userid }

// SplitRoot returns the kind ("site", "board", "user") and the name of a
// root; ok is false when root is not one.
func SplitRoot(root string) (kind, name string, ok bool) {
	if root == RootSite {
		return RootSite, "", true
	}
	if n, found := strings.CutPrefix(root, RootBoardPrefix); found && ValidName(n) {
		return "board", n, true
	}
	if n, found := strings.CutPrefix(root, RootUserPrefix); found && ValidName(n) {
		return "user", n, true
	}
	return "", "", false
}

// ValidRoot tells whether root names a gem area.
func ValidRoot(root string) bool {
	_, _, ok := SplitRoot(root)
	return ok
}

// NormalizeRoot takes a bare board name as the root of its gem area.
func NormalizeRoot(s string) string {
	if s == RootSite || strings.Contains(s, ":") {
		return s
	}
	return BoardRoot(s)
}

func blobKey(h BlobID) []byte { return append([]byte{pBlob}, h[:]...) }

func chunkKey(serial uint64, i uint32) []byte {
	k := make([]byte, 1+8+4)
	k[0] = pChunk
	binary.BigEndian.PutUint64(k[1:], serial)
	binary.BigEndian.PutUint32(k[9:], i)
	return k
}

func chunkPrefix(serial uint64) []byte {
	k := make([]byte, 1+8)
	k[0] = pChunk
	binary.BigEndian.PutUint64(k[1:], serial)
	return k
}

// blobMeta is the value of B + hash.
type blobMeta struct {
	refs, size, serial uint64
}

func decodeBlobMeta(v []byte) (blobMeta, error) {
	d := &decoder{b: v}
	m := blobMeta{refs: d.uvarint(), size: d.uvarint(), serial: d.uvarint()}
	return m, d.err
}

func (m blobMeta) encode() []byte {
	v := binary.AppendUvarint(nil, m.refs)
	v = binary.AppendUvarint(v, m.size)
	return binary.AppendUvarint(v, m.serial)
}

// prefixEnd is the first key after all keys starting with p.
func prefixEnd(p []byte) []byte {
	end := append([]byte(nil), p...)
	for i := len(end) - 1; i >= 0; i-- {
		end[i]++
		if end[i] != 0 {
			return end[:i+1]
		}
	}
	return nil
}

// Options open a Store.
type Options struct {
	// Dir holds the Pebble database (e.g. $BBSHOME/db/gem.db/gem.pebble).
	Dir string
	// CacheMB is the size of Pebble's block cache (default 128).
	CacheMB int
	// Limits gives the quotas of a board; nil for no quota.
	Limits func(board string) Limits
	// NoSync commits changes without syncing the WAL (tests).
	NoSync bool
	// NoReaper does not free listings in the background; FreeAll does.
	NoReaper bool
	// MaxOpenFiles of each database (default: from RLIMIT_NOFILE, see
	// maxOpenFiles).
	MaxOpenFiles int
	// ContentsDir overrides ContentsDir(Dir), e.g. for a backup.
	ContentsDir string
	// ReadOnly opens the databases read only (backups); nothing may be
	// changed, and NoReaper is implied.
	ReadOnly bool
}

// quietLogger drops Pebble's informational messages.
type quietLogger struct{}

func (quietLogger) Infof(string, ...interface{}) {}
func (quietLogger) Fatalf(format string, args ...interface{}) {
	pebble.DefaultLogger.Fatalf(format, args...)
}

// OpenBackup opens a backup made by Checkpoint (dir/index, dir/contents)
// read only, e.g. to Verify it.
func OpenBackup(dir string) (*Store, error) {
	return Open(Options{
		Dir:         filepath.Join(dir, "index"),
		ContentsDir: filepath.Join(dir, "contents"),
		ReadOnly:    true,
		CacheMB:     64,
	})
}

// maxOpenFiles is a quarter of RLIMIT_NOFILE, from 1000 to 16384, per
// database: Pebble's default 1000 reopened tables on most reads on ptt2.
func maxOpenFiles() int {
	var rlim unix.Rlimit
	if err := unix.Getrlimit(unix.RLIMIT_NOFILE, &rlim); err != nil {
		return 1000
	}
	return int(min(max(rlim.Cur/4, 1000), 16384))
}

// Store is the database of the gem areas: the index in Dir and the contents
// in contents.pebble. Contents are written before what refers to them and
// removed after, so a crash leaves at worst unreachable contents.
type Store struct {
	db       *pebble.DB // the index
	data     *pebble.DB // the contents
	opts     Options
	wmu      sync.Mutex // one writer at a time
	nextID   atomic.Uint64
	nextBlob atomic.Uint64

	reapWake chan struct{}
	reapStop chan struct{}
	reapDone chan struct{}
	closed   atomic.Bool
}

// Open opens (creating if needed) the database in opts.Dir.
func Open(opts Options) (*Store, error) {
	if opts.Dir == "" {
		return nil, errors.New("storage: no directory")
	}
	if err := os.MkdirAll(filepath.Dir(opts.Dir), 0755); err != nil {
		return nil, err
	}
	if opts.CacheMB <= 0 {
		opts.CacheMB = 128
	}
	if opts.ContentsDir == "" {
		opts.ContentsDir = ContentsDir(opts.Dir)
	}
	if opts.ReadOnly {
		opts.NoReaper = true
	}
	cache := pebble.NewCache(int64(opts.CacheMB) << 20) // shared by both
	defer cache.Unref()
	// Keys are whole (no prefix of versions): Split says so, which seeks by
	// prefix (and the bloom filters behind them, see Tx.lookupBlob) need.
	cmp := *pebble.DefaultComparer
	cmp.Split = func(k []byte) int { return len(k) }
	po := &pebble.Options{
		Comparer:                    &cmp,
		Cache:                       cache,
		MemTableSize:                64 << 20,
		MemTableStopWritesThreshold: 4,
		// Listings and article text compress well with zstd, which costs
		// CPU at each compaction: only the two last levels (most of the
		// data ends there) use it, the others Snappy.
		Levels: make([]pebble.LevelOptions, 7),
	}
	for i := range po.Levels {
		l := &po.Levels[i]
		l.BlockSize = 32 << 10
		l.FilterPolicy = bloomFilter
		l.Compression = pebble.SnappyCompression
		if i >= 5 {
			l.Compression = pebble.ZstdCompression
		}
	}
	if opts.MaxOpenFiles <= 0 {
		opts.MaxOpenFiles = maxOpenFiles()
	}
	po.MaxOpenFiles = opts.MaxOpenFiles
	po.ReadOnly = opts.ReadOnly
	if opts.ReadOnly {
		// Opening a backup replays its WAL: nothing to tell the operator.
		po.Logger = quietLogger{}
	}
	compactions := min(8, max(2, runtime.NumCPU()/2))
	po.MaxConcurrentCompactions = func() int { return compactions }
	po.L0CompactionThreshold = 4
	po.L0StopWritesThreshold = 64
	po.LBaseMaxBytes = 512 << 20
	db, err := pebble.Open(opts.Dir, po)
	if err != nil {
		return nil, err
	}
	// The contents are read by iterating their chunks (no Get): no bloom
	// filters.
	dpo := *po
	dpo.Levels = make([]pebble.LevelOptions, len(po.Levels))
	copy(dpo.Levels, po.Levels)
	// Their tables, written in order, overlap no other: L0 stays at one
	// or two sublevels, and Pebble would leave them there until 500
	// tables. Bigger tables, moved down after 64.
	for i := range dpo.Levels {
		dpo.Levels[i].FilterPolicy = nil
		dpo.Levels[i].TargetFileSize = 64 << 20
	}
	dpo.L0CompactionFileThreshold = 64
	data, err := pebble.Open(opts.ContentsDir, &dpo)
	if err != nil {
		db.Close()
		return nil, err
	}
	s := &Store{
		db:       db,
		data:     data,
		opts:     opts,
		reapWake: make(chan struct{}, 1),
		reapStop: make(chan struct{}),
		reapDone: make(chan struct{}),
	}
	next := uint64(1)
	if v, closer, err := db.Get(keyNextID); err == nil {
		if n, k := binary.Uvarint(v); k > 0 && n > next {
			next = n
		}
		closer.Close()
	} else if !errors.Is(err, pebble.ErrNotFound) {
		db.Close()
		data.Close()
		return nil, err
	}
	s.nextID.Store(next)
	if err := s.loadMeta(); err != nil {
		db.Close()
		data.Close()
		return nil, err
	}
	if opts.NoReaper {
		close(s.reapDone)
	} else {
		go s.reaper()
		s.wakeReaper()
	}
	return s, nil
}

// loadMeta reads the next content serial and checks the layout of the
// keys: a database of another layout is refused (import it again).
func (s *Store) loadMeta() error {
	n, err := readUvarint(s.db, keyNextBlob)
	if err != nil {
		return err
	}
	last, err := lastChunkSerial(s.data)
	if err != nil {
		return err
	}
	s.nextBlob.Store(max(n, last+1, 1))
	v, err := readUvarint(s.db, keyVersion)
	if err != nil {
		return err
	}
	if v == schemaVersion {
		return nil
	}
	if v != 0 {
		return fmt.Errorf("storage: layout %d, not %d: import the gem areas again into a new database", v, schemaVersion)
	}
	// No version: a new database, or one of before versions (refused).
	// An index with chunks in it (layout 2) or anything at all.
	it, err := s.db.NewIter(nil)
	if err != nil {
		return err
	}
	used := it.First()
	if err := it.Close(); err != nil {
		return err
	}
	if used {
		return fmt.Errorf("storage: a database of an older layout: import the gem areas again into a new database")
	}
	if s.opts.ReadOnly {
		return nil
	}
	return s.db.Set(keyVersion, binary.AppendUvarint(nil, schemaVersion), pebble.Sync)
}

// Close stops the reaper and closes the database.
func (s *Store) Close() error {
	if s.closed.Swap(true) {
		return nil
	}
	close(s.reapStop)
	<-s.reapDone
	s.wmu.Lock()
	defer s.wmu.Unlock()
	var err error
	if !s.opts.ReadOnly {
		err = s.data.Flush()
		if err2 := s.db.Flush(); err == nil {
			err = err2
		}
	}
	if err2 := s.data.Close(); err == nil {
		err = err2
	}
	if err2 := s.db.Close(); err == nil {
		err = err2
	}
	return err
}

// ContentsDir is where the contents are kept, next to the index in dir.
func ContentsDir(dir string) string {
	return filepath.Join(filepath.Dir(dir), "contents.pebble")
}

// Metrics are Pebble's metrics of the index and of the contents.
func (s *Store) Metrics() string {
	return "index:\n" + s.db.Metrics().String() + "\ncontents:\n" + s.data.Metrics().String()
}

// Checkpoint writes a backup (Pebble checkpoints) into dest/index and
// dest/contents, the contents after the index so that all it refers to is
// there.
func (s *Store) Checkpoint(dest string) error {
	if err := os.MkdirAll(dest, 0755); err != nil {
		return err
	}
	if err := s.db.Checkpoint(filepath.Join(dest, "index"), pebble.WithFlushedWAL()); err != nil {
		return err
	}
	return s.data.Checkpoint(filepath.Join(dest, "contents"), pebble.WithFlushedWAL())
}

func (s *Store) writeOpts() *pebble.WriteOptions {
	if s.opts.NoSync {
		return pebble.NoSync
	}
	return pebble.Sync
}

func (s *Store) limits(board string) Limits {
	if s.opts.Limits == nil {
		return Limits{}
	}
	return s.opts.Limits(board)
}

// reader is a consistent view for reads: a snapshot, or the database.
type reader interface {
	Get(key []byte) ([]byte, io.Closer, error)
	NewIter(o *pebble.IterOptions) (*pebble.Iterator, error)
}

func getCopy(r reader, key []byte) ([]byte, error) {
	v, closer, err := r.Get(key)
	if err != nil {
		return nil, err
	}
	defer closer.Close()
	return append([]byte(nil), v...), nil
}

func readRoot(r reader, board string) (ListingID, string, error) {
	v, err := getCopy(r, rootKey(board))
	if errors.Is(err, pebble.ErrNotFound) {
		return 0, "", errorf(ErrNoRoot, "%s", board)
	}
	if err != nil {
		return 0, "", err
	}
	id, n := binary.Uvarint(v)
	if n <= 0 {
		return 0, "", errorf(ErrCorruptRecord, "root of %s", board)
	}
	return ListingID(id), string(v[n:]), nil
}

func readListing(r reader, id ListingID) (*Listing, error) {
	v, closer, err := r.Get(idKey(pList, id))
	if errors.Is(err, pebble.ErrNotFound) {
		return nil, errorf(ErrCorruptRecord, "listing %d is gone", id)
	}
	if err != nil {
		return nil, err
	}
	defer closer.Close()
	l, err := decodeListing(v)
	if err != nil {
		return nil, errorf(ErrCorruptRecord, "listing %d: %v", id, err)
	}
	return l, nil
}

func readUvarint(r reader, key []byte) (uint64, error) {
	v, closer, err := r.Get(key)
	if errors.Is(err, pebble.ErrNotFound) {
		return 0, nil
	}
	if err != nil {
		return 0, err
	}
	defer closer.Close()
	n, k := binary.Uvarint(v)
	if k <= 0 {
		return 0, errorf(ErrCorruptRecord, "counter %x", key)
	}
	return n, nil
}

// SplitPath splits "a/b/c" into its names; "", "." and "/" are the root.
func SplitPath(p string) ([]string, error) {
	p = strings.Trim(p, "/")
	if p == "" || p == "." {
		return nil, nil
	}
	names := strings.Split(p, "/")
	for _, n := range names {
		if !ValidName(n) {
			return nil, errorf(ErrInvalidName, "%q", n)
		}
	}
	return names, nil
}

// walk follows path from the root listing of board.
func walk(r reader, board string, path []string) (*Listing, error) {
	id, _, err := readRoot(r, board)
	if err != nil {
		return nil, err
	}
	l, err := readListing(r, id)
	if err != nil {
		return nil, err
	}
	for i, name := range path {
		j := l.Find(-1, name)
		if j < 0 {
			return nil, errorf(ErrNotFound, "%s/%s", board, strings.Join(path[:i+1], "/"))
		}
		e := &l.Entries[j]
		if e.Kind != KindDir {
			return nil, errorf(ErrNotDir, "%s/%s", board, strings.Join(path[:i+1], "/"))
		}
		if l, err = readListing(r, e.Child); err != nil {
			return nil, err
		}
	}
	return l, nil
}

// List returns the listing of the directory path of board's gem area.
func (s *Store) List(board string, path []string) (*Listing, error) {
	snap := s.db.NewSnapshot()
	defer snap.Close()
	return walk(snap, board, path)
}

// View reads the store as it was when it was made (a Pebble snapshot), e.g.
// to walk a whole tree consistently, reading each directory once.
type View struct {
	snap *pebble.Snapshot
}

// View makes a View; Close it when done.
func (s *Store) View() *View { return &View{snap: s.db.NewSnapshot()} }

// Close releases the View.
func (v *View) Close() { v.snap.Close() }

// List returns the listing of the directory path of root.
func (v *View) List(root string, path []string) (*Listing, error) {
	return walk(v.snap, root, path)
}

// Dir returns the listing of the directory e (an entry of a listing of v).
func (v *View) Dir(e *Entry) (*Listing, error) {
	if e.Kind != KindDir {
		return nil, errorf(ErrNotDir, "%s is a %s", e.Name(), e.Kind)
	}
	return readListing(v.snap, e.Child)
}

// Stat returns the entry name (hint: its index last seen) in directory path.
func (s *Store) Stat(board string, path []string, hint int, name string) (Entry, int, error) {
	l, err := s.List(board, path)
	if err != nil {
		return Entry{}, -1, err
	}
	i := l.Find(hint, name)
	if i < 0 {
		return Entry{}, -1, errorf(ErrNotFound, "%s/%s", board, strings.Join(append(path, name), "/"))
	}
	return l.Entries[i], i, nil
}

// RootTotals returns the totals of board's whole gem area.
func (s *Store) RootTotals(board string) (Totals, error) {
	id, _, err := readRoot(s.db, board)
	if err != nil {
		return Totals{}, err
	}
	v, err := getCopy(s.db, idKey(pList, id))
	if err != nil {
		return Totals{}, err
	}
	return decodeTotals(v)
}

// HasRoot tells whether board has a gem area here.
func (s *Store) HasRoot(board string) bool {
	_, _, err := readRoot(s.db, board)
	return err == nil
}

// Boards lists the boards with a gem area, by their names.
func (s *Store) Roots() ([]string, error) {
	it, err := s.db.NewIter(&pebble.IterOptions{LowerBound: []byte{pRoot}, UpperBound: []byte{pRoot + 1}})
	if err != nil {
		return nil, err
	}
	defer it.Close()
	var out []string
	for it.First(); it.Valid(); it.Next() {
		v := it.Value()
		// Not the trash of a root (see TrashRoot).
		if _, n := binary.Uvarint(v); n > 0 && ValidRoot(string(v[n:])) {
			out = append(out, string(v[n:]))
		}
	}
	return out, it.Error()
}

// ReadBlob returns the content of a blob.
func (s *Store) ReadBlob(h BlobID) ([]byte, error) {
	return s.readBlob(s.db, h)
}

// readBlob finds the content h in the index r and reads it.
func (s *Store) readBlob(r reader, h BlobID) ([]byte, error) {
	v, err := getCopy(r, blobKey(h))
	if errors.Is(err, pebble.ErrNotFound) {
		return nil, errorf(ErrCorruptRecord, "blob %s is gone", h)
	}
	if err != nil {
		return nil, err
	}
	m, err := decodeBlobMeta(v)
	if err != nil {
		return nil, errorf(ErrCorruptRecord, "blob %s", h)
	}
	size := m.size
	out := make([]byte, 0, size)
	p := chunkPrefix(m.serial)
	it, err := s.data.NewIter(&pebble.IterOptions{LowerBound: p, UpperBound: prefixEnd(p)})
	if err != nil {
		return nil, err
	}
	defer it.Close()
	for it.First(); it.Valid(); it.Next() {
		out = append(out, it.Value()...)
	}
	if err := it.Error(); err != nil {
		return nil, err
	}
	if uint64(len(out)) != size {
		return nil, errorf(ErrCorruptRecord, "blob %s has %d of %d bytes", h, len(out), size)
	}
	return out, nil
}

// ReadFile returns the content of the file name (hint: its index last seen)
// in directory path.
func (s *Store) ReadFile(board string, path []string, hint int, name string) ([]byte, Entry, error) {
	snap := s.db.NewSnapshot()
	defer snap.Close()
	l, err := walk(snap, board, path)
	if err != nil {
		return nil, Entry{}, err
	}
	i := l.Find(hint, name)
	if i < 0 {
		return nil, Entry{}, errorf(ErrNotFound, "%s/%s", board, strings.Join(append(path, name), "/"))
	}
	e := l.Entries[i]
	if e.Kind != KindFile {
		return nil, e, errorf(ErrNotFile, "%s/%s is a %s", board, strings.Join(append(path, name), "/"), e.Kind)
	}
	data, err := s.readBlob(snap, e.Blob)
	return data, e, err
}

// HashContent is the BlobID of data.
func HashContent(data []byte) BlobID { return sha256.Sum256(data) }

// ListingTotals returns the totals of listing id.
func (s *Store) ListingTotals(id ListingID) (Totals, error) {
	v, err := getCopy(s.db, idKey(pList, id))
	if err != nil {
		return Totals{}, err
	}
	return decodeTotals(v)
}
