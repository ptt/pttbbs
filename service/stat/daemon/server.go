package daemon

import (
	"encoding/json"
	"fmt"
	"log"
	"net"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"

	"pttbbs/stat/model"
	"pttbbs/stat/storage"
)

type Server struct {
	socketPath   string
	store        *storage.Storage
	saveInterval time.Duration
	startTime    time.Time
	sem          chan struct{}
	stopCh       chan struct{}
}

func NewServer(socketPath string, store *storage.Storage, saveInterval time.Duration) *Server {
	if saveInterval <= 0 {
		saveInterval = 1 * time.Hour
	}
	return &Server{
		socketPath:   socketPath,
		store:        store,
		saveInterval: saveInterval,
		startTime:    time.Now(),
		sem:          make(chan struct{}, 512),
		stopCh:       make(chan struct{}),
	}
}

// IsSocketOccupied checks if an active process is listening on the UNIX domain socket.
func IsSocketOccupied(socketPath string) bool {
	conn, err := net.DialTimeout("unix", socketPath, 200*time.Millisecond)
	if err == nil {
		conn.Close()
		return true
	}
	return false
}

func (s *Server) Start() error {
	dir := filepath.Dir(s.socketPath)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return fmt.Errorf("failed to create socket directory: %w", err)
	}

	// Check if another active instance is already listening on this socket
	if IsSocketOccupied(s.socketPath) {
		return fmt.Errorf("another instance of stat.svc is already running on %s", s.socketPath)
	}
	_ = os.Remove(s.socketPath)

	listener, err := net.Listen("unix", s.socketPath)
	if err != nil {
		return fmt.Errorf("failed to listen on socket %s: %w", s.socketPath, err)
	}
	defer listener.Close()

	if err := os.Chmod(s.socketPath, 0666); err != nil {
		log.Printf("[stat.svc] Warning: failed to chmod socket: %v", err)
	}

	log.Printf("[stat.svc] Listening on UNIX socket: %s", s.socketPath)

	// Start background save/prune ticker
	go s.backgroundLoop()

	// Setup graceful shutdown signal handler
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, os.Interrupt, syscall.SIGTERM)
	go func() {
		sig := <-sigCh
		log.Printf("[stat.svc] Received signal %v, shutting down...", sig)
		close(s.stopCh)
		listener.Close()
	}()

	for {
		conn, err := listener.Accept()
		if err != nil {
			select {
			case <-s.stopCh:
				return nil
			default:
				log.Printf("[stat.svc] Accept error: %v", err)
				time.Sleep(50 * time.Millisecond)
				continue
			}
		}

		select {
		case s.sem <- struct{}{}:
			go func(c net.Conn) {
				defer func() { <-s.sem }()
				s.handleConn(c)
			}(conn)
		default:
			_ = conn.SetDeadline(time.Now().Add(500 * time.Millisecond))
			_ = json.NewEncoder(conn).Encode(model.Response{
				Success: false,
				Error:   "service overloaded",
			})
			conn.Close()
		}
	}
}

func (s *Server) handleConn(conn net.Conn) {
	defer conn.Close()
	_ = conn.SetDeadline(time.Now().Add(3 * time.Second))

	var req model.Request
	if err := json.NewDecoder(conn).Decode(&req); err != nil {
		_ = json.NewEncoder(conn).Encode(model.Response{
			Success: false,
			Error:   err.Error(),
		})
		return
	}

	resp := s.ProcessRequest(req)
	_ = json.NewEncoder(conn).Encode(resp)
}

func (s *Server) ProcessRequest(req model.Request) model.Response {
	switch req.Action {
	case "get":
		stats := s.store.Get(req.UserID)
		if req.Key != "" {
			var val int32
			switch req.Key {
			case "post":
				val = stats.PostCount
			case "mail":
				val = stats.MailCount
			case "song":
				val = stats.SongCount
			case "lastsong":
				val = int32(stats.LastSongTime)
			default:
				return model.Response{Success: false, Error: "unknown stat key: " + req.Key}
			}
			return model.Response{
				Success: true,
				Value:   val,
			}
		}
		return model.Response{
			Success:      true,
			Stats:        stats,
			PostCount:    stats.PostCount,
			MailCount:    stats.MailCount,
			SongCount:    stats.SongCount,
			LastSongTime: stats.LastSongTime,
		}

	case "inc":
		switch req.Key {
		case "post":
			cnt, err := s.store.IncPost(req.UserID)
		if err != nil {
			return model.Response{Success: false, Error: err.Error()}
		}
			return model.Response{Success: true, Value: cnt, PostCount: cnt}
		case "song":
			cnt, err := s.store.IncSong(req.UserID)
		if err != nil {
			return model.Response{Success: false, Error: err.Error()}
		}
			return model.Response{Success: true, Value: cnt, SongCount: cnt, LastSongTime: time.Now().Unix()}
		default:
			return model.Response{Success: false, Error: "unsupported inc stat key: " + req.Key}
		}

	case "add":
		switch req.Key {
		case "mail":
			cnt, err := s.store.AddMail(req.UserID, req.Count)
		if err != nil {
			return model.Response{Success: false, Error: err.Error()}
		}
			return model.Response{Success: true, Value: cnt, MailCount: cnt}
		default:
			return model.Response{Success: false, Error: "unsupported add stat key: " + req.Key}
		}

	case "inc_post":
		cnt, err := s.store.IncPost(req.UserID)
		if err != nil {
			return model.Response{Success: false, Error: err.Error()}
		}
		return model.Response{Success: true, Value: cnt, PostCount: cnt}

	case "add_mail":
		cnt, err := s.store.AddMail(req.UserID, req.Count)
		if err != nil {
			return model.Response{Success: false, Error: err.Error()}
		}
		return model.Response{Success: true, Value: cnt, MailCount: cnt}

	case "inc_song":
		cnt, err := s.store.IncSong(req.UserID)
		if err != nil {
			return model.Response{Success: false, Error: err.Error()}
		}
		return model.Response{Success: true, Value: cnt, SongCount: cnt, LastSongTime: time.Now().Unix()}

	case "set":
		if err := s.store.Set(req.UserID, req.PostCount, req.MailCount, req.SongCount); err != nil {
			return model.Response{Success: false, Error: err.Error()}
		}
		return model.Response{Success: true, Message: "updated"}

	case "status":
		today := model.TodayDate()
		lastSavedStr := "never"
		if !s.store.LastSaved().IsZero() {
			lastSavedStr = s.store.LastSaved().Format(time.RFC3339)
		}
		return model.Response{
			Success: true,
			Status: &model.ServiceStatus{
				Now:          time.Now().Format(time.RFC3339),
				Date:         today,
				TrackedUsers: s.store.CountTracked(),
				Uptime:       time.Since(s.startTime).Truncate(time.Second).String(),
				LastSave:     lastSavedStr,
			},
		}

	case "save", "flush":
		n, err := s.store.Save()
		if err != nil {
			return model.Response{Success: false, Error: err.Error()}
		}
		return model.Response{
			Success: true,
			Message: fmt.Sprintf("saved %d active entries to disk", n),
		}

	case "prune":
		n := s.store.Prune(model.TodayDate())
		return model.Response{
			Success: true,
			Message: fmt.Sprintf("pruned %d expired entries", n),
		}

	default:
		return model.Response{
			Success: false,
			Error:   fmt.Sprintf("unknown action: %s", req.Action),
		}
	}
}

func (s *Server) backgroundLoop() {
	saveTicker := time.NewTicker(s.saveInterval)
	pruneTicker := time.NewTicker(1 * time.Hour)
	defer saveTicker.Stop()
	defer pruneTicker.Stop()

	for {
		select {
		case <-s.stopCh:
			// Final flush on exit
			if s.store.IsDirty() {
				n, err := s.store.Save()
				if err != nil {
					log.Printf("[stat.svc] Final save error: %v", err)
				} else {
					log.Printf("[stat.svc] Final save finished: %d entries saved", n)
				}
			}
			_ = os.Remove(s.socketPath)
			return

		case <-saveTicker.C:
			if s.store.IsDirty() {
				n, err := s.store.Save()
				if err != nil {
					log.Printf("[stat.svc] Periodic save error: %v", err)
				} else {
					log.Printf("[stat.svc] Periodic save: %d entries saved", n)
				}
			}

		case <-pruneTicker.C:
			today := model.TodayDate()
			pruned := s.store.Prune(today)
			if pruned > 0 {
				log.Printf("[stat.svc] Pruned %d expired entries for date < %d", pruned, today)
			}
		}
	}
}
