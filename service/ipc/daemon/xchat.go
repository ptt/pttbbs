package daemon

import (
	"fmt"
	"log"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"sync"
	"sync/atomic"
	"time"

	"pttbbs/bbs"
)

// XChatBridge proxies UNIX domain socket connections to the native backend xchatd daemon.
// This preserves 100% of all traditional xchatd features (MUD verbs, Room OP, permissions,
// flags, kicking, banning) and ensures exact Big5 byte-level transparency without encoding issues.
type XChatBridge struct {
	socketPath  string
	backendAddr string
	bbsHome     string
	listener    net.Listener
	conns       map[net.Conn]net.Conn
	activeConns int64
	totalBytes  int64
	verbose     int
	stopChan    chan struct{}
	wg          sync.WaitGroup
	mu          sync.Mutex
}

type XChatServer = XChatBridge

func NewXChatServer(socketPath string, verbose int) *XChatBridge {
	return NewXChatBridge(socketPath, "127.0.0.1:3838", verbose)
}

func NewXChatBridge(socketPath string, backendAddr string, verbose int) *XChatBridge {
	bbsHome := os.Getenv("BBSHOME")
	if bbsHome == "" {
		bbsHome = bbs.BBSHome()
	}
	return &XChatBridge{
		socketPath:  socketPath,
		backendAddr: backendAddr,
		bbsHome:     bbsHome,
		conns:       make(map[net.Conn]net.Conn),
		verbose:     verbose,
		stopChan:    make(chan struct{}),
	}
}

func (s *XChatBridge) SetBackendAddr(addr string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.backendAddr = addr
}

func (s *XChatBridge) Start() error {
	_ = os.Remove(s.socketPath)
	l, err := net.Listen("unix", s.socketPath)
	if err != nil {
		return fmt.Errorf("failed to listen on %s: %w", s.socketPath, err)
	}
	_ = os.Chmod(s.socketPath, 0666)
	s.listener = l

	log.Printf("[xchat] Bridge listening on %s (backend: %s)", s.socketPath, s.backendAddr)

	s.wg.Add(1)
	go s.acceptLoop()
	return nil
}

func (s *XChatBridge) Stop() {
	close(s.stopChan)
	if s.listener != nil {
		s.listener.Close()
	}

	s.mu.Lock()
	for c1, c2 := range s.conns {
		if c1 != nil {
			_ = c1.Close()
		}
		if c2 != nil {
			_ = c2.Close()
		}
	}
	s.conns = make(map[net.Conn]net.Conn)
	s.mu.Unlock()

	s.wg.Wait()
	_ = os.Remove(s.socketPath)
	log.Printf("[xchat] Bridge stopped cleanly")
}

func (s *XChatBridge) acceptLoop() {
	defer s.wg.Done()
	for {
		clientConn, err := s.listener.Accept()
		if err != nil {
			select {
			case <-s.stopChan:
				return
			default:
				log.Printf("[xchat] Accept error: %v", err)
				time.Sleep(10 * time.Millisecond)
				continue
			}
		}

		s.wg.Add(1)
		go s.handleClient(clientConn)
	}
}

func (s *XChatBridge) dialBackend() (net.Conn, error) {
	s.mu.Lock()
	backend := s.backendAddr
	s.mu.Unlock()

	conn, err := net.DialTimeout("tcp", backend, 200*time.Millisecond)
	if err == nil {
		return conn, nil
	}

	// If connection failed and xchatd binary exists, attempt to spawn xchatd
	xchatdBin := filepath.Join(s.bbsHome, "bin", "xchatd")
	if _, statErr := os.Stat(xchatdBin); statErr == nil {
		if s.verbose > 0 {
			log.Printf("[xchat] Backend %s not running, attempting to spawn %s...", backend, xchatdBin)
		}
		cmd := exec.Command(xchatdBin)
		_ = cmd.Start()
		// Wait for xchatd to bind
		for i := 0; i < 15; i++ {
			time.Sleep(50 * time.Millisecond)
			if retryConn, retryErr := net.DialTimeout("tcp", backend, 200*time.Millisecond); retryErr == nil {
				return retryConn, nil
			}
		}
	}

	return nil, err
}

func (s *XChatBridge) handleClient(clientConn net.Conn) {
	defer s.wg.Done()
	defer clientConn.Close()

	backendConn, err := s.dialBackend()
	if err != nil {
		log.Printf("[xchat] Failed to connect to xchatd backend (%s): %v", s.backendAddr, err)
		return
	}
	defer backendConn.Close()

	s.mu.Lock()
	s.conns[clientConn] = backendConn
	s.mu.Unlock()

	defer func() {
		s.mu.Lock()
		delete(s.conns, clientConn)
		s.mu.Unlock()
	}()

	atomic.AddInt64(&s.activeConns, 1)
	defer atomic.AddInt64(&s.activeConns, -1)

	if s.verbose > 0 {
		log.Printf("[xchat] Bridged new connection: %s <-> %s", clientConn.RemoteAddr(), s.backendAddr)
	}

	var once sync.Once
	closeBoth := func() {
		_ = clientConn.Close()
		_ = backendConn.Close()
	}

	var wg sync.WaitGroup
	wg.Add(2)

	pipe := func(src, dst net.Conn) {
		defer wg.Done()
		defer once.Do(closeBoth)
		buf := make([]byte, 4096)
		for {
			n, rerr := src.Read(buf)
			if n > 0 {
				atomic.AddInt64(&s.totalBytes, int64(n))
				if _, werr := dst.Write(buf[:n]); werr != nil {
					return
				}
			}
			if rerr != nil {
				return
			}
		}
	}

	go pipe(clientConn, backendConn)
	go pipe(backendConn, clientConn)

	wg.Wait()

	if s.verbose > 0 {
		log.Printf("[xchat] Bridged connection terminated")
	}
}

func (s *XChatBridge) GetStats() (roomCount int, userCount int) {
	return 1, int(atomic.LoadInt64(&s.activeConns))
}
