package daemon

import (
	"bufio"
	"bytes"
	"io"
	"net"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"
)

func TestIPCServerWmsgPubSubAndTalk(t *testing.T) {
	tempDir := t.TempDir()
	sockPath := filepath.Join(tempDir, "ipc.svc.sock")

	svc, err := NewService(sockPath, WithVerbose(1))
	if err != nil {
		t.Fatalf("NewService failed: %v", err)
	}
	if err := svc.Start(); err != nil {
		t.Fatalf("Start failed: %v", err)
	}
	defer svc.Stop()

	// Verify Magic constant 'IPCV'
	if IPCMagic != 0x49504356 {
		t.Fatalf("IPCMagic must be 0x49504356 ('IPCV'), got 0x%08x", IPCMagic)
	}

	// 1. Connect Client A (Alice)
	connA, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatalf("Dial A failed: %v", err)
	}
	defer connA.Close()

	regA := &RegisterPayload{PID: 1001, USlot: 1}
	stringToCstr("Alice", regA.UserID[:])
	regAPkt := append((&Header{Magic: IPCMagic, Version: IPCVersion, Type: IPCTypeRegister, Seq: 1, Len: uint32(len(regA.Encode()))}).Encode(), regA.Encode()...)
	if _, err := connA.Write(regAPkt); err != nil {
		t.Fatalf("Write regA failed: %v", err)
	}

	hdrA, err := ReadHeader(connA)
	if err != nil || hdrA.Type != IPCTypeAck {
		t.Fatalf("Read regA ack failed: %v, hdr=%+v", err, hdrA)
	}
	ackABuf := make([]byte, hdrA.Len)
	if _, err := connA.Read(ackABuf); err != nil {
		t.Fatalf("Read regA ack payload failed: %v", err)
	}

	// 2. Connect Client B (Bob)
	connB, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatalf("Dial B failed: %v", err)
	}
	defer connB.Close()

	regB := &RegisterPayload{PID: 1002, USlot: 2}
	stringToCstr("Bob", regB.UserID[:])
	regBPkt := append((&Header{Magic: IPCMagic, Version: IPCVersion, Type: IPCTypeRegister, Seq: 2, Len: uint32(len(regB.Encode()))}).Encode(), regB.Encode()...)
	if _, err := connB.Write(regBPkt); err != nil {
		t.Fatalf("Write regB failed: %v", err)
	}
	hdrB, err := ReadHeader(connB)
	if err != nil || hdrB.Type != IPCTypeAck {
		t.Fatalf("Read regB ack failed: %v", err)
	}
	ackBBuf := make([]byte, hdrB.Len)
	if _, err := connB.Read(ackBBuf); err != nil {
		t.Fatalf("Read regB ack payload failed: %v", err)
	}

	// 3. Alice sends Waterball to Bob
	wmsg := &WmsgPayload{
		FromPID:   1001,
		FromUSlot: 1,
		MsgMode:   0,
		Timestamp: uint32(time.Now().Unix()),
	}
	stringToCstr("Alice", wmsg.FromUserID[:])
	stringToCstr("Bob", wmsg.ToUserID[:])
	stringToCstr("Hello Bob from Alice!", wmsg.Text[:])

	wmsgEncoded := wmsg.Encode()
	wmsgPkt := append((&Header{Magic: IPCMagic, Version: IPCVersion, Type: IPCTypeWmsgSend, Seq: 10, Len: uint32(len(wmsgEncoded))}).Encode(), wmsgEncoded...)
	if _, err := connA.Write(wmsgPkt); err != nil {
		t.Fatalf("Write wmsg failed: %v", err)
	}

	// Alice receives ACK
	hdrAck, err := ReadHeader(connA)
	if err != nil || hdrAck.Type != IPCTypeAck {
		t.Fatalf("Read wmsg ack failed: %v, hdr=%+v", err, hdrAck)
	}
	if hdrAck.Len > 0 {
		ackBuf := make([]byte, hdrAck.Len)
		if _, err := io.ReadFull(connA, ackBuf); err != nil {
			t.Fatalf("Read wmsg ack payload failed: %v", err)
		}
	}

	// Bob receives WMSG_DELIVER
	hdrDeliv, err := ReadHeader(connB)
	if err != nil || hdrDeliv.Type != IPCTypeWmsgDeliver {
		t.Fatalf("Bob read wmsg deliver failed: %v, hdr=%+v", err, hdrDeliv)
	}
	delivPayload := make([]byte, hdrDeliv.Len)
	if _, err := io.ReadFull(connB, delivPayload); err != nil {
		t.Fatalf("Bob read wmsg payload failed: %v", err)
	}
	receivedWmsg, err := DecodeWmsgPayload(delivPayload)
	if err != nil {
		t.Fatalf("Decode delivered wmsg failed: %v", err)
	}
	if receivedWmsg.FromUserIDString() != "Alice" || receivedWmsg.TextString() != "Hello Bob from Alice!" {
		t.Fatalf("Delivered wmsg mismatch: from=%s, text=%s", receivedWmsg.FromUserIDString(), receivedWmsg.TextString())
	}

	// 4. Test Talk Request via ipc.svc: Alice pages Bob
	talk := &TalkPayload{
		FromPID:   1001,
		FromUSlot: 1,
		Sig:       1, // SIG_TALK
		IsUrgent:  0,
	}
	stringToCstr("Bob", talk.ToUserID[:])
	stringToCstr("Alice", talk.FromUserID[:])

	talkEncoded := talk.Encode()
	talkPkt := append((&Header{Magic: IPCMagic, Version: IPCVersion, Type: IPCTypeTalkReq, Seq: 15, Len: uint32(len(talkEncoded))}).Encode(), talkEncoded...)
	if _, err := connA.Write(talkPkt); err != nil {
		t.Fatalf("Write talk req failed: %v", err)
	}

	// Alice receives ACK for Talk
	hdrTalkAck, err := ReadHeader(connA)
	if err != nil || hdrTalkAck.Type != IPCTypeAck {
		t.Fatalf("Read talk ack failed: %v", err)
	}
	if hdrTalkAck.Len > 0 {
		buf := make([]byte, hdrTalkAck.Len)
		_, _ = io.ReadFull(connA, buf)
	}

	// Bob receives Talk Request via IPC!
	hdrTalkBob, err := ReadHeader(connB)
	if err != nil || hdrTalkBob.Type != IPCTypeTalkReq {
		t.Fatalf("Bob read talk req failed: %v, hdr=%+v", err, hdrTalkBob)
	}
	talkBobPayload := make([]byte, hdrTalkBob.Len)
	if _, err := io.ReadFull(connB, talkBobPayload); err != nil {
		t.Fatalf("Bob read talk payload failed: %v", err)
	}
	receivedTalk, err := DecodeTalkPayload(talkBobPayload)
	if err != nil {
		t.Fatalf("Decode delivered talk req failed: %v", err)
	}
	if receivedTalk.FromUserIDString() != "Alice" || receivedTalk.Sig != 1 || receivedTalk.FromUSlot != 1 {
		t.Fatalf("Delivered talk mismatch: from=%s, sig=%d, uslot=%d", receivedTalk.FromUserIDString(), receivedTalk.Sig, receivedTalk.FromUSlot)
	}

	// 5. Bob subscribes to Live post topic
	sub := &PubSubPayload{Bid: 1, Aid: 0x1730000000}
	subEncoded := sub.Encode()
	subPkt := append((&Header{Magic: IPCMagic, Version: IPCVersion, Type: IPCTypeSub, Seq: 20, Len: uint32(len(subEncoded))}).Encode(), subEncoded...)
	if _, err := connB.Write(subPkt); err != nil {
		t.Fatalf("Write sub failed: %v", err)
	}
	hdrSubAck, err := ReadHeader(connB)
	if err != nil || hdrSubAck.Type != IPCTypeAck {
		t.Fatalf("Read sub ack failed: %v, hdr=%+v", err, hdrSubAck)
	}
	if hdrSubAck.Len > 0 {
		subAckBuf := make([]byte, hdrSubAck.Len)
		if _, err := io.ReadFull(connB, subAckBuf); err != nil {
			t.Fatalf("Read sub ack payload failed: %v", err)
		}
	}

	// 6. Alice publishes Live Comment to topic
	pub := &PubSubPayload{Bid: 1, Aid: 0x1730000000, Timestamp: uint32(time.Now().Unix())}
	stringToCstr("推 hungte: 真 Live 文太棒了！", pub.Text[:])
	pubEncoded := pub.Encode()
	pubPkt := append((&Header{Magic: IPCMagic, Version: IPCVersion, Type: IPCTypePub, Seq: 30, Len: uint32(len(pubEncoded))}).Encode(), pubEncoded...)
	if _, err := connA.Write(pubPkt); err != nil {
		t.Fatalf("Write pub failed: %v", err)
	}
	hdrPubAck, err := ReadHeader(connA)
	if err != nil || hdrPubAck.Type != IPCTypeAck {
		t.Fatalf("Read pub ack failed: %v, hdr=%+v", err, hdrPubAck)
	}
	if hdrPubAck.Len > 0 {
		pubAckBuf := make([]byte, hdrPubAck.Len)
		if _, err := io.ReadFull(connA, pubAckBuf); err != nil {
			t.Fatalf("Read pub ack payload failed: %v", err)
		}
	}

	// Bob receives LIVE_COMMENT
	hdrLive, err := ReadHeader(connB)
	if err != nil || hdrLive.Type != IPCTypeLiveComment {
		t.Fatalf("Bob read live comment failed: %v, hdr=%+v", err, hdrLive)
	}
	livePayload := make([]byte, hdrLive.Len)
	if _, err := io.ReadFull(connB, livePayload); err != nil {
		t.Fatalf("Bob read live payload failed: %v", err)
	}
	receivedLive, err := DecodePubSubPayload(livePayload)
	if err != nil {
		t.Fatalf("Decode live payload failed: %v", err)
	}
	if receivedLive.Bid != 1 || receivedLive.Aid != 0x1730000000 || receivedLive.TextString() != "推 hungte: 真 Live 文太棒了！" {
		t.Fatalf("Delivered live comment mismatch: bid=%d, aid=%d, text=%s", receivedLive.Bid, receivedLive.Aid, receivedLive.TextString())
	}
}

func TestXChatServerIntegration(t *testing.T) {
	tempDir := t.TempDir()
	sockPath := filepath.Join(tempDir, "ipc.svc.sock")
	xchatSock := filepath.Join(tempDir, "xchat.ipc.sock")

	// Start a mock backend xchatd TCP server
	mockBackend, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("Failed to listen on mock backend: %v", err)
	}
	defer mockBackend.Close()
	mockAddr := mockBackend.Addr().String()

	svc, err := NewService(sockPath, WithVerbose(1))
	if err != nil {
		t.Fatalf("NewService failed: %v", err)
	}
	svc.xchatServer.SetBackendAddr(mockAddr)
	if err := svc.Start(); err != nil {
		t.Fatalf("Start failed: %v", err)
	}
	defer svc.Stop()

	// Mock backend handler: on /! return "OK\0", and broadcast other messages to all other connections
	var (
		mockConnsMu sync.Mutex
		mockConns   []net.Conn
	)
	go func() {
		for {
			conn, err := mockBackend.Accept()
			if err != nil {
				return
			}
			mockConnsMu.Lock()
			mockConns = append(mockConns, conn)
			mockConnsMu.Unlock()

			go func(c net.Conn) {
				reader := bufio.NewReader(c)
				for {
					line, err := reader.ReadBytes('\n')
					if err != nil {
						mockConnsMu.Lock()
						for i, item := range mockConns {
							if item == c {
								mockConns = append(mockConns[:i], mockConns[i+1:]...)
								break
							}
						}
						mockConnsMu.Unlock()
						return
					}
					if bytes.HasPrefix(line, []byte("/!")) {
						_, _ = c.Write([]byte("OK\x00"))
					} else if bytes.HasPrefix(line, []byte("/m ")) {
						mockConnsMu.Lock()
						for _, other := range mockConns {
							if other != c {
								_, _ = other.Write([]byte("SecretMessage\x00"))
							}
						}
						mockConnsMu.Unlock()
					} else {
						mockConnsMu.Lock()
						for _, other := range mockConns {
							if other != c {
								_, _ = other.Write(append(bytes.TrimRight(line, "\r\n"), 0))
							}
						}
						mockConnsMu.Unlock()
					}
				}
			}(conn)
		}
	}()

	// 1. Connect User 1 (Alice) to XChat UNIX socket
	connAlice, err := net.Dial("unix", xchatSock)
	if err != nil {
		t.Fatalf("Dial xchat Alice failed: %v", err)
	}
	defer connAlice.Close()

	// Alice handshake: /! UserID ChatID Token\n
	_, err = connAlice.Write([]byte("/! alice AliceNick 123456\n"))
	if err != nil {
		t.Fatalf("Alice handshake write failed: %v", err)
	}

	// Alice should receive 3-byte "OK\0"
	loginResp := make([]byte, 3)
	if _, err := io.ReadFull(connAlice, loginResp); err != nil {
		t.Fatalf("Alice read login resp failed: %v", err)
	}
	if !bytes.Equal(loginResp, []byte("OK\x00")) {
		t.Fatalf("Expected 'OK\\0', got %q", loginResp)
	}

	// 2. Connect User 2 (Bob) to XChat UNIX socket
	connBob, err := net.Dial("unix", xchatSock)
	if err != nil {
		t.Fatalf("Dial xchat Bob failed: %v", err)
	}
	defer connBob.Close()

	_, err = connBob.Write([]byte("/! bob BobNick 654321\n"))
	if err != nil {
		t.Fatalf("Bob handshake write failed: %v", err)
	}

	loginRespBob := make([]byte, 3)
	if _, err := io.ReadFull(connBob, loginRespBob); err != nil {
		t.Fatalf("Bob read login resp failed: %v", err)
	}
	if !bytes.Equal(loginRespBob, []byte("OK\x00")) {
		t.Fatalf("Bob expected 'OK\\0', got %q", loginRespBob)
	}

	// 3. Alice sends chat message to room
	_, err = connAlice.Write([]byte("Hello XChat everyone!\n"))
	if err != nil {
		t.Fatalf("Alice write chat failed: %v", err)
	}

	// Bob reads messages until he sees Alice's chat
	time.Sleep(50 * time.Millisecond)
	foundMsg := false
	bobReader := bufio.NewReader(connBob)
	for i := 0; i < 10; i++ {
		_ = connBob.SetReadDeadline(time.Now().Add(500 * time.Millisecond))
		s, err := bobReader.ReadString(0)
		if err != nil {
			break
		}
		clean := strings.TrimRight(s, "\x00")
		if strings.Contains(clean, "Hello XChat everyone!") {
			foundMsg = true
			break
		}
	}
	if !foundMsg {
		t.Fatalf("Bob did not receive Alice's chat message")
	}

	// 4. Bob whispers to Alice: /m AliceNick SecretMessage
	_, err = connBob.Write([]byte("/m AliceNick SecretMessage\n"))
	if err != nil {
		t.Fatalf("Bob whisper failed: %v", err)
	}

	// Alice should receive whisper
	aliceReader := bufio.NewReader(connAlice)
	foundWhisper := false
	for i := 0; i < 10; i++ {
		_ = connAlice.SetReadDeadline(time.Now().Add(500 * time.Millisecond))
		s, err := aliceReader.ReadString(0)
		if err != nil {
			break
		}
		clean := strings.TrimRight(s, "\x00")
		if strings.Contains(clean, "SecretMessage") {
			foundWhisper = true
			break
		}
	}
	if !foundWhisper {
		t.Fatalf("Alice did not receive Bob's whisper")
	}
}

func TestIPCChessSession(t *testing.T) {
	tempDir := t.TempDir()
	sockPath := filepath.Join(tempDir, "ipc.svc.sock")
	chessSockPath := filepath.Join(tempDir, "chess.ipc.sock")

	svc, err := NewService(sockPath, WithVerbose(1))
	if err != nil {
		t.Fatalf("NewService failed: %v", err)
	}
	if err := svc.Start(); err != nil {
		t.Fatalf("Start failed: %v", err)
	}
	defer svc.Stop()

	// 1. Caller (Alice) connects to chess socket and creates a Gomoku session with Bob
	connAlice, err := net.Dial("unix", chessSockPath)
	if err != nil {
		t.Fatalf("Dial chess sock failed for Alice: %v", err)
	}
	defer connAlice.Close()

	if _, err := connAlice.Write([]byte("CREATE Alice Bob 3\n")); err != nil {
		t.Fatalf("Alice write CREATE failed: %v", err)
	}
	bufCreate := make([]byte, 3)
	if _, err := io.ReadFull(connAlice, bufCreate); err != nil {
		t.Fatalf("Alice read CREATE ack failed: %v", err)
	}
	if string(bufCreate) != "OK\n" {
		t.Fatalf("Alice expected 'OK\\n', got %q", bufCreate)
	}

	// 2. Callee (Bob) joins the session
	connBob, err := net.Dial("unix", chessSockPath)
	if err != nil {
		t.Fatalf("Dial chess sock failed for Bob: %v", err)
	}
	defer connBob.Close()

	if _, err := connBob.Write([]byte("JOIN Bob Alice\n")); err != nil {
		t.Fatalf("Bob write JOIN failed: %v", err)
	}

	// 3. Alice receives "READY\n", Bob receives "OK\n"
	bufAlice := make([]byte, 6)
	if _, err := io.ReadFull(connAlice, bufAlice); err != nil {
		t.Fatalf("Alice read confirmation failed: %v", err)
	}
	if string(bufAlice) != "READY\n" {
		t.Fatalf("Alice expected 'READY\\n', got %q", bufAlice)
	}

	bufBob := make([]byte, 3)
	if _, err := io.ReadFull(connBob, bufBob); err != nil {
		t.Fatalf("Bob read confirmation failed: %v", err)
	}
	if string(bufBob) != "OK\n" {
		t.Fatalf("Bob expected 'OK\\n', got %q", bufBob)
	}

	// 4. Test two-way game move / packet forwarding
	// Handshake: Alice sends 'T' (traditional mode)
	if _, err := connAlice.Write([]byte("T")); err != nil {
		t.Fatalf("Alice write mode failed: %v", err)
	}
	modeBuf := make([]byte, 1)
	if _, err := io.ReadFull(connBob, modeBuf); err != nil {
		t.Fatalf("Bob read mode failed: %v", err)
	}
	if modeBuf[0] != 'T' {
		t.Fatalf("Bob expected 'T', got %c", modeBuf[0])
	}

	// Bob accepts with 'y'
	if _, err := connBob.Write([]byte("y")); err != nil {
		t.Fatalf("Bob write accept failed: %v", err)
	}
	ansBuf := make([]byte, 1)
	if _, err := io.ReadFull(connAlice, ansBuf); err != nil {
		t.Fatalf("Alice read accept failed: %v", err)
	}
	if ansBuf[0] != 'y' {
		t.Fatalf("Alice expected 'y', got %c", ansBuf[0])
	}

	// Moves: Alice plays (7, 7)
	aliceMove := []byte{7, 7, 1}
	if _, err := connAlice.Write(aliceMove); err != nil {
		t.Fatalf("Alice write move failed: %v", err)
	}
	bobRecvMove := make([]byte, 3)
	if _, err := io.ReadFull(connBob, bobRecvMove); err != nil {
		t.Fatalf("Bob read move failed: %v", err)
	}
	if !bytes.Equal(bobRecvMove, aliceMove) {
		t.Fatalf("Bob received move %v, expected %v", bobRecvMove, aliceMove)
	}

	// Bob plays (8, 8)
	bobMove := []byte{8, 8, 2}
	if _, err := connBob.Write(bobMove); err != nil {
		t.Fatalf("Bob write move failed: %v", err)
	}
	aliceRecvMove := make([]byte, 3)
	if _, err := io.ReadFull(connAlice, aliceRecvMove); err != nil {
		t.Fatalf("Alice read move failed: %v", err)
	}
	if !bytes.Equal(aliceRecvMove, bobMove) {
		t.Fatalf("Alice received move %v, expected %v", aliceRecvMove, bobMove)
	}

	// Check active stats
	activeGames, totalMoves := svc.GetChessStats()
	if activeGames != 1 {
		t.Fatalf("Expected 1 active chess game, got %d", activeGames)
	}
	if totalMoves < 4 {
		t.Fatalf("Expected at least 4 moves/packets, got %d", totalMoves)
	}

	// 5. Alice leaves
	connAlice.Close()

	// Bob should see EOF
	eofBuf := make([]byte, 1)
	_ = connBob.SetReadDeadline(time.Now().Add(500 * time.Millisecond))
	n, _ := connBob.Read(eofBuf)
	if n != 0 {
		t.Fatalf("Expected EOF on Bob's socket after Alice closed, got n=%d", n)
	}
}

func TestIPCChessCallerDisconnectsDuringHandshake(t *testing.T) {
	tempDir := t.TempDir()
	sockPath := filepath.Join(tempDir, "ipc.svc.sock")
	chessSockPath := filepath.Join(tempDir, "chess.ipc.sock")

	svc, err := NewService(sockPath, WithVerbose(1))
	if err != nil {
		t.Fatalf("NewService failed: %v", err)
	}
	if err := svc.Start(); err != nil {
		t.Fatalf("Start failed: %v", err)
	}
	defer svc.Stop()

	// 1. Caller (Alice) connects to chess socket and creates a session
	connAlice, err := net.Dial("unix", chessSockPath)
	if err != nil {
		t.Fatalf("Dial chess sock failed for Alice: %v", err)
	}
	defer connAlice.Close()

	if _, err := connAlice.Write([]byte("CREATE Alice Bob 3\n")); err != nil {
		t.Fatalf("Alice write CREATE failed: %v", err)
	}
	bufCreate := make([]byte, 3)
	if _, err := io.ReadFull(connAlice, bufCreate); err != nil || string(bufCreate) != "OK\n" {
		t.Fatalf("Alice CREATE failed: %v", err)
	}

	// 2. Callee (Bob) joins
	connBob, err := net.Dial("unix", chessSockPath)
	if err != nil {
		t.Fatalf("Dial chess sock failed for Bob: %v", err)
	}
	defer connBob.Close()

	if _, err := connBob.Write([]byte("JOIN Bob Alice\n")); err != nil {
		t.Fatalf("Bob write JOIN failed: %v", err)
	}

	// 3. Confirmations
	bufAlice := make([]byte, 6)
	if _, err := io.ReadFull(connAlice, bufAlice); err != nil || string(bufAlice) != "READY\n" {
		t.Fatalf("Alice READY failed: %v", err)
	}
	bufBob := make([]byte, 3)
	if _, err := io.ReadFull(connBob, bufBob); err != nil || string(bufBob) != "OK\n" {
		t.Fatalf("Bob OK failed: %v", err)
	}

	// 4. Alice closes connection immediately (Ctrl-D before mode exchange)
	_ = connAlice.Close()

	// Bob must see EOF immediately without blocking
	eofBuf := make([]byte, 1)
	_ = connBob.SetReadDeadline(time.Now().Add(500 * time.Millisecond))
	n, err := connBob.Read(eofBuf)
	if n != 0 && err != io.EOF {
		t.Fatalf("Expected immediate EOF on Bob's socket, got n=%d, err=%v", n, err)
	}
}

func TestIPCChessCallerDisconnectsBeforeJoin(t *testing.T) {
	tempDir := t.TempDir()
	sockPath := filepath.Join(tempDir, "ipc.svc.sock")
	chessSockPath := filepath.Join(tempDir, "chess.ipc.sock")

	svc, err := NewService(sockPath, WithVerbose(1))
	if err != nil {
		t.Fatalf("NewService failed: %v", err)
	}
	if err := svc.Start(); err != nil {
		t.Fatalf("Start failed: %v", err)
	}
	defer svc.Stop()

	// 1. Caller (Alice) connects and creates session
	connAlice, err := net.Dial("unix", chessSockPath)
	if err != nil {
		t.Fatalf("Dial chess sock failed for Alice: %v", err)
	}
	if _, err := connAlice.Write([]byte("CREATE Alice Bob 3\n")); err != nil {
		t.Fatalf("Alice write CREATE failed: %v", err)
	}
	bufCreate := make([]byte, 3)
	if _, err := io.ReadFull(connAlice, bufCreate); err != nil || string(bufCreate) != "OK\n" {
		t.Fatalf("Alice CREATE failed: %v", err)
	}

	// 2. Alice closes/disconnects BEFORE Bob joins (e.g. hits Ctrl-D)
	_ = connAlice.Close()

	// 3. Bob attempts to join
	connBob, err := net.Dial("unix", chessSockPath)
	if err != nil {
		t.Fatalf("Dial chess sock failed for Bob: %v", err)
	}
	defer connBob.Close()

	if _, err := connBob.Write([]byte("JOIN Bob Alice\n")); err != nil {
		t.Fatalf("Bob write JOIN failed: %v", err)
	}

	// Bob should receive error (caller_disconnected or no_session) and connection closed
	_ = connBob.SetReadDeadline(time.Now().Add(500 * time.Millisecond))
	respBuf := make([]byte, 64)
	n, _ := connBob.Read(respBuf)
	resp := string(respBuf[:n])
	if !strings.HasPrefix(resp, "ERR") {
		t.Fatalf("Expected ERR response for Bob, got %q", resp)
	}
}

func TestIPCSlowSubscriberNonBlocking(t *testing.T) {
	tempDir := t.TempDir()
	sockPath := filepath.Join(tempDir, "ipc.svc.sock")

	svc, err := NewService(sockPath)
	if err != nil {
		t.Fatalf("NewService failed: %v", err)
	}
	if err := svc.Start(); err != nil {
		t.Fatalf("Service Start failed: %v", err)
	}
	defer svc.Stop()

	// Connect Alice (publisher)
	connPub, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatalf("Dial pub failed: %v", err)
	}
	defer connPub.Close()

	// Connect Bob (fast subscriber)
	connFast, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatalf("Dial fast sub failed: %v", err)
	}
	defer connFast.Close()

	// Connect Charlie (slow subscriber that does not read)
	connSlow, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatalf("Dial slow sub failed: %v", err)
	}
	defer connSlow.Close()

	subPayload := &PubSubPayload{Bid: 99, Aid: 0x999999}
	subEncoded := subPayload.Encode()
	subPkt := append((&Header{Magic: IPCMagic, Version: IPCVersion, Type: IPCTypeSub, Seq: 1, Len: uint32(len(subEncoded))}).Encode(), subEncoded...)

	// Subscribe Fast
	if _, err := connFast.Write(subPkt); err != nil {
		t.Fatalf("Fast sub write failed: %v", err)
	}
	hdrFast, _ := ReadHeader(connFast)
	if hdrFast.Len > 0 {
		buf := make([]byte, hdrFast.Len)
		_, _ = io.ReadFull(connFast, buf)
	}

	// Subscribe Slow
	if _, err := connSlow.Write(subPkt); err != nil {
		t.Fatalf("Slow sub write failed: %v", err)
	}
	hdrSlow, _ := ReadHeader(connSlow)
	if hdrSlow.Len > 0 {
		buf := make([]byte, hdrSlow.Len)
		_, _ = io.ReadFull(connSlow, buf)
	}

	// Now publish multiple comments. Slow client does NOT read.
	// Publishing must not hang or be slowed down.
	pubPayload := &PubSubPayload{Bid: 99, Aid: 0x999999}
	stringToCstr("推 test message", pubPayload.Text[:])
	pubEncoded := pubPayload.Encode()

	start := time.Now()
	numMsgs := 300
	for i := 0; i < numMsgs; i++ {
		pubPkt := append((&Header{Magic: IPCMagic, Version: IPCVersion, Type: IPCTypePub, Seq: uint32(100 + i), Len: uint32(len(pubEncoded))}).Encode(), pubEncoded...)
		if _, err := connPub.Write(pubPkt); err != nil {
			t.Fatalf("Publish write failed at %d: %v", i, err)
		}
		// Read ACK
		hdrAck, err := ReadHeader(connPub)
		if err != nil || hdrAck.Type != IPCTypeAck {
			t.Fatalf("Read ack failed at %d: %v", i, err)
		}
		if hdrAck.Len > 0 {
			buf := make([]byte, hdrAck.Len)
			_, _ = io.ReadFull(connPub, buf)
		}
	}
	elapsed := time.Since(start)

	// Publishing 300 messages to 2 subscribers should take well under 1 second.
	if elapsed > 2*time.Second {
		t.Fatalf("Publishing was too slow (%v), slow subscriber blocked fan-out!", elapsed)
	}

	// Fast client receives messages promptly
	_ = connFast.SetReadDeadline(time.Now().Add(500 * time.Millisecond))
	receivedCount := 0
	for i := 0; i < numMsgs; i++ {
		hdrMsg, err := ReadHeader(connFast)
		if err != nil {
			break
		}
		if hdrMsg.Type == IPCTypeLiveComment {
			receivedCount++
			buf := make([]byte, hdrMsg.Len)
			_, _ = io.ReadFull(connFast, buf)
		}
	}

	if receivedCount < numMsgs {
		t.Fatalf("Fast subscriber received only %d/%d messages", receivedCount, numMsgs)
	}
}

func TestIPCAiduNormalization(t *testing.T) {
	tempDir := t.TempDir()
	sockPath := filepath.Join(tempDir, "ipc.svc.sock")

	svc, err := NewService(sockPath)
	if err != nil {
		t.Fatalf("NewService failed: %v", err)
	}
	if err := svc.Start(); err != nil {
		t.Fatalf("Start failed: %v", err)
	}
	defer svc.Stop()

	connSub, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatalf("Dial failed: %v", err)
	}
	defer connSub.Close()

	connPub, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatalf("Dial failed: %v", err)
	}
	defer connPub.Close()

	// Base AID without index hint
	baseAid := uint64(0x1730000000)
	// AID with a 19-bit index hint (bit 45 shifted)
	aidWithHint := baseAid | (uint64(12345) << 45)

	// Subscribe with aidWithHint
	subPayload := (&PubSubPayload{Bid: 1, Aid: aidWithHint}).Encode()
	subHdr := (&Header{Magic: IPCMagic, Version: IPCVersion, Type: IPCTypeSub, Seq: 1, Len: uint32(len(subPayload))}).Encode()
	_, _ = connSub.Write(append(subHdr, subPayload...))
	ackHdr, _ := ReadHeader(connSub)
	ackBuf := make([]byte, ackHdr.Len)
	_, _ = io.ReadFull(connSub, ackBuf)

	// Publish with baseAid (different upper bits)
	pubPayload := (&PubSubPayload{Bid: 1, Aid: baseAid, Text: [128]byte{'h', 'e', 'l', 'l', 'o'}}).Encode()
	pubHdr := (&Header{Magic: IPCMagic, Version: IPCVersion, Type: IPCTypePub, Seq: 0, Len: uint32(len(pubPayload))}).Encode()
	_, _ = connPub.Write(append(pubHdr, pubPayload...))

	// Subscriber should receive it because AiduRawMask normalized both to baseAid
	_ = connSub.SetReadDeadline(time.Now().Add(500 * time.Millisecond))
	recvHdr, err := ReadHeader(connSub)
	if err != nil {
		t.Fatalf("Failed to read header: %v", err)
	}
	if recvHdr.Type != IPCTypeLiveComment {
		t.Fatalf("Expected IPCTypeLiveComment, got 0x%04x", recvHdr.Type)
	}
	recvPayload := make([]byte, recvHdr.Len)
	if _, err := io.ReadFull(connSub, recvPayload); err != nil {
		t.Fatalf("Failed to read payload: %v", err)
	}
	ps, err := DecodePubSubPayload(recvPayload)
	if err != nil {
		t.Fatalf("Failed to decode payload: %v", err)
	}
	if ps.Aid != baseAid {
		t.Fatalf("Expected normalized aid 0x%x, got 0x%x", baseAid, ps.Aid)
	}
}
