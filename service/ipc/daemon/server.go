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
	ClientWriteTimeout  = 50 * time.Millisecond
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
	for {
		select {
		case <-c.done:
			return
		case pkt := <-c.sendCh:
			_ = c.conn.SetWriteDeadline(time.Now().Add(ClientWriteTimeout))
			if _, err := c.conn.Write(pkt); err != nil {
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
	hdr.Len = uint32(len(payload))
	buf := append(hdr.Encode(), payload...)
	return c.Enqueue(buf)
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
		client.userid = normUID
		client.displayUserid = rawUID
		client.pid = reg.PID
		client.uslot = reg.USlot
		client.registered = true

		s.mu.Lock()
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

		deliverHdr := &Header{
			Magic:   IPCMagic,
			Version: IPCVersion,
			Type:    IPCTypeWmsgDeliver,
			Seq:     hdr.Seq,
			Len:     uint32(len(payload)),
		}

		delivered := 0
		for _, tgt := range targetList {
			if err := tgt.Send(deliverHdr, payload); err == nil {
				delivered++
			}
		}

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

		deliverHdr := &Header{
			Magic:   IPCMagic,
			Version: IPCVersion,
			Type:    IPCTypeTalkReq,
			Seq:     hdr.Seq,
			Len:     uint32(len(payload)),
		}

		delivered := 0
		for _, tgt := range targetList {
			if err := tgt.Send(deliverHdr, payload); err == nil {
				delivered++
			}
		}

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
		return client.SendAck(hdr.Seq, 0, "unsubscribed")

	case IPCTypePub:
		ps, err := DecodePubSubPayload(payload)
		if err != nil {
			_ = client.SendAck(hdr.Seq, -1, "invalid pubsub payload")
			return err
		}
		key := ps.Key()

		s.mu.RLock()
		subMap := s.topics[key]
		var subs []*Client
		for sub := range subMap {
			subs = append(subs, sub)
		}
		s.mu.RUnlock()

		deliverHdr := &Header{
			Magic:   IPCMagic,
			Version: IPCVersion,
			Type:    IPCTypeLiveComment,
			Seq:     hdr.Seq,
			Len:     uint32(len(payload)),
		}

		sentCount := 0
		for _, sub := range subs {
			if err := sub.Send(deliverHdr, payload); err == nil {
				sentCount++
			}
		}

		atomic.AddInt64(&s.stats.TotalLiveComments, 1)
		atomic.AddInt64(&s.stats.TotalPublishes, 1)

		if s.verbose > 0 {
			log.Printf("[ipc.svc] LIVE_COMMENT: topic=%s (%d subscribers notified): %s",
				key, sentCount, ps.TextString())
		}

		return client.SendAck(hdr.Seq, 0, "published")

	case IPCTypeSysBroadcast:
		s.mu.RLock()
		var allClients []*Client
		for _, clist := range s.byUser {
			allClients = append(allClients, clist...)
		}
		s.mu.RUnlock()

		deliverHdr := &Header{
			Magic:   IPCMagic,
			Version: IPCVersion,
			Type:    IPCTypeSysBroadcast,
			Seq:     hdr.Seq,
			Len:     uint32(len(payload)),
		}

		for _, cl := range allClients {
			_ = cl.Send(deliverHdr, payload)
		}
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
	c.topics = nil
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
