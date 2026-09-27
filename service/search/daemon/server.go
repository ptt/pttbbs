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
	"runtime"
	"runtime/pprof"
	"sort"
	"strings"
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
	Action  string `json:"action"`
	Bid     int32  `json:"bid,omitempty"`
	Board   string `json:"board,omitempty"`
	Limit   int    `json:"limit,omitempty"`
	SortBy  string `json:"sort_by,omitempty"`
	Level   *int   `json:"level,omitempty"`
	Profile string `json:"profile,omitempty"`
	Seconds int    `json:"seconds,omitempty"`
	Debug   int    `json:"debug,omitempty"`
}

type ControlResponse struct {
	Status  string      `json:"status"`
	Message string      `json:"message,omitempty"`
	Data    interface{} `json:"data,omitempty"`
}

type CachedEntryInfo struct {
	Board       string    `json:"board"`
	Bid         int32     `json:"bid"`
	Predicates  string    `json:"predicates"`
	Matches     int       `json:"matches"`
	ScannedRecs int32     `json:"scanned_recs"`
	CreatedAt   time.Time `json:"created_at"`
}

type BoardStats struct {
	Direct          string  `json:"direct"`
	Board           string  `json:"board"`
	Bid             int32   `json:"bid"`
	CachedEntries   int     `json:"cached_entries"`
	CachedIndices   int64   `json:"cached_indices"`
	CachedAID       int     `json:"cached_aid_entries"`
	SearchQueries   int64   `json:"search_queries"`
	SearchHits      int64   `json:"search_hits"`
	SearchMisses    int64   `json:"search_misses"`
	AIDQueries      int64   `json:"aid_queries"`
	AIDHits         int64   `json:"aid_hits"`
	AIDMisses       int64   `json:"aid_misses"`
	ScanDurationMs  float64 `json:"scan_duration_ms"`
	MaxBacktrack    int32   `json:"max_backtrack"`
	MaxTimeDiffSecs int64   `json:"max_time_diff_secs"`
}

type BoardBacktrackInfo struct {
	Direct          string    `json:"direct"`
	Board           string    `json:"board"`
	Bid             int32     `json:"bid"`
	TotalRecs       int32     `json:"total_recs"`
	MaxBacktrack    int32     `json:"max_backtrack"`
	MaxTimeDiffSecs int64     `json:"max_time_diff_secs"`
	DirMtime        time.Time `json:"dir_mtime"`
	LastScanned     time.Time `json:"last_scanned"`
}

type PprofResult struct {
	Profile string `json:"profile"`
	Seconds int    `json:"seconds,omitempty"`
	IsText  bool   `json:"is_text"`
	Text    string `json:"text,omitempty"`
	Bytes   []byte `json:"bytes,omitempty"`
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

type boardActivity struct {
	direct         string
	bid            atomic.Int32
	searchQueries  atomic.Int64
	searchHits     atomic.Int64
	searchMisses   atomic.Int64
	aidQueries     atomic.Int64
	aidHits        atomic.Int64
	aidMisses      atomic.Int64
	scanDurationNs atomic.Int64
}

type Service struct {
	bbsHome    string
	socketPath string
	listener   net.Listener
	shm        *bbs.SHMClient
	verbose    atomic.Int32
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

	boardMu sync.RWMutex
	boards  map[string]*boardActivity

	backtrackMu sync.RWMutex
	backtrack   map[string]*BoardBacktrackInfo

	cpuProfileMu sync.Mutex

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
		boards:        make(map[string]*boardActivity),
		backtrack:     make(map[string]*BoardBacktrackInfo),
		stopChan:      make(chan struct{}),
	}
}

func (s *Service) Verbose() int {
	return int(s.verbose.Load())
}

func (s *Service) SetVerbose(level int) {
	s.verbose.Store(int32(level))
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

func (s *Service) getPeerInfo(conn net.Conn) string {
	unixConn, ok := conn.(*net.UnixConn)
	if !ok {
		return ""
	}
	raw, err := unixConn.SyscallConn()
	if err != nil {
		return ""
	}
	var ucred *syscall.Ucred
	var sysErr error
	_ = raw.Control(func(fd uintptr) {
		ucred, sysErr = syscall.GetsockoptUcred(int(fd), syscall.SOL_SOCKET, syscall.SO_PEERCRED)
	})
	if sysErr != nil || ucred == nil || ucred.Pid <= 0 {
		return ""
	}
	pid := ucred.Pid
	if userid := UserIDByPID(pid); userid != "" {
		return fmt.Sprintf("user=%s(pid=%d)", userid, pid)
	}
	if comm, err := os.ReadFile(fmt.Sprintf("/proc/%d/comm", pid)); err == nil {
		name := strings.TrimSpace(string(comm))
		if name != "" {
			return fmt.Sprintf("pid=%d(%s)", pid, name)
		}
	}
	return fmt.Sprintf("pid=%d", pid)
}

func (s *Service) handleConn(conn net.Conn) {
	defer conn.Close()
	_ = conn.SetDeadline(time.Now().Add(5 * time.Second))

	peerInfo := s.getPeerInfo(conn)

	br := bufio.NewReader(conn)
	first, err := br.Peek(1)
	if err != nil {
		return
	}
	if first[0] == '{' {
		s.handleControlConn(br, conn, conn)
		return
	}
	s.handleBinaryConn(br, conn, peerInfo)
}

func (s *Service) handleControlConn(r io.Reader, w io.Writer, conn net.Conn) {
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

	case "verbose":
		if req.Level != nil {
			s.SetVerbose(*req.Level)
		}
		v := s.Verbose()
		_ = json.NewEncoder(w).Encode(ControlResponse{
			Status:  "ok",
			Message: fmt.Sprintf("verbose level is %d", v),
			Data:    map[string]int{"verbose": v},
		})

	case "top":
		limit := req.Limit
		if limit <= 0 {
			limit = 20
		}
		results := s.TopBoards(limit, req.SortBy)
		_ = json.NewEncoder(w).Encode(ControlResponse{
			Status:  "ok",
			Message: fmt.Sprintf("top %d boards", len(results)),
			Data:    results,
		})

	case "backtrack":
		infos := s.AllBacktrackInfo()
		_ = json.NewEncoder(w).Encode(ControlResponse{
			Status:  "ok",
			Message: fmt.Sprintf("%d boards backtrack cached", len(infos)),
			Data:    infos,
		})

	case "entries":
		filter := req.Board
		if filter == "" && req.Bid > 0 {
			filter = s.ResolveBoardName("", req.Bid)
		}
		var list []CachedEntryInfo
		s.cacheMu.Lock()
		for k, e := range s.entries {
			bname := s.ResolveBoardName(k.direct, k.bid)
			if filter != "" && !strings.EqualFold(bname, filter) && !strings.Contains(k.direct, filter) {
				continue
			}
			numPreds := len(e.key.predsHex) / PredSize()
			predsStr := FormatPreds([]byte(e.key.predsHex), numPreds)
			list = append(list, CachedEntryInfo{
				Board:       bname,
				Bid:         k.bid,
				Predicates:  predsStr,
				Matches:     len(e.indices),
				ScannedRecs: e.scannedRecs,
				CreatedAt:   e.createdAt,
			})
		}
		s.cacheMu.Unlock()
		sort.Slice(list, func(i, j int) bool {
			if list[i].Matches != list[j].Matches {
				return list[i].Matches > list[j].Matches
			}
			return list[i].CreatedAt.After(list[j].CreatedAt)
		})
		if req.Limit > 0 && len(list) > req.Limit {
			list = list[:req.Limit]
		}
		_ = json.NewEncoder(w).Encode(ControlResponse{
			Status:  "ok",
			Message: fmt.Sprintf("%d cached entries", len(list)),
			Data:    list,
		})

	case "pprof":
		s.handleControlPprof(w, req, conn)

	default:
		_ = json.NewEncoder(w).Encode(ControlResponse{
			Status:  "error",
			Message: fmt.Sprintf("unknown action: %s", req.Action),
		})
	}
}

func (s *Service) handleControlPprof(w io.Writer, req ControlRequest, conn net.Conn) {
	profile := req.Profile
	if profile == "" {
		profile = "goroutine"
	}

	switch profile {
	case "cpu":
		s.cpuProfileMu.Lock()
		defer s.cpuProfileMu.Unlock()

		seconds := req.Seconds
		if seconds <= 0 {
			seconds = 10
		}
		if seconds > 60 {
			seconds = 60
		}
		if conn != nil {
			_ = conn.SetDeadline(time.Now().Add(time.Duration(seconds+15) * time.Second))
		}

		var buf bytes.Buffer
		if err := pprof.StartCPUProfile(&buf); err != nil {
			_ = json.NewEncoder(w).Encode(ControlResponse{
				Status:  "error",
				Message: fmt.Sprintf("failed to start cpu profile: %v", err),
			})
			return
		}
		time.Sleep(time.Duration(seconds) * time.Second)
		pprof.StopCPUProfile()

		_ = json.NewEncoder(w).Encode(ControlResponse{
			Status:  "ok",
			Message: fmt.Sprintf("collected %d seconds cpu profile", seconds),
			Data: PprofResult{
				Profile: "cpu",
				Seconds: seconds,
				IsText:  false,
				Bytes:   buf.Bytes(),
			},
		})

	case "goroutine":
		p := pprof.Lookup("goroutine")
		if p == nil {
			_ = json.NewEncoder(w).Encode(ControlResponse{
				Status:  "error",
				Message: "goroutine profile not found",
			})
			return
		}
		debug := req.Debug
		if debug == 0 {
			debug = 2
		}
		var buf bytes.Buffer
		if err := p.WriteTo(&buf, debug); err != nil {
			_ = json.NewEncoder(w).Encode(ControlResponse{
				Status:  "error",
				Message: fmt.Sprintf("failed to write goroutine profile: %v", err),
			})
			return
		}
		if debug > 0 {
			_ = json.NewEncoder(w).Encode(ControlResponse{
				Status:  "ok",
				Message: "goroutine dump",
				Data: PprofResult{
					Profile: "goroutine",
					IsText:  true,
					Text:    buf.String(),
				},
			})
		} else {
			_ = json.NewEncoder(w).Encode(ControlResponse{
				Status:  "ok",
				Message: "goroutine profile",
				Data: PprofResult{
					Profile: "goroutine",
					IsText:  false,
					Bytes:   buf.Bytes(),
				},
			})
		}

	case "heap":
		runtime.GC()
		p := pprof.Lookup("heap")
		if p == nil {
			_ = json.NewEncoder(w).Encode(ControlResponse{
				Status:  "error",
				Message: "heap profile not found",
			})
			return
		}
		debug := req.Debug
		var buf bytes.Buffer
		if err := p.WriteTo(&buf, debug); err != nil {
			_ = json.NewEncoder(w).Encode(ControlResponse{
				Status:  "error",
				Message: fmt.Sprintf("failed to write heap profile: %v", err),
			})
			return
		}
		if debug > 0 {
			_ = json.NewEncoder(w).Encode(ControlResponse{
				Status:  "ok",
				Message: "heap dump",
				Data: PprofResult{
					Profile: "heap",
					IsText:  true,
					Text:    buf.String(),
				},
			})
		} else {
			_ = json.NewEncoder(w).Encode(ControlResponse{
				Status:  "ok",
				Message: "heap profile",
				Data: PprofResult{
					Profile: "heap",
					IsText:  false,
					Bytes:   buf.Bytes(),
				},
			})
		}

	case "block", "mutex", "threadcreate":
		p := pprof.Lookup(profile)
		if p == nil {
			_ = json.NewEncoder(w).Encode(ControlResponse{
				Status:  "error",
				Message: fmt.Sprintf("%s profile not found", profile),
			})
			return
		}
		debug := req.Debug
		var buf bytes.Buffer
		if err := p.WriteTo(&buf, debug); err != nil {
			_ = json.NewEncoder(w).Encode(ControlResponse{
				Status:  "error",
				Message: fmt.Sprintf("failed to write %s profile: %v", profile, err),
			})
			return
		}
		if debug > 0 {
			_ = json.NewEncoder(w).Encode(ControlResponse{
				Status:  "ok",
				Message: fmt.Sprintf("%s dump", profile),
				Data: PprofResult{
					Profile: profile,
					IsText:  true,
					Text:    buf.String(),
				},
			})
		} else {
			_ = json.NewEncoder(w).Encode(ControlResponse{
				Status:  "ok",
				Message: fmt.Sprintf("%s profile", profile),
				Data: PprofResult{
					Profile: profile,
					IsText:  false,
					Bytes:   buf.Bytes(),
				},
			})
		}

	default:
		_ = json.NewEncoder(w).Encode(ControlResponse{
			Status:  "error",
			Message: fmt.Sprintf("unknown profile type: %s", profile),
		})
	}
}

func (s *Service) getBoardActivity(direct string, bid int32) *boardActivity {
	s.boardMu.RLock()
	b, ok := s.boards[direct]
	s.boardMu.RUnlock()
	if ok {
		if bid > 0 && b.bid.Load() == 0 {
			b.bid.Store(bid)
		}
		return b
	}

	s.boardMu.Lock()
	defer s.boardMu.Unlock()
	if b, ok = s.boards[direct]; ok {
		if bid > 0 && b.bid.Load() == 0 {
			b.bid.Store(bid)
		}
		return b
	}
	b = &boardActivity{
		direct: direct,
	}
	b.bid.Store(bid)
	s.boards[direct] = b
	return b
}

func (s *Service) ResolveBoardName(direct string, bid int32) string {
	if bid > 0 {
		if name := BoardName(bid); name != "" {
			return name
		}
	}
	dir := filepath.Dir(direct)
	base := filepath.Base(dir)
	if base != "" && base != "." && base != "/" {
		return base
	}
	return direct
}

func (s *Service) TopBoards(limit int, sortBy string) []BoardStats {
	statsMap := make(map[string]*BoardStats)

	s.boardMu.RLock()
	for direct, act := range s.boards {
		bid := act.bid.Load()
		statsMap[direct] = &BoardStats{
			Direct:         direct,
			Board:          s.ResolveBoardName(direct, bid),
			Bid:            bid,
			SearchQueries:  act.searchQueries.Load(),
			SearchHits:     act.searchHits.Load(),
			SearchMisses:   act.searchMisses.Load(),
			AIDQueries:     act.aidQueries.Load(),
			AIDHits:        act.aidHits.Load(),
			AIDMisses:      act.aidMisses.Load(),
			ScanDurationMs: float64(act.scanDurationNs.Load()) / 1e6,
		}
	}
	s.boardMu.RUnlock()

	s.cacheMu.Lock()
	for k, e := range s.entries {
		bs, ok := statsMap[k.direct]
		if !ok {
			bs = &BoardStats{
				Direct: k.direct,
				Board:  s.ResolveBoardName(k.direct, k.bid),
				Bid:    k.bid,
			}
			statsMap[k.direct] = bs
		}
		bs.CachedEntries++
		bs.CachedIndices += int64(len(e.indices))
		if bs.Bid == 0 && k.bid > 0 {
			bs.Bid = k.bid
		}
	}
	s.cacheMu.Unlock()

	s.aidMu.Lock()
	for k := range s.aidEntries {
		bs, ok := statsMap[k.direct]
		if !ok {
			bs = &BoardStats{
				Direct: k.direct,
				Board:  s.ResolveBoardName(k.direct, k.bid),
				Bid:    k.bid,
			}
			statsMap[k.direct] = bs
		}
		bs.CachedAID++
		if bs.Bid == 0 && k.bid > 0 {
			bs.Bid = k.bid
		}
	}
	s.aidMu.Unlock()

	s.backtrackMu.RLock()
	for direct, bs := range statsMap {
		if binfo, ok := s.backtrack[direct]; ok && binfo != nil {
			bs.MaxBacktrack = binfo.MaxBacktrack
			bs.MaxTimeDiffSecs = binfo.MaxTimeDiffSecs
		} else {
			bs.MaxBacktrack = -1
		}
	}
	s.backtrackMu.RUnlock()

	list := make([]BoardStats, 0, len(statsMap))
	for _, bs := range statsMap {
		list = append(list, *bs)
	}

	sort.Slice(list, func(i, j int) bool {
		switch sortBy {
		case "queries":
			totI := list[i].SearchQueries + list[i].AIDQueries
			totJ := list[j].SearchQueries + list[j].AIDQueries
			if totI != totJ {
				return totI > totJ
			}
		case "time":
			if list[i].ScanDurationMs != list[j].ScanDurationMs {
				return list[i].ScanDurationMs > list[j].ScanDurationMs
			}
		case "indices":
			if list[i].CachedIndices != list[j].CachedIndices {
				return list[i].CachedIndices > list[j].CachedIndices
			}
		case "entries":
			if list[i].CachedEntries != list[j].CachedEntries {
				return list[i].CachedEntries > list[j].CachedEntries
			}
		case "aid":
			if list[i].CachedAID != list[j].CachedAID {
				return list[i].CachedAID > list[j].CachedAID
			}
		default: // "misses" or empty
			totMissI := list[i].SearchMisses + list[i].AIDMisses
			totMissJ := list[j].SearchMisses + list[j].AIDMisses
			if totMissI != totMissJ {
				return totMissI > totMissJ
			}
			totI := list[i].SearchQueries + list[i].AIDQueries
			totJ := list[j].SearchQueries + list[j].AIDQueries
			if totI != totJ {
				return totI > totJ
			}
		}
		return list[i].Direct < list[j].Direct
	})

	if limit > 0 && len(list) > limit {
		list = list[:limit]
	}
	return list
}

func (s *Service) GetBoardBacktrack(direct string, bid int32) int32 {
	st, err := os.Stat(direct)
	if err != nil {
		return -1
	}

	fhSize := int64(FileheaderSize())
	curTotal := int32(st.Size() / fhSize)

	s.backtrackMu.RLock()
	info, ok := s.backtrack[direct]
	if ok && info != nil && info.MaxBacktrack >= 0 {
		// Boards usually only grow or stay the same size (in-place pushes).
		// If total records did not increase, historical timestamps cannot change.
		// If new articles were added, only refresh after a reasonable interval (10 min).
		if curTotal <= info.TotalRecs || time.Since(info.LastScanned) < 10*time.Minute {
			val := info.MaxBacktrack
			s.backtrackMu.RUnlock()
			return val
		}
	}
	s.backtrackMu.RUnlock()

	return s.refreshBoardBacktrack(direct, bid, curTotal, st.ModTime())
}

func (s *Service) refreshBoardBacktrack(direct string, bid int32, curTotal int32, mtime time.Time) int32 {
	s.backtrackMu.Lock()
	info, ok := s.backtrack[direct]
	if ok && info != nil && info.MaxBacktrack >= 0 {
		if curTotal <= info.TotalRecs || time.Since(info.LastScanned) < 10*time.Minute {
			val := info.MaxBacktrack
			s.backtrackMu.Unlock()
			return val
		}
		// Mark LastScanned now so concurrent callers don't duplicate the scan
		info.LastScanned = time.Now()
		s.backtrackMu.Unlock()
	} else {
		// First time seeing this board
		info = &BoardBacktrackInfo{
			Direct:          direct,
			Board:           s.ResolveBoardName(direct, bid),
			Bid:             bid,
			TotalRecs:       curTotal,
			MaxBacktrack:    0,
			MaxTimeDiffSecs: 0,
			DirMtime:        mtime,
			LastScanned:     time.Now(),
		}
		s.backtrack[direct] = info
		s.backtrackMu.Unlock()
	}

	// Compute without holding backtrackMu so other boards and queries are not blocked!
	totalRecs, maxBacktrack, maxTimeDiff := ComputeDirBacktrack(direct)

	s.backtrackMu.Lock()
	if info != nil {
		info.TotalRecs = totalRecs
		info.MaxBacktrack = maxBacktrack
		info.MaxTimeDiffSecs = maxTimeDiff
		info.DirMtime = mtime
		info.LastScanned = time.Now()
	}
	s.backtrackMu.Unlock()

	return maxBacktrack
}

func (s *Service) AllBacktrackInfo() []BoardBacktrackInfo {
	s.backtrackMu.RLock()
	defer s.backtrackMu.RUnlock()
	res := make([]BoardBacktrackInfo, 0, len(s.backtrack))
	for _, info := range s.backtrack {
		res = append(res, *info)
	}
	sort.Slice(res, func(i, j int) bool {
		if res[i].MaxBacktrack != res[j].MaxBacktrack {
			return res[i].MaxBacktrack > res[j].MaxBacktrack
		}
		return res[i].Board < res[j].Board
	})
	return res
}

func (s *Service) Flush(bid int32) int {
	return s.Invalidate("", bid)
}

func (s *Service) Invalidate(resolvedDirect string, bid int32) int {
	return s.InvalidateWithPeer("", resolvedDirect, bid)
}

func (s *Service) InvalidateWithPeer(peerInfo string, resolvedDirect string, bid int32) int {
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

	if s.Verbose() >= 1 {
		peerPrefix := ""
		if peerInfo != "" {
			peerPrefix = peerInfo + " "
		}
		log.Printf("[search.svc] [INVAL] Invalidate: %sboard=%s bid=%d flushed=%d", peerPrefix, s.ResolveBoardName(resolvedDirect, bid), bid, flushed)
	}
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

func (s *Service) handleBinaryConn(r *bufio.Reader, w io.Writer, peerInfo string) {
	magicBytes, err := r.Peek(4)
	if err != nil {
		return
	}
	magic := binary.LittleEndian.Uint32(magicBytes)
	switch magic {
	case SearchSvcMagic:
		s.handleBinarySearchConn(r, w, peerInfo)
	case SearchAIDMagic:
		s.handleBinaryAIDConn(r, w, peerInfo)
	case SearchInvalMagic:
		s.handleBinaryInvalConn(r, w, peerInfo)
	default:
		writeBinaryError(w, -1)
	}
}

func (s *Service) handleBinaryInvalConn(r io.Reader, w io.Writer, peerInfo string) {
	var req binaryInvalReqHeader
	if err := binary.Read(r, binary.LittleEndian, &req); err != nil {
		return
	}
	resolvedDirect := s.resolveDirectPath(req.Direct)
	s.InvalidateWithPeer(peerInfo, resolvedDirect, req.Bid)
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

func (s *Service) handleBinaryAIDConn(r io.Reader, w io.Writer, peerInfo string) {
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

	foundIdx, fhBytes, err := s.QueryAIDWithPeer(peerInfo, resolvedDirect, req.Bid, req.AIDU, req.RequiredMode)
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

func (s *Service) handleBinarySearchConn(r io.Reader, w io.Writer, peerInfo string) {
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

	indices, err := s.QueryIndicesWithPeer(peerInfo, resolvedDirect, hdr.Bid, predsRaw, int(hdr.NumPreds))
	if err != nil {
		if s.Verbose() > 0 {
			peerPrefix := ""
			if peerInfo != "" {
				peerPrefix = peerInfo + " "
			}
			log.Printf("[search.svc] QueryIndices error on %s%s: %v", peerPrefix, resolvedDirect, err)
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

func (s *Service) isAIDEntryValidLocked(e *aidCacheEntry, curTotalRecs int32, curInode uint64) bool {
	if e == nil {
		return false
	}
	if time.Since(e.createdAt) > s.cacheTTL {
		return false
	}
	if curInode != 0 && e.dirInode != 0 && e.dirInode != curInode {
		return false
	}
	if curTotalRecs < e.scannedRecs {
		return false
	}
	return true
}

func (s *Service) QueryAID(resolvedDirect string, bid int32, aidu uint64, requiredMode int32) (int32, [128]byte, error) {
	return s.QueryAIDWithPeer("", resolvedDirect, bid, aidu, requiredMode)
}

func (s *Service) QueryAIDWithPeer(peerInfo string, resolvedDirect string, bid int32, aidu uint64, requiredMode int32) (int32, [128]byte, error) {
	start := time.Now()
	act := s.getBoardActivity(resolvedDirect, bid)
	act.aidQueries.Add(1)

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
	var startRec int32 = 1

	s.aidMu.Lock()
	if entry, ok := s.aidEntries[key]; ok && s.isAIDEntryValidLocked(entry, curTotalRecs, curInode) {
		if entry.scannedRecs == curTotalRecs {
			s.aidLRU.MoveToFront(entry.elem)
			idx := entry.foundIdx
			fh := entry.fhBytes
			s.aidMu.Unlock()
			if idx > 0 {
				s.aidHits.Add(1)
				act.aidHits.Add(1)
			} else {
				s.aidNegativeHits.Add(1)
				act.aidHits.Add(1)
			}
			if s.Verbose() >= 2 {
				peerPrefix := ""
				if peerInfo != "" {
					peerPrefix = peerInfo + " "
				}
				log.Printf("[search.svc] [AID-HIT] QueryAID: %sboard=%s bid=%d foundIdx=%d", peerPrefix, s.ResolveBoardName(resolvedDirect, bid), bid, idx)
			}
			return idx, fh, nil
		}
		if entry.foundIdx > 0 {
			cachedHintIdx = entry.foundIdx
		} else if entry.foundIdx == 0 && entry.scannedRecs > 0 && curTotalRecs > entry.scannedRecs {
			startRec = entry.scannedRecs + 1
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

	maxBacktrack := s.GetBoardBacktrack(resolvedDirect, bid)
	gen := s.gen.Load()
	foundIdx, fhBytes, actualTotal, err := SearchAIDInDir(resolvedDirect, aiduWithHint, int(requiredMode), startRec, int(maxBacktrack))
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

	dur := time.Since(start)
	act.scanDurationNs.Add(dur.Nanoseconds())
	peerPrefix := ""
	if peerInfo != "" {
		peerPrefix = peerInfo + " "
	}
	if cachedHintIdx > 0 && foundIdx > 0 {
		s.aidHits.Add(1)
		act.aidHits.Add(1)
		if s.Verbose() >= 2 {
			log.Printf("[search.svc] [AID-HINT-HIT] QueryAID: %sboard=%s bid=%d aidu=%012x foundIdx=%d dur=%v",
				peerPrefix, s.ResolveBoardName(resolvedDirect, bid), bid, aidu, foundIdx, dur)
		}
	} else {
		s.aidMisses.Add(1)
		act.aidMisses.Add(1)
		if s.Verbose() >= 1 {
			log.Printf("[search.svc] [AID-MISS] QueryAID: %sboard=%s bid=%d aidu=%012x foundIdx=%d dur=%v",
				peerPrefix, s.ResolveBoardName(resolvedDirect, bid), bid, aidu, foundIdx, dur)
		}
	}

	call.foundIdx, call.fhBytes = foundIdx, fhBytes
	return foundIdx, fhBytes, nil
}

func predsRequireSRExpire(predsRaw []byte, numPreds int) bool {
	predSize := PredSize()
	for i := 0; i < numPreds; i++ {
		offset := i * predSize
		if offset+4 <= len(predsRaw) {
			mode := int(binary.LittleEndian.Uint32(predsRaw[offset : offset+4]))
			if mode&(RS_RECOMMEND|RS_MARK|RS_SOLVED|RS_MONEY) != 0 {
				return true
			}
		}
	}
	return false
}

func (s *Service) isEntryValidLocked(e *cacheEntry, curSRExpire int64, curTotalRecs int32, curInode uint64, requireSRExpire bool) bool {
	if e == nil {
		return false
	}
	if time.Since(e.createdAt) > s.cacheTTL {
		return false
	}
	if curInode != 0 && e.dirInode != 0 && e.dirInode != curInode {
		return false
	}
	if requireSRExpire && curSRExpire != 0 && e.srExpire != curSRExpire {
		return false
	}
	if curTotalRecs < e.scannedRecs {
		return false
	}
	return true
}

func (s *Service) QueryIndices(resolvedDirect string, bid int32, predsRaw []byte, numPreds int) ([]int32, error) {
	return s.QueryIndicesWithPeer("", resolvedDirect, bid, predsRaw, numPreds)
}

func (s *Service) QueryIndicesWithPeer(peerInfo string, resolvedDirect string, bid int32, predsRaw []byte, numPreds int) ([]int32, error) {
	start := time.Now()
	act := s.getBoardActivity(resolvedDirect, bid)
	act.searchQueries.Add(1)

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

	requireSRExpire := predsRequireSRExpire(predsRaw, numPreds)

	// Fast-path cache check
	s.cacheMu.Lock()
	if entry, ok := s.entries[key]; ok {
		if s.isEntryValidLocked(entry, curSRExpire, curTotalRecs, curInode, requireSRExpire) &&
			entry.scannedRecs == curTotalRecs {
			valid := entry.dirMtime.Equal(curMtime)
			if !valid && !requireSRExpire {
				if name, ok := ReadFilenameAt(resolvedDirect, entry.scannedRecs); ok && name == entry.tailName {
					entry.dirMtime = curMtime
					valid = true
				}
			}
			if valid {
				s.lruList.MoveToFront(entry.elem)
				res := entry.indices
				s.cacheMu.Unlock()
				s.hits.Add(1)
				act.searchHits.Add(1)
				if s.Verbose() >= 2 {
					peerPrefix := ""
					if peerInfo != "" {
						peerPrefix = peerInfo + " "
					}
					log.Printf("[search.svc] [HIT] QueryIndices: %sboard=%s bid=%d preds=[%s] matches=%d",
						peerPrefix, s.ResolveBoardName(resolvedDirect, bid), bid, FormatPreds(predsRaw, numPreds), len(res))
				}
				return res, nil
			}
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

	call.indices, call.err = s.computeAndCache(key, resolvedDirect, bid, predsRaw, numPreds, curTotalRecs, curMtime, curInode, curSRExpire, act, start, peerInfo)
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
	act *boardActivity,
	start time.Time,
	peerInfo string,
) ([]int32, error) {
	var baseEntryCopy *cacheEntry
	var prefixIndices []int32
	hasPrefix := false

	predSize := PredSize()
	predsStr := FormatPreds(predsRaw, numPreds)
	gen := s.gen.Load()
	peerPrefix := ""
	if peerInfo != "" {
		peerPrefix = peerInfo + " "
	}
	requireSRExpire := predsRequireSRExpire(predsRaw, numPreds)

	s.cacheMu.Lock()
	if entry, ok := s.entries[key]; ok && s.isEntryValidLocked(entry, curSRExpire, curTotalRecs, curInode, requireSRExpire) {
		if entry.scannedRecs == curTotalRecs {
			valid := entry.dirMtime.Equal(curMtime)
			if !valid && !requireSRExpire {
				if name, ok := ReadFilenameAt(resolvedDirect, entry.scannedRecs); ok && name == entry.tailName {
					entry.dirMtime = curMtime
					valid = true
				}
			}
			if valid {
				s.lruList.MoveToFront(entry.elem)
				res := entry.indices
				s.cacheMu.Unlock()
				s.hits.Add(1)
				act.searchHits.Add(1)
				if s.Verbose() >= 2 {
					log.Printf("[search.svc] [HIT] QueryIndices: %sboard=%s bid=%d preds=[%s] matches=%d",
						peerPrefix, s.ResolveBoardName(resolvedDirect, bid), bid, predsStr, len(res))
				}
				return res, nil
			}
		}
		// Incremental tail reuse is safe when the prefix is unchanged.
		if curTotalRecs > entry.scannedRecs && entry.scannedRecs > 0 {
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
		prefixRequireSRExpire := predsRequireSRExpire(predsRaw[:(numPreds-1)*predSize], numPreds-1)
		if pEntry, ok := s.entries[prefixKey]; ok &&
			s.isEntryValidLocked(pEntry, curSRExpire, curTotalRecs, curInode, prefixRequireSRExpire) &&
			pEntry.scannedRecs == curTotalRecs {
			valid := pEntry.dirMtime.Equal(curMtime)
			if !valid && !prefixRequireSRExpire {
				if name, ok := ReadFilenameAt(resolvedDirect, pEntry.scannedRecs); ok && name == pEntry.tailName {
					pEntry.dirMtime = curMtime
					valid = true
				}
			}
			if valid {
				prefixIndices = append([]int32(nil), pEntry.indices...)
				hasPrefix = true
			}
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
			dur := time.Since(start)
			act.scanDurationNs.Add(dur.Nanoseconds())
			act.searchHits.Add(1)
			s.incrementalUpdates.Add(1)
			if s.Verbose() >= 1 {
				log.Printf("[search.svc] [INCR] IncrementalUpdate: %sboard=%s bid=%d preds=[%s] scanned=%d..%d newMatches=%d total=%d dur=%v",
					peerPrefix, s.ResolveBoardName(resolvedDirect, bid), bid, predsStr, baseEntryCopy.scannedRecs+1, actualTotal, len(tailIndices), len(combined), dur)
			}
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
			dur := time.Since(start)
			act.scanDurationNs.Add(dur.Nanoseconds())
			act.searchHits.Add(1)
			s.chainedHits.Add(1)
			if s.Verbose() >= 2 {
				log.Printf("[search.svc] [CHAIN] ChainedHit: %sboard=%s bid=%d preds=[%s] cands=%d filtered=%d dur=%v",
					peerPrefix, s.ResolveBoardName(resolvedDirect, bid), bid, predsStr, len(prefixIndices), len(filtered), dur)
			}
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
	dur := time.Since(start)
	act.scanDurationNs.Add(dur.Nanoseconds())
	act.searchMisses.Add(1)
	s.misses.Add(1)
	if s.Verbose() >= 1 {
		log.Printf("[search.svc] [MISS] ScanDirRange: %sboard=%s bid=%d preds=[%s] total=%d matches=%d dur=%v",
			peerPrefix, s.ResolveBoardName(resolvedDirect, bid), bid, predsStr, actualTotal, len(indices), dur)
	}
	return indices, nil
}
