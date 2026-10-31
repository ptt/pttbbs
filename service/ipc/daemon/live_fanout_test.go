package daemon

import (
	"fmt"
	"io"
	"net"
	"path/filepath"
	"sync"
	"testing"
	"time"
)

func startTestService(t *testing.T) (*Service, string) {
	t.Helper()
	sockPath := filepath.Join(t.TempDir(), "ipc.svc.sock")
	svc, err := NewService(sockPath)
	if err != nil {
		t.Fatalf("NewService failed: %v", err)
	}
	if err := svc.Start(); err != nil {
		t.Fatalf("Start failed: %v", err)
	}
	t.Cleanup(svc.Stop)
	return svc, sockPath
}

func readPacket(t *testing.T, conn net.Conn) (*Header, []byte) {
	t.Helper()
	hdr, err := ReadHeader(conn)
	if err != nil {
		t.Fatalf("ReadHeader failed: %v", err)
	}
	payload := make([]byte, hdr.Len)
	if _, err := io.ReadFull(conn, payload); err != nil {
		t.Fatalf("read payload failed: %v", err)
	}
	return hdr, payload
}

func pubsubPacket(typ uint16, seq uint32, ps *PubSubPayload) []byte {
	return EncodePacket(&Header{Magic: IPCMagic, Version: IPCVersion, Type: typ, Seq: seq}, ps.Encode())
}

// TestIPCLiveFanoutManySubscribers simulates a hot live post: many
// subscribers on one topic and a burst of comments. Every subscriber must
// receive every comment intact and in order (batched writes must never
// interleave or corrupt the shared packets).
func TestIPCLiveFanoutManySubscribers(t *testing.T) {
	_, sockPath := startTestService(t)

	const numSubs = 500
	const numMsgs = 100
	topic := &PubSubPayload{Bid: 7, Aid: 0x1234567}

	subs := make([]net.Conn, numSubs)
	for i := range subs {
		c, err := net.Dial("unix", sockPath)
		if err != nil {
			t.Fatalf("Dial %d failed: %v", i, err)
		}
		defer c.Close()
		if _, err := c.Write(pubsubPacket(IPCTypeSub, 1, topic)); err != nil {
			t.Fatalf("sub write failed: %v", err)
		}
		if hdr, _ := readPacket(t, c); hdr.Type != IPCTypeAck {
			t.Fatalf("expected ACK, got 0x%04x", hdr.Type)
		}
		subs[i] = c
	}

	pub, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatalf("Dial pub failed: %v", err)
	}
	defer pub.Close()

	for i := 0; i < numMsgs; i++ {
		ps := &PubSubPayload{Bid: topic.Bid, Aid: topic.Aid}
		stringToCstr(fmt.Sprintf("comment %03d", i), ps.Text[:])
		if _, err := pub.Write(pubsubPacket(IPCTypePub, 0, ps)); err != nil {
			t.Fatalf("pub write failed: %v", err)
		}
	}

	var wg sync.WaitGroup
	errs := make(chan error, numSubs)
	for _, c := range subs {
		wg.Add(1)
		go func(c net.Conn) {
			defer wg.Done()
			_ = c.SetReadDeadline(time.Now().Add(10 * time.Second))
			for i := 0; i < numMsgs; i++ {
				hdr, err := ReadHeader(c)
				if err != nil {
					errs <- fmt.Errorf("msg %d: %v", i, err)
					return
				}
				payload := make([]byte, hdr.Len)
				if _, err := io.ReadFull(c, payload); err != nil {
					errs <- fmt.Errorf("msg %d payload: %v", i, err)
					return
				}
				ps, err := DecodePubSubPayload(payload)
				if err != nil {
					errs <- err
					return
				}
				if want := fmt.Sprintf("comment %03d", i); hdr.Type != IPCTypeLiveComment || ps.TextString() != want {
					errs <- fmt.Errorf("got type 0x%04x %q, want %q", hdr.Type, ps.TextString(), want)
					return
				}
			}
		}(c)
	}
	wg.Wait()
	close(errs)
	for err := range errs {
		t.Fatal(err)
	}
}

// TestIPCUnsubFireAndForget verifies UNSUB with seq 0 is not ACKed and that
// it really removes the subscription.
func TestIPCUnsubFireAndForget(t *testing.T) {
	_, sockPath := startTestService(t)
	topic := &PubSubPayload{Bid: 8, Aid: 0x7654321}

	c, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatalf("Dial failed: %v", err)
	}
	defer c.Close()

	if _, err := c.Write(pubsubPacket(IPCTypeSub, 1, topic)); err != nil {
		t.Fatalf("sub write failed: %v", err)
	}
	if hdr, _ := readPacket(t, c); hdr.Type != IPCTypeAck {
		t.Fatalf("expected ACK, got 0x%04x", hdr.Type)
	}

	if _, err := c.Write(pubsubPacket(IPCTypeUnsub, 0, topic)); err != nil {
		t.Fatalf("unsub write failed: %v", err)
	}
	// Publish on the same connection after the UNSUB (ordered on the stream),
	// then PING: the first packet back must be the PONG.
	ps := &PubSubPayload{Bid: topic.Bid, Aid: topic.Aid}
	stringToCstr("should not be delivered", ps.Text[:])
	if _, err := c.Write(pubsubPacket(IPCTypePub, 0, ps)); err != nil {
		t.Fatalf("pub write failed: %v", err)
	}
	ping := EncodePacket(&Header{Magic: IPCMagic, Version: IPCVersion, Type: IPCTypePing, Seq: 2}, nil)
	if _, err := c.Write(ping); err != nil {
		t.Fatalf("ping write failed: %v", err)
	}
	_ = c.SetReadDeadline(time.Now().Add(2 * time.Second))
	hdr, payload := readPacket(t, c)
	if hdr.Type != IPCTypePong || hdr.Seq != 2 {
		t.Fatalf("expected PONG seq 2, got type 0x%04x seq %d", hdr.Type, hdr.Seq)
	}
	stats, err := DecodeStatsPayload(payload)
	if err != nil {
		t.Fatalf("decode stats: %v", err)
	}
	if stats.ActiveTopics != 0 {
		t.Fatalf("expected no active topics after unsub, got %d", stats.ActiveTopics)
	}
}

// TestIPCPubSubFSize verifies the FSize dedupe token is relayed to
// subscribers, and that older 144-byte PUB payloads (no FSize) still work.
func TestIPCPubSubFSize(t *testing.T) {
	_, sockPath := startTestService(t)
	topic := &PubSubPayload{Bid: 9, Aid: 0x1111111}

	sub, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatalf("Dial sub failed: %v", err)
	}
	defer sub.Close()
	if _, err := sub.Write(pubsubPacket(IPCTypeSub, 1, topic)); err != nil {
		t.Fatalf("sub write failed: %v", err)
	}
	if hdr, _ := readPacket(t, sub); hdr.Type != IPCTypeAck {
		t.Fatalf("expected ACK, got 0x%04x", hdr.Type)
	}

	pub, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatalf("Dial pub failed: %v", err)
	}
	defer pub.Close()

	ps := &PubSubPayload{Bid: topic.Bid, Aid: topic.Aid, FSize: 4242}
	stringToCstr("new", ps.Text[:])
	if _, err := pub.Write(pubsubPacket(IPCTypePub, 0, ps)); err != nil {
		t.Fatalf("pub write failed: %v", err)
	}
	old := ps.Encode()[:PubSubPayloadMinSize]
	stringToCstr("old", old[16:16+IPCTextLen])
	oldPkt := EncodePacket(&Header{Magic: IPCMagic, Version: IPCVersion, Type: IPCTypePub}, old)
	if _, err := pub.Write(oldPkt); err != nil {
		t.Fatalf("old pub write failed: %v", err)
	}

	_ = sub.SetReadDeadline(time.Now().Add(2 * time.Second))
	for _, want := range []struct {
		text  string
		fsize uint64
	}{{"new", 4242}, {"old", 0}} {
		hdr, payload := readPacket(t, sub)
		got, err := DecodePubSubPayload(payload)
		if err != nil || hdr.Type != IPCTypeLiveComment {
			t.Fatalf("bad delivery: type 0x%04x err %v", hdr.Type, err)
		}
		if got.TextString() != want.text || got.FSize != want.fsize {
			t.Fatalf("got %q fsize %d, want %q fsize %d", got.TextString(), got.FSize, want.text, want.fsize)
		}
	}
}
