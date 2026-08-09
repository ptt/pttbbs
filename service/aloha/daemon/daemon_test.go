package daemon

import (
	"bytes"
	"encoding/json"
	"log"
	"net"
	"os"
	"path/filepath"
	"strings"
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

	svc, err := NewService(tempDir, sockPath)
	if err != nil {
		t.Fatalf("Failed to create service: %v", err)
	}
	svc.shmClient = nil

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

	// 2. Create alohaed file for abc and send reload request
	alohaFileDir := filepath.Join(tempDir, "home", "a", "abc")
	_ = os.MkdirAll(alohaFileDir, 0755)
	_ = os.WriteFile(filepath.Join(alohaFileDir, "alohaed"), []byte("xyz\n"), 0644)

	resp2 := svc.ProcessRequest(Request{Action: "reload", UserID: "abc"})
	if !resp2.Success {
		t.Fatalf("abc reload failed: %v", resp2)
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

func TestDuplicateInstancePrevention(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "aloha_dup_test_*")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	sockPath := filepath.Join(tempDir, "aloha.svc.sock")
	svc1, err := NewService(tempDir, sockPath)
	if err != nil {
		t.Fatalf("Failed to create service 1: %v", err)
	}
	svc1.shmClient = nil

	go func() {
		_ = svc1.Start()
	}()

	// Wait for socket to be active
	var conn net.Conn
	for i := 0; i < 50; i++ {
		time.Sleep(10 * time.Millisecond)
		conn, err = net.Dial("unix", sockPath)
		if err == nil {
			conn.Close()
			break
		}
	}
	if err != nil {
		t.Fatalf("Failed to connect to service 1: %v", err)
	}

	// Try starting instance 2 on the same socket -> should fail!
	svc2, err := NewService(tempDir, sockPath)
	if err != nil {
		t.Fatalf("Failed to create service 2: %v", err)
	}
	svc2.shmClient = nil

	err2 := svc2.Start()
	if err2 == nil {
		t.Fatalf("Expected service 2 Start() to fail, but it succeeded")
	}

	if !strings.Contains(err2.Error(), "already running") {
		t.Fatalf("Expected error to contain \"already running\", got: %v", err2)
	}
}

func TestReconcileOnlineSessions(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "aloha_reconcile_test_*")
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

	svc.SetReconcileInterval(1 * time.Hour)
	if svc.reconcileInterval != 1*time.Hour {
		t.Fatalf("Expected reconcileInterval to be 1h, got %v", svc.reconcileInterval)
	}

	svc.ProcessRequest(Request{Action: "login", UserID: "staleuser", PID: 9999, SID: 9})

	svc.mu.RLock()
	if len(svc.onlineSessions) != 1 {
		t.Fatalf("Expected 1 online session, got %d", len(svc.onlineSessions))
	}
	svc.mu.RUnlock()

	added, removed := svc.ReconcileOnlineSessions()
	if added != 0 || removed != 0 {
		t.Fatalf("Expected 0 added and 0 removed when shmClient is nil, got added=%d removed=%d", added, removed)
	}
}

func TestVerboseZeroLoginLogSuppression(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "aloha_verbose_test_*")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	svc, err := NewService(tempDir, filepath.Join(tempDir, "aloha.svc.sock"))
	if err != nil {
		t.Fatalf("Failed to create service: %v", err)
	}
	svc.shmClient = nil

	// 1. Test verbose == 0 with 0 notifications -> should NOT log LOGIN
	svc.SetVerbose(0)
	var logBuf bytes.Buffer
	log.SetOutput(&logBuf)

	resp1 := svc.HandleUserLogin("user0", 101, 1)
	if !resp1.Success {
		t.Fatalf("Login failed: %v", resp1)
	}
	if strings.Contains(logBuf.String(), "LOGIN: user=user0") {
		t.Fatalf("Expected NO login log when verbose=0 and 0 notifications, got log: %s", logBuf.String())
	}

	// 2. Test verbose == 1 with 0 notifications -> SHOULD log LOGIN
	svc.SetVerbose(1)
	logBuf.Reset()

	resp2 := svc.HandleUserLogin("user1", 102, 2)
	if !resp2.Success {
		t.Fatalf("Login failed: %v", resp2)
	}
	if !strings.Contains(logBuf.String(), "LOGIN: user=user1") {
		t.Fatalf("Expected login log when verbose=1 even with 0 notifications, got log: %s", logBuf.String())
	}

	// 3. Test verbose == 0 with >0 notifications -> SHOULD log LOGIN
	svc.SetVerbose(0)
	logBuf.Reset()

	// user1 watches target user2
	user1Dir := filepath.Join(tempDir, "home", "u", "user1")
	_ = os.MkdirAll(user1Dir, 0755)
	_ = os.WriteFile(filepath.Join(user1Dir, "alohaed"), []byte("user2\n"), 0644)
	svc.HandleReloadAloha("user1")

	// user2 logs in -> notifies user1 (1 notification)
	resp3 := svc.HandleUserLogin("user2", 103, 3)
	if !resp3.Success {
		t.Fatalf("Login failed: %v", resp3)
	}
	if !strings.Contains(logBuf.String(), "LOGIN: user=user2") {
		t.Fatalf("Expected login log when notifications > 0 even if verbose=0, got log: %s", logBuf.String())
	}
}

func TestReloadAlohaTargets(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "aloha_reload_test_*")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	svc, err := NewService(tempDir, filepath.Join(tempDir, "aloha.svc.sock"))
	if err != nil {
		t.Fatalf("Failed to create service: %v", err)
	}
	svc.shmClient = nil

	// subscriber "sub1" initial file has target "target1"
	sub1Dir := filepath.Join(tempDir, "home", "s", "sub1")
	_ = os.MkdirAll(sub1Dir, 0755)
	_ = os.WriteFile(filepath.Join(sub1Dir, "alohaed"), []byte("target1\n"), 0644)

	// sub1 logs in
	svc.ProcessRequest(Request{Action: "login", UserID: "sub1", PID: 101, SID: 1})

	// target1 logs in -> notifies sub1
	resp1 := svc.ProcessRequest(Request{Action: "login", UserID: "target1", PID: 201, SID: 2})
	if !strings.Contains(resp1.Message, "notified 1 subscribers") && !strings.Contains(resp1.Message, "found 1 subscribers") {
		t.Fatalf("Expected 1 notification for target1, got %s", resp1.Message)
	}

	// Update sub1's alohaed file to watch target2 instead of target1
	_ = os.WriteFile(filepath.Join(sub1Dir, "alohaed"), []byte("target2\n"), 0644)

	// Reload sub1
	reloadResp := svc.ProcessRequest(Request{Action: "reload", UserID: "sub1"})
	if !reloadResp.Success {
		t.Fatalf("Reload failed: %v", reloadResp)
	}

	// target1 logs in again -> should NOT notify sub1
	resp2 := svc.ProcessRequest(Request{Action: "login", UserID: "target1", PID: 202, SID: 2})
	if strings.Contains(resp2.Message, "notified 1 subscribers") || strings.Contains(resp2.Message, "found 1 subscribers") {
		t.Fatalf("Expected 0 notifications for target1 after reload, got %s", resp2.Message)
	}

	// target2 logs in -> SHOULD notify sub1
	resp3 := svc.ProcessRequest(Request{Action: "login", UserID: "target2", PID: 301, SID: 3})
	if !strings.Contains(resp3.Message, "notified 1 subscribers") && !strings.Contains(resp3.Message, "found 1 subscribers") {
		t.Fatalf("Expected 1 notification for target2 after reload, got %s", resp3.Message)
	}
}
