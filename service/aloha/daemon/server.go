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
	PID    int
	SID    int
	UserID string
}

type Service struct {
	bbsHome    string
	shmClient  *bbs.SHMClient
	socketPath string
	mu         sync.RWMutex

	// onlineSessions maps pid -> SubscriberSession
	onlineSessions map[int]SubscriberSession

	// onlineSubscribers maps targetUserID (lowercase) -> pid -> SubscriberSession
	onlineSubscribers map[string]map[int]SubscriberSession
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
		onlineSessions:    make(map[int]SubscriberSession),
		onlineSubscribers: make(map[string]map[int]SubscriberSession),
	}, nil
}

// Request and Response formats for IPC via UNIX domain socket
type Request struct {
	Action   string `json:"action"` // "login", "logout", "add", "remove", "status"
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

func (s *Service) Start() error {
	if err := os.MkdirAll(filepath.Dir(s.socketPath), 0755); err != nil {
		return err
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

	for {
		conn, err := listener.Accept()
		if err != nil {
			log.Printf("[aloha.svc] Accept error: %v", err)
			continue
		}
		go s.handleConnection(conn)
	}
}

func (s *Service) handleConnection(conn net.Conn) {
	defer conn.Close()
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
	case "add":
		return s.HandleAddAloha(req.SubID, req.TargetID)
	case "remove":
		return s.HandleRemoveAloha(req.SubID, req.TargetID)
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
		log.Printf("[aloha.svc] LOGIN: user=%s, pid=%d, sid=%d -> notified %d subscribers", userID, pid, sid, notifiedCount)
		return Response{
			Success: true,
			Message: fmt.Sprintf("User %s logged in, notified %d subscribers", userID, notifiedCount),
		}
	} else {
		foundCount := len(subscribersToNotify)
		s.RegisterSubscriber(userID, pid, sid)
		log.Printf("[aloha.svc] LOGIN: user=%s, pid=%d, sid=%d -> found %d subscribers", userID, pid, sid, foundCount)
		return Response{
			Success: true,
			Message: fmt.Sprintf("User %s logged in, found %d subscribers", userID, foundCount),
		}
	}
}

func (s *Service) RegisterSubscriber(subscriberID string, pid int, sid int) {
	s.mu.Lock()
	defer s.mu.Unlock()

	session := SubscriberSession{
		PID:    pid,
		SID:    sid,
		UserID: subscriberID,
	}

	s.onlineSessions[pid] = session

	alohaList, err := storage.LoadAlohaTargets(s.bbsHome, subscriberID)
	if err != nil || len(alohaList) == 0 {
		return
	}

	for _, target := range alohaList {
		targetLower := strings.ToLower(target)
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

	delete(s.onlineSessions, pid)

	cleaned := 0
	for target, subMap := range s.onlineSubscribers {
		if _, ok := subMap[pid]; ok {
			delete(subMap, pid)
			cleaned++
			if len(subMap) == 0 {
				delete(s.onlineSubscribers, target)
			}
		}
	}

	log.Printf("[aloha.svc] LOGOUT: user=%s, pid=%d -> cleaned %d subscriptions", userID, pid, cleaned)

	return Response{
		Success: true,
		Message: fmt.Sprintf("User %s (PID %d) logged out, cleaned %d subscriptions", userID, pid, cleaned),
	}
}

func (s *Service) HandleAddAloha(subID string, targetID string) Response {
	s.mu.Lock()
	targetLower := strings.ToLower(targetID)
	for pid, session := range s.onlineSessions {
		if strings.EqualFold(session.UserID, subID) {
			if _, ok := s.onlineSubscribers[targetLower]; !ok {
				s.onlineSubscribers[targetLower] = make(map[int]SubscriberSession)
			}
			s.onlineSubscribers[targetLower][pid] = session
		}
	}
	s.mu.Unlock()

	log.Printf("[aloha.svc] ADD: sub=%s -> target=%s", subID, targetID)
	return Response{Success: true, Message: fmt.Sprintf("Added %s to watch target %s", subID, targetID)}
}

func (s *Service) HandleRemoveAloha(subID string, targetID string) Response {
	s.mu.Lock()
	targetLower := strings.ToLower(targetID)
	if subMap, ok := s.onlineSubscribers[targetLower]; ok {
		for pid, session := range subMap {
			if strings.EqualFold(session.UserID, subID) {
				delete(subMap, pid)
			}
		}
		if len(subMap) == 0 {
			delete(s.onlineSubscribers, targetLower)
		}
	}
	s.mu.Unlock()

	log.Printf("[aloha.svc] REMOVE: sub=%s -> target=%s", subID, targetID)
	return Response{Success: true, Message: fmt.Sprintf("Removed %s watching target %s", subID, targetID)}
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
