package daemon

import (
	"encoding/json"
	"net"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestDaemonIPCProtocol(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "aloha_daemon_test_*")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	sockPath := filepath.Join(tempDir, "aloha.svc.sock")

	svc := &Service{
		bbsHome:           tempDir,
		socketPath:        sockPath,
		onlineSessions:    make(map[int]SubscriberSession),
		onlineSubscribers: make(map[string]map[int]SubscriberSession),
	}

	go func() {
		_ = svc.Start()
	}()

	// Wait for socket to be created
	var conn net.Conn
	for i := 0; i < 50; i++ {
		time.Sleep(10 * time.Millisecond)
		conn, err = net.Dial("unix", sockPath)
		if err == nil {
			break
		}
	}
	if err != nil {
		t.Fatalf("Failed to connect to daemon socket: %v", err)
	}
	defer conn.Close()

	// 1. Send status request
	req := Request{Action: "status"}
	if err := json.NewEncoder(conn).Encode(req); err != nil {
		t.Fatalf("Failed to encode request: %v", err)
	}

	var resp Response
	if err := json.NewDecoder(conn).Decode(&resp); err != nil {
		t.Fatalf("Failed to decode response: %v", err)
	}

	if !resp.Success {
		t.Fatalf("Expected status success, got %v", resp)
	}
}

func TestSubscriberNotificationFlow(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "aloha_sub_test_*")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	sockPath := filepath.Join(tempDir, "aloha.svc.sock")

	svc, err := NewService(tempDir, sockPath)
	if err != nil {
		t.Fatalf("Failed to create service: %v", err)
	}
	svc.shmClient = nil

	go func() {
		_ = svc.Start()
	}()

	time.Sleep(20 * time.Millisecond)

	// 1. abc logs in
	resp1 := svc.ProcessRequest(Request{Action: "login", UserID: "abc", PID: 100, SID: 1})
	if !resp1.Success {
		t.Fatalf("abc login failed: %v", resp1)
	}

	// 2. abc adds xyz to abc's aloha list
	resp2 := svc.ProcessRequest(Request{Action: "add", SubID: "abc", TargetID: "xyz"})
	if !resp2.Success {
		t.Fatalf("abc add xyz failed: %v", resp2)
	}

	// 3. xyz logs in -> should notify 1 subscriber (abc)
	resp3 := svc.ProcessRequest(Request{Action: "login", UserID: "xyz", PID: 200, SID: 2})
	if !resp3.Success {
		t.Fatalf("xyz login failed: %v", resp3)
	}

	if resp3.Message != "User xyz logged in, notified 1 subscribers" && resp3.Message != "User xyz logged in, found 1 subscribers" {
		t.Fatalf("Expected 1 subscriber found/notified, got: %s", resp3.Message)
	}
}
