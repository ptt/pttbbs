package daemon

import (
	"encoding/json"
	"fmt"
	"log"
	"net"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"time"

	"pttbbs/aloha/storage"
	"pttbbs/bbs"
)

type SubscriberSession struct {
	PID     int
	SID     int
	UserID  string
	Targets []string // list of target UserIDs (lowercased) watched by this session
}

type Service struct {
	bbsHome    string
	shmClient  *bbs.SHMClient
	socketPath string
	verbose    int
	mu         sync.RWMutex

	// onlineSessions maps pid -> SubscriberSession
	onlineSessions map[int]SubscriberSession

	// onlineSubscribers maps targetUserID (lowercase) -> pid -> SubscriberSession
	onlineSubscribers map[string]map[int]SubscriberSession

	// userPIDs maps userID (lowercase) -> pid -> true
	userPIDs map[string]map[int]bool

	// sem limits concurrent active IPC connections to prevent goroutine explosion
	sem chan struct{}

	reconcileInterval time.Duration
}

func NewService(bbsHome string, optSocketPath ...string) (*Service, error) {
	socketPath := ""
	if len(optSocketPath) > 0 && optSocketPath[0] != "" {
		socketPath = optSocketPath[0]
	} else {
		socketPath = filepath.Join(bbsHome, "run", "aloha.svc.sock")
	}

	var shmClient *bbs.SHMClient
	if bbsHome != "" {
		var err error
		shmClient, err = bbs.AttachSHM()
		if err != nil {
			log.Printf("[aloha.svc] Warning: failed to attach SHM: %v", err)
		}
	}

	return &Service{
		bbsHome:           bbsHome,
		shmClient:         shmClient,
		socketPath:        socketPath,
		reconcileInterval: 1 * time.Hour,
		onlineSessions:    make(map[int]SubscriberSession),
		onlineSubscribers: make(map[string]map[int]SubscriberSession),
		userPIDs:          make(map[string]map[int]bool),
		sem:               make(chan struct{}, 5000),
	}, nil
}

func (s *Service) SetVerbose(v int) {
	s.verbose = v
}

// Request and Response formats for IPC via UNIX domain socket
type Request struct {
	Action   string `json:"action"` // "login", "logout", "reload", "status"
	UserID   string `json:"userid,omitempty"`
	TargetID string `json:"target_id,omitempty"`
	SubID    string `json:"sub_id,omitempty"`
	PID      int    `json:"pid,omitempty"`
	SID      int    `json:"sid,omitempty"`
}

type Response struct {
	Success bool   `json:"success"`
	Message string `json:"message,omitempty"`
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
		return fmt.Errorf("another instance of aloha.svc is already running and listening on socket %s", s.socketPath)
	}

	_ = os.Remove(s.socketPath)

	listener, err := net.Listen("unix", s.socketPath)
	if err != nil {
		return fmt.Errorf("failed to listen on socket %s: %w", s.socketPath, err)
	}
	defer listener.Close()

	log.Printf("[aloha.svc] Aloha Service started listening on UNIX socket: %s", s.socketPath)

	logMemStats()
	startMemoryReporter(1 * time.Hour)

	s.ScanOnlineSessions()
	if s.reconcileInterval > 0 {
		s.StartReconciler(s.reconcileInterval)
	}

	for {
		conn, err := listener.Accept()
		if err != nil {
			log.Printf("[aloha.svc] Accept error: %v", err)
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

func (s *Service) ProcessRequest(req Request) Response {
	switch strings.ToLower(req.Action) {
	case "login":
		return s.HandleUserLogin(req.UserID, req.PID, req.SID)
	case "logout":
		return s.HandleUserLogout(req.UserID, req.PID)
	case "reload":
		return s.HandleReloadAloha(req.UserID)
	case "status":
		return s.HandleStatus()
	default:
		log.Printf("[aloha.svc] Received unknown action: %s", req.Action)
		return Response{Success: false, Message: fmt.Sprintf("unknown action: %s", req.Action)}
	}
}

func (s *Service) HandleUserLogin(userID string, pid int, sid int) Response {
	if userID == "" || pid <= 0 {
		return Response{Success: false, Message: "invalid userid or pid"}
	}

	userIDLower := strings.ToLower(userID)

	// 1. Notify all active online subscribers that target userID has logged in
	s.mu.RLock()
	subMap, exists := s.onlineSubscribers[userIDLower]
	var subscribersToNotify []SubscriberSession
	if exists {
		for _, sub := range subMap {
			subscribersToNotify = append(subscribersToNotify, sub)
		}
	}
	s.mu.RUnlock()

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
					log.Printf("[aloha.svc] SendAlohaMessage FAIL: sub=%s sid=%d pid=%d: %v", sub.UserID, sub.SID, sub.PID, err)
				}
			}
		} else {
			// Standalone test mode without SHM
			notifiedCount = len(subscribersToNotify)
		}
		s.RegisterSubscriber(userID, pid, sid)
		if s.verbose > 0 || notifiedCount > 0 {
			log.Printf("[aloha.svc] LOGIN: user=%s, pid=%d, sid=%d -> notified %d subscribers", userID, pid, sid, notifiedCount)
		}
		return Response{
			Success: true,
			Message: fmt.Sprintf("User %s logged in, notified %d subscribers", userID, notifiedCount),
		}
	} else {
		foundCount := len(subscribersToNotify)
		s.RegisterSubscriber(userID, pid, sid)
		if s.verbose > 0 || foundCount > 0 {
			log.Printf("[aloha.svc] LOGIN: user=%s, pid=%d, sid=%d -> found %d subscribers", userID, pid, sid, foundCount)
		}
		return Response{
			Success: true,
			Message: fmt.Sprintf("User %s logged in, found %d subscribers", userID, foundCount),
		}
	}
}

func (s *Service) RegisterSubscriber(subscriberID string, pid int, sid int) {
	// Perform Disk I/O OUTSIDE global mutex lock
	alohaList, _ := storage.LoadAlohaTargets(s.bbsHome, subscriberID)

	subscriberLower := strings.ToLower(subscriberID)

	var targetsLower []string
	if len(alohaList) > 0 {
		for _, t := range alohaList {
			targetsLower = append(targetsLower, strings.ToLower(t))
		}
	}

	s.mu.Lock()
	defer s.mu.Unlock()

	// Cleanup old session for this PID if present
	if oldSess, ok := s.onlineSessions[pid]; ok {
		s.cleanupSessionLocked(oldSess)
	}

	session := SubscriberSession{
		PID:     pid,
		SID:     sid,
		UserID:  subscriberID,
		Targets: targetsLower,
	}

	s.onlineSessions[pid] = session

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
}

func (s *Service) ScanOnlineSessions() int {
	if s.shmClient == nil {
		return 0
	}
	sessions := s.shmClient.GetOnlineSessions()
	count := 0
	for _, sess := range sessions {
		if sess.UserID != "" && sess.PID > 0 {
			s.RegisterSubscriber(sess.UserID, sess.PID, sess.SID)
			count++
		}
	}
	log.Printf("[aloha.svc] Scanned SHM: loaded %d online user sessions into memory", count)
	return count
}

func (s *Service) HandleUserLogout(userID string, pid int) Response {
	s.mu.Lock()
	defer s.mu.Unlock()

	session, exists := s.onlineSessions[pid]
	if !exists {
		return Response{Success: true, Message: fmt.Sprintf("User %s (PID %d) logged out, cleaned 0 subscriptions", userID, pid)}
	}

	cleaned := s.cleanupSessionLocked(session)

	log.Printf("[aloha.svc] LOGOUT: user=%s, pid=%d -> cleaned %d subscriptions", userID, pid, cleaned)

	return Response{
		Success: true,
		Message: fmt.Sprintf("User %s (PID %d) logged out, cleaned %d subscriptions", userID, pid, cleaned),
	}
}

func (s *Service) cleanupSessionLocked(session SubscriberSession) int {
	pid := session.PID
	userLower := strings.ToLower(session.UserID)

	delete(s.onlineSessions, pid)

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
	return cleaned
}

func (s *Service) HandleReloadAloha(subscriberID string) Response {
	if subscriberID == "" {
		return Response{Success: false, Message: "missing userid for reload"}
	}

	alohaList, _ := storage.LoadAlohaTargets(s.bbsHome, subscriberID)

	subscriberLower := strings.ToLower(subscriberID)

	var targetsLower []string
	if len(alohaList) > 0 {
		for _, t := range alohaList {
			targetsLower = append(targetsLower, strings.ToLower(t))
		}
	}

	s.mu.Lock()
	defer s.mu.Unlock()

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
			updatedSessions++
		}
	}

	log.Printf("[aloha.svc] RELOAD: user=%s -> %d targets (updated %d sessions)", subscriberID, len(targetsLower), updatedSessions)
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
	}

	log.Printf("[aloha.svc] STATUS requested: online_sessions=%d, targets_watched=%d", onlineCount, len(s.onlineSubscribers))

	return Response{
		Success: true,
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
	log.Printf("[aloha.svc] [MemStats] HeapAlloc: %.2f MB, HeapSys: %.2f MB, Sys: %.2f MB, NumGC: %d",
		float64(m.HeapAlloc)/(1024*1024),
		float64(m.HeapSys)/(1024*1024),
		float64(m.Sys)/(1024*1024),
		m.NumGC)
}

func (s *Service) SetReconcileInterval(interval time.Duration) {
	s.reconcileInterval = interval
}

func (s *Service) StartReconciler(interval time.Duration) {
	if interval <= 0 {
		return
	}
	ticker := time.NewTicker(interval)
	go func() {
		log.Printf("[aloha.svc] Started background reconciler with interval: %v", interval)
		for range ticker.C {
			s.ReconcileOnlineSessions()
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
		sid    int
	}
	activeSHM := make(map[int]shmSessInfo, len(sessions))
	for _, sess := range sessions {
		if sess.UserID != "" && sess.PID > 0 {
			activeSHM[sess.PID] = shmSessInfo{
				userID: sess.UserID,
				sid:    sess.SID,
			}
		}
	}

	s.mu.Lock()
	var toClean []SubscriberSession
	for pid, session := range s.onlineSessions {
		shmInfo, exists := activeSHM[pid]
		if !exists || !strings.EqualFold(shmInfo.userID, session.UserID) {
			toClean = append(toClean, session)
		}
	}

	cleanedCount := 0
	for _, sess := range toClean {
		cleanedCount += s.cleanupSessionLocked(sess)
		removed++
	}
	s.mu.Unlock()

	for pid, shmInfo := range activeSHM {
		s.mu.RLock()
		_, exists := s.onlineSessions[pid]
		s.mu.RUnlock()

		if !exists {
			s.RegisterSubscriber(shmInfo.userID, pid, shmInfo.sid)
			added++
		}
	}

	elapsed := time.Since(start)
	log.Printf("[aloha.svc] RECONCILE: cleaned %d stale sessions (removed %d subscriptions), added %d new sessions from SHM in %v",
		len(toClean), cleanedCount, added, elapsed)
	return added, removed
}
