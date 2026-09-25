package daemon

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"io"
	"net"
	"os"
	"path/filepath"
	"reflect"
	"testing"
	"time"

	"pttbbs/bbs"
)

func queryBinaryClient(t *testing.T, socketPath string, bid int32, direct string, preds [][]byte, offset, limit int32) ([]int32, int32) {
	t.Helper()
	conn, err := net.Dial("unix", socketPath)
	if err != nil {
		t.Fatalf("failed to dial %s: %v", socketPath, err)
	}
	defer conn.Close()

	var hdr binaryReqHeader
	hdr.Magic = SearchSvcMagic
	hdr.Bid = bid
	hdr.Offset = offset
	hdr.Limit = limit
	hdr.NumPreds = int32(len(preds))
	copy(hdr.Direct[:], direct)

	var reqBuf bytes.Buffer
	if err := binary.Write(&reqBuf, binary.LittleEndian, &hdr); err != nil {
		t.Fatalf("write hdr: %v", err)
	}
	for _, p := range preds {
		reqBuf.Write(p)
	}

	if _, err := conn.Write(reqBuf.Bytes()); err != nil {
		t.Fatalf("conn write: %v", err)
	}

	var respHdr binaryRespHeader
	if err := binary.Read(conn, binary.LittleEndian, &respHdr); err != nil {
		t.Fatalf("read resp hdr: %v", err)
	}
	if respHdr.Status != 0 {
		t.Fatalf("expected status 0, got %d", respHdr.Status)
	}

	indices := make([]int32, respHdr.Count)
	if respHdr.Count > 0 {
		if err := binary.Read(conn, binary.LittleEndian, &indices); err != nil && err != io.EOF {
			t.Fatalf("read indices: %v", err)
		}
	}
	return indices, respHdr.Total
}

func queryAIDClient(t *testing.T, socketPath string, bid int32, direct string, aidu uint64, requiredMode int32) int32 {
	t.Helper()
	conn, err := net.Dial("unix", socketPath)
	if err != nil {
		t.Fatalf("failed to dial %s: %v", socketPath, err)
	}
	defer conn.Close()

	var req binaryAIDReqHeader
	req.Magic = SearchAIDMagic
	req.Bid = bid
	req.AIDU = aidu
	req.RequiredMode = requiredMode
	copy(req.Direct[:], direct)

	if err := binary.Write(conn, binary.LittleEndian, &req); err != nil {
		t.Fatalf("write aid req: %v", err)
	}

	var resp binaryAIDResp
	if err := binary.Read(conn, binary.LittleEndian, &resp); err != nil {
		t.Fatalf("read aid resp: %v", err)
	}
	if resp.Status != 0 {
		t.Fatalf("expected aid status 0, got %d", resp.Status)
	}
	return resp.FoundIdx
}

func sendControlClient(t *testing.T, socketPath string, req ControlRequest) ControlResponse {
	t.Helper()
	conn, err := net.Dial("unix", socketPath)
	if err != nil {
		t.Fatalf("failed to dial %s: %v", socketPath, err)
	}
	defer conn.Close()

	if err := json.NewEncoder(conn).Encode(req); err != nil {
		t.Fatalf("encode control req: %v", err)
	}
	var resp ControlResponse
	if err := json.NewDecoder(conn).Decode(&resp); err != nil {
		t.Fatalf("decode control resp: %v", err)
	}
	return resp
}

func TestSearchServiceEndToEnd(t *testing.T) {
	tmpDir := t.TempDir()
	socketPath := filepath.Join(tmpDir, "run", "search.svc.sock")
	boardDir := filepath.Join(tmpDir, "boards", "T", "TestBoard")
	if err := os.MkdirAll(boardDir, 0755); err != nil {
		t.Fatalf("mkdir boardDir: %v", err)
	}
	dirPath := filepath.Join(boardDir, ".DIR")

	// Populate 6 records in .DIR (1-based recno 1..6)
	records := []struct {
		fn    string
		owner string
		title string
		mode  int
		rec   int
		money int
	}{
		{"M.1700000001.A.001", "alice", "[閒聊] 地震警報", 0, 10, 100},
		{"M.1700000002.A.002", "bob", "[新聞] 天氣預報", FILE_MARKED, 5, 50},
		{"M.1700000003.A.003", "alice", "Re: [閒聊] 地震警報", FILE_MARKED, 99, 500},
		{"M.1700000004.A.004", "-deleted", "[閒聊] 地震已被刪除", 0, 50, 100},
		{"M.1700000005.A.005", "charlie", "[爆卦] 又有地震", 0, 25, 200},
		{"M.1700000006.A.006", "alice", "[問題] 測試置底", FILE_BOTTOM | FILE_MARKED, 30, 300},
	}
	for _, r := range records {
		if err := AppendTestFileheader(dirPath, r.fn, r.owner, r.title, r.mode, r.rec, r.money); err != nil {
			t.Fatalf("append record: %v", err)
		}
	}

	shm, _ := bbs.AttachSHM() // Optional in tests.
	svc := newService(tmpDir, socketPath, shm)
	go func() {
		_ = svc.Start()
	}()
	defer svc.Stop()

	// Wait for socket to become ready
	for i := 0; i < 50; i++ {
		if IsSocketOccupied(socketPath) {
			break
		}
		time.Sleep(10 * time.Millisecond)
	}

	// 1. Keyword search for "地震" (should match recno 1, 3, 5; recno 4 is soft-deleted)
	predKw := MakePredBytes(RS_KEYWORD, "地震", 0, 0)
	relDir := filepath.Join("boards", "T", "TestBoard", ".DIR")
	indices, total := queryBinaryClient(t, socketPath, 1, relDir, [][]byte{predKw}, 0, 100)
	expectedKw := []int32{1, 3, 5}
	if total != 3 || !reflect.DeepEqual(indices, expectedKw) {
		t.Fatalf("Keyword search mismatch: got total=%d indices=%v, want %v", total, indices, expectedKw)
	}

	// 2. Repeat identical search -> verify cache hit
	indices2, total2 := queryBinaryClient(t, socketPath, 1, relDir, [][]byte{predKw}, 0, 100)
	if total2 != 3 || !reflect.DeepEqual(indices2, expectedKw) {
		t.Fatalf("Cached keyword search mismatch: got %v", indices2)
	}
	if svc.hits.Load() != 1 || svc.misses.Load() != 1 {
		t.Fatalf("Expected hits=1 misses=1, got hits=%d misses=%d", svc.hits.Load(), svc.misses.Load())
	}

	// 3. Windowed pagination (offset=1, limit=1 -> should return [3], total=3)
	winIndices, winTotal := queryBinaryClient(t, socketPath, 1, relDir, [][]byte{predKw}, 1, 1)
	if winTotal != 3 || !reflect.DeepEqual(winIndices, []int32{3}) {
		t.Fatalf("Window query mismatch: total=%d indices=%v", winTotal, winIndices)
	}

	// 4. Chained predicate search: [RS_KEYWORD("地震"), RS_AUTHOR("alice")]
	predAuthor := MakePredBytes(RS_AUTHOR, "alice", 0, 0)
	chainedIndices, chainedTotal := queryBinaryClient(t, socketPath, 1, relDir, [][]byte{predKw, predAuthor}, 0, 100)
	expectedChained := []int32{1, 3}
	if chainedTotal != 2 || !reflect.DeepEqual(chainedIndices, expectedChained) {
		t.Fatalf("Chained search mismatch: got total=%d indices=%v, want %v", chainedTotal, chainedIndices, expectedChained)
	}
	if svc.chainedHits.Load() != 1 {
		t.Fatalf("Expected chainedHits=1, got %d", svc.chainedHits.Load())
	}

	// 5. Append a new matching record (recno 7) to .DIR and verify incremental tail scan!
	if err := AppendTestFileheader(dirPath, "M.1700000007.A.007", "david", "[情報] 最新地震速報", 0, 80, 100); err != nil {
		t.Fatalf("append rec 7: %v", err)
	}
	incIndices, incTotal := queryBinaryClient(t, socketPath, 1, relDir, [][]byte{predKw}, 0, 100)
	expectedInc := []int32{1, 3, 5, 7}
	if incTotal != 4 || !reflect.DeepEqual(incIndices, expectedInc) {
		t.Fatalf("Incremental tail scan mismatch: got total=%d indices=%v, want %v", incTotal, incIndices, expectedInc)
	}
	// Incremental reuse requires SHM (SRexpire) to detect in-place edits.
	if wantInc := int64(0); SHMReady() {
		wantInc = 1
		if svc.incrementalUpdates.Load() != wantInc {
			t.Fatalf("Expected incrementalUpdates=%d, got %d", wantInc, svc.incrementalUpdates.Load())
		}
	} else if svc.incrementalUpdates.Load() != wantInc {
		t.Fatalf("Expected incrementalUpdates=1, got %d", svc.incrementalUpdates.Load())
	}

	// 6. AID Lookup Cache: positive lookup (M.1700000005.A.005 -> recno 5)
	aidu5 := (uint64(1700000005) << 12) | 0x005
	if idx := queryAIDClient(t, socketPath, 1, relDir, aidu5, 0); idx != 5 {
		t.Fatalf("Expected AID lookup idx=5, got %d", idx)
	}
	if idx := queryAIDClient(t, socketPath, 1, relDir, aidu5, 0); idx != 5 {
		t.Fatalf("Expected cached AID lookup idx=5, got %d", idx)
	}
	if svc.aidHits.Load() != 1 {
		t.Fatalf("Expected aidHits=1, got %d", svc.aidHits.Load())
	}

	// 7. AID Negative Cache: non-existent AID (M.1799999999.A.FFF -> 0)
	aiduMissing := (uint64(1799999999) << 12) | 0xFFF
	if idx := queryAIDClient(t, socketPath, 1, relDir, aiduMissing, 0); idx != 0 {
		t.Fatalf("Expected missing AID idx=0, got %d", idx)
	}
	// Second lookup should hit Negative Cache without re-scanning .DIR!
	if idx := queryAIDClient(t, socketPath, 1, relDir, aiduMissing, 0); idx != 0 {
		t.Fatalf("Expected negative-cached AID idx=0, got %d", idx)
	}
	if svc.aidNegativeHits.Load() != 1 {
		t.Fatalf("Expected aidNegativeHits=1, got %d", svc.aidNegativeHits.Load())
	}

	// 8. Binary Invalidation (SINV): mbbsd notifies search.svc that cached indices for relDir are stale
	invalConn, err := net.Dial("unix", socketPath)
	if err != nil {
		t.Fatalf("dial for inval: %v", err)
	}
	var invalReq binaryInvalReqHeader
	invalReq.Magic = SearchInvalMagic
	invalReq.Bid = 1
	copy(invalReq.Direct[:], relDir)
	if err := binary.Write(invalConn, binary.LittleEndian, &invalReq); err != nil {
		t.Fatalf("write invalReq: %v", err)
	}
	var invalStatus int32
	if err := binary.Read(invalConn, binary.LittleEndian, &invalStatus); err != nil || invalStatus != 0 {
		t.Fatalf("read invalStatus: err=%v status=%d", err, invalStatus)
	}
	invalConn.Close()
	if svc.invalidations.Load() != 1 {
		t.Fatalf("Expected invalidations=1, got %d", svc.invalidations.Load())
	}

	// 9. Control API: status and flush
	statusResp := sendControlClient(t, socketPath, ControlRequest{Action: "status"})
	if statusResp.Status != "ok" {
		t.Fatalf("status failed: %+v", statusResp)
	}
	flushResp := sendControlClient(t, socketPath, ControlRequest{Action: "flush", Bid: 1})
	if flushResp.Status != "ok" {
		t.Fatalf("flush failed: %+v", flushResp)
	}
}
