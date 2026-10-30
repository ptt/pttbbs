package daemon

import (
	"bufio"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

// ChessSession represents an active or pending chess game between two players.
type ChessSession struct {
	ID         uint32
	Caller     string // lowercase
	Callee     string // lowercase
	Sig        int
	GameName   string
	CallerConn net.Conn
	CalleeConn net.Conn
	CreatedAt  time.Time
	MovesCount int64
	Closed     bool
	mu         sync.Mutex
	joinChan   chan struct{}
}

// ChessServer brokers direct two-way chess sessions over a dedicated UNIX domain socket.
type ChessServer struct {
	socketPath string
	listener   net.Listener
	pending    map[string]*ChessSession // key: caller + ":" + callee
	active     map[uint32]*ChessSession
	nextID     uint32
	totalMoves int64
	mu         sync.RWMutex
	verbose    int
	stopChan   chan struct{}
	wg         sync.WaitGroup
}

func NewChessServer(socketPath string, verbose int) *ChessServer {
	return &ChessServer{
		socketPath: socketPath,
		pending:    make(map[string]*ChessSession),
		active:     make(map[uint32]*ChessSession),
		verbose:    verbose,
		stopChan:   make(chan struct{}),
	}
}

func (s *ChessServer) Start() error {
	_ = os.Remove(s.socketPath)
	l, err := net.Listen("unix", s.socketPath)
	if err != nil {
		return fmt.Errorf("failed to listen on %s: %w", s.socketPath, err)
	}
	_ = os.Chmod(s.socketPath, 0666)
	s.listener = l

	log.Printf("[chess] Listening on UNIX domain socket: %s", s.socketPath)

	s.wg.Add(1)
	go s.acceptLoop()
	return nil
}

func (s *ChessServer) Stop() {
	close(s.stopChan)
	if s.listener != nil {
		s.listener.Close()
	}
	s.mu.Lock()
	for _, sess := range s.pending {
		if sess.CallerConn != nil {
			_ = sess.CallerConn.Close()
		}
	}
	for _, sess := range s.active {
		sess.mu.Lock()
		sess.Closed = true
		if sess.CallerConn != nil {
			_ = sess.CallerConn.Close()
		}
		if sess.CalleeConn != nil {
			_ = sess.CalleeConn.Close()
		}
		sess.mu.Unlock()
	}
	s.pending = make(map[string]*ChessSession)
	s.active = make(map[uint32]*ChessSession)
	s.mu.Unlock()

	s.wg.Wait()
	_ = os.Remove(s.socketPath)
	log.Printf("[chess] Server stopped cleanly")
}

func (s *ChessServer) acceptLoop() {
	defer s.wg.Done()
	for {
		conn, err := s.listener.Accept()
		if err != nil {
			select {
			case <-s.stopChan:
				return
			default:
				log.Printf("[chess] Accept error: %v", err)
				time.Sleep(10 * time.Millisecond)
				continue
			}
		}

		s.wg.Add(1)
		go s.handleConnection(conn)
	}
}

func (s *ChessServer) handleConnection(conn net.Conn) {
	defer s.wg.Done()

	reader := bufio.NewReader(conn)
	line, err := reader.ReadString('\n')
	if err != nil {
		_ = conn.Close()
		return
	}
	line = strings.TrimSpace(line)
	parts := strings.Fields(line)
	if len(parts) < 3 {
		_, _ = conn.Write([]byte("ERR invalid_command\n"))
		_ = conn.Close()
		return
	}

	cmd := strings.ToUpper(parts[0])
	switch cmd {
	case "CREATE":
		// CREATE <caller> <callee> <sig>
		caller := strings.ToLower(parts[1])
		callee := strings.ToLower(parts[2])
		sig := 3 // default Gomoku (SIG_GOMO)
		if len(parts) >= 4 {
			if parsedSig, err := strconv.Atoi(parts[3]); err == nil {
				sig = parsedSig
			}
		}

		gameName := "五子棋"
		switch sig {
		case 1:
			gameName = "Talk"
		case 3:
			gameName = "五子棋"
		case 4:
			gameName = "象棋"
		case 8:
			gameName = "六子棋"
		default:
			gameName = "棋類對局"
		}

		key := caller + ":" + callee
		sess := &ChessSession{
			ID:         atomic.AddUint32(&s.nextID, 1),
			Caller:     caller,
			Callee:     callee,
			Sig:        sig,
			GameName:   gameName,
			CallerConn: conn,
			CreatedAt:  time.Now(),
			joinChan:   make(chan struct{}),
		}

		s.mu.Lock()
		if existing, ok := s.pending[key]; ok {
			_ = existing.CallerConn.Close()
		}
		s.pending[key] = sess
		s.mu.Unlock()

		if s.verbose > 0 {
			log.Printf("[chess] Session #%d created (pending): %s -> %s [%s]", sess.ID, caller, callee, gameName)
		}

		// Acknowledge session creation to caller immediately
		if _, err := conn.Write([]byte("OK\n")); err != nil {
			s.mu.Lock()
			if s.pending[key] == sess {
				delete(s.pending, key)
			}
			s.mu.Unlock()
			_ = conn.Close()
			return
		}

		// Wait until callee joins, timeout (120s), or server stop
		select {
		case <-sess.joinChan:
			// Callee joined and session is active
			return
		case <-time.After(120 * time.Second):
			s.mu.Lock()
			if s.pending[key] == sess {
				delete(s.pending, key)
				if s.verbose > 0 {
					log.Printf("[chess] Pending session #%d timed out: %s -> %s", sess.ID, caller, callee)
				}
			}
			s.mu.Unlock()
			_ = conn.Close()
			return
		case <-s.stopChan:
			_ = conn.Close()
			return
		}

	case "JOIN":
		// JOIN <callee> <caller>
		callee := strings.ToLower(parts[1])
		caller := strings.ToLower(parts[2])
		key := caller + ":" + callee

		s.mu.Lock()
		sess, ok := s.pending[key]
		if ok && sess != nil {
			delete(s.pending, key)
			sess.CalleeConn = conn
			s.active[sess.ID] = sess
		}
		s.mu.Unlock()

		if s.verbose > 0 {
			log.Printf("[chess] JOIN request: callee=%s, caller=%s (key=%s, found=%v)", callee, caller, key, ok)
		}

		if !ok || sess == nil {
			_, _ = conn.Write([]byte("ERR no_session\n"))
			_ = conn.Close()
			return
		}

		// Unblock the CREATE goroutine
		close(sess.joinChan)

		// Reset deadlines
		_ = sess.CallerConn.SetDeadline(time.Time{})
		_ = sess.CalleeConn.SetDeadline(time.Time{})

		// Notify caller that peer has joined
		if _, err := sess.CallerConn.Write([]byte("READY\n")); err != nil {
			s.mu.Lock()
			delete(s.active, sess.ID)
			s.mu.Unlock()
			_, _ = conn.Write([]byte("ERR caller_disconnected\n"))
			_ = conn.Close()
			return
		}
		// Notify callee that session is established
		if _, err := sess.CalleeConn.Write([]byte("OK\n")); err != nil {
			s.mu.Lock()
			delete(s.active, sess.ID)
			s.mu.Unlock()
			_ = sess.CallerConn.Close()
			_ = conn.Close()
			return
		}

		if s.verbose > 0 {
			log.Printf("[chess] Session #%d connected: %s vs %s [%s]",
				sess.ID, sess.Caller, sess.Callee, sess.GameName)
		}

		// Launch proxy between Caller and Callee
		s.wg.Add(1)
		go s.proxySession(sess)

	default:
		_, _ = conn.Write([]byte("ERR unknown_command\n"))
		_ = conn.Close()
	}
}

func (s *ChessServer) proxySession(sess *ChessSession) {
	defer s.wg.Done()
	defer func() {
		s.mu.Lock()
		delete(s.active, sess.ID)
		s.mu.Unlock()

		sess.mu.Lock()
		sess.Closed = true
		_ = sess.CallerConn.Close()
		_ = sess.CalleeConn.Close()
		sess.mu.Unlock()

		if s.verbose > 0 {
			log.Printf("[chess] Session #%d finished (%s vs %s [%s], moves: %d)",
				sess.ID, sess.Caller, sess.Callee, sess.GameName, atomic.LoadInt64(&sess.MovesCount))
		}
	}()

	var wg sync.WaitGroup
	wg.Add(2)

	var once sync.Once
	closeBoth := func() {
		_ = sess.CallerConn.Close()
		_ = sess.CalleeConn.Close()
	}

	pipe := func(src, dst net.Conn) {
		defer wg.Done()
		defer once.Do(closeBoth)
		buf := make([]byte, 1024)
		for {
			n, err := src.Read(buf)
			if n > 0 {
				atomic.AddInt64(&sess.MovesCount, 1)
				atomic.AddInt64(&s.totalMoves, 1)
				if _, werr := dst.Write(buf[:n]); werr != nil {
					return
				}
			}
			if err != nil {
				if err != io.EOF && s.verbose > 1 {
					log.Printf("[chess] Session #%d pipe error: %v", sess.ID, err)
				}
				return
			}
		}
	}

	go pipe(sess.CallerConn, sess.CalleeConn)
	go pipe(sess.CalleeConn, sess.CallerConn)

	wg.Wait()
}

func (s *ChessServer) GetStats() (activeGames int, totalMoves int64) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return len(s.active), atomic.LoadInt64(&s.totalMoves)
}

func (s *ChessServer) GetActiveGames() []string {
	s.mu.RLock()
	defer s.mu.RUnlock()

	var result []string
	for _, sess := range s.active {
		result = append(result, fmt.Sprintf("#%d: %s vs %s [%s] (moves: %d)",
			sess.ID, sess.Caller, sess.Callee, sess.GameName, atomic.LoadInt64(&sess.MovesCount)))
	}
	return result
}
