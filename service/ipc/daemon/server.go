package daemon

import (
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	_ "pttbbs/bbs"
)

const (
	ClientSendQueueSize = 256
	// ClientWriteTimeout bounds how long a single (batched) write to an mbbsd
	// may block. Each client has its own writer goroutine, so a slow client
	// never stalls fan-out to others; the timeout only decides when a client
	// that stopped reading is considered dead. A partial write leaves the
	// stream unusable, so a timeout always closes the connection. Keep this
	// generous: mbbsd may block for a while writing to a slow terminal, and
	// dropping the IPC link also drops its subscriptions.
	ClientWriteTimeout = 2 * time.Second
	// ClientWriteBatch is the maximum number of queued packets coalesced into
	// a single writev(2). Under bursty fan-out (e.g. thousands of subscribers
	// on one live post) this cuts syscalls and receiver wake-ups, and lets the
	// kernel socket buffer hold far more packets (fewer, larger skbs).
	ClientWriteBatch = 64
)

type Client struct {
	conn          net.Conn
	userid        string // normalized lowercase
	displayUserid string
	pid           uint32
	uslot         int32
	topics        map[TopicKey]struct{}
	sendCh        chan []byte
	done          chan struct{}
	closeOnce     sync.Once
	registered    bool
}

func (c *Client) Close() {
	c.closeOnce.Do(func() {
		close(c.done)
		if c.conn != nil {
			_ = c.conn.Close()
		}
	})
}

func (c *Client) writeLoop() {
	batch := make(net.Buffers, 0, ClientWriteBatch)
	for {
		select {
		case <-c.done:
			return
		case pkt := <-c.sendCh:
			batch = append(batch[:0], pkt)
		drain:
			for len(batch) < ClientWriteBatch {
				select {
				case more := <-c.sendCh:
					batch = append(batch, more)
				default:
					break drain
				}
			}
			_ = c.conn.SetWriteDeadline(time.Now().Add(ClientWriteTimeout))
			// WriteTo consumes (and nils out) the entries it writes; packets
			// may be shared with other clients, but only the slice headers are
			// modified, never the underlying bytes.
			bufs := batch
			if _, err := bufs.WriteTo(c.conn); err != nil {
				c.Close()
				return
			}
		}
	}
}

func (c *Client) Enqueue(data []byte) error {
	select {
	case <-c.done:
		return io.ErrClosedPipe
	default:
	}

	select {
	case c.sendCh <- data:
		return nil
	default:
		return errors.New("client send queue full")
	}
}

func (c *Client) Send(hdr *Header, payload []byte) error {
	return c.Enqueue(EncodePacket(hdr, payload))
}

// EncodePacket serializes a header and payload into one wire packet. The
// returned slice is treated as immutable once queued, so a single packet can
// be enqueued to many clients during fan-out without per-client copies.
func EncodePacket(hdr *Header, payload []byte) []byte {
	hdr.Len = uint32(len(payload))
	buf := make([]byte, 0, 16+len(payload))
	buf = append(buf, hdr.Encode()...)
	return append(buf, payload...)
}

// fanout enqueues one pre-encoded packet to every target and returns how many
// accepted it. Clients with a full queue are skipped (never blocks).
func fanout(targets []*Client, pkt []byte) int {
	sent := 0
	for _, tgt := range targets {
		if tgt.Enqueue(pkt) == nil {
			sent++
		}
	}
	return sent
}

func (c *Client) SendAck(seq uint32, code int32, msg string) error {
	pkt := MakeAckPacket(seq, code, msg)
	return c.Enqueue(pkt)
}

type Stats struct {
	TotalClients      int64
	ActiveClients     int64
	TotalWmsgSent     int64
	TotalTalkReqs     int64
	TotalLiveComments int64
	TotalPublishes    int64
	TotalChatMsgs     int64
}

type Service struct {
	socketPath  string
	listener    net.Listener
	xchatServer *XChatServer
	chessServer *ChessServer
	mu          sync.RWMutex
	clients     map[*Client]struct{}
	byUser      map[string][]*Client
	byPid       map[uint32]*Client
	topics      map[TopicKey]map[*Client]struct{}
	verbose     int
	stats       Stats
	startTime   time.Time
	stopChan    chan struct{}
	wg          sync.WaitGroup
}

type ServiceOption func(*Service)

func WithVerbose(v int) ServiceOption {
	return func(s *Service) {
		s.verbose = v
	}
}

func IsSocketOccupied(socketPath string) bool {
	conn, err := net.Dial("unix", socketPath)
	if err == nil {
		conn.Close()
		return true
	}
	return false
}

func NewService(socketPath string, opts ...ServiceOption) (*Service, error) {
	s := &Service{
		socketPath: socketPath,
		clients:    make(map[*Client]struct{}),
		byUser:     make(map[string][]*Client),
		byPid:      make(map[uint32]*Client),
		topics:     make(map[TopicKey]map[*Client]struct{}),
		startTime:  time.Now(),
		stopChan:   make(chan struct{}),
	}
	for _, opt := range opts {
		opt(s)
	}

	xchatSock := filepath.Join(filepath.Dir(socketPath), "xchat.ipc.sock")
	s.xchatServer = NewXChatServer(xchatSock, s.verbose)

	chessSock := filepath.Join(filepath.Dir(socketPath), "chess.ipc.sock")
	s.chessServer = NewChessServer(chessSock, s.verbose)

	return s, nil
}

func (s *Service) Start() error {
	if IsSocketOccupied(s.socketPath) {
		return fmt.Errorf("socket %s is already occupied", s.socketPath)
	}

	_ = os.Remove(s.socketPath)
	l, err := net.Listen("unix", s.socketPath)
	if err != nil {
		return fmt.Errorf("failed to listen on %s: %w", s.socketPath, err)
	}
	_ = os.Chmod(s.socketPath, 0666) // accessible by mbbsd
	s.listener = l

	log.Printf("[ipc.svc] Listening on UNIX domain socket: %s", s.socketPath)

	if err := s.xchatServer.Start(); err != nil {
		s.listener.Close()
		return fmt.Errorf("failed to start xchat server: %w", err)
	}

	if err := s.chessServer.Start(); err != nil {
		s.xchatServer.Stop()
		s.listener.Close()
		return fmt.Errorf("failed to start chess server: %w", err)
	}

	s.wg.Add(1)
	go s.acceptLoop()
	return nil
}

func (s *Service) Stop() {
	close(s.stopChan)
	if s.listener != nil {
		s.listener.Close()
	}
	s.mu.Lock()
	for c := range s.clients {
		c.Close()
	}
	s.mu.Unlock()

	if s.xchatServer != nil {
		s.xchatServer.Stop()
	}
	if s.chessServer != nil {
		s.chessServer.Stop()
	}
	s.wg.Wait()
	_ = os.Remove(s.socketPath)
	log.Printf("[ipc.svc] Service stopped cleanly")
}

func (s *Service) acceptLoop() {
	defer s.wg.Done()
	for {
		conn, err := s.listener.Accept()
		if err != nil {
			select {
			case <-s.stopChan:
				return
			default:
				log.Printf("[ipc.svc] Accept error: %v", err)
				time.Sleep(10 * time.Millisecond)
				continue
			}
		}

		atomic.AddInt64(&s.stats.TotalClients, 1)
		atomic.AddInt64(&s.stats.ActiveClients, 1)

		client := &Client{
			conn:   conn,
			topics: make(map[TopicKey]struct{}),
			sendCh: make(chan []byte, ClientSendQueueSize),
			done:   make(chan struct{}),
		}

		s.mu.Lock()
		s.clients[client] = struct{}{}
		s.mu.Unlock()

		go client.writeLoop()

		s.wg.Add(1)
		go s.handleClient(client)
	}
}

func (s *Service) handleClient(client *Client) {
	defer s.wg.Done()
	defer func() {
		client.Close()
		s.removeClient(client)
		atomic.AddInt64(&s.stats.ActiveClients, -1)
	}()

	for {
		hdr, err := ReadHeader(client.conn)
		if err != nil {
			if err != io.EOF && !strings.Contains(err.Error(), "use of closed network connection") {
				if s.verbose > 0 {
					log.Printf("[ipc.svc] Client read header error: %v", err)
				}
			}
			return
		}

		var payload []byte
		if hdr.Len > 0 {
			if hdr.Len > 65536 {
				log.Printf("[ipc.svc] Payload too large: %d bytes", hdr.Len)
				return
			}
			payload = make([]byte, hdr.Len)
			if _, err := io.ReadFull(client.conn, payload); err != nil {
				log.Printf("[ipc.svc] Error reading payload: %v", err)
				return
			}
		}

		if err := s.dispatch(client, hdr, payload); err != nil {
			if s.verbose > 0 {
				log.Printf("[ipc.svc] Dispatch error for type 0x%04x: %v", hdr.Type, err)
			}
		}
	}
}

func (s *Service) dispatch(client *Client, hdr *Header, payload []byte) error {
	switch hdr.Type {
	case IPCTypeRegister:
		reg, err := DecodeRegisterPayload(payload)
		if err != nil {
			_ = client.SendAck(hdr.Seq, -1, "invalid register payload")
			return err
		}
		rawUID := reg.UserIDString()
		normUID := strings.ToLower(rawUID)

		s.mu.Lock()
		if client.registered && client.userid != "" {
			if clist, ok := s.byUser[client.userid]; ok {
				var updated []*Client
				for _, item := range clist {
					if item != client {
						updated = append(updated, item)
					}
				}
				if len(updated) > 0 {
					s.byUser[client.userid] = updated
				} else {
					delete(s.byUser, client.userid)
				}
			}
			if client.pid > 0 && s.byPid[client.pid] == client {
				delete(s.byPid, client.pid)
			}
		}
		client.userid = normUID
		client.displayUserid = rawUID
		client.pid = reg.PID
		client.uslot = reg.USlot
		client.registered = true

		s.byUser[normUID] = append(s.byUser[normUID], client)
		if reg.PID > 0 {
			s.byPid[reg.PID] = client
		}
		s.mu.Unlock()

		if s.verbose > 0 {
			log.Printf("[ipc.svc] REGISTER: user=%s, pid=%d, slot=%d", rawUID, reg.PID, reg.USlot)
		}
		return client.SendAck(hdr.Seq, 0, "registered")

	case IPCTypeUnregister:
		s.removeClient(client)
		client.registered = false
		return client.SendAck(hdr.Seq, 0, "unregistered")

	case IPCTypePing:
		stats := s.StatsPayload()
		payload := stats.Encode()
		pongHdr := &Header{
			Magic:   IPCMagic,
			Version: IPCVersion,
			Type:    IPCTypePong,
			Seq:     hdr.Seq,
			Len:     uint32(len(payload)),
		}
		return client.Send(pongHdr, payload)

	case IPCTypeStatsReq:
		stats := s.StatsPayload()
		payload := stats.Encode()
		respHdr := &Header{
			Magic:   IPCMagic,
			Version: IPCVersion,
			Type:    IPCTypeStatsResp,
			Seq:     hdr.Seq,
			Len:     uint32(len(payload)),
		}
		return client.Send(respHdr, payload)

	case IPCTypeWmsgSend:
		wmsg, err := DecodeWmsgPayload(payload)
		if err != nil {
			_ = client.SendAck(hdr.Seq, -1, "invalid wmsg payload")
			return err
		}

		targetUser := strings.ToLower(wmsg.ToUserIDString())
		s.mu.RLock()
		targets, ok := s.byUser[targetUser]
		targetList := make([]*Client, len(targets))
		copy(targetList, targets)
		s.mu.RUnlock()

		if !ok || len(targetList) == 0 {
			if s.verbose > 0 {
				log.Printf("[ipc.svc] WMSG_SEND: target %s offline or not connected", wmsg.ToUserIDString())
			}
			return client.SendAck(hdr.Seq, -1, "target not online")
		}

		deliverPkt := EncodePacket(&Header{
			Magic:   IPCMagic,
			Version: IPCVersion,
			Type:    IPCTypeWmsgDeliver,
			Seq:     hdr.Seq,
		}, payload)
		delivered := fanout(targetList, deliverPkt)

		atomic.AddInt64(&s.stats.TotalWmsgSent, 1)

		if s.verbose > 0 {
			log.Printf("[ipc.svc] WMSG_DELIVER: %s -> %s (%d sessions notified): %s",
				wmsg.FromUserIDString(), wmsg.ToUserIDString(), delivered, wmsg.TextString())
		}

		return client.SendAck(hdr.Seq, 0, "delivered")

	case IPCTypeTalkReq:
		talk, err := DecodeTalkPayload(payload)
		if err != nil {
			_ = client.SendAck(hdr.Seq, -1, "invalid talk payload")
			return err
		}

		targetUser := strings.ToLower(talk.ToUserIDString())
		s.mu.RLock()
		targets, ok := s.byUser[targetUser]
		targetList := make([]*Client, len(targets))
		copy(targetList, targets)
		s.mu.RUnlock()

		if !ok || len(targetList) == 0 {
			if s.verbose > 0 {
				log.Printf("[ipc.svc] TALK_REQ: target %s offline or not connected", talk.ToUserIDString())
			}
			return client.SendAck(hdr.Seq, -1, "target not online")
		}

		deliverPkt := EncodePacket(&Header{
			Magic:   IPCMagic,
			Version: IPCVersion,
			Type:    IPCTypeTalkReq,
			Seq:     hdr.Seq,
		}, payload)
		delivered := fanout(targetList, deliverPkt)

		atomic.AddInt64(&s.stats.TotalTalkReqs, 1)

		if s.verbose > 0 {
			log.Printf("[ipc.svc] TALK_REQ: %s -> %s (sig=%d, urgent=%d, %d sessions notified)",
				talk.FromUserIDString(), talk.ToUserIDString(), talk.Sig, talk.IsUrgent, delivered)
		}

		return client.SendAck(hdr.Seq, 0, "delivered")

	case IPCTypeChatMsg:
		chat, err := DecodeChatPayload(payload)
		if err != nil {
			_ = client.SendAck(hdr.Seq, -1, "invalid chat payload")
			return err
		}
		atomic.AddInt64(&s.stats.TotalChatMsgs, 1)
		if s.verbose > 0 {
			log.Printf("[ipc.svc] CHAT_MSG: room=%s user=%s chatid=%s text=%s",
				chat.RoomString(), chat.UserIDString(), chat.ChatIDString(), chat.TextString())
		}
		return client.SendAck(hdr.Seq, 0, "sent")

	case IPCTypeSub:
		ps, err := DecodePubSubPayload(payload)
		if err != nil {
			_ = client.SendAck(hdr.Seq, -1, "invalid pubsub payload")
			return err
		}
		key := ps.Key()
		if key.Bid == 0 || key.Aid == 0 {
			return client.SendAck(hdr.Seq, -1, "invalid bid or aid")
		}

		s.mu.Lock()
		if client.topics == nil {
			client.topics = make(map[TopicKey]struct{})
		}
		client.topics[key] = struct{}{}
		if s.topics[key] == nil {
			s.topics[key] = make(map[*Client]struct{})
		}
		s.topics[key][client] = struct{}{}
		count := len(s.topics[key])
		s.mu.Unlock()

		if s.verbose > 0 {
			log.Printf("[ipc.svc] SUB: user=%s topic=%s (total subs=%d)", client.displayUserid, key, count)
		}
		return client.SendAck(hdr.Seq, 0, "subscribed")

	case IPCTypeUnsub:
		ps, err := DecodePubSubPayload(payload)
		if err != nil {
			_ = client.SendAck(hdr.Seq, -1, "invalid pubsub payload")
			return err
		}
		key := ps.Key()

		s.mu.Lock()
		delete(client.topics, key)
		if subMap, ok := s.topics[key]; ok {
			delete(subMap, client)
			if len(subMap) == 0 {
				delete(s.topics, key)
			}
		}
		s.mu.Unlock()

		if s.verbose > 0 {
			log.Printf("[ipc.svc] UNSUB: user=%s topic=%s", client.displayUserid, key)
		}
		// seq == 0 means fire-and-forget (e.g. leaving a live post): skip
		// the ACK so the client never has to wait for or drain it.
		if hdr.Seq == 0 {
			return nil
		}
		return client.SendAck(hdr.Seq, 0, "unsubscribed")

	case IPCTypePub:
		ps, err := DecodePubSubPayload(payload)
		if err != nil {
			if hdr.Seq != 0 {
				_ = client.SendAck(hdr.Seq, -1, "invalid pubsub payload")
			}
			return err
		}
		key := ps.Key()

		s.mu.RLock()
		subMap := s.topics[key]
		subs := make([]*Client, 0, len(subMap))
		for sub := range subMap {
			subs = append(subs, sub)
		}
		s.mu.RUnlock()

		// Encode once and share the same immutable packet with every
		// subscriber: a hot live post may have thousands of them.
		deliverPkt := EncodePacket(&Header{
			Magic:   IPCMagic,
			Version: IPCVersion,
			Type:    IPCTypeLiveComment,
			Seq:     hdr.Seq,
		}, ps.Encode())
		sentCount := fanout(subs, deliverPkt)

		atomic.AddInt64(&s.stats.TotalLiveComments, 1)
		atomic.AddInt64(&s.stats.TotalPublishes, 1)

		if s.verbose > 0 {
			log.Printf("[ipc.svc] LIVE_COMMENT: topic=%s (%d subscribers notified): %s",
				key, sentCount, ps.TextString())
		}

		if hdr.Seq != 0 {
			return client.SendAck(hdr.Seq, 0, "published")
		}
		return nil

	case IPCTypeSysBroadcast:
		s.mu.RLock()
		var allClients []*Client
		for _, clist := range s.byUser {
			allClients = append(allClients, clist...)
		}
		s.mu.RUnlock()

		deliverPkt := EncodePacket(&Header{
			Magic:   IPCMagic,
			Version: IPCVersion,
			Type:    IPCTypeSysBroadcast,
			Seq:     hdr.Seq,
		}, payload)
		fanout(allClients, deliverPkt)
		return client.SendAck(hdr.Seq, 0, "broadcasted")

	default:
		log.Printf("[ipc.svc] Unknown command type: 0x%04x", hdr.Type)
		return client.SendAck(hdr.Seq, -1, "unknown command")
	}
}

func (s *Service) removeClient(c *Client) {
	s.mu.Lock()
	defer s.mu.Unlock()

	delete(s.clients, c)

	if c.registered {
		if clist, ok := s.byUser[c.userid]; ok {
			var updated []*Client
			for _, item := range clist {
				if item != c {
					updated = append(updated, item)
				}
			}
			if len(updated) > 0 {
				s.byUser[c.userid] = updated
			} else {
				delete(s.byUser, c.userid)
			}
		}
		if c.pid > 0 && s.byPid[c.pid] == c {
			delete(s.byPid, c.pid)
		}
	}

	for key := range c.topics {
		if subMap, ok := s.topics[key]; ok {
			delete(subMap, c)
			if len(subMap) == 0 {
				delete(s.topics, key)
			}
		}
	}
	c.topics = make(map[TopicKey]struct{})
}

func (s *Service) GetStats() (onlineUsers int, activeTopics int, wmsgSent int64, talkReqs int64, liveComments int64, chatRooms int, chatUsers int) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	cRooms, cUsers := 0, 0
	if s.xchatServer != nil {
		cRooms, cUsers = s.xchatServer.GetStats()
	}

	return len(s.byUser), len(s.topics), atomic.LoadInt64(&s.stats.TotalWmsgSent), atomic.LoadInt64(&s.stats.TotalTalkReqs), atomic.LoadInt64(&s.stats.TotalLiveComments), cRooms, cUsers
}

func (s *Service) GetChessStats() (activeGames int, totalMoves int64) {
	if s.chessServer != nil {
		return s.chessServer.GetStats()
	}
	return 0, 0
}

func (s *Service) StatsPayload() *StatsPayload {
	s.mu.RLock()
	defer s.mu.RUnlock()

	activeChess := 0
	if s.chessServer != nil {
		activeChess, _ = s.chessServer.GetStats()
	}
	activeXChat := 0
	if s.xchatServer != nil {
		_, activeXChat = s.xchatServer.GetStats()
	}

	uptime := uint32(0)
	if !s.startTime.IsZero() {
		uptime = uint32(time.Since(s.startTime).Seconds())
	}

	activeClients := atomic.LoadInt64(&s.stats.ActiveClients)
	if activeClients < 0 {
		activeClients = 0
	}

	return &StatsPayload{
		ActiveClients:   uint32(activeClients),
		RegisteredUsers: uint32(len(s.byUser)),
		ActiveSessions:  uint32(len(s.byPid)),
		ActiveTopics:    uint32(len(s.topics)),
		ActiveChess:     uint32(activeChess),
		ActiveXChat:     uint32(activeXChat),
		TotalWmsg:       uint64(atomic.LoadInt64(&s.stats.TotalWmsgSent)),
		TotalTalk:       uint64(atomic.LoadInt64(&s.stats.TotalTalkReqs)),
		TotalLive:       uint64(atomic.LoadInt64(&s.stats.TotalLiveComments)),
		UptimeSec:       uptime,
	}
}
