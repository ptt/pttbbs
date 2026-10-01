package daemon

import (
	"encoding/json"
	"fmt"
	"log"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"pttbbs/bbs"
)

type ServiceOption func(*Service)

func WithInterval(d time.Duration) ServiceOption {
	return func(s *Service) {
		if d >= 100*time.Millisecond {
			s.interval = d
		}
	}
}

func WithFixInterval(interval time.Duration) ServiceOption {
	return func(s *Service) {
		s.fixInterval = interval
	}
}

func WithVerbose(v int) ServiceOption {
	return func(s *Service) {
		s.verbose = v
	}
}

type Service struct {
	bbsHome        string
	socketPath     string
	listener       net.Listener
	shmClient      *bbs.SHMClient
	interval       time.Duration
	fixInterval    time.Duration
	verbose        int
	mu             sync.Mutex
	stopChan       chan struct{}
	wg             sync.WaitGroup
	busystateCount int
}

func IsSocketOccupied(socketPath string) bool {
	conn, err := net.Dial("unix", socketPath)
	if err == nil {
		conn.Close()
		return true
	}
	return false
}

func NewService(bbsHome string, socketPath string, opts ...ServiceOption) (*Service, error) {
	if socketPath == "" {
		socketPath = filepath.Join(bbsHome, "run", "utmp.svc.sock")
	}

	shm, err := bbs.AttachSHM()
	if err != nil {
		log.Printf("[utmp.svc] Notice: attach SHM returned error: %v (running in mock/test mode)", err)
	}

	s := &Service{
		bbsHome:        bbsHome,
		socketPath:     socketPath,
		shmClient:      shm,
		interval:       1 * time.Second,
		stopChan:       make(chan struct{}),
	}

	for _, opt := range opts {
		opt(s)
	}

	return s, nil
}

func (s *Service) Start() error {
	_ = os.MkdirAll(filepath.Dir(s.socketPath), 0755)
	_ = os.Remove(s.socketPath)

	l, err := net.Listen("unix", s.socketPath)
	if err != nil {
		return fmt.Errorf("failed to listen on socket %s: %w", s.socketPath, err)
	}
	_ = os.Chmod(s.socketPath, 0666)
	s.listener = l

	log.Printf("[utmp.svc] Started listening on UNIX socket: %s", s.socketPath)

	if s.shmClient != nil {
		// Initial update on startup
		s.shmClient.UtmpUpdate()
		log.Printf("[utmp.svc] Initial utmp_update completed: %d online users", s.shmClient.GetUtmpNumber())
	}

	// Start sortutmp worker
	s.wg.Add(1)
	go s.startSortUtmpWorker()

	// Start periodic fix worker if configured
	if s.fixInterval > 0 {
		s.wg.Add(1)
		go s.startFixWorker()
		log.Printf("[utmp.svc] Periodic background fix worker started (interval=%v)", s.fixInterval)
	}

	for {
		conn, err := s.listener.Accept()
		if err != nil {
			select {
			case <-s.stopChan:
				return nil
			default:
				log.Printf("[utmp.svc] Accept error: %v", err)
				continue
			}
		}
		go s.handleConnection(conn)
	}
}

func (s *Service) Stop() {
	close(s.stopChan)
	if s.listener != nil {
		s.listener.Close()
	}
	s.wg.Wait()
	_ = os.Remove(s.socketPath)
	log.Printf("[utmp.svc] Service stopped cleanly")
}

func (s *Service) handleConnection(conn net.Conn) {
	defer conn.Close()

	var req Request
	if err := json.NewDecoder(conn).Decode(&req); err != nil {
		resp := Response{Success: false, Error: fmt.Sprintf("invalid json request: %v", err)}
		_ = json.NewEncoder(conn).Encode(resp)
		return
	}

	resp := s.ProcessRequest(req)
	_ = json.NewEncoder(conn).Encode(resp)
}

func (s *Service) startSortUtmpWorker() {
	defer s.wg.Done()
	ticker := time.NewTicker(s.interval)
	defer ticker.Stop()

	for {
		select {
		case <-s.stopChan:
			return
		case <-ticker.C:
			if s.shmClient == nil {
				continue
			}

			// Watchdog: detect stuck busystate
			if s.shmClient.GetUtmpBusystate() != 0 {
				s.busystateCount++
				if s.busystateCount >= 5 {
					log.Printf("[utmp.svc] Watchdog: SHM->UTMPbusystate stuck at 1 for %d checks, resetting to 0", s.busystateCount)
					s.shmClient.ResetUtmpBusystate()
					s.busystateCount = 0
				}
				continue
			}
			s.busystateCount = 0

			if s.shmClient.GetUtmpNeedUpdate() != 0 {
				s.mu.Lock()
				s.shmClient.UtmpUpdate()
				s.shmClient.SetUtmpNeedUpdate(0)
				s.mu.Unlock()
				if s.verbose > 0 {
					log.Printf("[utmp.svc] utmp_update triggered by needupdate (online=%d)", s.shmClient.GetUtmpNumber())
				}
			}
		}
	}
}

func (s *Service) startFixWorker() {
	defer s.wg.Done()
	ticker := time.NewTicker(s.fixInterval)
	defer ticker.Stop()

	for {
		select {
		case <-s.stopChan:
			return
		case <-ticker.C:
			if s.shmClient == nil {
				continue
			}
			s.mu.Lock()
			_ = RunFix(s.shmClient)
			s.mu.Unlock()
		}
	}
}

func (s *Service) getStatusLocked() *UTMPStatus {
	now := time.Now()
	res := &UTMPStatus{
		Now: now.Format(time.ANSIC),
	}

	if s.shmClient == nil {
		res.Uptime = now.Format(time.ANSIC)
		res.UptimeTimestamp = now.Unix()
		return res
	}

	st := s.shmClient.GetUtmpStatus()
	res.Uptime = st.Uptime.Format(time.ANSIC)
	res.UptimeTimestamp = st.Uptime.Unix()
	res.Number = st.Number
	res.Busystate = st.Busystate
	res.NeedUpdate = st.NeedUpdate

	bids := s.shmClient.GetHotBoardBIDs(128)
	for _, bid := range bids {
		if b, ok := s.shmClient.GetBoardByBID(bid); ok {
			res.HotBoards = append(res.HotBoards, HotBoardInfo{
				BID:     bid,
				BrdName: b.BrdName,
				NUsers:  s.shmClient.GetBoardNUser(bid),
			})
		}
	}

	return res
}

func (s *Service) ProcessRequest(req Request) Response {
	s.mu.Lock()
	defer s.mu.Unlock()

	switch req.Action {
	case "status":
		status := s.getStatusLocked()
		return Response{
			Success: true,
			Message: "ok",
			Status:  status,
		}

	case "num":
		num := 0
		if s.shmClient != nil {
			num = s.shmClient.GetUtmpNumber()
		}
		return Response{
			Success: true,
			Message: fmt.Sprintf("%d.0", num),
			Number:  &num,
		}

	case "update":
		if s.shmClient != nil {
			s.shmClient.UtmpUpdate()
			s.shmClient.SetUtmpNeedUpdate(0)
		}
		status := s.getStatusLocked()
		return Response{
			Success: true,
			Message: "utmp and hotboards updated",
			Status:  status,
		}

	case "reset":
		if s.shmClient != nil {
			s.shmClient.ResetUtmpBusystate()
		}
		s.busystateCount = 0
		status := s.getStatusLocked()
		return Response{
			Success: true,
			Message: "UTMPbusystate reset to 0",
			Status:  status,
		}

	case "rebuild":
		if s.shmClient != nil {
			s.shmClient.RebuildUtmpUser()
		}
		num := 0
		if s.shmClient != nil {
			num = s.shmClient.GetUtmpNumber()
		}
		return Response{
			Success: true,
			Message: fmt.Sprintf("utmp_user table rebuilt in-place from active sessions (%d online)", num),
		}

	case "watch":
		stuck := false
		if s.shmClient != nil && s.shmClient.GetUtmpBusystate() != 0 {
			stuck = true
			s.shmClient.ResetUtmpBusystate()
		}
		s.busystateCount = 0
		status := s.getStatusLocked()
		msg := "busystate normal"
		if stuck {
			msg = "busystate was busy, reset to 0"
		}
		return Response{
			Success: true,
			Message: msg,
			Status:  status,
		}

	case "fix":
		res := RunFix(s.shmClient)
		return Response{
			Success: true,
			Message: fmt.Sprintf("utmpfix finished in %s, cleaned %d dead sessions", res.Duration, res.TotalCleaned),
			Fix:     &res,
		}

	case "kick":
		if req.Target == "" {
			return Response{Success: false, Error: "missing target (userid or pid)"}
		}
		if s.shmClient == nil {
			return Response{Success: false, Error: "null shm client"}
		}

		targetPID, err := strconv.Atoi(req.Target)
		candidates := s.shmClient.GetUtmpCandidates()
		var matched []CleanDetail

		for _, cand := range candidates {
			match := false
			if err == nil && cand.PID == targetPID {
				match = true
			} else if strings.EqualFold(cand.UserID, req.Target) {
				match = true
			}

			if match {
				s.shmClient.PurgeUtmpSlot(cand.Slot)
				matched = append(matched, CleanDetail{
					Slot:    cand.Slot,
					PID:     cand.PID,
					UID:     cand.UID,
					UserID:  cand.UserID,
					Reason:  "kicked by operator",
					IdleSec: cand.IdleSec,
				})
				if cand.PID > 0 {
					_ = syscall.Kill(cand.PID, syscall.SIGHUP)
				}
			}
		}

		if len(matched) == 0 {
			return Response{Success: false, Error: fmt.Sprintf("target session '%s' not found online", req.Target)}
		}

		s.shmClient.RebuildUtmpUser()
		s.shmClient.UtmpUpdate()
		s.shmClient.SetUtmpNeedUpdate(0)

		return Response{
			Success: true,
			Message: fmt.Sprintf("kicked %d session(s) for target '%s'", len(matched), req.Target),
			Fix: &FixResult{
				TotalCleaned: len(matched),
				Details:      matched,
				OnlineAfter:  s.shmClient.GetUtmpNumber(),
			},
		}

	default:
		return Response{
			Success: false,
			Error:   fmt.Sprintf("unknown action: %s", req.Action),
		}
	}
}
