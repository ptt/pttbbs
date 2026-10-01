package daemon

import (
	"encoding/json"
	"net"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestUtmpDaemonIPCProtocol(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "utmp_daemon_test_*")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	sockPath := filepath.Join(tempDir, "utmp.svc.sock")

	svc, err := NewService(tempDir, sockPath, WithInterval(100*time.Millisecond))
	if err != nil {
		t.Fatalf("Failed to create service: %v", err)
	}
	svc.shmClient = nil // mock / nil SHM mode

	go func() {
		_ = svc.Start()
	}()
	defer svc.Stop()

	// Wait for socket to be ready
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
	reqStatus := Request{Action: "status"}
	if err := json.NewEncoder(conn).Encode(reqStatus); err != nil {
		t.Fatalf("Failed to encode status request: %v", err)
	}

	var respStatus Response
	if err := json.NewDecoder(conn).Decode(&respStatus); err != nil {
		t.Fatalf("Failed to decode status response: %v", err)
	}
	if !respStatus.Success || respStatus.Status == nil {
		t.Fatalf("Expected success status, got: %+v", respStatus)
	}

	// 2. Send num request
	conn2, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatalf("Failed to dial: %v", err)
	}
	defer conn2.Close()

	reqNum := Request{Action: "num"}
	if err := json.NewEncoder(conn2).Encode(reqNum); err != nil {
		t.Fatalf("Failed to encode num request: %v", err)
	}
	var respNum Response
	if err := json.NewDecoder(conn2).Decode(&respNum); err != nil {
		t.Fatalf("Failed to decode num response: %v", err)
	}
	if !respNum.Success || respNum.Number == nil {
		t.Fatalf("Expected success num response, got: %+v", respNum)
	}

	// 3. Send fix request (nil SHM mode)
	conn3, err := net.Dial("unix", sockPath)
	if err != nil {
		t.Fatalf("Failed to dial: %v", err)
	}
	defer conn3.Close()

	reqFix := Request{Action: "fix"}
	if err := json.NewEncoder(conn3).Encode(reqFix); err != nil {
		t.Fatalf("Failed to encode fix request: %v", err)
	}
	var respFix Response
	if err := json.NewDecoder(conn3).Decode(&respFix); err != nil {
		t.Fatalf("Failed to decode fix response: %v", err)
	}
	if !respFix.Success || respFix.Fix == nil {
		t.Fatalf("Expected success fix response, got: %+v", respFix)
	}
}

func TestDuplicateUtmpInstancePrevention(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "utmp_dup_test_*")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	sockPath := filepath.Join(tempDir, "utmp.svc.sock")

	svc, err := NewService(tempDir, sockPath)
	if err != nil {
		t.Fatalf("Failed to create service: %v", err)
	}
	svc.shmClient = nil

	go func() {
		_ = svc.Start()
	}()
	defer svc.Stop()

	// Wait for socket
	for i := 0; i < 50; i++ {
		time.Sleep(10 * time.Millisecond)
		if IsSocketOccupied(sockPath) {
			break
		}
	}

	if !IsSocketOccupied(sockPath) {
		t.Fatalf("Expected socket %s to be occupied", sockPath)
	}
}
