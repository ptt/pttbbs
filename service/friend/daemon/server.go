package daemon

import (
	"encoding/json"
	"fmt"
	"log"
	"net"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"sync"
	"time"

	"pttbbs/friend/storage"
	"pttbbs/bbs"
)

const (
	FriendBitIRH = 1  // I reject him (pure reject when normalized)
	FriendBitHRM = 2  // He rejects me (pure reject when normalized)
	FriendBitIBH = 4  // Board friend
	FriendBitIFH = 8  // I friend him
	FriendBitHFM = 16 // He friends me
	FriendBitISH = 32 // I super-friend him (in both friend & reject)
	FriendBitHSM = 64 // He super-friends me (in both friend & reject)

	MaxFriend            = 256
	MaxReject            = 32
	MaxFriendOnline      = MaxFriend + MaxReject
	FriendOnlineSlotBits = 18
	FriendOnlineUIDBits  = 6
	FriendOnlineStatBits = 8
	FriendOnlineSlotMask = (1 << FriendOnlineSlotBits) - 1
	FriendOnlineUIDMask  = (1 << FriendOnlineUIDBits) - 1
	FriendOnlineStatMask = (1 << FriendOnlineStatBits) - 1
)

// Compile-time assertion that bitfields sum to 32 bits
var _ [32 - (FriendOnlineSlotBits + FriendOnlineUIDBits + FriendOnlineStatBits)]struct{}
var _ [(FriendOnlineSlotBits + FriendOnlineUIDBits + FriendOnlineStatBits) - 32]struct{}

func NormalizeFriendStat(stat int) int {
	if (stat&(FriendBitIFH|FriendBitIRH)) == (FriendBitIFH|FriendBitIRH) || (stat&FriendBitISH) != 0 {
		stat = (stat &^ FriendBitIRH) | FriendBitIFH | FriendBitISH
	}
	if (stat&(FriendBitHFM|FriendBitHRM)) == (FriendBitHFM|FriendBitHRM) || (stat&FriendBitHSM) != 0 {
		stat = (stat &^ FriendBitHRM) | FriendBitHFM | FriendBitHSM
	}
	return stat
}

func RawFriendStat(stat int) int {
	if (stat&(FriendBitIFH|FriendBitIRH)) == (FriendBitIFH|FriendBitIRH) || (stat&FriendBitISH) != 0 {
		stat |= FriendBitIFH | FriendBitIRH | FriendBitISH
	}
	if (stat&(FriendBitHFM|FriendBitHRM)) == (FriendBitHFM|FriendBitHRM) || (stat&FriendBitHSM) != 0 {
		stat |= FriendBitHFM | FriendBitHRM | FriendBitHSM
	}
	return stat
}

func PackFriendOnline(stat int, uid int, slot int) uint32 {
	if slot < 0 || slot > FriendOnlineSlotMask {
		panic(fmt.Sprintf("PackFriendOnline: slot %d out of %d-bit range [0..%d]", slot, FriendOnlineSlotBits, FriendOnlineSlotMask))
	}
	rawStat := RawFriendStat(stat)
	if rawStat < 0 || rawStat > FriendOnlineStatMask {
		panic(fmt.Sprintf("PackFriendOnline: stat 0x%x out of %d-bit range [0..0x%x]", rawStat, FriendOnlineStatBits, FriendOnlineStatMask))
	}
	if uid < 0 {
		panic(fmt.Sprintf("PackFriendOnline: negative uid %d", uid))
	}
	var uidTag uint32
	if uid > 0 {
		uidTag = (uint32(uid)%FriendOnlineUIDMask + 1) & FriendOnlineUIDMask
		if uidTag == 0 || uidTag > FriendOnlineUIDMask {
			panic(fmt.Sprintf("PackFriendOnline: uidTag %d out of %d-bit range [1..%d]", uidTag, FriendOnlineUIDBits, FriendOnlineUIDMask))
		}
	}
	return ((uint32(rawStat) & FriendOnlineStatMask) << (FriendOnlineSlotBits + FriendOnlineUIDBits)) |
		(uidTag << FriendOnlineSlotBits) |
		(uint32(slot) & FriendOnlineSlotMask)
}

func UnpackFriendOnlineRaw(entry uint32) (stat int, uidTag uint32, slot int) {
	stat = int((entry >> (FriendOnlineSlotBits + FriendOnlineUIDBits)) & FriendOnlineStatMask)
	uidTag = (entry >> FriendOnlineSlotBits) & FriendOnlineUIDMask
	slot = int(entry & FriendOnlineSlotMask)
	return
}

func UnpackFriendOnline(entry uint32) (stat int, uidTag uint32, slot int) {
	rawStat, uidTag, slot := UnpackFriendOnlineRaw(entry)
	return NormalizeFriendStat(rawStat), uidTag, slot
}


type SubscriberSession struct {
	PID     int
	SID     int
	UID     int
	UserID  string
	Targets []string // list of target UserIDs (lowercased) watched by this session
}

type BoardVisableCache struct {
	BID     int
	BrdName string
	BrdAttr uint32
	ModTime time.Time
	Size    int64
	Exists  bool
	UIDs    map[int]struct{}
	// Names records the (lowercased) userid each uid was resolved from, so a
	// uid freed by account deletion and re-assigned is not granted access.
	Names map[int]string
}

type HBFLBoardInfo struct {
	BID          int              `json:"bid"`
	BrdName      string           `json:"brdname"`
	VisableCount int              `json:"visable_count"`
	ModTime      string           `json:"mtime,omitempty"`
	Friends      []FriendPeerInfo `json:"friends,omitempty"`
}

type AlohaCacheEntry struct {
	ModTime      time.Time
	Size         int64
	Exists       bool
	TargetsLower []string
}

type Service struct {
	bbsHome    string
	shmClient  *bbs.SHMClient
	socketPath string
	verbose    int
	mu         sync.RWMutex

	// onlineSessions maps pid -> SubscriberSession
	onlineSessions map[int]SubscriberSession

	// sidToPID maps sid -> pid to detect and evict stale sessions on utmp slot reuse
	sidToPID map[int]int

	// onlineSubscribers maps targetUserID (lowercase) -> pid -> SubscriberSession
	onlineSubscribers map[string]map[int]SubscriberSession

	// alohaCache caches parsed home/<c>/<userid>/alohaed by mtime/size to avoid disk open/read on rapid reconnects
	alohaCache map[string]AlohaCacheEntry

	// lastAlohaNotify tracks the last time an Aloha waterball notification was sent for a login userID (lowercase)
	lastAlohaNotify map[string]time.Time

	// alohaCooldown suppresses duplicate Aloha waterball notifications when the same user rapidly reconnects (default 60s)
	alohaCooldown time.Duration

	// userPIDs maps userID (lowercase) -> pid -> true
	userPIDs map[string]map[int]bool

	// uidToSIDs maps uid -> sid -> pid for online sessions
	uidToSIDs map[int]map[int]int

	// userFriends maps uid -> targetUID -> true (IFH)
	userFriends map[int]map[int]bool

	// revFriends maps targetUID -> uid -> true (HFM)
	revFriends map[int]map[int]bool

	// userRejects maps uid -> targetUID -> true (IRH)
	userRejects map[int]map[int]bool

	// revRejects maps targetUID -> uid -> true (HRM)
	revRejects map[int]map[int]bool

	// sessionFriendsCache stores computed packed friend_online entries per sid
	sessionFriendsCache map[int][]uint32

	// hbflBoards maps 1-indexed bid -> *BoardVisableCache (hidden board friend hash set)
	hbflBoards map[int]*BoardVisableCache

	// hbflBoardNameToBID maps lowercased brdname -> 1-indexed bid
	hbflBoardNameToBID map[string]int

	// hbflUserBoards maps uid -> set of 1-indexed bids where uid is in visable
	hbflUserBoards map[int]map[int]struct{}

	// hbflGen is the monotonic generation counter for hidden board friend list changes
	hbflGen int

	// lastHBFLStatCheck tracks the last time mtime check ran across tracked boards
	lastHBFLStatCheck time.Time

	// testUserToUID assigns deterministic UIDs when running without SHM in unit tests
	testUserToUID map[string]int
	nextTestUID   int

	// sem limits concurrent active IPC connections to prevent goroutine explosion
	sem chan struct{}

	reconcileInterval time.Duration
	enableUIDTag      bool
	autoLegacyCompat  bool // auto-switch reconcileInterval at the cutoff
	autoUIDTag        bool // auto-enable uid tags at the cutoff
}

// LegacyCompatCutoff is 2026-09-27 09:00:00 +0800 (Asia/Taipei).
// Before this cutoff, friend.svc runs in legacy mbbsd compatibility mode by default
// (reconcileInterval=10s, enableUIDTag=false, and 2s HBFL visable mtime polling).
// Once this cutoff passes, friend.svc automatically disables compatibility mode
// (reconcileInterval=1h, enableUIDTag=true, and event-driven HBFL updates).
var LegacyCompatCutoff = time.Date(2026, 9, 27, 9, 0, 0, 0, time.FixedZone("Asia/Taipei", 8*3600))

func IsLegacyCompatActive(now time.Time) bool {
	return now.Before(LegacyCompatCutoff)
}

func NewService(bbsHome string, optSocketPath ...string) (*Service, error) {
	socketPath := ""
	if len(optSocketPath) > 0 && optSocketPath[0] != "" {
		socketPath = optSocketPath[0]
	} else {
		socketPath = filepath.Join(bbsHome, "run", "friend.svc.sock")
	}

	var shmClient *bbs.SHMClient
	if bbsHome != "" {
		if _, err := os.Stat(filepath.Join(bbsHome, ".PASSWDS")); err == nil {
			shmClient, err = bbs.AttachSHM()
			if err != nil {
				log.Printf("[friend.svc] Warning: failed to attach SHM: %v", err)
			}
		}
	}

	initGen := 1
	if shmClient != nil {
		if g := shmClient.GetHBFLGeneration(); g >= initGen {
			initGen = g + 1
		}
		shmClient.SetHBFLGeneration(initGen)
	}

	legacyCompat := shmClient != nil && IsLegacyCompatActive(time.Now())
	defaultReconcile := 1 * time.Hour
	defaultUIDTag := true
	if legacyCompat {
		defaultReconcile = 10 * time.Second
		defaultUIDTag = false
	}

	svc := &Service{
		bbsHome:             bbsHome,
		shmClient:           shmClient,
		socketPath:          socketPath,
		reconcileInterval:   defaultReconcile,
		enableUIDTag:        defaultUIDTag,
		autoLegacyCompat:    legacyCompat,
		autoUIDTag:          legacyCompat,
		alohaCooldown:       60 * time.Second,
		onlineSessions:      make(map[int]SubscriberSession),
		sidToPID:            make(map[int]int),
		onlineSubscribers:   make(map[string]map[int]SubscriberSession),
		alohaCache:          make(map[string]AlohaCacheEntry),
		lastAlohaNotify:     make(map[string]time.Time),
		userPIDs:            make(map[string]map[int]bool),
		uidToSIDs:           make(map[int]map[int]int),
		userFriends:         make(map[int]map[int]bool),
		revFriends:          make(map[int]map[int]bool),
		userRejects:         make(map[int]map[int]bool),
		revRejects:          make(map[int]map[int]bool),
		sessionFriendsCache: make(map[int][]uint32),
		hbflBoards:          make(map[int]*BoardVisableCache),
		hbflBoardNameToBID:  make(map[string]int),
		hbflUserBoards:      make(map[int]map[int]struct{}),
		hbflGen:             initGen,
		testUserToUID:       make(map[string]int),
		nextTestUID:         1,
		sem:                 make(chan struct{}, 5000),
	}
	return svc, nil
}

func (s *Service) SetVerbose(v int) {
	s.verbose = v
}

func (s *Service) SetAlohaCooldown(d time.Duration) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.alohaCooldown = d
}

// GetSessionFriendsOnline returns a copy of the computed friend_online entries for sid
func (s *Service) GetSessionFriendsOnline(sid int) []uint32 {
	s.mu.RLock()
	defer s.mu.RUnlock()
	entries := s.sessionFriendsCache[sid]
	out := make([]uint32, len(entries))
	copy(out, entries)
	return out
}

// Request and Response formats for IPC via UNIX domain socket
type Request struct {
	Action   string `json:"action"` // "login", "friend_sync", "logout", "reload", "hbfl_user", "hbfl_check", "hbfl_reload", "hbfl_board", "status"
	UserID   string `json:"userid,omitempty"`
	TargetID string `json:"target_id,omitempty"`
	SubID    string `json:"sub_id,omitempty"`
	BrdName  string `json:"brdname,omitempty"`
	BID      int    `json:"bid,omitempty"`
	UID      int    `json:"uid,omitempty"`
	PID      int    `json:"pid,omitempty"`
	SID      int    `json:"sid,omitempty"`
}

type Response struct {
	Success bool   `json:"success"`
	Message string `json:"message,omitempty"`
	Gen     int    `json:"gen,omitempty"`
	BIDs    []int  `json:"bids,omitempty"`
	Allowed *bool  `json:"allowed,omitempty"`
	Data    any    `json:"data,omitempty"`
}

// IsSocketOccupied checks if another active process is listening on the UNIX domain socket
func IsSocketOccupied(socketPath string) bool {
	conn, err := net.DialTimeout("unix", socketPath, 500*time.Millisecond)
	if err == nil {
		conn.Close()
		return true
	}
	return false
}

func (s *Service) Start() error {
	if err := os.MkdirAll(filepath.Dir(s.socketPath), 0755); err != nil {
		return err
	}

	// Check if another active instance is already listening on this socket
	if IsSocketOccupied(s.socketPath) {
		return fmt.Errorf("another instance of friend.svc is already running and listening on socket %s", s.socketPath)
	}

	_ = os.Remove(s.socketPath)

	listener, err := net.Listen("unix", s.socketPath)
	if err != nil {
		return fmt.Errorf("failed to listen on socket %s: %w", s.socketPath, err)
	}
	defer listener.Close()

	log.Printf("[friend.svc] Friend Service started listening on UNIX socket: %s", s.socketPath)

	logMemStats()
	startMemoryReporter(1 * time.Hour)

	s.ScanHiddenBoards()
	s.ScanOnlineSessions()
	if s.reconcileInterval > 0 {
		s.StartReconciler(s.reconcileInterval)
	} else if s.autoUIDTag {
		// No reconciler to drive the cutoff transition.
		time.AfterFunc(time.Until(LegacyCompatCutoff), func() { s.checkLegacyCompatTransition() })
	}

	for {
		conn, err := listener.Accept()
		if err != nil {
			log.Printf("[friend.svc] Accept error: %v", err)
			time.Sleep(50 * time.Millisecond) // e.g. EMFILE: don't spin
			continue
		}

		select {
		case s.sem <- struct{}{}:
			go func(c net.Conn) {
				defer func() { <-s.sem }()
				s.handleConnection(c)
			}(conn)
		default:
			_ = conn.SetDeadline(time.Now().Add(500 * time.Millisecond))
			_ = json.NewEncoder(conn).Encode(Response{Success: false, Message: "service overloaded"})
			conn.Close()
		}
	}
}

func (s *Service) handleConnection(conn net.Conn) {
	defer conn.Close()
	_ = conn.SetDeadline(time.Now().Add(3 * time.Second))

	decoder := json.NewDecoder(conn)
	encoder := json.NewEncoder(conn)

	var req Request
	if err := decoder.Decode(&req); err != nil {
		encoder.Encode(Response{Success: false, Message: err.Error()})
		return
	}

	resp := s.ProcessRequest(req)
	encoder.Encode(resp)
}

func FormatFriendStat(stat int) string {
	stat = NormalizeFriendStat(stat)
	var parts []string
	if stat&FriendBitIFH != 0 {
		parts = append(parts, "IFH")
	}
	if stat&FriendBitISH != 0 {
		parts = append(parts, "ISH")
	}
	if stat&FriendBitHFM != 0 {
		parts = append(parts, "HFM")
	}
	if stat&FriendBitHSM != 0 {
		parts = append(parts, "HSM")
	}
	if stat&FriendBitIRH != 0 {
		parts = append(parts, "IRH")
	}
	if stat&FriendBitHRM != 0 {
		parts = append(parts, "HRM")
	}
	if stat&FriendBitIBH != 0 {
		parts = append(parts, "IBH")
	}
	if len(parts) == 0 {
		return "NONE"
	}
	return strings.Join(parts, "|")
}

type OnlineFriendEntry struct {
	SID    int    `json:"sid"`
	UID    int    `json:"uid,omitempty"`
	UserID string `json:"userid,omitempty"`
	Stat   string `json:"stat"`
	UIDTag uint32 `json:"uid_tag"`
	Source string `json:"source"`
}

type UserSessionInfo struct {
	SID            int                 `json:"sid"`
	PID            int                 `json:"pid"`
	SyncSource     string              `json:"sync_source"`
	SHMFriendTotal int                 `json:"shm_friendtotal"`
	OnlineFriends  []OnlineFriendEntry `json:"online_friends,omitempty"`
}

type FriendPeerInfo struct {
	UID    int    `json:"uid"`
	UserID string `json:"userid,omitempty"`
	Online bool   `json:"online"`
}

type UserQueryReport struct {
	UserID               string            `json:"userid"`
	UID                  int               `json:"uid,omitempty"`
	Online               bool              `json:"online"`
	SessionCount         int               `json:"session_count"`
	Sessions             []UserSessionInfo `json:"sessions,omitempty"`
	FriendsStatus        string            `json:"friends_status"`
	MyFriends            []FriendPeerInfo  `json:"my_friends,omitempty"`
	MyRejects            []FriendPeerInfo  `json:"my_rejects,omitempty"`
	ReverseFriendsOnline []FriendPeerInfo  `json:"reverse_friends_online,omitempty"`
	HiddenBoards         []HBFLBoardInfo   `json:"hidden_boards,omitempty"`
	AlohaTargets         []string          `json:"aloha_targets,omitempty"`
}

func (s *Service) ProcessRequest(req Request) Response {
	switch strings.ToLower(req.Action) {
	case "login":
		return s.HandleUserLoginWithUID(req.UserID, req.PID, req.SID, req.UID)
	case "friend_sync":
		return s.HandleFriendSync(req.UserID, req.UID, req.PID, req.SID)
	case "logout":
		return s.HandleUserLogout(req.UserID, req.PID)
	case "reload":
		return s.HandleReloadAloha(req.UserID)
	case "hbfl_user":
		// Without SHM, board BIDs are synthesized locally and do not match
		// the real bcache BIDs mbbsd uses; fail so callers read the file.
		if s.shmClient == nil {
			return Response{Success: false, Message: "hbfl_user unavailable without SHM"}
		}
		return s.HandleHBFLUser(req.UID, req.UserID)
	case "hbfl_check":
		return s.HandleHBFLCheck(req.BID, req.BrdName, req.UID, req.UserID)
	case "hbfl_reload":
		return s.HandleHBFLReload(req.BID, req.BrdName)
	case "hbfl_board", "hbfl":
		return s.HandleHBFLBoard(req.BID, req.BrdName)
	case "query", "user", "info":
		return s.HandleQueryUser(req.UserID)
	case "status":
		return s.HandleStatus()
	default:
		log.Printf("[friend.svc] Received unknown action: %s", req.Action)
		return Response{Success: false, Message: fmt.Sprintf("unknown action: %s", req.Action)}
	}
}

func (s *Service) lookupUserIDByUIDLocked(uid int) string {
	if uid <= 0 {
		return ""
	}
	if s.shmClient != nil {
		if id, err := s.shmClient.GetUserID(uid); err == nil && id != "" {
			return id
		}
	}
	for sids := range s.uidToSIDs[uid] {
		if pid, ok := s.sidToPID[sids]; ok {
			if sess, ok2 := s.onlineSessions[pid]; ok2 && sess.UserID != "" {
				return sess.UserID
			}
		}
	}
	for lower, tuid := range s.testUserToUID {
		if tuid == uid {
			return lower
		}
	}
	return ""
}

func (s *Service) decodeOnlineEntryLocked(entry uint32) OnlineFriendEntry {
	stat, uidTag, slot := UnpackFriendOnline(entry)
	source := "mbbsd-local"
	if uidTag > 0 {
		source = "friend.svc"
	}
	var peerUID int
	var peerUserID string
	if s.shmClient != nil {
		if detail, ok := s.shmClient.GetSessionDetail(slot); ok {
			peerUID = detail.UID
			peerUserID = detail.UserID
		}
	}
	if peerUserID == "" {
		if pid, ok := s.sidToPID[slot]; ok {
			if sess, ok2 := s.onlineSessions[pid]; ok2 {
				peerUID = sess.UID
				peerUserID = sess.UserID
			}
		}
	}
	return OnlineFriendEntry{
		SID:    slot,
		UID:    peerUID,
		UserID: peerUserID,
		Stat:   FormatFriendStat(stat),
		UIDTag: uidTag,
		Source: source,
	}
}

func (s *Service) HandleQueryUser(userID string) Response {
	userID = strings.TrimSpace(userID)
	if userID == "" {
		return Response{Success: false, Message: "missing userid"}
	}

	s.maybeRefreshHBFLStat()

	s.mu.RLock()
	defer s.mu.RUnlock()

	lower := strings.ToLower(userID)
	var uid int
	if s.shmClient != nil {
		uid = s.shmClient.GetUID(userID)
	}
	if uid <= 0 {
		uid = s.testUserToUID[lower]
	}

	var sessions []UserSessionInfo
	var alohaTargets []string
	seenSIDs := make(map[int]bool)

	if pids, ok := s.userPIDs[lower]; ok {
		for pid := range pids {
			sess, exists := s.onlineSessions[pid]
			if !exists {
				continue
			}
			if uid <= 0 && sess.UID > 0 {
				uid = sess.UID
			}
			if len(alohaTargets) == 0 && len(sess.Targets) > 0 {
				alohaTargets = append([]string(nil), sess.Targets...)
			}
			syncSource := "friend.svc"
			entries := s.sessionFriendsCache[sess.SID]
			if s.shmClient != nil && sess.SID >= 0 {
				if detail, ok := s.shmClient.GetSessionDetail(sess.SID); ok && detail.PID == sess.PID {
					if detail.SyncedBySvc {
						syncSource = "friend.svc"
					} else {
						syncSource = "mbbsd-local"
					}
					entries = detail.FriendOnline
				}
			}
			var decoded []OnlineFriendEntry
			for _, e := range entries {
				decoded = append(decoded, s.decodeOnlineEntryLocked(e))
			}
			sessions = append(sessions, UserSessionInfo{
				SID:            sess.SID,
				PID:            sess.PID,
				SyncSource:     syncSource,
				SHMFriendTotal: len(entries),
				OnlineFriends:  decoded,
			})
			seenSIDs[sess.SID] = true
		}
	}

	// Also check SHM directly in case a session exists in SHM that hasn't registered with friend.svc
	if s.shmClient != nil {
		for _, shmSess := range s.shmClient.GetOnlineSessions() {
			if strings.EqualFold(shmSess.UserID, userID) && !seenSIDs[shmSess.SID] {
				if uid <= 0 && shmSess.UID > 0 {
					uid = shmSess.UID
				}
				syncSource := "mbbsd-local"
				var entries []uint32
				if detail, ok := s.shmClient.GetSessionDetail(shmSess.SID); ok {
					if detail.SyncedBySvc {
						syncSource = "friend.svc"
					}
					entries = detail.FriendOnline
				}
				var decoded []OnlineFriendEntry
				for _, e := range entries {
					decoded = append(decoded, s.decodeOnlineEntryLocked(e))
				}
				sessions = append(sessions, UserSessionInfo{
					SID:            shmSess.SID,
					PID:            shmSess.PID,
					SyncSource:     syncSource,
					SHMFriendTotal: len(entries),
					OnlineFriends:  decoded,
				})
				seenSIDs[shmSess.SID] = true
			}
		}
	}

	sort.Slice(sessions, func(i, j int) bool {
		return sessions[i].SID < sessions[j].SID
	})

	online := len(sessions) > 0
	_, hasLoadedGraph := s.uidToSIDs[uid]
	friendsStatus := "not_loaded (user offline)"
	var myFriends []FriendPeerInfo
	var myRejects []FriendPeerInfo
	var revFriendsOnline []FriendPeerInfo
	var hiddenBoards []HBFLBoardInfo

	if uid > 0 {
		for bid := range s.hbflUserBoards[uid] {
			if b, ok := s.hbflBoards[bid]; ok {
				hiddenBoards = append(hiddenBoards, HBFLBoardInfo{
					BID:          b.BID,
					BrdName:      b.BrdName,
					VisableCount: len(b.UIDs),
				})
			}
		}
		sort.Slice(hiddenBoards, func(i, j int) bool { return hiddenBoards[i].BID < hiddenBoards[j].BID })
	}

	if uid > 0 && hasLoadedGraph {
		friendsStatus = "loaded"
		for fuid := range s.userFriends[uid] {
			myFriends = append(myFriends, FriendPeerInfo{
				UID:    fuid,
				UserID: s.lookupUserIDByUIDLocked(fuid),
				Online: len(s.uidToSIDs[fuid]) > 0,
			})
		}
		sort.Slice(myFriends, func(i, j int) bool { return myFriends[i].UID < myFriends[j].UID })

		for ruid := range s.userRejects[uid] {
			myRejects = append(myRejects, FriendPeerInfo{
				UID:    ruid,
				UserID: s.lookupUserIDByUIDLocked(ruid),
				Online: len(s.uidToSIDs[ruid]) > 0,
			})
		}
		sort.Slice(myRejects, func(i, j int) bool { return myRejects[i].UID < myRejects[j].UID })

		for puid := range s.revFriends[uid] {
			if len(s.uidToSIDs[puid]) > 0 {
				revFriendsOnline = append(revFriendsOnline, FriendPeerInfo{
					UID:    puid,
					UserID: s.lookupUserIDByUIDLocked(puid),
					Online: true,
				})
			}
		}
		sort.Slice(revFriendsOnline, func(i, j int) bool { return revFriendsOnline[i].UID < revFriendsOnline[j].UID })
	}

	report := UserQueryReport{
		UserID:               userID,
		UID:                  uid,
		Online:               online,
		SessionCount:         len(sessions),
		Sessions:             sessions,
		FriendsStatus:        friendsStatus,
		MyFriends:            myFriends,
		MyRejects:            myRejects,
		ReverseFriendsOnline: revFriendsOnline,
		HiddenBoards:         hiddenBoards,
		AlohaTargets:         alohaTargets,
	}

	return Response{
		Success: true,
		Message: fmt.Sprintf("User %s: online=%v, sessions=%d, friends=%s, hidden_boards=%d", userID, online, len(sessions), friendsStatus, len(hiddenBoards)),
		Data:    report,
	}
}


func (s *Service) migrateTestUIDLocked(oldUID, newUID int) {
	if oldUID <= 0 || newUID <= 0 || oldUID == newUID {
		return
	}
	if bids, ok := s.hbflUserBoards[oldUID]; ok {
		if _, exists := s.hbflUserBoards[newUID]; !exists {
			s.hbflUserBoards[newUID] = make(map[int]struct{})
		}
		for bid := range bids {
			s.hbflUserBoards[newUID][bid] = struct{}{}
			if b, ok2 := s.hbflBoards[bid]; ok2 && b.UIDs != nil {
				delete(b.UIDs, oldUID)
				b.UIDs[newUID] = struct{}{}
			}
		}
		delete(s.hbflUserBoards, oldUID)
	}
	if rev, ok := s.revFriends[oldUID]; ok {
		if _, exists := s.revFriends[newUID]; !exists {
			s.revFriends[newUID] = make(map[int]bool)
		}
		for puid := range rev {
			s.revFriends[newUID][puid] = true
			if uf, ok2 := s.userFriends[puid]; ok2 {
				delete(uf, oldUID)
				uf[newUID] = true
			}
		}
		delete(s.revFriends, oldUID)
	}
	if rev, ok := s.revRejects[oldUID]; ok {
		if _, exists := s.revRejects[newUID]; !exists {
			s.revRejects[newUID] = make(map[int]bool)
		}
		for puid := range rev {
			s.revRejects[newUID][puid] = true
			if ur, ok2 := s.userRejects[puid]; ok2 {
				delete(ur, oldUID)
				ur[newUID] = true
			}
		}
		delete(s.revRejects, oldUID)
	}
}

func (s *Service) resolveUIDLocked(userID string, hintUID int) int {
	lower := strings.ToLower(userID)
	if s.shmClient != nil {
		if hintUID > 0 {
			return hintUID
		}
		if lower == "" {
			return 0
		}
		return s.shmClient.GetUID(userID)
	}
	if hintUID > 0 {
		if lower != "" {
			if oldUID, ok := s.testUserToUID[lower]; ok && oldUID != hintUID {
				s.migrateTestUIDLocked(oldUID, hintUID)
			}
			s.testUserToUID[lower] = hintUID
		}
		return hintUID
	}
	if lower == "" {
		return 0
	}
	if uid, ok := s.testUserToUID[lower]; ok {
		return uid
	}
	uid := s.nextTestUID
	s.nextTestUID++
	s.testUserToUID[lower] = uid
	return uid
}

func (s *Service) loadSessionFriendData(userID string, hintUID int, pid int, sid int) (uid int, diskFriends []string, diskRejects []string, ok bool) {
	uid = hintUID
	if uid <= 0 && s.shmClient != nil && sid >= 0 {
		if detail, ok := s.shmClient.GetSessionDetail(sid); ok {
			if pid <= 0 || detail.PID == pid {
				uid = detail.UID
			}
		}
	}
	if s.bbsHome != "" && userID != "" {
		df, dr, err := storage.LoadFriendLists(s.bbsHome, userID)
		if err != nil {
			log.Printf("[friend.svc] Warning: failed to load friend lists for %s: %v", userID, err)
			return uid, nil, nil, false
		}
		return uid, df, dr, true
	}
	return uid, nil, nil, true
}


func (s *Service) HandleFriendSync(userID string, uid int, pid int, sid int) Response {
	if userID == "" || pid <= 0 || sid < 0 {
		return Response{Success: false, Message: "invalid userid, pid, or sid"}
	}
	s.registerSubscriberFull(userID, uid, pid, sid, true)
	return Response{
		Success: true,
		Message: fmt.Sprintf("Synced friends for %s (uid=%d, pid=%d, sid=%d)", userID, uid, pid, sid),
	}
}

func (s *Service) getOrLoadAlohaTargets(subscriberID string, force bool) []string {
	subscriberID = strings.TrimSpace(subscriberID)
	if subscriberID == "" || s.bbsHome == "" {
		return nil
	}
	subscriberLower := strings.ToLower(subscriberID)

	filePath, err := storage.GetHomeFile(s.bbsHome, subscriberID, "alohaed")
	if err != nil {
		return nil
	}

	statInfo, statErr := os.Stat(filePath)
	fileExists := statErr == nil && statInfo != nil

	s.mu.RLock()
	cached, hasCached := s.alohaCache[subscriberLower]
	if hasCached && !force {
		if !fileExists && !cached.Exists {
			out := append([]string(nil), cached.TargetsLower...)
			s.mu.RUnlock()
			return out
		}
		if fileExists && cached.Exists &&
			cached.ModTime.Equal(statInfo.ModTime()) &&
			cached.Size == statInfo.Size() {
			out := append([]string(nil), cached.TargetsLower...)
			s.mu.RUnlock()
			return out
		}
	}
	s.mu.RUnlock()

	var targetsLower []string
	var modTime time.Time
	var size int64
	if fileExists {
		alohaList, _ := storage.LoadAlohaTargets(s.bbsHome, subscriberID)
		for _, t := range alohaList {
			targetsLower = append(targetsLower, strings.ToLower(t))
		}
		modTime = statInfo.ModTime()
		size = statInfo.Size()
	}

	s.mu.Lock()
	s.alohaCache[subscriberLower] = AlohaCacheEntry{
		ModTime:      modTime,
		Size:         size,
		Exists:       fileExists,
		TargetsLower: append([]string(nil), targetsLower...),
	}
	s.mu.Unlock()

	return targetsLower
}

func (s *Service) HandleUserLogin(userID string, pid int, sid int) Response {
	return s.HandleUserLoginWithUID(userID, pid, sid, 0)
}

func (s *Service) HandleUserLoginWithUID(userID string, pid int, sid int, uid int) Response {
	if userID == "" || pid <= 0 {
		return Response{Success: false, Message: "invalid userid or pid"}
	}

	userIDLower := strings.ToLower(userID)

	// Register session and load friend/reject lists if not already synced for this (pid, sid)
	s.mu.RLock()
	existingSess, alreadySynced := s.onlineSessions[pid]
	if alreadySynced && (existingSess.SID != sid || !strings.EqualFold(existingSess.UserID, userID)) {
		alreadySynced = false
	}
	s.mu.RUnlock()
	if !alreadySynced {
		s.registerSubscriberFull(userID, uid, pid, sid, true)
	}

	// 1. Notify all active online subscribers that target userID has logged in (subject to cooldown and reject list)
	now := time.Now()
	s.mu.Lock()
	resolvedUID := s.resolveUIDLocked(userID, uid)
	subMap, exists := s.onlineSubscribers[userIDLower]
	var subscribersToNotify []SubscriberSession
	inCooldown := false
	if exists && len(subMap) > 0 {
		if s.alohaCooldown > 0 {
			if lastTime, ok := s.lastAlohaNotify[userIDLower]; ok && now.Sub(lastTime) < s.alohaCooldown {
				inCooldown = true
			}
		}
		if !inCooldown {
			for _, sub := range subMap {
				if strings.EqualFold(sub.UserID, userID) || (resolvedUID > 0 && sub.UID == resolvedUID) {
					continue
				}
				if resolvedUID > 0 && sub.UID > 0 && s.userRejects[resolvedUID][sub.UID] && !s.userFriends[resolvedUID][sub.UID] {
					continue
				}
				subscribersToNotify = append(subscribersToNotify, sub)
			}
			s.lastAlohaNotify[userIDLower] = now
		}
	}
	s.mu.Unlock()

	alohaSvcEnabled := false
	if s.shmClient != nil {
		alohaSvcEnabled = s.shmClient.IsAlohaSvcEnabled()
	} else {
		// In standalone test mode without SHM
		alohaSvcEnabled = true
	}

	notifiedCount := 0
	if alohaSvcEnabled {
		if s.shmClient != nil {
			for _, sub := range subscribersToNotify {
				err := s.shmClient.SendAlohaMessage(sub.SID, sub.PID, pid, userID)
				if err == nil {
					notifiedCount++
				} else {
					log.Printf("[friend.svc] SendAlohaMessage FAIL: sub=%s sid=%d pid=%d: %v", sub.UserID, sub.SID, sub.PID, err)
				}
			}
		} else {
			// Standalone test mode without SHM
			notifiedCount = len(subscribersToNotify)
		}
		if s.verbose > 0 || notifiedCount > 0 {
			log.Printf("[friend.svc] LOGIN: user=%s, pid=%d, sid=%d -> notified %d subscribers", userID, pid, sid, notifiedCount)
		}
		return Response{
			Success: true,
			Message: fmt.Sprintf("User %s logged in, notified %d subscribers", userID, notifiedCount),
		}
	} else {
		foundCount := len(subscribersToNotify)
		if s.verbose > 0 || foundCount > 0 {
			log.Printf("[friend.svc] LOGIN: user=%s, pid=%d, sid=%d -> found %d subscribers", userID, pid, sid, foundCount)
		}
		return Response{
			Success: true,
			Message: fmt.Sprintf("User %s logged in, found %d subscribers", userID, foundCount),
		}
	}
}

func (s *Service) RegisterSubscriber(subscriberID string, pid int, sid int) {
	s.registerSubscriberFull(subscriberID, 0, pid, sid, true)
}

func (s *Service) registerSubscriberFull(subscriberID string, hintUID int, pid int, sid int, syncSHM bool) {
	// Perform Disk/SHM reads OUTSIDE global mutex lock (using mtime cache for alohaed)
	targetsLower := s.getOrLoadAlohaTargets(subscriberID, false)
	uid, diskFriends, diskRejects, listsOK := s.loadSessionFriendData(subscriberID, hintUID, pid, sid)

	subscriberLower := strings.ToLower(subscriberID)

	s.mu.Lock()
	defer s.mu.Unlock()

	if s.shmClient != nil && sid >= 0 && pid > 0 {
		if detail, ok := s.shmClient.GetSessionDetail(sid); !ok || detail.PID != pid {
			return
		}
	}

	uid = s.resolveUIDLocked(subscriberID, uid)
	var friendUIDs, rejectUIDs []int
	for _, fid := range diskFriends {
		if len(friendUIDs) >= MaxFriend {
			break
		}
		if fuid := s.resolveUIDLocked(fid, 0); fuid > 0 {
			friendUIDs = append(friendUIDs, fuid)
		}
	}
	for _, rid := range diskRejects {
		if len(rejectUIDs) >= MaxReject {
			break
		}
		if ruid := s.resolveUIDLocked(rid, 0); ruid > 0 {
			rejectUIDs = append(rejectUIDs, ruid)
		}
	}
	if !listsOK {
		// Transient read error: keep the graph we already have instead of
		// wiping the user's friends and (worse) rejects.
		for fuid := range s.userFriends[uid] {
			friendUIDs = append(friendUIDs, fuid)
		}
		for ruid := range s.userRejects[uid] {
			rejectUIDs = append(rejectUIDs, ruid)
		}
	}

	affectedUIDs := make(map[int]bool)

	// Evict any crashed session that previously occupied the same utmp slot (sid)
	if sid >= 0 {
		if oldPID, ok := s.sidToPID[sid]; ok && oldPID != pid {
			if staleSess, exists := s.onlineSessions[oldPID]; exists {
				_, evictedAffected := s.cleanupSessionLocked(staleSess)
				for auid := range evictedAffected {
					affectedUIDs[auid] = true
				}
			}
		}
	}

	// Cleanup old session for this PID if present
	if oldSess, ok := s.onlineSessions[pid]; ok {
		_, oldAffected := s.cleanupSessionLocked(oldSess)
		for auid := range oldAffected {
			affectedUIDs[auid] = true
		}
	}

	session := SubscriberSession{
		PID:     pid,
		SID:     sid,
		UID:     uid,
		UserID:  subscriberID,
		Targets: targetsLower,
	}

	s.onlineSessions[pid] = session
	if sid >= 0 {
		s.sidToPID[sid] = pid
	}

	if _, ok := s.userPIDs[subscriberLower]; !ok {
		s.userPIDs[subscriberLower] = make(map[int]bool)
	}
	s.userPIDs[subscriberLower][pid] = true

	for _, targetLower := range targetsLower {
		if _, ok := s.onlineSubscribers[targetLower]; !ok {
			s.onlineSubscribers[targetLower] = make(map[int]SubscriberSession)
		}
		s.onlineSubscribers[targetLower][pid] = session
	}

	if uid > 0 && sid >= 0 {
		if _, ok := s.uidToSIDs[uid]; !ok {
			s.uidToSIDs[uid] = make(map[int]int)
		}
		s.uidToSIDs[uid][sid] = pid

		s.updateUserFriendGraphLocked(uid, friendUIDs, rejectUIDs, affectedUIDs)
	}

	if syncSHM {
		for auid := range affectedUIDs {
			s.syncUIDFriendsToSHMLocked(auid)
		}
	}
}

func (s *Service) updateUserFriendGraphLocked(uid int, friendUIDs []int, rejectUIDs []int, affectedUIDs map[int]bool) {
	affectedUIDs[uid] = true

	// Clear old forward friends (IFH) and reverse friends (HFM)
	for oldFUID := range s.userFriends[uid] {
		affectedUIDs[oldFUID] = true
		if rev, ok := s.revFriends[oldFUID]; ok {
			delete(rev, uid)
			if len(rev) == 0 {
				delete(s.revFriends, oldFUID)
			}
		}
	}
	if len(friendUIDs) > 0 {
		newF := make(map[int]bool, len(friendUIDs))
		for _, fuid := range friendUIDs {
			if fuid <= 0 {
				continue
			}
			newF[fuid] = true
			affectedUIDs[fuid] = true
			if _, ok := s.revFriends[fuid]; !ok {
				s.revFriends[fuid] = make(map[int]bool)
			}
			s.revFriends[fuid][uid] = true
		}
		s.userFriends[uid] = newF
	} else {
		delete(s.userFriends, uid)
	}

	// Clear old forward rejects (IRH) and reverse rejects (HRM)
	for oldRUID := range s.userRejects[uid] {
		affectedUIDs[oldRUID] = true
		if rev, ok := s.revRejects[oldRUID]; ok {
			delete(rev, uid)
			if len(rev) == 0 {
				delete(s.revRejects, oldRUID)
			}
		}
	}
	if len(rejectUIDs) > 0 {
		newR := make(map[int]bool, len(rejectUIDs))
		for _, ruid := range rejectUIDs {
			if ruid <= 0 {
				continue
			}
			newR[ruid] = true
			affectedUIDs[ruid] = true
			if _, ok := s.revRejects[ruid]; !ok {
				s.revRejects[ruid] = make(map[int]bool)
			}
			s.revRejects[ruid][uid] = true
		}
		s.userRejects[uid] = newR
	} else {
		delete(s.userRejects, uid)
	}

	// Peers who have uid in their friend/reject lists are also affected when uid comes online
	for puid := range s.revFriends[uid] {
		affectedUIDs[puid] = true
	}
	for puid := range s.revRejects[uid] {
		affectedUIDs[puid] = true
	}
}

func friendStatPriority(stat int) int {
	if (stat & (FriendBitHRM | FriendBitIRH | FriendBitISH | FriendBitHSM)) != 0 {
		return 3
	}
	if (stat & FriendBitIFH) != 0 {
		return 2
	}
	return 1
}

func (s *Service) computeFriendOnlineForUIDLocked(uid int) []uint32 {
	peerStats := make(map[int]int)
	for fuid := range s.userFriends[uid] {
		if len(s.uidToSIDs[fuid]) > 0 {
			peerStats[fuid] |= FriendBitIFH
		}
	}
	for fuid := range s.revFriends[uid] {
		if len(s.uidToSIDs[fuid]) > 0 {
			peerStats[fuid] |= FriendBitHFM
		}
	}
	for ruid := range s.userRejects[uid] {
		if len(s.uidToSIDs[ruid]) > 0 {
			if s.userFriends[uid][ruid] {
				peerStats[ruid] |= FriendBitIFH | FriendBitISH
			} else {
				peerStats[ruid] |= FriendBitIRH
			}
		}
	}
	for ruid := range s.revRejects[uid] {
		if len(s.uidToSIDs[ruid]) > 0 {
			if s.revFriends[uid][ruid] {
				peerStats[ruid] |= FriendBitHFM | FriendBitHSM
			} else {
				peerStats[ruid] |= FriendBitHRM
			}
		}
	}
	if len(peerStats) == 0 {
		return nil
	}

	type sidEntry struct {
		sid    int
		stat   int
		packed uint32
	}
	var list []sidEntry
	for peerUID, stat := range peerStats {
		if stat == 0 {
			continue
		}
		tagUID := 0
		if s.enableUIDTag {
			tagUID = peerUID
		}
		for peerSID := range s.uidToSIDs[peerUID] {
			list = append(list, sidEntry{
				sid:    peerSID,
				stat:   stat,
				packed: PackFriendOnline(stat, tagUID, peerSID),
			})
		}
	}
	// Before the cutoff, sessions may belong to legacy binaries whose SHM
	// layout only has MAX_FRIEND friend_online entries (C side caps too).
	limit := MaxFriendOnline
	if IsLegacyCompatActive(time.Now()) {
		limit = MaxFriend
	}
	if len(list) > limit {
		sort.Slice(list, func(i, j int) bool {
			pi := friendStatPriority(list[i].stat)
			pj := friendStatPriority(list[j].stat)
			if pi != pj {
				return pi > pj
			}
			return list[i].sid < list[j].sid
		})
		list = list[:limit]
	}
	sort.Slice(list, func(i, j int) bool {
		return list[i].sid < list[j].sid
	})
	entries := make([]uint32, len(list))
	for i, item := range list {
		entries[i] = item.packed
	}
	return entries
}

func (s *Service) syncUIDFriendsToSHMLocked(uid int) {
	if uid <= 0 {
		return
	}
	sids, ok := s.uidToSIDs[uid]
	if !ok || len(sids) == 0 {
		return
	}
	entries := s.computeFriendOnlineForUIDLocked(uid)
	for sid, pid := range sids {
		s.sessionFriendsCache[sid] = entries
		if s.shmClient != nil {
			s.shmClient.SetSessionFriends(sid, pid, uid, entries)
		}
	}
}

func (s *Service) ScanOnlineSessions() int {
	if s.shmClient == nil {
		return 0
	}
	sessions := s.shmClient.GetOnlineSessions()
	count := 0
	for _, sess := range sessions {
		if sess.UserID != "" && sess.PID > 0 {
			s.registerSubscriberFull(sess.UserID, sess.UID, sess.PID, sess.SID, false)
			count++
		}
	}
	s.mu.Lock()
	for uid := range s.uidToSIDs {
		s.syncUIDFriendsToSHMLocked(uid)
	}
	s.mu.Unlock()

	log.Printf("[friend.svc] Scanned SHM: loaded %d online user sessions into memory", count)
	return count
}

func (s *Service) HandleUserLogout(userID string, pid int) Response {
	s.mu.Lock()
	defer s.mu.Unlock()

	session, exists := s.onlineSessions[pid]
	// The pid may have been reused by another live session (e.g. a stale
	// utmp entry purged later); never tear down a different user's session.
	if !exists || (userID != "" && !strings.EqualFold(session.UserID, userID)) {
		return Response{Success: true, Message: fmt.Sprintf("User %s (PID %d) logged out, cleaned 0 subscriptions", userID, pid)}
	}

	cleaned, affectedUIDs := s.cleanupSessionLocked(session)
	for auid := range affectedUIDs {
		s.syncUIDFriendsToSHMLocked(auid)
	}

	if s.verbose > 0 {
		log.Printf("[friend.svc] LOGOUT: user=%s, pid=%d -> cleaned %d subscriptions", userID, pid, cleaned)
	}

	return Response{
		Success: true,
		Message: fmt.Sprintf("User %s (PID %d) logged out, cleaned %d subscriptions", userID, pid, cleaned),
	}
}

func (s *Service) cleanupSessionLocked(session SubscriberSession) (int, map[int]bool) {
	pid := session.PID
	userLower := strings.ToLower(session.UserID)
	affectedUIDs := make(map[int]bool)

	delete(s.onlineSessions, pid)
	if session.SID >= 0 {
		if curPID, ok := s.sidToPID[session.SID]; ok && curPID == pid {
			delete(s.sidToPID, session.SID)
		}
		delete(s.sessionFriendsCache, session.SID)
	}

	if pids, ok := s.userPIDs[userLower]; ok {
		delete(pids, pid)
		if len(pids) == 0 {
			delete(s.userPIDs, userLower)
		}
	}

	cleaned := 0
	for _, targetLower := range session.Targets {
		if subMap, ok := s.onlineSubscribers[targetLower]; ok {
			if _, present := subMap[pid]; present {
				delete(subMap, pid)
				cleaned++
				if len(subMap) == 0 {
					delete(s.onlineSubscribers, targetLower)
				}
			}
		}
	}

	if session.UID > 0 {
		uid := session.UID
		affectedUIDs[uid] = true
		for fuid := range s.userFriends[uid] {
			affectedUIDs[fuid] = true
		}
		for ruid := range s.userRejects[uid] {
			affectedUIDs[ruid] = true
		}
		for puid := range s.revFriends[uid] {
			affectedUIDs[puid] = true
		}
		for puid := range s.revRejects[uid] {
			affectedUIDs[puid] = true
		}

		if sids, ok := s.uidToSIDs[uid]; ok {
			delete(sids, session.SID)
			if len(sids) == 0 {
				delete(s.uidToSIDs, uid)
				for fuid := range s.userFriends[uid] {
					if rev, ok := s.revFriends[fuid]; ok {
						delete(rev, uid)
						if len(rev) == 0 {
							delete(s.revFriends, fuid)
						}
					}
				}
				delete(s.userFriends, uid)

				for ruid := range s.userRejects[uid] {
					if rev, ok := s.revRejects[ruid]; ok {
						delete(rev, uid)
						if len(rev) == 0 {
							delete(s.revRejects, ruid)
						}
					}
				}
				delete(s.userRejects, uid)
			}
		}
	}

	return cleaned, affectedUIDs
}

func (s *Service) HandleReloadAloha(subscriberID string) Response {
	if subscriberID == "" {
		return Response{Success: false, Message: "missing userid for reload"}
	}

	targetsLower := s.getOrLoadAlohaTargets(subscriberID, true)
	subscriberLower := strings.ToLower(subscriberID)

	s.mu.Lock()
	var sessionsToRefresh []SubscriberSession
	pids, exists := s.userPIDs[subscriberLower]
	updatedSessions := 0
	if exists {
		for pid := range pids {
			session, ok := s.onlineSessions[pid]
			if !ok {
				continue
			}

			for _, oldTarget := range session.Targets {
				if subMap, ok := s.onlineSubscribers[oldTarget]; ok {
					delete(subMap, pid)
					if len(subMap) == 0 {
						delete(s.onlineSubscribers, oldTarget)
					}
				}
			}

			session.Targets = targetsLower
			s.onlineSessions[pid] = session

			for _, targetLower := range targetsLower {
				if _, ok := s.onlineSubscribers[targetLower]; !ok {
					s.onlineSubscribers[targetLower] = make(map[int]SubscriberSession)
				}
				s.onlineSubscribers[targetLower][pid] = session
			}
			sessionsToRefresh = append(sessionsToRefresh, session)
			updatedSessions++
		}
	}
	s.mu.Unlock()

	if len(sessionsToRefresh) > 0 {
		sess := sessionsToRefresh[0]
		s.registerSubscriberFull(sess.UserID, sess.UID, sess.PID, sess.SID, true)
	}

	log.Printf("[friend.svc] RELOAD: user=%s -> %d targets (updated %d sessions)", subscriberID, len(targetsLower), updatedSessions)
	return Response{
		Success: true,
		Message: fmt.Sprintf("Reloaded aloha targets for %s (%d targets, %d sessions updated)", subscriberID, len(targetsLower), updatedSessions),
	}
}

func (s *Service) HandleStatus() Response {
	s.mu.RLock()
	defer s.mu.RUnlock()

	onlineCount := len(s.onlineSessions)

	stats := map[string]any{
		"online_sessions_count": onlineCount,
		"targets_watched_count": len(s.onlineSubscribers),
		"online_uids_count":     len(s.uidToSIDs),
		"hbfl_boards_count":     len(s.hbflBoards),
		"hbfl_generation":       s.hbflGen,
		"reconcile_interval":    s.reconcileInterval.String(),
		"enable_uid_tag":        s.enableUIDTag,
		"legacy_compat_mode":    s.autoLegacyCompat && IsLegacyCompatActive(time.Now()),
		"legacy_compat_cutoff":  LegacyCompatCutoff.Format(time.RFC3339),
	}

	log.Printf("[friend.svc] STATUS requested: online_sessions=%d, targets_watched=%d, online_uids=%d, hbfl_boards=%d, hbfl_gen=%d, uid_tag=%v, reconcile=%v",
		onlineCount, len(s.onlineSubscribers), len(s.uidToSIDs), len(s.hbflBoards), s.hbflGen, s.enableUIDTag, s.reconcileInterval)

	return Response{
		Success: true,
		Gen:     s.hbflGen,
		Message: "Status OK",
		Data:    stats,
	}
}

func startMemoryReporter(interval time.Duration) {
	ticker := time.NewTicker(interval)
	go func() {
		for range ticker.C {
			logMemStats()
		}
	}()
}

func logMemStats() {
	var m runtime.MemStats
	runtime.ReadMemStats(&m)
	log.Printf("[friend.svc] [MemStats] HeapAlloc: %.2f MB, HeapSys: %.2f MB, Sys: %.2f MB, NumGC: %d",
		float64(m.HeapAlloc)/(1024*1024),
		float64(m.HeapSys)/(1024*1024),
		float64(m.Sys)/(1024*1024),
		m.NumGC)
}

func (s *Service) SetReconcileInterval(interval time.Duration) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.reconcileInterval = interval
	s.autoLegacyCompat = false
}

func (s *Service) SetEnableUIDTag(enable bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.enableUIDTag = enable
	s.autoUIDTag = false
}

func (s *Service) checkLegacyCompatTransition() (transitioned bool, nextInterval time.Duration) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if (!s.autoLegacyCompat && !s.autoUIDTag) || IsLegacyCompatActive(time.Now()) {
		return false, s.reconcileInterval
	}
	if s.autoLegacyCompat {
		s.autoLegacyCompat = false
		s.reconcileInterval = 1 * time.Hour
		transitioned = true
	}
	if s.autoUIDTag {
		s.autoUIDTag = false
		if !s.enableUIDTag {
			s.enableUIDTag = true
			for uid := range s.uidToSIDs {
				s.syncUIDFriendsToSHMLocked(uid)
			}
		}
	}
	log.Printf("[friend.svc] Legacy compatibility cutoff (%s) reached: switched reconcileInterval to %v and enableUIDTag to %v",
		LegacyCompatCutoff.Format(time.RFC3339), s.reconcileInterval, s.enableUIDTag)
	return transitioned, s.reconcileInterval
}

func (s *Service) StartReconciler(interval time.Duration) {
	if interval <= 0 {
		return
	}
	ticker := time.NewTicker(interval)
	go func() {
		log.Printf("[friend.svc] Started background reconciler with interval: %v (autoLegacyCompat=%v, cutoff=%s)",
			interval, s.autoLegacyCompat, LegacyCompatCutoff.Format(time.RFC3339))
		for range ticker.C {
			s.ScanHiddenBoards()
			s.ReconcileOnlineSessions()
			if transitioned, nextInterval := s.checkLegacyCompatTransition(); transitioned && nextInterval > 0 {
				ticker.Reset(nextInterval)
			}
		}
	}()
}

func (s *Service) ReconcileOnlineSessions() (added int, removed int) {
	if s.shmClient == nil {
		return 0, 0
	}
	start := time.Now()

	sessions := s.shmClient.GetOnlineSessions()

	type shmSessInfo struct {
		userID string
		uid    int
		sid    int
	}
	activeSHM := make(map[int]shmSessInfo, len(sessions))
	for _, sess := range sessions {
		if sess.UserID != "" && sess.PID > 0 {
			activeSHM[sess.PID] = shmSessInfo{
				userID: sess.UserID,
				uid:    sess.UID,
				sid:    sess.SID,
			}
		}
	}

	s.mu.Lock()
	var toClean []SubscriberSession
	for pid, session := range s.onlineSessions {
		shmInfo, exists := activeSHM[pid]
		if exists && strings.EqualFold(shmInfo.userID, session.UserID) {
			continue
		}
		// The snapshot was taken without s.mu; the session may have registered
		// after its slot was scanned. Re-validate against live SHM.
		if session.SID >= 0 {
			if d, ok := s.shmClient.GetSessionDetail(session.SID); ok &&
				d.PID == pid && strings.EqualFold(d.UserID, session.UserID) {
				continue
			}
		}
		toClean = append(toClean, session)
	}

	cleanedCount := 0
	affectedUIDs := make(map[int]bool)
	for _, sess := range toClean {
		c, affected := s.cleanupSessionLocked(sess)
		cleanedCount += c
		for auid := range affected {
			affectedUIDs[auid] = true
		}
		removed++
	}
	for auid := range affectedUIDs {
		s.syncUIDFriendsToSHMLocked(auid)
	}
	s.mu.Unlock()

	for pid, shmInfo := range activeSHM {
		s.mu.RLock()
		_, exists := s.onlineSessions[pid]
		s.mu.RUnlock()

		if !exists {
			s.registerSubscriberFull(shmInfo.userID, shmInfo.uid, pid, shmInfo.sid, true)
			added++
		}
	}

	elapsed := time.Since(start)
	if len(toClean) > 0 || added > 0 || s.verbose > 0 {
		log.Printf("[friend.svc] RECONCILE: cleaned %d stale sessions (removed %d subscriptions), added %d new sessions from SHM in %v",
			len(toClean), cleanedCount, added, elapsed)
	}
	return added, removed
}

// RegisterBoard registers or updates a board (1-indexed bid and brdName) and loads its visable list
func (s *Service) RegisterBoard(bid int, brdName string, brdAttr uint32) int {
	if bid <= 0 || brdName == "" {
		return 0
	}
	_, count := s.reloadBoardVisable(bid, brdName, brdAttr, true)
	return count
}

func (s *Service) bumpHBFLGenLocked() int {
	s.hbflGen++
	if s.shmClient != nil {
		if shmGen := s.shmClient.BumpHBFLGeneration(); shmGen > s.hbflGen {
			s.hbflGen = shmGen
		} else {
			s.shmClient.SetHBFLGeneration(s.hbflGen)
		}
	}
	return s.hbflGen
}

func (s *Service) resolveBoardIdentity(bid int, brdName string) (int, string, uint32) {
	brdName = strings.TrimSpace(brdName)
	s.mu.RLock()
	if bid > 0 {
		if b, ok := s.hbflBoards[bid]; ok {
			if brdName == "" {
				brdName = b.BrdName
			}
			attr := b.BrdAttr
			s.mu.RUnlock()
			return bid, brdName, attr
		}
	}
	if brdName != "" {
		if foundBID, ok := s.hbflBoardNameToBID[strings.ToLower(brdName)]; ok {
			if b, ok2 := s.hbflBoards[foundBID]; ok2 {
				attr := b.BrdAttr
				name := b.BrdName
				s.mu.RUnlock()
				return foundBID, name, attr
			}
		}
	}
	s.mu.RUnlock()

	if s.shmClient != nil {
		if bid > 0 {
			if info, ok := s.shmClient.GetBoardByBID(bid); ok {
				return info.BID, info.BrdName, info.BrdAttr
			}
		}
		if brdName != "" {
			if info, ok := s.shmClient.GetBoardByName(brdName); ok {
				return info.BID, info.BrdName, info.BrdAttr
			}
		}
	}
	return bid, brdName, 0
}

func (s *Service) reloadBoardVisable(bid int, brdName string, brdAttr uint32, force bool) (changed bool, count int) {
	if bid <= 0 || brdName == "" {
		return false, 0
	}

	var filePath string
	if s.bbsHome != "" {
		filePath, _ = storage.GetBoardFile(s.bbsHome, brdName, "visable")
	}

	var statInfo os.FileInfo
	var statErr error
	if filePath != "" {
		statInfo, statErr = os.Stat(filePath)
	} else {
		statErr = os.ErrNotExist
	}
	fileExists := statErr == nil && statInfo != nil

	s.mu.RLock()
	existing, hasExisting := s.hbflBoards[bid]
	if hasExisting && !force && existing.BrdName == brdName {
		if !fileExists && !existing.Exists {
			cnt := len(existing.UIDs)
			s.mu.RUnlock()
			return false, cnt
		}
		if fileExists && existing.Exists &&
			existing.ModTime.Equal(statInfo.ModTime()) &&
			existing.Size == statInfo.Size() {
			cnt := len(existing.UIDs)
			s.mu.RUnlock()
			return false, cnt
		}
	}
	s.mu.RUnlock()

	var userIDs []string
	var modTime time.Time
	var size int64
	if fileExists && s.bbsHome != "" {
		var err error
		userIDs, modTime, size, fileExists, err = storage.LoadBoardVisableFile(s.bbsHome, brdName)
		if err != nil {
			log.Printf("[friend.svc] Warning: failed to read visable for board %s (bid=%d): %v", brdName, bid, err)
		}
	}

	s.mu.Lock()
	defer s.mu.Unlock()

	entry, ok := s.hbflBoards[bid]
	if !ok {
		entry = &BoardVisableCache{
			BID:     bid,
			BrdName: brdName,
			BrdAttr: brdAttr,
			UIDs:    make(map[int]struct{}),
		}
		s.hbflBoards[bid] = entry
	} else {
		if entry.BrdName != "" && !strings.EqualFold(entry.BrdName, brdName) {
			delete(s.hbflBoardNameToBID, strings.ToLower(entry.BrdName))
		}
		entry.BrdName = brdName
		if brdAttr != 0 {
			entry.BrdAttr = brdAttr
		}
	}
	s.hbflBoardNameToBID[strings.ToLower(brdName)] = bid

	newUIDs := make(map[int]struct{}, len(userIDs))
	newNames := make(map[int]string, len(userIDs))
	for _, uidStr := range userIDs {
		if u := s.resolveUIDLocked(uidStr, 0); u > 0 {
			newUIDs[u] = struct{}{}
			newNames[u] = strings.ToLower(uidStr)
		}
	}

	membershipChanged := false
	if len(newUIDs) != len(entry.UIDs) {
		membershipChanged = true
	} else {
		for u := range newUIDs {
			if _, present := entry.UIDs[u]; !present {
				membershipChanged = true
				break
			}
		}
	}

	if membershipChanged {
		for oldUID := range entry.UIDs {
			if _, keep := newUIDs[oldUID]; !keep {
				if umap, ok2 := s.hbflUserBoards[oldUID]; ok2 {
					delete(umap, bid)
					if len(umap) == 0 {
						delete(s.hbflUserBoards, oldUID)
					}
				}
			}
		}
		for newUID := range newUIDs {
			if _, had := entry.UIDs[newUID]; !had {
				if _, ok2 := s.hbflUserBoards[newUID]; !ok2 {
					s.hbflUserBoards[newUID] = make(map[int]struct{})
				}
				s.hbflUserBoards[newUID][bid] = struct{}{}
			}
		}
		entry.UIDs = newUIDs
	}

	entry.Names = newNames
	entry.Exists = fileExists
	entry.ModTime = modTime
	entry.Size = size

	if membershipChanged || force {
		s.bumpHBFLGenLocked()
	}

	return membershipChanged, len(entry.UIDs)
}

func (s *Service) ScanHiddenBoards() int {
	loaded := 0
	if s.shmClient != nil {
		boards := s.shmClient.GetBoards()
		for _, b := range boards {
			if b.BID <= 0 || b.BrdName == "" {
				continue
			}
			// Track board if it has a visable file on disk or has BRD_HIDE (0x10) / BRD_RESTRICTEDPOST (0x40000)
			const brdHBFLMask uint32 = 0x10 | 0x40000
			hasVisable := false
			if s.bbsHome != "" {
				if fp, err := storage.GetBoardFile(s.bbsHome, b.BrdName, "visable"); err == nil {
					if _, stErr := os.Stat(fp); stErr == nil {
						hasVisable = true
					}
				}
			}
			if hasVisable || (b.BrdAttr&brdHBFLMask) != 0 {
				_, cnt := s.reloadBoardVisable(b.BID, b.BrdName, b.BrdAttr, false)
				if cnt > 0 || hasVisable {
					loaded++
				}
			}
		}
	} else if s.bbsHome != "" {
		matches, _ := filepath.Glob(filepath.Join(s.bbsHome, "boards", "*", "*", "visable"))
		for _, m := range matches {
			brdName := filepath.Base(filepath.Dir(m))
			if brdName == "" {
				continue
			}
			s.mu.Lock()
			bid, exists := s.hbflBoardNameToBID[strings.ToLower(brdName)]
			if !exists {
				bid = len(s.hbflBoards) + 1
				for s.hbflBoards[bid] != nil {
					bid++
				}
			}
			s.mu.Unlock()
			_, cnt := s.reloadBoardVisable(bid, brdName, 0, false)
			if cnt > 0 {
				loaded++
			}
		}
	}

	s.mu.Lock()
	s.lastHBFLStatCheck = time.Now()
	s.mu.Unlock()
	return loaded
}

func (s *Service) maybeRefreshHBFLStat() {
	s.mu.RLock()
	numBoards := len(s.hbflBoards)
	// Legacy mtime polling is only needed before the cutoff; afterwards rely
	// on hbfl_reload events (standalone/no-SHM mode keeps polling).
	if numBoards > 0 && s.shmClient != nil && !IsLegacyCompatActive(time.Now()) {
		s.mu.RUnlock()
		return
	}
	lastCheck := s.lastHBFLStatCheck
	type boardTarget struct {
		bid     int
		brdName string
		brdAttr uint32
	}
	var targets []boardTarget
	if numBoards <= 8 || time.Since(lastCheck) >= 2*time.Second {
		for _, b := range s.hbflBoards {
			targets = append(targets, boardTarget{
				bid:     b.BID,
				brdName: b.BrdName,
				brdAttr: b.BrdAttr,
			})
		}
	}
	s.mu.RUnlock()

	if numBoards == 0 {
		s.ScanHiddenBoards()
		return
	}
	if len(targets) == 0 {
		return
	}

	s.mu.Lock()
	s.lastHBFLStatCheck = time.Now()
	s.mu.Unlock()

	for _, t := range targets {
		s.reloadBoardVisable(t.bid, t.brdName, t.brdAttr, false)
	}
}

// hbflMemberLocked reports whether uid is listed in b and still belongs to the
// same account it was resolved from when the visable file was loaded.
func (s *Service) hbflMemberLocked(b *BoardVisableCache, uid int) bool {
	if _, ok := b.UIDs[uid]; !ok {
		return false
	}
	name, ok := b.Names[uid]
	if s.shmClient == nil || !ok {
		return true
	}
	cur, err := s.shmClient.GetUserID(uid)
	return err == nil && strings.EqualFold(cur, name)
}

func (s *Service) HandleHBFLUser(uid int, userID string) Response {
	s.maybeRefreshHBFLStat()

	s.mu.Lock()
	if uid <= 0 && userID != "" {
		uid = s.resolveUIDLocked(userID, 0)
	}
	if uid <= 0 {
		gen := s.hbflGen
		s.mu.Unlock()
		return Response{
			Success: false,
			Gen:     gen,
			Message: "invalid uid or userid",
		}
	}

	var bids []int
	for bid := range s.hbflUserBoards[uid] {
		if b, ok := s.hbflBoards[bid]; ok && s.hbflMemberLocked(b, uid) {
			bids = append(bids, bid)
		}
	}
	gen := s.hbflGen
	s.mu.Unlock()

	sort.Ints(bids)
	if bids == nil {
		bids = []int{}
	}

	return Response{
		Success: true,
		Gen:     gen,
		BIDs:    bids,
		Message: fmt.Sprintf("uid %d has access to %d hidden boards (gen=%d)", uid, len(bids), gen),
		Data: map[string]any{
			"uid": uid,
			"gen": gen,
		},
	}
}

func (s *Service) HandleHBFLCheck(bid int, brdName string, uid int, userID string) Response {
	resolvedBID, resolvedName, resolvedAttr := s.resolveBoardIdentity(bid, brdName)
	if resolvedBID <= 0 || resolvedName == "" {
		return Response{Success: false, Message: "invalid bid or brdname"}
	}

	// Stat and reload if mtime changed on disk
	s.reloadBoardVisable(resolvedBID, resolvedName, resolvedAttr, false)

	s.mu.Lock()
	if uid <= 0 && userID != "" {
		uid = s.resolveUIDLocked(userID, 0)
	}
	allowed := false
	if b, ok := s.hbflBoards[resolvedBID]; ok && uid > 0 {
		allowed = s.hbflMemberLocked(b, uid)
	}
	gen := s.hbflGen
	s.mu.Unlock()

	return Response{
		Success: true,
		Gen:     gen,
		Allowed: &allowed,
		Message: fmt.Sprintf("hbfl_check bid=%d (%s) uid=%d -> %v", resolvedBID, resolvedName, uid, allowed),
		Data: map[string]any{
			"bid":     resolvedBID,
			"brdname": resolvedName,
			"uid":     uid,
			"allowed": allowed,
			"gen":     gen,
		},
	}
}

func (s *Service) HandleHBFLReload(bid int, brdName string) Response {
	if bid <= 0 && strings.TrimSpace(brdName) == "" {
		loaded := s.ScanHiddenBoards()
		s.mu.Lock()
		type boardPair struct {
			bid  int
			name string
			attr uint32
		}
		var all []boardPair
		for _, b := range s.hbflBoards {
			all = append(all, boardPair{b.BID, b.BrdName, b.BrdAttr})
		}
		s.mu.Unlock()
		for _, bp := range all {
			s.reloadBoardVisable(bp.bid, bp.name, bp.attr, true)
		}
		s.mu.Lock()
		gen := s.bumpHBFLGenLocked()
		s.mu.Unlock()
		return Response{
			Success: true,
			Gen:     gen,
			Message: fmt.Sprintf("Reloaded all hidden boards (count=%d, gen=%d)", loaded, gen),
		}
	}

	resolvedBID, resolvedName, resolvedAttr := s.resolveBoardIdentity(bid, brdName)
	if resolvedBID <= 0 || resolvedName == "" {
		s.mu.Lock()
		gen := s.bumpHBFLGenLocked()
		s.mu.Unlock()
		return Response{Success: false, Gen: gen, Message: "board not found"}
	}

	_, count := s.reloadBoardVisable(resolvedBID, resolvedName, resolvedAttr, true)
	s.mu.Lock()
	gen := s.hbflGen
	s.mu.Unlock()

	return Response{
		Success: true,
		Gen:     gen,
		Message: fmt.Sprintf("Reloaded hidden board %s (bid=%d): %d friends (gen=%d)", resolvedName, resolvedBID, count, gen),
		Data: map[string]any{
			"bid":           resolvedBID,
			"brdname":       resolvedName,
			"visable_count": count,
			"gen":           gen,
		},
	}
}

func (s *Service) HandleHBFLBoard(bid int, brdName string) Response {
	resolvedBID, resolvedName, resolvedAttr := s.resolveBoardIdentity(bid, brdName)
	if resolvedBID <= 0 || resolvedName == "" {
		return Response{Success: false, Message: "board not found"}
	}

	s.reloadBoardVisable(resolvedBID, resolvedName, resolvedAttr, false)

	s.mu.RLock()
	defer s.mu.RUnlock()

	b, ok := s.hbflBoards[resolvedBID]
	if !ok {
		return Response{Success: false, Message: "board not loaded"}
	}

	var friends []FriendPeerInfo
	for uid := range b.UIDs {
		friends = append(friends, FriendPeerInfo{
			UID:    uid,
			UserID: s.lookupUserIDByUIDLocked(uid),
			Online: len(s.uidToSIDs[uid]) > 0,
		})
	}
	sort.Slice(friends, func(i, j int) bool {
		return friends[i].UID < friends[j].UID
	})

	mtimeStr := ""
	if !b.ModTime.IsZero() {
		mtimeStr = b.ModTime.Format(time.RFC3339)
	}

	info := HBFLBoardInfo{
		BID:          b.BID,
		BrdName:      b.BrdName,
		VisableCount: len(b.UIDs),
		ModTime:      mtimeStr,
		Friends:      friends,
	}

	return Response{
		Success: true,
		Gen:     s.hbflGen,
		Message: fmt.Sprintf("Board %s (bid=%d): %d hidden board friends (gen=%d)", b.BrdName, b.BID, len(b.UIDs), s.hbflGen),
		Data:    info,
	}
}

