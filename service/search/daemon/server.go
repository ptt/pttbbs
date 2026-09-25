package daemon

import (
	"bufio"
	"bytes"
	"container/list"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"os/signal"
	"path/filepath"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"pttbbs/bbs"
)

const (
	SearchSvcMagic       uint32 = 0x53524348 // "SRCH"
	SearchAIDMagic       uint32 = 0x53414944 // "SAID"
	SearchInvalMagic     uint32 = 0x53494E56 // "SINV"
	MaxSearchPredicates  int32  = 8
	DefaultMaxEntries    int    = 2048
	DefaultMaxIndices    int64  = 16 * 1024 * 1024 // 16M int32s (~64MB)
	DefaultMaxAIDEntries int    = 16384            // 16K AID cache entries (~2.5MB)
	DefaultCacheTTL             = 1 * time.Hour
	aiduRawMask          uint64 = 0x00001FFFFFFFFFFF
	aiduIdxShift                = 45
)

type ControlRequest struct {
	Action string `json:"action"`
	Bid    int32  `json:"bid,omitempty"`
}

type ControlResponse struct {
	Status  string      `json:"status"`
	Message string      `json:"message,omitempty"`
	Data    interface{} `json:"data,omitempty"`
}

type ServiceStats struct {
	UptimeSeconds      int64 `json:"uptime_seconds"`
	CachedEntries      int   `json:"cached_entries"`
	CachedIndices      int64 `json:"cached_indices"`
	MaxEntries         int   `json:"max_entries"`
	MaxIndices         int64 `json:"max_indices"`
	CachedAIDEntries   int   `json:"cached_aid_entries"`
	MaxAIDEntries      int   `json:"max_aid_entries"`
	Hits               int64 `json:"hits"`
	Misses             int64 `json:"misses"`
	IncrementalUpdates int64 `json:"incremental_updates"`
	ChainedHits        int64 `json:"chained_hits"`
	Evictions          int64 `json:"evictions"`
	Invalidations      int64 `json:"invalidations"`
	AIDHits            int64 `json:"aid_hits"`
	AIDNegativeHits    int64 `json:"aid_negative_hits"`
	AIDMisses          int64 `json:"aid_misses"`
	AIDEvictions       int64 `json:"aid_evictions"`
}

type cacheKey struct {
	direct   string
	bid      int32
	predsHex string
}

type cacheEntry struct {
	key         cacheKey
	indices     []int32
	scannedRecs int32
	tailName    string // filename of record #scannedRecs, to detect shifted records
	dirMtime    time.Time
	dirInode    uint64
	srExpire    int64
	createdAt   time.Time
	elem        *list.Element
}

type aidCacheKey struct {
	direct       string
	bid          int32
	aiduRaw      uint64
	requiredMode int32
}

type aidCacheEntry struct {
	key         aidCacheKey
	foundIdx    int32 // >0 if found, 0 for negative cache (not found in [1..scannedRecs])
	fhBytes     [128]byte
	scannedRecs int32
	dirMtime    time.Time
	dirInode    uint64
	srExpire    int64
	createdAt   time.Time
	elem        *list.Element
}

func fileInode(st os.FileInfo) uint64 {
	if st == nil {
		return 0
	}
	if sys, ok := st.Sys().(*syscall.Stat_t); ok && sys != nil {
		return sys.Ino
	}
	return 0
}

type inflightCall struct {
	wg      sync.WaitGroup
	indices []int32
	err     error
}

type aidInflightCall struct {
	wg       sync.WaitGroup
	foundIdx int32
	fhBytes  [128]byte
	err      error
}

type Service struct {
	bbsHome    string
	socketPath string
	listener   net.Listener
	shm        *bbs.SHMClient
	verbose    int
	startTime  time.Time

	cacheMu      sync.Mutex
	entries      map[cacheKey]*cacheEntry
	lruList      *list.List
	totalIndices int64
	maxEntries   int
	maxIndices   int64
	cacheTTL     time.Duration

	aidMu         sync.Mutex
	aidEntries    map[aidCacheKey]*aidCacheEntry
	aidLRU        *list.List
	maxAIDEntries int

	sfMu        sync.Mutex
	inflight    map[cacheKey]*inflightCall
	aidInflight map[aidCacheKey]*aidInflightCall

	// gen is bumped by Invalidate so that scans started before an
	// invalidation do not re-insert their (possibly stale) results.
	gen atomic.Uint64

	hits               atomic.Int64
	misses             atomic.Int64
	incrementalUpdates atomic.Int64
	chainedHits        atomic.Int64
	evictions          atomic.Int64
	invalidations      atomic.Int64
	aidHits            atomic.Int64
	aidNegativeHits    atomic.Int64
	aidMisses          atomic.Int64
	aidEvictions       atomic.Int64

	stopChan chan struct{}
	wg       sync.WaitGroup
}

func IsSocketOccupied(socketPath string) bool {
	if _, err := os.Stat(socketPath); os.IsNotExist(err) {
		return false
	}
	conn, err := net.DialTimeout("unix", socketPath, 200*time.Millisecond)
	if err == nil {
		conn.Close()
		return true
	}
	return false
}

// NewService creates the service. SHM is required: without bcache[].SRexpire
// the cache cannot detect in-place edits and would serve stale results.
func NewService(bbsHome, socketPath string) (*Service, error) {
	shm, err := bbs.AttachSHM()
	if err != nil {
		return nil, fmt.Errorf("SHM not attached: %w", err)
	}
	return newService(bbsHome, socketPath, shm), nil
}

// newService creates the service with an optional SHM (nil only in tests).
func newService(bbsHome, socketPath string, shm *bbs.SHMClient) *Service {
	return &Service{
		bbsHome:       bbsHome,
		socketPath:    socketPath,
		shm:           shm,
		startTime:     time.Now(),
		entries:       make(map[cacheKey]*cacheEntry),
		lruList:       list.New(),
		maxEntries:    DefaultMaxEntries,
		maxIndices:    DefaultMaxIndices,
		cacheTTL:      DefaultCacheTTL,
		aidEntries:    make(map[aidCacheKey]*aidCacheEntry),
		aidLRU:        list.New(),
		maxAIDEntries: DefaultMaxAIDEntries,
		inflight:      make(map[cacheKey]*inflightCall),
		aidInflight:   make(map[aidCacheKey]*aidInflightCall),
		stopChan:      make(chan struct{}),
	}
}

func (s *Service) SetVerbose(level int) {
	s.verbose = level
}

func (s *Service) SetCacheLimits(maxEntries int, maxIndices int64, maxAIDEntries int, cacheTTL time.Duration) {
	s.cacheMu.Lock()
	if maxEntries > 0 {
		s.maxEntries = maxEntries
	}
	if maxIndices > 0 {
		s.maxIndices = maxIndices
	}
	if cacheTTL > 0 {
		s.cacheTTL = cacheTTL
	}
	s.cacheMu.Unlock()

	s.aidMu.Lock()
	if maxAIDEntries > 0 {
		s.maxAIDEntries = maxAIDEntries
	}
	s.aidMu.Unlock()
}

func (s *Service) Start() error {
	if IsSocketOccupied(s.socketPath) {
		return fmt.Errorf("socket %s is already occupied", s.socketPath)
	}
	if err := os.MkdirAll(filepath.Dir(s.socketPath), 0755); err != nil {
		return fmt.Errorf("failed to create socket dir: %w", err)
	}
	_ = os.Remove(s.socketPath)

	l, err := net.Listen("unix", s.socketPath)
	if err != nil {
		return fmt.Errorf("failed to listen on %s: %w", s.socketPath, err)
	}
	_ = os.Chmod(s.socketPath, 0660)
	s.listener = l

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	go func() {
		select {
		case <-sigChan:
			s.Stop()
		case <-s.stopChan:
		}
	}()

	for {
		conn, err := s.listener.Accept()
		if err != nil {
			select {
			case <-s.stopChan:
				return nil
			default:
				if errors.Is(err, net.ErrClosed) {
					return nil
				}
				continue
			}
		}
		s.wg.Add(1)
		go func(c net.Conn) {
			defer s.wg.Done()
			s.handleConn(c)
		}(conn)
	}
}

func (s *Service) Stop() {
	select {
	case <-s.stopChan:
		return
	default:
		close(s.stopChan)
	}
	if s.listener != nil {
		_ = s.listener.Close()
	}
	_ = os.Remove(s.socketPath)
	s.wg.Wait()
}

func (s *Service) handleConn(conn net.Conn) {
	defer conn.Close()
	_ = conn.SetDeadline(time.Now().Add(5 * time.Second))

	br := bufio.NewReader(conn)
	first, err := br.Peek(1)
	if err != nil {
		return
	}
	if first[0] == '{' {
		s.handleControlConn(br, conn)
		return
	}
	s.handleBinaryConn(br, conn)
}

func (s *Service) handleControlConn(r io.Reader, w io.Writer) {
	var req ControlRequest
	if err := json.NewDecoder(r).Decode(&req); err != nil {
		_ = json.NewEncoder(w).Encode(ControlResponse{
			Status:  "error",
			Message: fmt.Sprintf("invalid json: %v", err),
		})
		return
	}

	switch req.Action {
	case "status":
		s.cacheMu.Lock()
		entriesCount := len(s.entries)
		indicesCount := s.totalIndices
		maxEntries := s.maxEntries
		maxIndices := s.maxIndices
		s.cacheMu.Unlock()

		s.aidMu.Lock()
		aidEntriesCount := len(s.aidEntries)
		maxAIDEntries := s.maxAIDEntries
		s.aidMu.Unlock()

		stats := ServiceStats{
			UptimeSeconds:      int64(time.Since(s.startTime).Seconds()),
			CachedEntries:      entriesCount,
			CachedIndices:      indicesCount,
			MaxEntries:         maxEntries,
			MaxIndices:         maxIndices,
			CachedAIDEntries:   aidEntriesCount,
			MaxAIDEntries:      maxAIDEntries,
			Hits:               s.hits.Load(),
			Misses:             s.misses.Load(),
			IncrementalUpdates: s.incrementalUpdates.Load(),
			ChainedHits:        s.chainedHits.Load(),
			Evictions:          s.evictions.Load(),
			Invalidations:      s.invalidations.Load(),
			AIDHits:            s.aidHits.Load(),
			AIDNegativeHits:    s.aidNegativeHits.Load(),
			AIDMisses:          s.aidMisses.Load(),
			AIDEvictions:       s.aidEvictions.Load(),
		}
		_ = json.NewEncoder(w).Encode(ControlResponse{
			Status:  "ok",
			Message: "search.svc running",
			Data:    stats,
		})

	case "flush":
		flushed := s.Flush(req.Bid)
		_ = json.NewEncoder(w).Encode(ControlResponse{
			Status:  "ok",
			Message: fmt.Sprintf("flushed %d cache entries", flushed),
		})

	default:
		_ = json.NewEncoder(w).Encode(ControlResponse{
			Status:  "error",
			Message: fmt.Sprintf("unknown action: %s", req.Action),
		})
	}
}

func (s *Service) Flush(bid int32) int {
	return s.Invalidate("", bid)
}

func (s *Service) Invalidate(resolvedDirect string, bid int32) int {
	flushed := 0
	match := func(kDirect string, kBid int32) bool {
		return (bid == 0 && resolvedDirect == "") ||
			(resolvedDirect != "" && kDirect == resolvedDirect) ||
			(bid > 0 && kBid == bid)
	}

	// Bump gen first: an in-flight scan either stores before this (and is
	// removed below) or observes the new gen and skips storing.
	s.gen.Add(1)

	// Detach matching in-flight scans so new requests start a fresh scan
	// instead of joining one that began before the invalidation.
	s.sfMu.Lock()
	for k := range s.inflight {
		if match(k.direct, k.bid) {
			delete(s.inflight, k)
		}
	}
	for k := range s.aidInflight {
		if match(k.direct, k.bid) {
			delete(s.aidInflight, k)
		}
	}
	s.sfMu.Unlock()

	s.cacheMu.Lock()
	for k, e := range s.entries {
		if match(k.direct, k.bid) {
			s.removeEntryLocked(e)
			flushed++
		}
	}
	s.cacheMu.Unlock()

	s.aidMu.Lock()
	for k, e := range s.aidEntries {
		if match(k.direct, k.bid) {
			s.removeAIDEntryLocked(e)
			flushed++
		}
	}
	s.aidMu.Unlock()

	return flushed
}

func (s *Service) removeEntryLocked(e *cacheEntry) {
	delete(s.entries, e.key)
	if e.elem != nil {
		s.lruList.Remove(e.elem)
		e.elem = nil
	}
	s.totalIndices -= int64(len(e.indices))
	if s.totalIndices < 0 {
		s.totalIndices = 0
	}
}

func (s *Service) putEntryLocked(e *cacheEntry) {
	if old, ok := s.entries[e.key]; ok {
		s.removeEntryLocked(old)
	}
	e.elem = s.lruList.PushFront(e)
	s.entries[e.key] = e
	s.totalIndices += int64(len(e.indices))

	for (len(s.entries) > s.maxEntries || s.totalIndices > s.maxIndices) && s.lruList.Len() > 1 {
		back := s.lruList.Back()
		if back == nil {
			break
		}
		victim := back.Value.(*cacheEntry)
		s.removeEntryLocked(victim)
		s.evictions.Add(1)
	}
}

func (s *Service) removeAIDEntryLocked(e *aidCacheEntry) {
	delete(s.aidEntries, e.key)
	if e.elem != nil {
		s.aidLRU.Remove(e.elem)
		e.elem = nil
	}
}

func (s *Service) putAIDEntryLocked(e *aidCacheEntry) {
	if old, ok := s.aidEntries[e.key]; ok {
		s.removeAIDEntryLocked(old)
	}
	e.elem = s.aidLRU.PushFront(e)
	s.aidEntries[e.key] = e

	for len(s.aidEntries) > s.maxAIDEntries && s.aidLRU.Len() > 1 {
		back := s.aidLRU.Back()
		if back == nil {
			break
		}
		victim := back.Value.(*aidCacheEntry)
		s.removeAIDEntryLocked(victim)
		s.aidEvictions.Add(1)
	}
}

type binaryReqHeader struct {
	Magic    uint32
	Bid      int32
	Offset   int32
	Limit    int32
	NumPreds int32
	Direct   [256]byte
}

type binaryRespHeader struct {
	Status int32
	Total  int32
	Count  int32
}

type binaryAIDReqHeader struct {
	Magic        uint32
	Bid          int32
	AIDU         uint64
	RequiredMode int32
	Direct       [256]byte
}

type binaryAIDResp struct {
	Status   int32
	FoundIdx int32
	FH       [128]byte
}

type binaryInvalReqHeader struct {
	Magic  uint32
	Bid    int32
	Direct [256]byte
}

func writeBinaryError(w io.Writer, status int32) {
	resp := binaryRespHeader{
		Status: status,
		Total:  0,
		Count:  0,
	}
	_ = binary.Write(w, binary.LittleEndian, &resp)
}

func (s *Service) handleBinaryConn(r *bufio.Reader, w io.Writer) {
	magicBytes, err := r.Peek(4)
	if err != nil {
		return
	}
	magic := binary.LittleEndian.Uint32(magicBytes)
	switch magic {
	case SearchSvcMagic:
		s.handleBinarySearchConn(r, w)
	case SearchAIDMagic:
		s.handleBinaryAIDConn(r, w)
	case SearchInvalMagic:
		s.handleBinaryInvalConn(r, w)
	default:
		writeBinaryError(w, -1)
	}
}

func (s *Service) handleBinaryInvalConn(r io.Reader, w io.Writer) {
	var req binaryInvalReqHeader
	if err := binary.Read(r, binary.LittleEndian, &req); err != nil {
		return
	}
	resolvedDirect := s.resolveDirectPath(req.Direct)
	s.Invalidate(resolvedDirect, req.Bid)
	s.invalidations.Add(1)
	status := int32(0)
	_ = binary.Write(w, binary.LittleEndian, &status)
}

func (s *Service) resolveDirectPath(raw [256]byte) string {
	nulIdx := bytes.IndexByte(raw[:], 0)
	var directStr string
	if nulIdx >= 0 {
		directStr = string(raw[:nulIdx])
	} else {
		directStr = string(raw[:])
	}
	if directStr == "" {
		return ""
	}
	if !filepath.IsAbs(directStr) {
		return filepath.Join(s.bbsHome, directStr)
	}
	return directStr
}

func (s *Service) handleBinaryAIDConn(r io.Reader, w io.Writer) {
	var req binaryAIDReqHeader
	if err := binary.Read(r, binary.LittleEndian, &req); err != nil {
		return
	}
	resolvedDirect := s.resolveDirectPath(req.Direct)
	if resolvedDirect == "" {
		resp := binaryAIDResp{Status: -1}
		_ = binary.Write(w, binary.LittleEndian, &resp)
		return
	}

	foundIdx, fhBytes, err := s.QueryAID(resolvedDirect, req.Bid, req.AIDU, req.RequiredMode)
	if err != nil {
		resp := binaryAIDResp{Status: -2}
		_ = binary.Write(w, binary.LittleEndian, &resp)
		return
	}

	resp := binaryAIDResp{
		Status:   0,
		FoundIdx: foundIdx,
		FH:       fhBytes,
	}
	_ = binary.Write(w, binary.LittleEndian, &resp)
}

func (s *Service) handleBinarySearchConn(r io.Reader, w io.Writer) {
	var hdr binaryReqHeader
	if err := binary.Read(r, binary.LittleEndian, &hdr); err != nil {
		return
	}
	if hdr.Magic != SearchSvcMagic || hdr.NumPreds <= 0 || hdr.NumPreds > MaxSearchPredicates {
		writeBinaryError(w, -1)
		return
	}

	predSize := PredSize()
	predsBytesLen := int(hdr.NumPreds) * predSize
	predsRaw := make([]byte, predsBytesLen)
	if _, err := io.ReadFull(r, predsRaw); err != nil {
		writeBinaryError(w, -2)
		return
	}
	SanitizePreds(predsRaw, int(hdr.NumPreds))

	resolvedDirect := s.resolveDirectPath(hdr.Direct)
	if resolvedDirect == "" {
		writeBinaryError(w, -3)
		return
	}

	indices, err := s.QueryIndices(resolvedDirect, hdr.Bid, predsRaw, int(hdr.NumPreds))
	if err != nil {
		if s.verbose > 0 {
			log.Printf("[search.svc] QueryIndices error on %s: %v", resolvedDirect, err)
		}
		writeBinaryError(w, -4)
		return
	}

	total := int32(len(indices))
	offset := hdr.Offset
	if offset < 0 {
		offset = 0
	}
	limit := hdr.Limit
	if limit < 0 {
		limit = 0
	}

	var window []int32
	if offset < total && limit > 0 {
		end64 := int64(offset) + int64(limit)
		if end64 > int64(total) {
			end64 = int64(total)
		}
		end := int32(end64)
		window = indices[offset:end]
	}

	resp := binaryRespHeader{
		Status: 0,
		Total:  total,
		Count:  int32(len(window)),
	}
	var outBuf bytes.Buffer
	outBuf.Grow(12 + len(window)*4)
	_ = binary.Write(&outBuf, binary.LittleEndian, &resp)
	if len(window) > 0 {
		_ = binary.Write(&outBuf, binary.LittleEndian, window)
	}
	_, _ = w.Write(outBuf.Bytes())
}

func (s *Service) isAIDEntryValidLocked(e *aidCacheEntry, curSRExpire int64, curTotalRecs int32, curInode uint64) bool {
	if e == nil {
		return false
	}
	if time.Since(e.createdAt) > s.cacheTTL {
		return false
	}
	if curInode != 0 && e.dirInode != 0 && e.dirInode != curInode {
		return false
	}
	if curSRExpire != 0 && e.srExpire != curSRExpire {
		return false
	}
	if curTotalRecs < e.scannedRecs {
		return false
	}
	return true
}

func (s *Service) QueryAID(resolvedDirect string, bid int32, aidu uint64, requiredMode int32) (int32, [128]byte, error) {
	var emptyFH [128]byte
	st, err := os.Stat(resolvedDirect)
	if err != nil {
		return 0, emptyFH, err
	}
	fhSize := int64(FileheaderSize())
	curTotalRecs := int32(st.Size() / fhSize)
	curMtime := st.ModTime()
	curInode := fileInode(st)
	curSRExpire := GetBoardSRExpire(bid)

	key := aidCacheKey{
		direct:       resolvedDirect,
		bid:          bid,
		aiduRaw:      aidu & aiduRawMask,
		requiredMode: requiredMode,
	}

	var cachedHintIdx int32

	s.aidMu.Lock()
	if entry, ok := s.aidEntries[key]; ok && s.isAIDEntryValidLocked(entry, curSRExpire, curTotalRecs, curInode) {
		if entry.scannedRecs == curTotalRecs && entry.dirMtime.Equal(curMtime) {
			s.aidLRU.MoveToFront(entry.elem)
			idx := entry.foundIdx
			fh := entry.fhBytes
			s.aidMu.Unlock()
			if idx > 0 {
				s.aidHits.Add(1)
			} else {
				s.aidNegativeHits.Add(1)
			}
			return idx, fh, nil
		}
		// A negative entry cannot be extended by scanning only the tail:
		// physical deletion shifts records and in-place edits (undelete,
		// unlock) change older records. Always rescan in that case.
		if entry.foundIdx > 0 {
			cachedHintIdx = entry.foundIdx
		}
	}
	s.aidMu.Unlock()

	s.sfMu.Lock()
	if call, ok := s.aidInflight[key]; ok {
		s.sfMu.Unlock()
		call.wg.Wait()
		return call.foundIdx, call.fhBytes, call.err
	}
	call := &aidInflightCall{}
	call.wg.Add(1)
	s.aidInflight[key] = call
	s.sfMu.Unlock()

	defer func() {
		s.sfMu.Lock()
		if s.aidInflight[key] == call {
			delete(s.aidInflight, key)
		}
		call.wg.Done()
		s.sfMu.Unlock()
	}()

	// If we had a previously cached positive index (or caller passed hint_idx in aidu), inject hint_idx into aidu!
	aiduWithHint := aidu
	if (aiduWithHint>>aiduIdxShift) == 0 && cachedHintIdx > 0 {
		aiduWithHint = (aidu & aiduRawMask) | (uint64(cachedHintIdx) << aiduIdxShift)
	}

	gen := s.gen.Load()
	foundIdx, fhBytes, actualTotal, err := SearchAIDInDir(resolvedDirect, aiduWithHint, int(requiredMode), 1)
	if err != nil {
		call.err = err
		return 0, emptyFH, err
	}

	newEntry := &aidCacheEntry{
		key:         key,
		foundIdx:    foundIdx,
		fhBytes:     fhBytes,
		scannedRecs: actualTotal,
		dirMtime:    curMtime,
		dirInode:    curInode,
		srExpire:    curSRExpire,
		createdAt:   time.Now(),
	}
	s.aidMu.Lock()
	if s.gen.Load() == gen {
		s.putAIDEntryLocked(newEntry)
	}
	s.aidMu.Unlock()

	if cachedHintIdx > 0 && foundIdx > 0 {
		s.aidHits.Add(1)
	} else {
		s.aidMisses.Add(1)
	}

	call.foundIdx, call.fhBytes = foundIdx, fhBytes
	return foundIdx, fhBytes, nil
}

func (s *Service) isEntryValidLocked(e *cacheEntry, curSRExpire int64, curTotalRecs int32, curInode uint64) bool {
	if e == nil {
		return false
	}
	if time.Since(e.createdAt) > s.cacheTTL {
		return false
	}
	if curInode != 0 && e.dirInode != 0 && e.dirInode != curInode {
		return false
	}
	if curSRExpire != 0 && e.srExpire != curSRExpire {
		return false
	}
	if curTotalRecs < e.scannedRecs {
		return false
	}
	return true
}

func (s *Service) QueryIndices(resolvedDirect string, bid int32, predsRaw []byte, numPreds int) ([]int32, error) {
	st, err := os.Stat(resolvedDirect)
	if err != nil {
		return nil, err
	}
	fhSize := int64(FileheaderSize())
	curTotalRecs := int32(st.Size() / fhSize)
	curMtime := st.ModTime()
	curInode := fileInode(st)
	curSRExpire := GetBoardSRExpire(bid)

	key := cacheKey{
		direct:   resolvedDirect,
		bid:      bid,
		predsHex: string(predsRaw),
	}

	// Fast-path cache check
	s.cacheMu.Lock()
	if entry, ok := s.entries[key]; ok {
		if s.isEntryValidLocked(entry, curSRExpire, curTotalRecs, curInode) &&
			entry.scannedRecs == curTotalRecs &&
			entry.dirMtime.Equal(curMtime) {
			s.lruList.MoveToFront(entry.elem)
			res := entry.indices
			s.cacheMu.Unlock()
			s.hits.Add(1)
			return res, nil
		}
	}
	s.cacheMu.Unlock()

	// Coalesce concurrent identical scans via singleflight
	s.sfMu.Lock()
	if call, ok := s.inflight[key]; ok {
		s.sfMu.Unlock()
		call.wg.Wait()
		return call.indices, call.err
	}
	call := &inflightCall{}
	call.wg.Add(1)
	s.inflight[key] = call
	s.sfMu.Unlock()

	defer func() {
		s.sfMu.Lock()
		if s.inflight[key] == call {
			delete(s.inflight, key)
		}
		call.wg.Done()
		s.sfMu.Unlock()
	}()

	call.indices, call.err = s.computeAndCache(key, resolvedDirect, bid, predsRaw, numPreds, curTotalRecs, curMtime, curInode, curSRExpire)
	return call.indices, call.err
}

func (s *Service) computeAndCache(
	key cacheKey,
	resolvedDirect string,
	bid int32,
	predsRaw []byte,
	numPreds int,
	curTotalRecs int32,
	curMtime time.Time,
	curInode uint64,
	curSRExpire int64,
) ([]int32, error) {
	var baseEntryCopy *cacheEntry
	var prefixIndices []int32
	hasPrefix := false

	predSize := PredSize()
	gen := s.gen.Load()

	s.cacheMu.Lock()
	if entry, ok := s.entries[key]; ok && s.isEntryValidLocked(entry, curSRExpire, curTotalRecs, curInode) {
		if entry.scannedRecs == curTotalRecs && entry.dirMtime.Equal(curMtime) {
			s.lruList.MoveToFront(entry.elem)
			res := entry.indices
			s.cacheMu.Unlock()
			s.hits.Add(1)
			return res, nil
		}
		// Incremental tail reuse is only safe when the prefix is unchanged;
		// in-place edits are only tracked (by SRexpire) on boards.
		// Without SHM, SRexpire always reads 0 and cannot detect in-place edits.
		if curTotalRecs > entry.scannedRecs && entry.scannedRecs > 0 && bid > 0 && SHMReady() {
			cp := *entry
			cp.indices = append([]int32(nil), entry.indices...)
			baseEntryCopy = &cp
		}
	}
	if baseEntryCopy == nil && numPreds > 1 {
		prefixKey := cacheKey{
			direct:   resolvedDirect,
			bid:      bid,
			predsHex: string(predsRaw[:(numPreds-1)*predSize]),
		}
		if pEntry, ok := s.entries[prefixKey]; ok &&
			s.isEntryValidLocked(pEntry, curSRExpire, curTotalRecs, curInode) &&
			pEntry.scannedRecs == curTotalRecs &&
			pEntry.dirMtime.Equal(curMtime) {
			prefixIndices = append([]int32(nil), pEntry.indices...)
			hasPrefix = true
		}
	}
	s.cacheMu.Unlock()

	// Case 1: Incremental tail update on existing entry
	if baseEntryCopy != nil {
		// Physical deletion shifts records in place; verify the anchor.
		if name, ok := ReadFilenameAt(resolvedDirect, baseEntryCopy.scannedRecs); !ok || name != baseEntryCopy.tailName {
			baseEntryCopy = nil
		}
	}
	if baseEntryCopy != nil {
		tailIndices, actualTotal, err := ScanDirRange(resolvedDirect, predsRaw, numPreds, baseEntryCopy.scannedRecs+1)
		if err == nil && actualTotal >= baseEntryCopy.scannedRecs {
			combined := append(baseEntryCopy.indices, tailIndices...)
			newEntry := &cacheEntry{
				key:         key,
				indices:     combined,
				scannedRecs: actualTotal,
				dirMtime:    curMtime,
				dirInode:    curInode,
				srExpire:    curSRExpire,
				createdAt:   baseEntryCopy.createdAt,
			}
			newEntry.tailName, _ = ReadFilenameAt(resolvedDirect, newEntry.scannedRecs)
			s.cacheMu.Lock()
			if s.gen.Load() == gen {
				s.putEntryLocked(newEntry)
			}
			s.cacheMu.Unlock()
			s.incrementalUpdates.Add(1)
			return combined, nil
		}
	}

	// Case 2: Chained predicate refinement from cached prefix [P1..Pn-1]
	if hasPrefix {
		lastPredRaw := predsRaw[(numPreds-1)*predSize : numPreds*predSize]
		filtered, err := FilterCandidates(resolvedDirect, lastPredRaw, 1, prefixIndices)
		if err == nil {
			newEntry := &cacheEntry{
				key:         key,
				indices:     filtered,
				scannedRecs: curTotalRecs,
				dirMtime:    curMtime,
				dirInode:    curInode,
				srExpire:    curSRExpire,
				createdAt:   time.Now(),
			}
			newEntry.tailName, _ = ReadFilenameAt(resolvedDirect, newEntry.scannedRecs)
			s.cacheMu.Lock()
			if s.gen.Load() == gen {
				s.putEntryLocked(newEntry)
			}
			s.cacheMu.Unlock()
			s.chainedHits.Add(1)
			return filtered, nil
		}
	}

	// Case 3: Full scan from record 1
	indices, actualTotal, err := ScanDirRange(resolvedDirect, predsRaw, numPreds, 1)
	if err != nil {
		return nil, err
	}
	newEntry := &cacheEntry{
		key:         key,
		indices:     indices,
		scannedRecs: actualTotal,
		dirMtime:    curMtime,
		dirInode:    curInode,
		srExpire:    curSRExpire,
		createdAt:   time.Now(),
	}
	newEntry.tailName, _ = ReadFilenameAt(resolvedDirect, newEntry.scannedRecs)
	s.cacheMu.Lock()
	if s.gen.Load() == gen {
		s.putEntryLocked(newEntry)
	}
	s.cacheMu.Unlock()
	s.misses.Add(1)
	return indices, nil
}
