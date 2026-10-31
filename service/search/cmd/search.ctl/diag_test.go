package main

import (
	"archive/tar"
	"compress/gzip"
	"encoding/json"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"pttbbs/search/daemon"
)

func TestRunDiagBundleAndText(t *testing.T) {
	tmpDir := t.TempDir()
	sockPath := filepath.Join(tmpDir, "search.svc.sock")

	ln, err := net.Listen("unix", sockPath)
	if err != nil {
		t.Fatalf("listen: %v", err)
	}
	defer ln.Close()

	var statusCalls atomic.Int32
	go func() {
		for {
			conn, err := ln.Accept()
			if err != nil {
				return
			}
			go func(c net.Conn) {
				defer c.Close()
				var req daemon.ControlRequest
				if err := json.NewDecoder(c).Decode(&req); err != nil {
					return
				}
				switch req.Action {
				case "verbose":
					_ = json.NewEncoder(c).Encode(daemon.ControlResponse{
						Status: "ok",
						Data:   map[string]int{"verbose": 0},
					})
				case "status":
					n := statusCalls.Add(1)
					hits := int64(100)
					if n > 1 {
						hits = 250
					}
					_ = json.NewEncoder(c).Encode(daemon.ControlResponse{
						Status: "ok",
						Data: daemon.ServiceStats{
							UptimeSeconds: 1000,
							Hits:          hits,
							Misses:        10,
							SearchSources: map[string]int64{"mbbsd_sr": hits + 10},
							AIDSources:    map[string]int64{"mbbsd_hash": 50},
						},
					})
				case "top":
					q := int64(110)
					if statusCalls.Load() > 1 {
						q = 260
					}
					_ = json.NewEncoder(c).Encode(daemon.ControlResponse{
						Status: "ok",
						Data: []daemon.BoardStats{
							{
								Direct:         "boards/G/Gossiping/.DIR",
								Board:          "Gossiping",
								Bid:            569,
								SearchQueries:  q,
								SearchMisses:   10,
								SearchSR:       q,
								ScanDurationMs: float64(q),
							},
						},
					})
				case "entries":
					_ = json.NewEncoder(c).Encode(daemon.ControlResponse{
						Status: "ok",
						Data: []daemon.CachedEntryInfo{
							{
								Board:       "Gossiping",
								Bid:         569,
								Predicates:  "rating=10",
								Matches:     177205,
								ScannedRecs: 789653,
								CreatedAt:   time.Now().Add(-30 * time.Second),
							},
						},
					})
				case "backtrack":
					_ = json.NewEncoder(c).Encode(daemon.ControlResponse{
						Status: "ok",
						Data:   []daemon.BoardBacktrackInfo{},
					})
				case "pprof":
					if req.Debug > 0 || req.Profile == "goroutine" {
						_ = json.NewEncoder(c).Encode(daemon.ControlResponse{
							Status: "ok",
							Data: daemon.PprofResult{
								Profile: req.Profile,
								IsText:  true,
								Text:    "mock " + req.Profile + " dump\n",
							},
						})
					} else {
						time.Sleep(50 * time.Millisecond)
						_ = json.NewEncoder(c).Encode(daemon.ControlResponse{
							Status: "ok",
							Data: daemon.PprofResult{
								Profile: req.Profile,
								IsText:  false,
								Bytes:   []byte("mock-binary-" + req.Profile),
							},
						})
					}
				}
			}(conn)
		}
	}()

	tarOut := filepath.Join(tmpDir, "diag.tar.gz")
	saved, preview, err := runDiag(sockPath, 1, tarOut, true)
	if err != nil {
		t.Fatalf("runDiag tar.gz failed: %v", err)
	}
	if saved != tarOut {
		t.Fatalf("unexpected saved path: %s", saved)
	}
	if !strings.Contains(preview, "Gossiping") || !strings.Contains(preview, "LIVE WINDOW ACTIVITY") {
		t.Fatalf("expected live preview with Gossiping, got:\n%s", preview)
	}

	f, err := os.Open(tarOut)
	if err != nil {
		t.Fatalf("open tar.gz: %v", err)
	}
	defer f.Close()
	gzr, err := gzip.NewReader(f)
	if err != nil {
		t.Fatalf("gzip reader: %v", err)
	}
	defer gzr.Close()
	tr := tar.NewReader(gzr)
	foundFiles := make(map[string]string)
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			t.Fatalf("tar next: %v", err)
		}
		b, _ := io.ReadAll(tr)
		foundFiles[hdr.Name] = string(b)
	}
	for _, want := range []string{"summary.txt", "snapshot.json", "cpu.pprof", "heap.pprof", "mutex.pprof"} {
		if _, ok := foundFiles[want]; !ok {
			t.Fatalf("missing %s in tar.gz bundle (got %v)", want, foundFiles)
		}
	}
	if !strings.Contains(foundFiles["summary.txt"], "rating=10") {
		t.Fatalf("summary.txt missing cached entry rating=10")
	}

	txtOut := filepath.Join(tmpDir, "diag.txt")
	if _, _, err := runDiag(sockPath, 0, txtOut, false); err != nil {
		t.Fatalf("runDiag txt failed: %v", err)
	}
	txtData, err := os.ReadFile(txtOut)
	if err != nil || !strings.Contains(string(txtData), "SEARCH.SVC DIAGNOSTIC REPORT") {
		t.Fatalf("unexpected txt output: err=%v", err)
	}
}
