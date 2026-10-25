package daemon

import (
	"encoding/json"
	"net"
	"os"
	"path/filepath"
	"testing"
	"time"

	"pttbbs/stat/model"
	"pttbbs/stat/storage"
)

func TestDaemonServerE2E(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "stat_daemon_test")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	sockPath := filepath.Join(tempDir, "stat.svc.sock")
	dbPath := filepath.Join(tempDir, "stat.db")

	store := storage.NewStorage(dbPath)
	srv := NewServer(sockPath, store, 1*time.Second)

	errCh := make(chan error, 1)
	go func() {
		errCh <- srv.Start()
	}()

	// Wait for server to start listening
	var conn net.Conn
	for i := 0; i < 50; i++ {
		conn, err = net.Dial("unix", sockPath)
		if err == nil {
			break
		}
		time.Sleep(20 * time.Millisecond)
	}
	if err != nil {
		t.Fatalf("failed to connect to server: %v", err)
	}

	sendReq := func(req model.Request) model.Response {
		c, err := net.Dial("unix", sockPath)
		if err != nil {
			t.Fatalf("Dial failed: %v", err)
		}
		defer c.Close()

		if err := json.NewEncoder(c).Encode(req); err != nil {
			t.Fatalf("Encode failed: %v", err)
		}
		var resp model.Response
		if err := json.NewDecoder(c).Decode(&resp); err != nil {
			t.Fatalf("Decode failed: %v", err)
		}
		return resp
	}
	conn.Close()

	// 1. Initial status
	statusResp := sendReq(model.Request{Action: "status"})
	if !statusResp.Success || statusResp.Status == nil {
		t.Errorf("status failed: %+v", statusResp)
	}

	// 2. Increment post
	postResp := sendReq(model.Request{Action: "inc_post", UserID: "alice"})
	if !postResp.Success || postResp.PostCount != 1 {
		t.Errorf("inc_post failed: %+v", postResp)
	}

	postResp2 := sendReq(model.Request{Action: "inc_post", UserID: "alice"})
	if !postResp2.Success || postResp2.PostCount != 2 {
		t.Errorf("inc_post 2 failed: %+v", postResp2)
	}

	// 3. Add mail
	mailResp := sendReq(model.Request{Action: "add_mail", UserID: "alice", Count: 5})
	if !mailResp.Success || mailResp.MailCount != 5 {
		t.Errorf("add_mail failed: %+v", mailResp)
	}

	// 3.1 Inc song
	songResp := sendReq(model.Request{Action: "inc_song", UserID: "alice"})
	if !songResp.Success || songResp.SongCount != 1 {
		t.Errorf("inc_song failed: %+v", songResp)
	}

	// 3.2 Test generic action with Key and Value
	keyPostResp := sendReq(model.Request{Action: "inc", Key: "post", UserID: "alice"})
	if !keyPostResp.Success || keyPostResp.Value != 3 {
		t.Errorf("inc post failed: %+v", keyPostResp)
	}

	keyGetPostResp := sendReq(model.Request{Action: "get", Key: "post", UserID: "alice"})
	if !keyGetPostResp.Success || keyGetPostResp.Value != 3 {
		t.Errorf("get post failed: %+v", keyGetPostResp)
	}

	keyAddMailResp := sendReq(model.Request{Action: "add", Key: "mail", UserID: "alice", Count: 2})
	if !keyAddMailResp.Success || keyAddMailResp.Value != 7 {
		t.Errorf("add mail failed: %+v", keyAddMailResp)
	}

	// 4. Query stats
	getResp := sendReq(model.Request{Action: "get", UserID: "Alice"})
	if !getResp.Success || getResp.PostCount != 3 || getResp.MailCount != 7 || getResp.SongCount != 1 {
		t.Errorf("get failed: %+v", getResp)
	}

	// 5. Trigger save
	saveResp := sendReq(model.Request{Action: "save"})
	if !saveResp.Success {
		t.Errorf("save failed: %+v", saveResp)
	}

	// Shutdown
	close(srv.stopCh)
}
