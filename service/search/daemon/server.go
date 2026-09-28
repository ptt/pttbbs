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
	"math"
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
	SearchSvcMagic       uint32 = 0x53524348 // "SRCH" (legacy V1)
	SearchSvcMagicV2     uint32 = 0x53524332 // "SRC2" (V2 with explicit source)
	SearchAIDMagic       uint32 = 0x53414944 // "SAID" (legacy V1)
	SearchAIDMagicV2     uint32 = 0x53414932 // "SAI2" (V2 with explicit source)
	SearchInvalMagic     uint32 = 0x53494E56 // "SINV"
	SearchHintMagic      uint32 = 0x53484E54 // "SHNT"
	MaxSearchPredicates  int32  = 8
	DefaultMaxEntries    int    = 2048
	DefaultMaxIndices    int64  = 16 * 1024 * 1024 // 16M int32s (~64MB)
	DefaultMaxAIDEntries     int           = 16384 // 16K AID cache entries (~2.5MB)
	DefaultMaxAIDTables      int           = 64    // 64 boards with in-memory AID table (~30-60MB)
	DefaultAIDTableMinReqs   int64         = 100   // Min cumulative queries (search + aid) before admitting board to AID table cache
	DefaultAIDTableTTL       time.Duration = 1 * time.Hour
	DefaultAIDTableEvictLead int64         = 100 // Query lead required to replace an expired cached board
	DefaultWebSearchMaxDepth int           = 100000 // Max records to scan backwards for web tail search (0 = unlimited)
	DefaultCacheTTL                        = 1 * time.Hour
	aiduRawMask          uint64 = 0x00001FFFFFFFFFFF
	aiduTypeG            uint64 = 1 << 44
	aiduIdxShift                = 45
)

const (
	HintTypePost    int32 = 1
	HintTypeComment int32 = 2
	HintTypeDelete  int32 = 3
)

const (
	SrcUnknown   int32 = 0
	SrcMbbsdHash int32 = 1 // mbbsd '#' AID search (user / mobile app / bot)
	SrcMbbsdSR   int32 = 2 // mbbsd select_read (title, author, mark, push search)
	SrcMbbsdLua  int32 = 3 // mbbsd bbslua banner / dynamic template (setaidfile)
	SrcBoarddWeb int32 = 4 // web / boardd query
	SrcExternal  int32 = 5 // external tools / scripts
)

func SourceName(src int32) string {
	switch src {
	case SrcMbbsdHash:
		return "mbbsd_hash"
	case SrcMbbsdSR:
		return "mbbsd_sr"
	case SrcMbbsdLua:
		return "mbbsd_lua"
	case SrcBoarddWeb:
		return "web_boardd"
	case SrcExternal:
		return "external"
	default:
		return "unknown"
	}
}

func SourceTag(src int32) string {
	switch src {
	case SrcMbbsdHash:
		return "HASH"
	case SrcMbbsdSR:
		return "SR"
	case SrcMbbsdLua:
		return "LUA"
	case SrcBoarddWeb:
		return "WEB"
	case SrcExternal:
		return "EXT"
	default:
		return "UNK"
	}
}

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

	AIDHashQueries int64            `json:"aid_hash_queries"`
	AIDHashMisses  int64            `json:"aid_hash_misses"`
	AIDLuaQueries  int64            `json:"aid_lua_queries"`
	AIDWebQueries  int64            `json:"aid_web_queries"`
	SearchSR       int64            `json:"search_sr_queries"`
	SearchWeb      int64            `json:"search_web_queries"`
	AIDSources     map[string]int64 `json:"aid_sources,omitempty"`
	AIDMissSources map[string]int64 `json:"aid_miss_sources,omitempty"`
	SearchSources  map[string]int64 `json:"search_sources,omitempty"`
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
	UptimeSeconds      int64            `json:"uptime_seconds"`
	PrivateDirtyKB     int64            `json:"private_dirty_kb"`
	CachedEntries      int              `json:"cached_entries"`
	CachedIndices      int64            `json:"cached_indices"`
	MaxEntries         int              `json:"max_entries"`
	MaxIndices         int64            `json:"max_indices"`
	CachedAIDEntries   int              `json:"cached_aid_entries"`
	MaxAIDEntries      int              `json:"max_aid_entries"`
	Hits               int64            `json:"hits"`
	Misses             int64            `json:"misses"`
	IncrementalUpdates int64            `json:"incremental_updates"`
	ChainedHits        int64            `json:"chained_hits"`
	Evictions          int64            `json:"evictions"`
	Invalidations      int64            `json:"invalidations"`
	AIDHits            int64            `json:"aid_hits"`
	AIDNegativeHits    int64            `json:"aid_negative_hits"`
	AIDMisses          int64            `json:"aid_misses"`
	AIDEvictions       int64            `json:"aid_evictions"`
	AIDTableHits       int64            `json:"aid_table_hits,omitempty"`
	CachedAIDTables    int              `json:"cached_aid_tables,omitempty"`
	MaxAIDTables       int              `json:"max_aid_tables,omitempty"`
	AIDTableMinReqs    int64            `json:"aid_table_min_reqs,omitempty"`
	AIDTableEvictions  int64            `json:"aid_table_evictions,omitempty"`
	AIDSources         map[string]int64 `json:"aid_sources,omitempty"`
	AIDMissSources     map[string]int64 `json:"aid_miss_sources,omitempty"`
	SearchSources      map[string]int64 `json:"search_sources,omitempty"`
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

type BoardAIDTable struct {
	mu           sync.RWMutex
	direct       string
	bid          int32
	maxBacktrack int32
	maxTimeDiff  int64
	minTS        uint32
	maxTS        uint32
	aids         []uint64 // 0-based: index i corresponds to 1-based recno i+1
	loadedAt     time.Time
}

// readPrivateDirtyKB reads Private_Dirty from /proc/$PID/smaps_rollup (or /proc/self/smaps_rollup).
// Returns memory in kilobytes (kB), or 0 if unavailable.
func readPrivateDirtyKB() int64 {
	path := fmt.Sprintf("/proc/%d/smaps_rollup", os.Getpid())
	data, err := os.ReadFile(path)
	if err != nil {
		data, err = os.ReadFile("/proc/self/smaps_rollup")
		if err != nil {
			return 0
		}
	}
	scanner := bufio.NewScanner(bytes.NewReader(data))
	for scanner.Scan() {
		line := scanner.Text()
		if strings.HasPrefix(line, "Private_Dirty:") {
			var kb int64
			if _, err := fmt.Sscanf(line, "Private_Dirty: %d", &kb); err == nil {
				return kb
			}
		}
	}
	return 0
}

func computeBoardAIDStats(aids []uint64) (maxBacktrack int32, maxTimeDiff int64, minTS uint32, maxTS uint32) {
	if len(aids) == 0 {
		return 0, 0, 0, 0
	}
	var maxSeenTS uint32 = 0
	maxSeenIdx := 0

	for i, raw := range aids {
		if raw == 0 {
			continue
		}
		ts := uint32((raw >> 12) & 0xFFFFFFFF)
		if ts == 0 {
			continue
		}
		if minTS == 0 || ts < minTS {
			minTS = ts
		}
		if ts > maxTS {
			maxTS = ts
		}

		if ts >= maxSeenTS {
			maxSeenTS = ts
			maxSeenIdx = i
		} else {
			tdiff := int64(maxSeenTS - ts)
			if tdiff > maxTimeDiff {
				maxTimeDiff = tdiff
			}
			earliest := maxSeenIdx
			for j := maxSeenIdx - 1; j >= 0 && j >= i-int(maxBacktrack)-1000; j-- {
				rawJ := aids[j]
				if rawJ == 0 {
					continue
				}
				tsJ := uint32((rawJ >> 12) & 0xFFFFFFFF)
				if tsJ > ts {
					earliest = j
				} else if ts-tsJ > 86400 {
					break
				}
			}
			btrack := int32(i - earliest)
			if btrack > maxBacktrack {
				maxBacktrack = btrack
			}
		}
	}
	return
}

func (t *BoardAIDTable) SearchAID(targetAIDU uint64) int32 {
	t.mu.RLock()
	defer t.mu.RUnlock()

	targetRaw := targetAIDU & aiduRawMask
	if targetRaw == 0 || len(t.aids) == 0 {
		return 0
	}
	targetTS := uint32((targetRaw >> 12) & 0xFFFFFFFF)

	// Quick range check
	if targetTS > 0 {
		if t.minTS > 0 && targetTS < t.minTS {
			return 0
		}
		if t.maxTS > 0 && targetTS > t.maxTS {
			return 0
		}
	}

	// 1. Fast check tail (last 64 records)
	tailStart := len(t.aids) - 64
	if tailStart < 0 {
		tailStart = 0
	}
	for i := len(t.aids) - 1; i >= tailStart; i-- {
		if t.aids[i] == targetRaw {
			return int32(i + 1)
		}
	}

	// 2. Binary search by timestamp
	low, high := 0, len(t.aids) - 1
	mid := 0
	for low <= high {
		mid = low + (high-low)/2
		midRaw := t.aids[mid]
		if midRaw == 0 {
			found := false
			for delta := 1; mid+delta <= high || mid-delta >= low; delta++ {
				if mid+delta <= high && t.aids[mid+delta] != 0 {
					mid = mid + delta
					midRaw = t.aids[mid]
					found = true
					break
				}
				if mid-delta >= low && t.aids[mid-delta] != 0 {
					mid = mid - delta
					midRaw = t.aids[mid]
					found = true
					break
				}
			}
			if !found {
				break
			}
		}
		if midRaw == targetRaw {
			return int32(mid + 1)
		}
		midTS := uint32((midRaw >> 12) & 0xFFFFFFFF)
		if midTS == targetTS {
			break
		} else if midTS < targetTS {
			low = mid + 1
		} else {
			high = mid - 1
		}
	}

	// 3. Scan window around mid
	winSize := int(t.maxBacktrack)*2 + 128
	if winSize < 256 {
		winSize = 256
	}
	wStart := mid - winSize/2
	if wStart < 0 {
		wStart = 0
	}
	wEnd := mid + winSize/2
	if wEnd > len(t.aids) {
		wEnd = len(t.aids)
	}

	for i := wEnd - 1; i >= wStart; i-- {
		if t.aids[i] == targetRaw {
			return int32(i + 1)
		}
	}
	return 0
}

func (t *BoardAIDTable) AppendPost(recno int32, aidu uint64) {
	t.mu.Lock()
	defer t.mu.Unlock()

	raw := aidu & aiduRawMask
	if raw == 0 {
		return
	}
	idx := int(recno - 1)
	if idx < 0 {
		return
	}
	ts := uint32((raw >> 12) & 0xFFFFFFFF)
	if ts > 0 {
		if t.minTS == 0 || ts < t.minTS {
			t.minTS = ts
		}
		if ts > t.maxTS {
			t.maxTS = ts
		}
	}
	for len(t.aids) < idx {
		t.aids = append(t.aids, 0)
	}
	if len(t.aids) == idx {
		t.aids = append(t.aids, raw)
	} else {
		t.aids[idx] = raw
	}
}

func (t *BoardAIDTable) DeletePost(recno int32) {
	t.mu.Lock()
	defer t.mu.Unlock()

	idx := int(recno - 1)
	if idx >= 0 && idx < len(t.aids) {
		t.aids = append(t.aids[:idx], t.aids[idx+1:]...)
	}
}

func (t *BoardAIDTable) SyncTail(direct string, curTotal int32) {
	t.mu.Lock()
	defer t.mu.Unlock()

	curLen := int32(len(t.aids))
	if curTotal <= curLen {
		return
	}
	for r := curLen + 1; r <= curTotal; r++ {
		fn, ok := ReadFilenameAt(direct, r)
		if ok && fn != "" {
			raw := FNToAIDU(fn) & aiduRawMask
			t.aids = append(t.aids, raw)
			ts := uint32((raw >> 12) & 0xFFFFFFFF)
			if ts > 0 {
				if t.minTS == 0 || ts < t.minTS {
					t.minTS = ts
				}
				if ts > t.maxTS {
					t.maxTS = ts
				}
			}
		} else {
			t.aids = append(t.aids, 0)
		}
	}
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

	aidBySrc        [6]atomic.Int64
	aidMissBySrc    [6]atomic.Int64
	searchBySrc     [6]atomic.Int64
	searchMissBySrc [6]atomic.Int64
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

	aidTableMu        sync.Mutex
	aidTables         map[string]*BoardAIDTable
	maxAIDTables      int
	aidTableMinReqs   int64
	aidTableTTL       time.Duration
	aidTableEvictLead int64
	aidTableEvictions atomic.Int64
	webSearchMaxDepth int

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
	aidTableHits       atomic.Int64
	aidBySrc           [6]atomic.Int64
	aidMissBySrc       [6]atomic.Int64
	searchBySrc        [6]atomic.Int64
	searchMissBySrc    [6]atomic.Int64

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
		aidTables:         make(map[string]*BoardAIDTable),
		maxAIDTables:      DefaultMaxAIDTables,
		aidTableMinReqs:   DefaultAIDTableMinReqs,
		aidTableTTL:       DefaultAIDTableTTL,
		aidTableEvictLead: DefaultAIDTableEvictLead,
		webSearchMaxDepth: DefaultWebSearchMaxDepth,
		stopChan:          make(chan struct{}),
	}
}

func (s *Service) Verbose() int {
	return int(s.verbose.Load())
}

func (s *Service) SetVerbose(level int) {
	s.verbose.Store(int32(level))
}

func (s *Service) SetCacheLimits(maxEntries int, maxIndices int64, maxAIDEntries int, maxAIDTables int, cacheTTL time.Duration) {
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

	s.aidTableMu.Lock()
	s.maxAIDTables = maxAIDTables
	s.aidTableMu.Unlock()
}

func (s *Service) SetAIDTablePolicy(minReqs int64, ttl time.Duration, evictLead int64) {
	s.aidTableMu.Lock()
	if minReqs >= 0 {
		s.aidTableMinReqs = minReqs
		if evictLead <= 0 {
			evictLead = minReqs
		}
	}
	if ttl > 0 {
		s.aidTableTTL = ttl
	}
	if evictLead > 0 {
		s.aidTableEvictLead = evictLead
	}
	s.aidTableMu.Unlock()
}

func (s *Service) SetWebSearchPolicy(maxDepth int) {
	s.cacheMu.Lock()
	if maxDepth >= 0 {
		s.webSearchMaxDepth = maxDepth
	}
	s.cacheMu.Unlock()
}

func (s *Service) boardTotalQueries(act *boardActivity) int64 {
	if act == nil {
		return 0
	}
	return act.searchQueries.Load() + act.aidQueries.Load()
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

func (s *Service) getPeerInfo(conn net.Conn) (string, string) {
	unixConn, ok := conn.(*net.UnixConn)
	if !ok {
		return "", ""
	}
	raw, err := unixConn.SyscallConn()
	if err != nil {
		return "", ""
	}
	var ucred *syscall.Ucred
	var sysErr error
	_ = raw.Control(func(fd uintptr) {
		ucred, sysErr = syscall.GetsockoptUcred(int(fd), syscall.SOL_SOCKET, syscall.SO_PEERCRED)
	})
	if sysErr != nil || ucred == nil || ucred.Pid <= 0 {
		return "", ""
	}
	pid := ucred.Pid
	var clientComm string
	if comm, err := os.ReadFile(fmt.Sprintf("/proc/%d/comm", pid)); err == nil {
		clientComm = strings.TrimSpace(string(comm))
	}
	if userid := UserIDByPID(pid); userid != "" {
		if clientComm != "" {
			return fmt.Sprintf("user=%s(%s,pid=%d)", userid, clientComm, pid), clientComm
		}
		return fmt.Sprintf("user=%s(pid=%d)", userid, pid), clientComm
	}
	if clientComm != "" {
		return fmt.Sprintf("pid=%d(%s)", pid, clientComm), clientComm
	}
	return fmt.Sprintf("pid=%d", pid), clientComm
}

func (s *Service) handleConn(conn net.Conn) {
	defer conn.Close()
	_ = conn.SetDeadline(time.Now().Add(5 * time.Second))

	peerInfo, clientComm := s.getPeerInfo(conn)

	br := bufio.NewReader(conn)
	first, err := br.Peek(1)
	if err != nil {
		return
	}
	if first[0] == '{' {
		s.handleControlConn(br, conn, conn)
		return
	}
	s.handleBinaryConn(br, conn, peerInfo, clientComm)
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
	case "status", "stats":
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

		s.aidTableMu.Lock()
		aidTablesCount := len(s.aidTables)
		maxAIDTables := s.maxAIDTables
		aidTableMinReqs := s.aidTableMinReqs
		s.aidTableMu.Unlock()

		aidSources := make(map[string]int64)
		aidMissSources := make(map[string]int64)
		searchSources := make(map[string]int64)
		for src := int32(0); src < 6; src++ {
			name := SourceName(src)
			aidSources[name] = s.aidBySrc[src].Load()
			aidMissSources[name] = s.aidMissBySrc[src].Load()
			searchSources[name] = s.searchBySrc[src].Load()
		}

		stats := ServiceStats{
			UptimeSeconds:      int64(time.Since(s.startTime).Seconds()),
			PrivateDirtyKB:     readPrivateDirtyKB(),
			CachedEntries:      entriesCount,
			CachedIndices:      indicesCount,
			MaxEntries:         maxEntries,
			MaxIndices:         maxIndices,
			CachedAIDEntries:   aidEntriesCount,
			MaxAIDEntries:      maxAIDEntries,
			CachedAIDTables:    aidTablesCount,
			MaxAIDTables:       maxAIDTables,
			AIDTableMinReqs:    aidTableMinReqs,
			AIDTableEvictions:  s.aidTableEvictions.Load(),
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
			AIDTableHits:       s.aidTableHits.Load(),
			AIDSources:         aidSources,
			AIDMissSources:     aidMissSources,
			SearchSources:      searchSources,
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
	if bid <= 0 {
		bid = s.ResolveBoardBID(direct, 0)
	}
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
	var bname string
	if bid > 0 {
		if name := BoardName(bid); name != "" {
			bname = name
		}
	}
	if bname == "" {
		dir := filepath.Dir(direct)
		base := filepath.Base(dir)
		if base != "" && base != "." && base != "/" {
			bname = base
		} else {
			bname = direct
		}
	}
	if filepath.Base(direct) == ".Names" || strings.HasSuffix(direct, "/.Names") {
		return bname + "/digest"
	}
	return bname
}

func (s *Service) ResolveBoardBID(direct string, bid int32) int32 {
	if bid > 0 {
		return bid
	}
	dir := filepath.Dir(direct)
	base := filepath.Base(dir)
	if base != "" && base != "." && base != "/" {
		if found := BoardBID(base); found > 0 {
			return found
		}
	}
	return 0
}

func (s *Service) TopBoards(limit int, sortBy string) []BoardStats {
	statsMap := make(map[string]*BoardStats)

	s.boardMu.RLock()
	for direct, act := range s.boards {
		bid := act.bid.Load()
		aidSrcs := make(map[string]int64)
		aidMissSrcs := make(map[string]int64)
		searchSrcs := make(map[string]int64)
		for src := int32(0); src < 6; src++ {
			if v := act.aidBySrc[src].Load(); v > 0 {
				aidSrcs[SourceName(src)] = v
			}
			if v := act.aidMissBySrc[src].Load(); v > 0 {
				aidMissSrcs[SourceName(src)] = v
			}
			if v := act.searchBySrc[src].Load(); v > 0 {
				searchSrcs[SourceName(src)] = v
			}
		}
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

			AIDHashQueries: act.aidBySrc[SrcMbbsdHash].Load(),
			AIDHashMisses:  act.aidMissBySrc[SrcMbbsdHash].Load(),
			AIDLuaQueries:  act.aidBySrc[SrcMbbsdLua].Load(),
			AIDWebQueries:  act.aidBySrc[SrcBoarddWeb].Load(),
			SearchSR:       act.searchBySrc[SrcMbbsdSR].Load(),
			SearchWeb:      act.searchBySrc[SrcBoarddWeb].Load(),
			AIDSources:     aidSrcs,
			AIDMissSources: aidMissSrcs,
			SearchSources:  searchSrcs,
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

// LookupBoardBacktrack returns the cached max backtrack for direct.
// It is strictly a non-blocking in-memory read. If not yet scanned, it returns 0.
// This guarantees the QueryAID query path NEVER triggers synchronous disk I/O,
// stat calls, or full directory scans when an AID is not in cache.
func (s *Service) LookupBoardBacktrack(direct string) int32 {
	s.backtrackMu.RLock()
	defer s.backtrackMu.RUnlock()
	if info, ok := s.backtrack[direct]; ok && info != nil && info.MaxBacktrack >= 0 {
		return info.MaxBacktrack
	}
	return -1
}

// RefreshBoardBacktrack computes or refreshes the backtrack for direct (used by admin ctl / maintenance).
func (s *Service) RefreshBoardBacktrack(direct string, bid int32) int32 {
	st, err := os.Stat(direct)
	if err != nil {
		return -1
	}

	fhSize := int64(FileheaderSize())
	curTotal := int32(st.Size() / fhSize)

	s.backtrackMu.Lock()
	info, ok := s.backtrack[direct]
	if ok && info != nil && info.MaxBacktrack >= 0 {
		if curTotal <= info.TotalRecs || time.Since(info.LastScanned) < 10*time.Minute {
			val := info.MaxBacktrack
			s.backtrackMu.Unlock()
			return val
		}
		info.LastScanned = time.Now()
		s.backtrackMu.Unlock()
	} else {
		info = &BoardBacktrackInfo{
			Direct:          direct,
			Board:           s.ResolveBoardName(direct, bid),
			Bid:             bid,
			TotalRecs:       curTotal,
			MaxBacktrack:    0,
			MaxTimeDiffSecs: 0,
			DirMtime:        st.ModTime(),
			LastScanned:     time.Now(),
		}
		s.backtrack[direct] = info
		s.backtrackMu.Unlock()
	}

	totalRecs, maxBacktrack, maxTimeDiff := ComputeDirBacktrack(direct)

	s.backtrackMu.Lock()
	if info != nil {
		info.TotalRecs = totalRecs
		info.MaxBacktrack = maxBacktrack
		info.MaxTimeDiffSecs = maxTimeDiff
		info.DirMtime = st.ModTime()
		info.LastScanned = time.Now()
	}
	s.backtrackMu.Unlock()

	return maxBacktrack
}

func (s *Service) AllBacktrackInfo() []BoardBacktrackInfo {
	s.boardMu.RLock()
	type target struct {
		direct string
		bid    int32
	}
	var targets []target
	for direct, act := range s.boards {
		targets = append(targets, target{direct: direct, bid: act.bid.Load()})
	}
	s.boardMu.RUnlock()

	for _, t := range targets {
		s.RefreshBoardBacktrack(t.direct, t.bid)
	}

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

	s.aidTableMu.Lock()
	for k, tbl := range s.aidTables {
		if match(k, tbl.bid) {
			delete(s.aidTables, k)
		}
	}
	s.aidTableMu.Unlock()

	if s.Verbose() >= 1 {
		peerPrefix := ""
		if peerInfo != "" {
			peerPrefix = peerInfo + " "
		}
		log.Printf("[search.svc] [INVAL] Invalidate: %sboard=%s bid=%d flushed=%d", peerPrefix, s.ResolveBoardName(resolvedDirect, bid), bid, flushed)
	}
	return flushed
}

func (s *Service) GetOrCreateAIDTable(resolvedDirect string, bid int32) *BoardAIDTable {
	if bid <= 0 {
		bid = s.ResolveBoardBID(resolvedDirect, 0)
	}
	s.aidTableMu.Lock()
	if s.maxAIDTables == 0 {
		s.aidTableMu.Unlock()
		return nil
	}
	if tbl := s.aidTables[resolvedDirect]; tbl != nil {
		s.aidTableMu.Unlock()
		return tbl
	}

	act := s.getBoardActivity(resolvedDirect, bid)
	candQueries := s.boardTotalQueries(act)

	// Rule 1: cumulative queries (search + aid) >= aidTableMinReqs to admit into cache
	if candQueries < s.aidTableMinReqs {
		s.aidTableMu.Unlock()
		return nil
	}

	// Rule 2 & 3: 當 cache 滿了，除去 TTL 還沒到的看板，query 次數大於目前看板 100 次的才能把 cache 內的替換出來
	if s.maxAIDTables > 0 && len(s.aidTables) >= s.maxAIDTables {
		now := time.Now()
		var minQueries int64 = math.MaxInt64
		var victimDirect string

		for direct, cachedTbl := range s.aidTables {
			if now.Sub(cachedTbl.loadedAt) < s.aidTableTTL {
				continue // Rule 2: TTL 1hr 不被 swap out
			}
			cachedAct := s.getBoardActivity(direct, cachedTbl.bid)
			q := s.boardTotalQueries(cachedAct)
			if q < minQueries {
				minQueries = q
				victimDirect = direct
			}
		}

		// Rule 4: 沒法替換的一律走傳統路
		if victimDirect == "" || candQueries < minQueries+s.aidTableEvictLead {
			s.aidTableMu.Unlock()
			return nil
		}
	}
	s.aidTableMu.Unlock()

	aids, err := LoadDirAIDs(resolvedDirect)
	if err != nil {
		if s.Verbose() >= 1 {
			log.Printf("[search.svc] [AID-TABLE] failed to load AIDs for %s: %v", resolvedDirect, err)
		}
		return nil
	}

	maxBtrack, maxTdiff, minTS, maxTS := computeBoardAIDStats(aids)

	tbl := &BoardAIDTable{
		direct:       resolvedDirect,
		bid:          bid,
		maxBacktrack: maxBtrack,
		maxTimeDiff:  maxTdiff,
		minTS:        minTS,
		maxTS:        maxTS,
		aids:         aids,
		loadedAt:     time.Now(),
	}

	s.aidTableMu.Lock()
	if existing := s.aidTables[resolvedDirect]; existing != nil {
		s.aidTableMu.Unlock()
		return existing
	}

	if s.maxAIDTables > 0 && len(s.aidTables) >= s.maxAIDTables {
		now := time.Now()
		var minQueries int64 = math.MaxInt64
		var victimDirect string

		for direct, cachedTbl := range s.aidTables {
			if now.Sub(cachedTbl.loadedAt) < s.aidTableTTL {
				continue
			}
			cachedAct := s.getBoardActivity(direct, cachedTbl.bid)
			q := s.boardTotalQueries(cachedAct)
			if q < minQueries {
				minQueries = q
				victimDirect = direct
			}
		}

		if victimDirect == "" || candQueries < minQueries+s.aidTableEvictLead {
			s.aidTableMu.Unlock()
			return nil
		}

		delete(s.aidTables, victimDirect)
		s.aidTableEvictions.Add(1)
		if s.Verbose() >= 1 {
			log.Printf("[search.svc] [AID-TABLE-REPLACE] Replaced board=%s (queries=%d) with board=%s (queries=%d)",
				s.ResolveBoardName(victimDirect, 0), minQueries,
				s.ResolveBoardName(resolvedDirect, bid), candQueries)
		}
	}

	s.aidTables[resolvedDirect] = tbl
	s.aidTableMu.Unlock()

	s.backtrackMu.Lock()
	s.backtrack[resolvedDirect] = &BoardBacktrackInfo{
		Direct:          resolvedDirect,
		Board:           s.ResolveBoardName(resolvedDirect, bid),
		Bid:             bid,
		TotalRecs:       int32(len(aids)),
		MaxBacktrack:    maxBtrack,
		MaxTimeDiffSecs: maxTdiff,
		LastScanned:     time.Now(),
	}
	s.backtrackMu.Unlock()

	if s.Verbose() >= 1 {
		log.Printf("[search.svc] [AID-TABLE] loaded %d AIDs for board %s (%.2f MB, maxBacktrack=%d, maxTimeDiff=%ds, queries=%d)",
			len(aids), s.ResolveBoardName(resolvedDirect, bid), float64(len(aids)*8)/(1024*1024), maxBtrack, maxTdiff, candQueries)
	}

	return tbl
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

// Legacy V1 request headers (without explicit source field)
type binaryReqHeaderV1 struct {
	Magic    uint32
	Bid      int32
	Offset   int32
	Limit    int32
	NumPreds int32
	Direct   [256]byte
}

type binaryAIDReqHeaderV1 struct {
	Magic        uint32
	Bid          int32
	AIDU         uint64
	RequiredMode int32
	Direct       [256]byte
}

// V2 request headers with explicit source field
type binaryReqHeader struct {
	Magic    uint32
	Bid      int32
	Offset   int32
	Limit    int32
	NumPreds int32
	Source   int32
	Direct   [256]byte
}

type binaryAIDReqHeader struct {
	Magic        uint32
	Bid          int32
	AIDU         uint64
	RequiredMode int32
	Source       int32
	Direct       [256]byte
}

type binaryRespHeader struct {
	Status int32
	Total  int32
	Count  int32
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

type binaryHintReqHeader struct {
	Magic  uint32
	Type   int32
	Bid    int32
	Recno  int32
	Aidu   uint64
	Data   int32
	Fh     [128]byte
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

func (s *Service) handleBinaryConn(r *bufio.Reader, w io.Writer, peerInfo, clientComm string) {
	magicBytes, err := r.Peek(4)
	if err != nil {
		return
	}
	magic := binary.LittleEndian.Uint32(magicBytes)
	switch magic {
	case SearchSvcMagic:
		s.handleBinarySearchConn(r, w, peerInfo, clientComm, false)
	case SearchSvcMagicV2:
		s.handleBinarySearchConn(r, w, peerInfo, clientComm, true)
	case SearchAIDMagic:
		s.handleBinaryAIDConn(r, w, peerInfo, clientComm, false)
	case SearchAIDMagicV2:
		s.handleBinaryAIDConn(r, w, peerInfo, clientComm, true)
	case SearchInvalMagic:
		s.handleBinaryInvalConn(r, w, peerInfo)
	case SearchHintMagic:
		s.handleBinaryHintConn(r, w, peerInfo)
	default:
		writeBinaryError(w, -1)
	}
}

func (s *Service) handleBinaryHintConn(r io.Reader, w io.Writer, peerInfo string) {
	var req binaryHintReqHeader
	if err := binary.Read(r, binary.LittleEndian, &req); err != nil {
		return
	}
	resolvedDirect := s.resolveDirectPath(req.Direct)

	switch req.Type {
	case HintTypePost:
		s.handleHintPost(peerInfo, resolvedDirect, req.Bid, req.Recno, req.Aidu, req.Fh)
	case HintTypeComment:
		s.handleHintComment(peerInfo, resolvedDirect, req.Bid, req.Recno, req.Data)
	case HintTypeDelete:
		s.handleHintDelete(peerInfo, resolvedDirect, req.Bid, req.Recno, req.Aidu)
	}

	status := int32(0)
	_ = binary.Write(w, binary.LittleEndian, &status)
}

func (s *Service) handleHintPost(peerInfo, direct string, bid, recno int32, aidu uint64, fhBytes [128]byte) {
	if aidu == 0 {
		fnBytes := bytes.TrimRight(fhBytes[:33], "\x00")
		aidu = FNToAIDU(string(fnBytes))
	}
	if aidu == 0 || recno <= 0 {
		return
	}

	key := aidCacheKey{
		direct:       direct,
		bid:          bid,
		aiduRaw:      aidu & aiduRawMask,
		requiredMode: 0,
	}

	entry := &aidCacheEntry{
		key:         key,
		foundIdx:    recno,
		fhBytes:     fhBytes,
		scannedRecs: recno,
		createdAt:   time.Now(),
	}

	s.aidMu.Lock()
	s.putAIDEntryLocked(entry)
	s.aidMu.Unlock()

	s.aidTableMu.Lock()
	tbl := s.aidTables[direct]
	s.aidTableMu.Unlock()
	if tbl != nil {
		tbl.AppendPost(recno, aidu)
	}

	if s.Verbose() >= 1 {
		log.Printf("[search.svc] [HINT-POST] %sboard=%s bid=%d recno=%d aidu=%012x",
			peerInfo, s.ResolveBoardName(direct, bid), bid, recno, aidu)
	}
}

func (s *Service) handleHintComment(peerInfo, direct string, bid, recno int32, recommend int32) {
	s.aidMu.Lock()
	for _, e := range s.aidEntries {
		if e.foundIdx == recno && (e.key.bid == bid || bid == 0 || e.key.direct == direct) {
			SetFileheaderRecommend(&e.fhBytes, int(recommend))
		}
	}
	s.aidMu.Unlock()

	if s.Verbose() >= 2 {
		log.Printf("[search.svc] [HINT-COMMENT] %sboard=%s bid=%d recno=%d recommend=%d",
			peerInfo, s.ResolveBoardName(direct, bid), bid, recno, recommend)
	}
}

func (s *Service) handleHintDelete(peerInfo, direct string, bid, recno int32, aidu uint64) {
	if aidu != 0 {
		key := aidCacheKey{
			direct:       direct,
			bid:          bid,
			aiduRaw:      aidu & aiduRawMask,
			requiredMode: 0,
		}
		s.aidMu.Lock()
		if e, ok := s.aidEntries[key]; ok {
			s.removeAIDEntryLocked(e)
		}
		s.aidMu.Unlock()
	}

	s.aidTableMu.Lock()
	if tbl, ok := s.aidTables[direct]; ok {
		if recno > 0 {
			tbl.DeletePost(recno)
		} else {
			delete(s.aidTables, direct)
		}
	}
	s.aidTableMu.Unlock()

	s.InvalidateWithPeer(peerInfo, direct, bid)

	if s.Verbose() >= 1 {
		log.Printf("[search.svc] [HINT-DEL] %sboard=%s bid=%d recno=%d aidu=%012x",
			peerInfo, s.ResolveBoardName(direct, bid), bid, recno, aidu)
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

func (s *Service) handleBinaryAIDConn(r io.Reader, w io.Writer, peerInfo, clientComm string, isV2 bool) {
	var bid int32
	var aidu uint64
	var requiredMode int32
	var source int32
	var directBytes [256]byte

	if isV2 {
		var req binaryAIDReqHeader
		if err := binary.Read(r, binary.LittleEndian, &req); err != nil {
			return
		}
		bid = req.Bid
		aidu = req.AIDU
		requiredMode = req.RequiredMode
		source = req.Source
		directBytes = req.Direct
	} else {
		var req binaryAIDReqHeaderV1
		if err := binary.Read(r, binary.LittleEndian, &req); err != nil {
			return
		}
		bid = req.Bid
		aidu = req.AIDU
		requiredMode = req.RequiredMode
		source = SrcUnknown
		directBytes = req.Direct
	}

	if source == SrcUnknown {
		if clientComm == "boardd" {
			source = SrcBoarddWeb
		} else if clientComm == "mbbsd" {
			source = SrcMbbsdHash // legacy mbbsd AID query was always from '#'
		}
	}

	resolvedDirect := s.resolveDirectPath(directBytes)
	if resolvedDirect == "" {
		resp := binaryAIDResp{Status: -1}
		_ = binary.Write(w, binary.LittleEndian, &resp)
		return
	}

	foundIdx, fhBytes, err := s.QueryAIDWithPeer(peerInfo, clientComm, source, resolvedDirect, bid, aidu, requiredMode)
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

func (s *Service) handleBinarySearchConn(r io.Reader, w io.Writer, peerInfo, clientComm string, isV2 bool) {
	var bid int32
	var offset int32
	var limit int32
	var numPreds int
	var source int32
	var directBytes [256]byte

	if isV2 {
		var hdr binaryReqHeader
		if err := binary.Read(r, binary.LittleEndian, &hdr); err != nil {
			return
		}
		bid = hdr.Bid
		offset = hdr.Offset
		limit = hdr.Limit
		numPreds = int(hdr.NumPreds)
		source = hdr.Source
		directBytes = hdr.Direct
	} else {
		var hdr binaryReqHeaderV1
		if err := binary.Read(r, binary.LittleEndian, &hdr); err != nil {
			return
		}
		bid = hdr.Bid
		offset = hdr.Offset
		limit = hdr.Limit
		numPreds = int(hdr.NumPreds)
		source = SrcUnknown
		directBytes = hdr.Direct
	}

	if source == SrcUnknown {
		if clientComm == "boardd" {
			source = SrcBoarddWeb
		} else if clientComm == "mbbsd" {
			source = SrcMbbsdSR // legacy mbbsd predicate search was select_read (SR)
		}
	}

	if numPreds <= 0 || numPreds > int(MaxSearchPredicates) {
		writeBinaryError(w, -1)
		return
	}

	predSize := PredSize()
	predsBytesLen := numPreds * predSize
	predsRaw := make([]byte, predsBytesLen)
	if _, err := io.ReadFull(r, predsRaw); err != nil {
		writeBinaryError(w, -2)
		return
	}
	SanitizePreds(predsRaw, numPreds)

	resolvedDirect := s.resolveDirectPath(directBytes)
	if resolvedDirect == "" {
		writeBinaryError(w, -3)
		return
	}

	var window []int32
	var total int32
	var err error

	if source == SrcBoarddWeb && offset < 0 && limit > 0 {
		window, total, err = s.QueryWebSearchTail(peerInfo, clientComm, source, resolvedDirect, bid, predsRaw, numPreds, offset, limit)
		if err != nil {
			if s.Verbose() > 0 {
				peerPrefix := ""
				if peerInfo != "" {
					peerPrefix = peerInfo + " "
				}
				log.Printf("[search.svc] QueryWebSearchTail error on %s%s: %v", peerPrefix, resolvedDirect, err)
			}
			writeBinaryError(w, -4)
			return
		}
	} else {
		indices, err := s.QueryIndicesWithPeer(peerInfo, clientComm, source, resolvedDirect, bid, predsRaw, numPreds)
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

		total = int32(len(indices))
		if offset < 0 {
			offset += total
		}
		if offset < 0 {
			offset = 0
		}
		if limit < 0 {
			limit = 0
		}

		if offset < total && limit > 0 {
			end64 := int64(offset) + int64(limit)
			if end64 > int64(total) {
				end64 = int64(total)
			}
			end := int32(end64)
			window = indices[offset:end]
		}
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
	return s.QueryAIDWithPeer("", "", SrcUnknown, resolvedDirect, bid, aidu, requiredMode)
}

func (s *Service) QueryAIDWithPeer(peerInfo, clientComm string, source int32, resolvedDirect string, bid int32, aidu uint64, requiredMode int32) (int32, [128]byte, error) {
	if source < 0 || source >= 6 {
		source = SrcUnknown
	}
	start := time.Now()
	act := s.getBoardActivity(resolvedDirect, bid)
	act.aidQueries.Add(1)
	act.aidBySrc[source].Add(1)
	s.aidBySrc[source].Add(1)

	srcTag := SourceTag(source)
	peerPrefix := ""
	if peerInfo != "" {
		peerPrefix = fmt.Sprintf("[%s] %s ", srcTag, peerInfo)
	} else {
		peerPrefix = fmt.Sprintf("[%s] ", srcTag)
	}

	var emptyFH [128]byte

	// Fast check: M. article cannot exist in .Names; G. article cannot exist in .DIR
	isG := (aidu & aiduTypeG) != 0
	baseName := filepath.Base(resolvedDirect)
	if isG && baseName == ".DIR" {
		act.aidMissBySrc[source].Add(1)
		s.aidMissBySrc[source].Add(1)
		return 0, emptyFH, nil
	}
	if !isG && (baseName == ".Names" || strings.HasSuffix(resolvedDirect, "/.Names")) {
		act.aidMissBySrc[source].Add(1)
		s.aidMissBySrc[source].Add(1)
		return 0, emptyFH, nil
	}

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
				act.aidMissBySrc[source].Add(1)
				s.aidMissBySrc[source].Add(1)
			}
			if s.Verbose() >= 2 {
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

	// Try in-memory BoardAIDTable (nanosecond lookup & negative resolution)
	tbl := s.GetOrCreateAIDTable(resolvedDirect, bid)
	if tbl != nil {
		if curTotalRecs > int32(len(tbl.aids)) {
			tbl.SyncTail(resolvedDirect, curTotalRecs)
		} else if curTotalRecs < int32(len(tbl.aids)) {
			// Directory shrank (e.g. unhinted delete); reload table
			s.aidTableMu.Lock()
			delete(s.aidTables, resolvedDirect)
			s.aidTableMu.Unlock()
			tbl = s.GetOrCreateAIDTable(resolvedDirect, bid)
		}

		if tbl != nil {
			foundRec := tbl.SearchAID(aidu)
			if foundRec == 0 {
				// Fast negative hit! The record definitely does not exist on this board.
				gen := s.gen.Load()
				newEntry := &aidCacheEntry{
					key:         key,
					foundIdx:    0,
					fhBytes:     emptyFH,
					scannedRecs: curTotalRecs,
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
				s.aidMisses.Add(1)
				act.aidMisses.Add(1)
				act.aidMissBySrc[source].Add(1)
				s.aidMissBySrc[source].Add(1)
				s.aidTableHits.Add(1)
				if s.Verbose() >= 2 {
					log.Printf("[search.svc] [AID-TABLE-NEG] QueryAID: %sboard=%s bid=%d aidu=%012x dur=%v",
						peerPrefix, s.ResolveBoardName(resolvedDirect, bid), bid, aidu, dur)
				}
				return 0, emptyFH, nil
			}

			// In-memory hit! Read the exact fileheader at foundRec (1 pread)
			fhBytes, ok := ReadFileheaderAt(resolvedDirect, foundRec)
			if ok {
				if MatchFileheaderMode(&fhBytes, requiredMode) {
					gen := s.gen.Load()
					newEntry := &aidCacheEntry{
						key:         key,
						foundIdx:    foundRec,
						fhBytes:     fhBytes,
						scannedRecs: curTotalRecs,
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
					s.aidMisses.Add(1)
					act.aidMisses.Add(1)
					s.aidTableHits.Add(1)
					if s.Verbose() >= 2 {
						log.Printf("[search.svc] [AID-TABLE-HIT] QueryAID: %sboard=%s bid=%d aidu=%012x foundIdx=%d dur=%v",
							peerPrefix, s.ResolveBoardName(resolvedDirect, bid), bid, aidu, foundRec, dur)
					}
					return foundRec, fhBytes, nil
				}
				act.aidMissBySrc[source].Add(1)
				s.aidMissBySrc[source].Add(1)
				return 0, emptyFH, nil
			}
		}
	}

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

	maxBacktrack := s.LookupBoardBacktrack(resolvedDirect)
	gen := s.gen.Load()
	foundIdx, fhBytes, actualTotal, computedBacktrack, computedTimeDiff, err := SearchAIDInDir(
		resolvedDirect, aiduWithHint, int(requiredMode), startRec, int(maxBacktrack))
	if err != nil {
		call.err = err
		return 0, emptyFH, err
	}

	if computedBacktrack >= 0 {
		s.backtrackMu.Lock()
		s.backtrack[resolvedDirect] = &BoardBacktrackInfo{
			Direct:          resolvedDirect,
			Board:           s.ResolveBoardName(resolvedDirect, bid),
			Bid:             bid,
			TotalRecs:       actualTotal,
			MaxBacktrack:    computedBacktrack,
			MaxTimeDiffSecs: computedTimeDiff,
			DirMtime:        curMtime,
			LastScanned:     time.Now(),
		}
		s.backtrackMu.Unlock()
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
		if foundIdx == 0 {
			act.aidMissBySrc[source].Add(1)
			s.aidMissBySrc[source].Add(1)
		}
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
	return s.QueryIndicesWithPeer("", "", SrcUnknown, resolvedDirect, bid, predsRaw, numPreds)
}

func (s *Service) QueryIndicesWithPeer(peerInfo, clientComm string, source int32, resolvedDirect string, bid int32, predsRaw []byte, numPreds int) ([]int32, error) {
	if source < 0 || source >= 6 {
		source = SrcUnknown
	}
	start := time.Now()
	act := s.getBoardActivity(resolvedDirect, bid)
	act.searchQueries.Add(1)
	act.searchBySrc[source].Add(1)
	s.searchBySrc[source].Add(1)

	srcTag := SourceTag(source)
	peerPrefix := ""
	if peerInfo != "" {
		peerPrefix = fmt.Sprintf("[%s] %s ", srcTag, peerInfo)
	} else {
		peerPrefix = fmt.Sprintf("[%s] ", srcTag)
	}

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

	call.indices, call.err = s.computeAndCache(key, resolvedDirect, bid, predsRaw, numPreds, curTotalRecs, curMtime, curInode, curSRExpire, act, start, peerPrefix, source)
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
	peerPrefix string,
	source int32,
) ([]int32, error) {
	var baseEntryCopy *cacheEntry
	var prefixIndices []int32
	hasPrefix := false

	predSize := PredSize()
	predsStr := FormatPreds(predsRaw, numPreds)
	gen := s.gen.Load()
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
	act.searchMissBySrc[source].Add(1)
	s.misses.Add(1)
	s.searchMissBySrc[source].Add(1)
	if s.Verbose() >= 1 {
		log.Printf("[search.svc] [MISS] ScanDirRange: %sboard=%s bid=%d preds=[%s] total=%d matches=%d dur=%v",
			peerPrefix, s.ResolveBoardName(resolvedDirect, bid), bid, predsStr, actualTotal, len(indices), dur)
	}
	return indices, nil
}

func (s *Service) QueryWebSearchTail(
	peerInfo, clientComm string, source int32,
	resolvedDirect string, bid int32,
	predsRaw []byte, numPreds int,
	offset, limit int32,
) ([]int32, int32, error) {
	start := time.Now()
	act := s.getBoardActivity(resolvedDirect, bid)
	act.searchQueries.Add(1)
	act.searchBySrc[source].Add(1)
	s.searchBySrc[source].Add(1)

	srcTag := SourceTag(source)
	peerPrefix := ""
	if peerInfo != "" {
		peerPrefix = fmt.Sprintf("[%s] %s ", srcTag, peerInfo)
	} else {
		peerPrefix = fmt.Sprintf("[%s] ", srcTag)
	}

	st, err := os.Stat(resolvedDirect)
	if err != nil {
		return nil, 0, err
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

	// Step 1: Check fast-path cache in s.entries (from prior full scan)
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
				indices := entry.indices
				s.cacheMu.Unlock()
				s.hits.Add(1)
				act.searchHits.Add(1)

				total := int32(len(indices))
				wOffset := offset + total
				if wOffset < 0 {
					wOffset = 0
				}
				var window []int32
				if wOffset < total && limit > 0 {
					end := wOffset + limit
					if end > total {
						end = total
					}
					window = indices[wOffset:end]
				}
				if s.Verbose() >= 2 {
					log.Printf("[search.svc] [HIT] QueryWebSearchTail: %sboard=%s bid=%d preds=[%s] matches=%d",
						peerPrefix, s.ResolveBoardName(resolvedDirect, bid), bid, FormatPreds(predsRaw, numPreds), total)
				}
				return window, total, nil
			}
		}
	}
	s.cacheMu.Unlock()

	// Step 2: Cache miss -> Perform fast Reverse Tail Scan!
	targetMatches := int(-offset)
	if targetMatches < int(limit) {
		targetMatches = int(limit)
	}

	maxDepth := s.webSearchMaxDepth
	revMatches, totalMatches, stoppedEarly, err := ScanDirReverse(resolvedDirect, predsRaw, numPreds, targetMatches, maxDepth)
	if err != nil {
		return nil, 0, err
	}

	dur := time.Since(start)
	act.scanDurationNs.Add(dur.Nanoseconds())
	s.misses.Add(1)
	s.searchMissBySrc[source].Add(1)
	act.searchMisses.Add(1)
	act.searchMissBySrc[source].Add(1)

	var window []int32
	if !stoppedEarly {
		// Full scan completed down to record 1: revMatches contains ALL matches in the board.
		// Reverse revMatches to chronological order (oldest to newest) and apply standard offset window.
		chronMatches := make([]int32, len(revMatches))
		for i := 0; i < len(revMatches); i++ {
			chronMatches[i] = revMatches[len(revMatches)-1-i]
		}
		wOffset := offset + totalMatches
		if wOffset < 0 {
			wOffset = 0
		}
		if wOffset < totalMatches && limit > 0 {
			end := wOffset + limit
			if end > totalMatches {
				end = totalMatches
			}
			if end > int32(len(chronMatches)) {
				end = int32(len(chronMatches))
			}
			if wOffset < end {
				window = chronMatches[wOffset:end]
			}
		}
	} else {
		// Stopped early: revMatches contains targetMatches newest matches in reverse chronological order.
		// Slice requested page from revMatches:
		startIdx := targetMatches - int(limit)
		endIdx := targetMatches
		if startIdx < 0 {
			startIdx = 0
		}
		if startIdx < len(revMatches) {
			if endIdx > len(revMatches) {
				endIdx = len(revMatches)
			}
			pageSlice := revMatches[startIdx:endIdx]
			// Reverse pageSlice into chronological order (oldest to newest) as expected by boardd/pttweb
			window = make([]int32, len(pageSlice))
			for i := 0; i < len(pageSlice); i++ {
				window[i] = pageSlice[len(pageSlice)-1-i]
			}
		}
	}

	if s.Verbose() >= 1 {
		log.Printf("[search.svc] [WEB-TAIL] QueryWebSearchTail: %sboard=%s bid=%d preds=[%s] offset=%d limit=%d matches=%d total=%d stoppedEarly=%v dur=%v",
			peerPrefix, s.ResolveBoardName(resolvedDirect, bid), bid, FormatPreds(predsRaw, numPreds), offset, limit, len(window), totalMatches, stoppedEarly, dur)
	}

	return window, totalMatches, nil
}
