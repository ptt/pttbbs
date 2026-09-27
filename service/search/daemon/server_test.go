package daemon

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"reflect"
	"strings"
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

	var hdr binaryReqHeaderV1
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

	var req binaryAIDReqHeaderV1
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

	// 10. Control API: verbose get and set
	vResp := sendControlClient(t, socketPath, ControlRequest{Action: "verbose"})
	if vResp.Status != "ok" {
		t.Fatalf("verbose get failed: %+v", vResp)
	}
	lvl2 := 2
	vSetResp := sendControlClient(t, socketPath, ControlRequest{Action: "verbose", Level: &lvl2})
	if vSetResp.Status != "ok" || svc.Verbose() != 2 {
		t.Fatalf("verbose set failed: %+v, svc.Verbose=%d", vSetResp, svc.Verbose())
	}

	// 11. Control API: top boards
	topResp := sendControlClient(t, socketPath, ControlRequest{Action: "top", Limit: 10, SortBy: "misses"})
	if topResp.Status != "ok" {
		t.Fatalf("top failed: %+v", topResp)
	}
	topBytes, _ := json.Marshal(topResp.Data)
	var topItems []BoardStats
	if err := json.Unmarshal(topBytes, &topItems); err != nil {
		t.Fatalf("unmarshal top: %v", err)
	}
	if len(topItems) == 0 {
		t.Fatalf("expected top boards to contain testboard, got empty")
	}
	if topItems[0].SearchQueries == 0 && topItems[0].AIDQueries == 0 {
		t.Fatalf("expected non-zero queries for top board: %+v", topItems[0])
	}

	// 12. Control API: pprof (goroutine, heap, cpu)
	gResp := sendControlClient(t, socketPath, ControlRequest{Action: "pprof", Profile: "goroutine", Debug: 2})
	if gResp.Status != "ok" {
		t.Fatalf("pprof goroutine failed: %+v", gResp)
	}
	gBytes, _ := json.Marshal(gResp.Data)
	var gResult PprofResult
	if err := json.Unmarshal(gBytes, &gResult); err != nil || !gResult.IsText || len(gResult.Text) == 0 {
		t.Fatalf("invalid goroutine dump result: %+v err=%v", gResult, err)
	}

	hResp := sendControlClient(t, socketPath, ControlRequest{Action: "pprof", Profile: "heap", Debug: 0})
	if hResp.Status != "ok" {
		t.Fatalf("pprof heap failed: %+v", hResp)
	}
	hBytes, _ := json.Marshal(hResp.Data)
	var hResult PprofResult
	if err := json.Unmarshal(hBytes, &hResult); err != nil || len(hResult.Bytes) == 0 {
		t.Fatalf("invalid heap profile result: %+v err=%v", hResult, err)
	}

	cpuResp := sendControlClient(t, socketPath, ControlRequest{Action: "pprof", Profile: "cpu", Seconds: 1})
	if cpuResp.Status != "ok" {
		t.Fatalf("pprof cpu failed: %+v", cpuResp)
	}
	cpuBytes, _ := json.Marshal(cpuResp.Data)
	var cpuResult PprofResult
	if err := json.Unmarshal(cpuBytes, &cpuResult); err != nil || len(cpuResult.Bytes) == 0 {
		t.Fatalf("invalid cpu profile result: %+v err=%v", cpuResult, err)
	}

	// 13. Control API: backtrack
	bResp := sendControlClient(t, socketPath, ControlRequest{Action: "backtrack"})
	if bResp.Status != "ok" {
		t.Fatalf("backtrack failed: %+v", bResp)
	}
	bBytes, _ := json.Marshal(bResp.Data)
	var bItems []BoardBacktrackInfo
	if err := json.Unmarshal(bBytes, &bItems); err != nil {
		t.Fatalf("unmarshal backtrack: %v", err)
	}
	if len(bItems) == 0 {
		t.Fatalf("expected backtrack items, got empty")
	}
	if bItems[0].MaxBacktrack < 0 || bItems[0].TotalRecs <= 0 {
		t.Fatalf("invalid backtrack info: %+v", bItems[0])
	}

	// 14. Push simulation (in-place mtime update) should NOT invalidate keyword search or AID cache!
	queryBinaryClient(t, socketPath, 1, relDir, [][]byte{predKw}, 0, 100)
	queryAIDClient(t, socketPath, 1, relDir, aidu5, 0)
	hitsBefore := svc.hits.Load()
	missesBefore := svc.misses.Load()
	aidHitsBefore := svc.aidHits.Load()
	aidMissesBefore := svc.aidMisses.Load()

	// Simulate a push by touching mtime forward
	newMtime := time.Now().Add(2 * time.Second)
	if err := os.Chtimes(dirPath, newMtime, newMtime); err != nil {
		t.Fatalf("chtimes: %v", err)
	}

	// Repeat keyword search: MUST be a cache hit (tailName matches, mode doesn't require SRExpire)!
	resIndices, resTotal := queryBinaryClient(t, socketPath, 1, relDir, [][]byte{predKw}, 0, 100)
	if resTotal == 0 || len(resIndices) == 0 {
		t.Fatalf("search after push failed: total=%d indices=%v", resTotal, resIndices)
	}
	if svc.hits.Load() != hitsBefore+1 {
		t.Fatalf("expected keyword search to hit cache after push, hitsBefore=%d, now=%d", hitsBefore, svc.hits.Load())
	}
	if svc.misses.Load() != missesBefore {
		t.Fatalf("expected 0 misses after push, missesBefore=%d, now=%d", missesBefore, svc.misses.Load())
	}

	// Repeat AID query: MUST also hit cache!
	if idx := queryAIDClient(t, socketPath, 1, relDir, aidu5, 0); idx != 5 {
		t.Fatalf("aid query after push failed: got %d", idx)
	}
	if svc.aidHits.Load() != aidHitsBefore+1 {
		t.Fatalf("expected aid query to hit cache after push, before=%d, now=%d", aidHitsBefore, svc.aidHits.Load())
	}
	if svc.aidMisses.Load() != aidMissesBefore {
		t.Fatalf("expected 0 aid misses after push, before=%d, now=%d", aidMissesBefore, svc.aidMisses.Load())
	}

	// 15. Query an unknown non-existent AID: triggers linear scan once, records backtrack
	fakeAIDU := uint64(0x065599999999) // does not exist
	if idx := queryAIDClient(t, socketPath, 1, relDir, fakeAIDU, 0); idx != 0 {
		t.Fatalf("expected 0 for fake aid, got %d", idx)
	}
	bt := svc.LookupBoardBacktrack(dirPath)
	if bt < 0 {
		t.Fatalf("expected backtrack to be recorded after bsearch miss, got %d", bt)
	}

	// 16. Test Write-side Hints: HINT_POST, HINT_PUSH, HINT_DELETE
	if err := AppendTestFileheader(dirPath, "M.1655888888.A.111", "testuser", "New Post", 0, 0, 0); err != nil {
		t.Fatalf("append test fh: %v", err)
	}
	aiduNew := (uint64(1655888888) << 12) | 0x111
	hintConn, err := net.Dial("unix", socketPath)
	if err != nil {
		t.Fatalf("dial hintConn: %v", err)
	}
	var hintReq binaryHintReqHeader
	hintReq.Magic = SearchHintMagic
	hintReq.Type = HintTypePost
	hintReq.Bid = 1
	hintReq.Recno = 8
	hintReq.Aidu = aiduNew
	copy(hintReq.Fh[:], []byte("M.1655888888.A.111"))
	copy(hintReq.Direct[:], relDir)
	if err := binary.Write(hintConn, binary.LittleEndian, &hintReq); err != nil {
		t.Fatalf("write hintReq: %v", err)
	}
	var hintStatus int32
	if err := binary.Read(hintConn, binary.LittleEndian, &hintStatus); err != nil || hintStatus != 0 {
		t.Fatalf("read hintStatus: %v %d", err, hintStatus)
	}
	hintConn.Close()

	// Query immediately: MUST hit aidCache as foundIdx=8!
	if idx := queryAIDClient(t, socketPath, 1, relDir, aiduNew, 0); idx != 8 {
		t.Fatalf("expected post hint to prewarm aidCache idx=8, got %d", idx)
	}

	// Send HINT_COMMENT to update recommend to 99
	hintConn2, err := net.Dial("unix", socketPath)
	if err != nil {
		t.Fatalf("dial hintConn2: %v", err)
	}
	var commentHint binaryHintReqHeader
	commentHint.Magic = SearchHintMagic
	commentHint.Type = HintTypeComment
	commentHint.Bid = 1
	commentHint.Recno = 8
	commentHint.Data = 99
	copy(commentHint.Direct[:], relDir)
	_ = binary.Write(hintConn2, binary.LittleEndian, &commentHint)
	_ = binary.Read(hintConn2, binary.LittleEndian, &hintStatus)
	hintConn2.Close()

	// Send HINT_DELETE
	hintConn3, err := net.Dial("unix", socketPath)
	if err != nil {
		t.Fatalf("dial hintConn3: %v", err)
	}
	var delHint binaryHintReqHeader
	delHint.Magic = SearchHintMagic
	delHint.Type = HintTypeDelete
	delHint.Bid = 1
	delHint.Recno = 8
	delHint.Aidu = aiduNew
	copy(delHint.Direct[:], relDir)
	_ = binary.Write(hintConn3, binary.LittleEndian, &delHint)
	_ = binary.Read(hintConn3, binary.LittleEndian, &hintStatus)
	hintConn3.Close()

	// In PTT BBS, delete_fileheader marks record as deleted in .DIR
	if err := DeleteTestFileheader(dirPath, "M.1655888888.A.111", 8); err != nil {
		t.Fatalf("delete test fh: %v", err)
	}

	// Query immediately: MUST hit as 0 (deleted)
	if idx := queryAIDClient(t, socketPath, 1, relDir, aiduNew, 0); idx != 0 {
		t.Fatalf("expected delete hint to mark 0, got %d", idx)
	}
}

func TestBoardAIDTable(t *testing.T) {
	total := 10000
	aids := make([]uint64, total)
	baseTS := uint32(1700000000)

	for i := 0; i < total; i++ {
		ts := baseTS + uint32(i*10)
		hex := uint32(i % 4096)
		aids[i] = (uint64(ts) << 12) | uint64(hex)
	}

	// Introduce an out-of-order article (backtrack of 100 records)
	outOfOrderTS := baseTS + uint32(400*10)
	outOfOrderHex := uint32(0xABC)
	aids[500] = (uint64(outOfOrderTS) << 12) | uint64(outOfOrderHex)

	maxBtrack, maxTdiff, minTS, maxTS := computeBoardAIDStats(aids)
	if maxBtrack < 90 {
		t.Fatalf("expected maxBacktrack >= 90, got %d", maxBtrack)
	}
	if maxTdiff < 900 {
		t.Fatalf("expected maxTimeDiff >= 900, got %d", maxTdiff)
	}

	tbl := &BoardAIDTable{
		direct:       "/tmp/test.DIR",
		bid:          1,
		maxBacktrack: maxBtrack,
		maxTimeDiff:  maxTdiff,
		minTS:        minTS,
		maxTS:        maxTS,
		aids:         aids,
	}

	// 1. Search normal article in middle
	targetAIDU := aids[3500]
	if rec := tbl.SearchAID(targetAIDU); rec != 3501 {
		t.Fatalf("expected rec=3501, got %d", rec)
	}

	// 2. Search out-of-order article
	targetOO := (uint64(outOfOrderTS) << 12) | uint64(outOfOrderHex)
	if rec := tbl.SearchAID(targetOO); rec != 501 {
		t.Fatalf("expected rec=501 for out-of-order post, got %d", rec)
	}

	// 3. Search tail article
	targetTail := aids[total-1]
	if rec := tbl.SearchAID(targetTail); rec != int32(total) {
		t.Fatalf("expected rec=%d for tail, got %d", total, rec)
	}

	// 4. Search non-existent AID (ts before minTS)
	beforeMin := (uint64(baseTS-1000) << 12) | 0x123
	if rec := tbl.SearchAID(beforeMin); rec != 0 {
		t.Fatalf("expected 0 for AID before minTS, got %d", rec)
	}

	// 5. Search non-existent AID (ts after maxTS)
	afterMax := (uint64(baseTS+uint32(total*10)+1000) << 12) | 0x123
	if rec := tbl.SearchAID(afterMax); rec != 0 {
		t.Fatalf("expected 0 for AID after maxTS, got %d", rec)
	}

	// 6. Search non-existent AID within ts range (wrong hex)
	midMissing := (uint64(baseTS+uint32(3500*10)) << 12) | 0xFFF
	if rec := tbl.SearchAID(midMissing); rec != 0 {
		t.Fatalf("expected 0 for missing AID within range, got %d", rec)
	}

	// 7. Test AppendPost
	newRecno := int32(total + 1)
	newAID := (uint64(baseTS+uint32(total*10)+10) << 12) | 0x555
	tbl.AppendPost(newRecno, newAID)
	if rec := tbl.SearchAID(newAID); rec != newRecno {
		t.Fatalf("expected rec=%d after AppendPost, got %d", newRecno, rec)
	}

	// 8. Test DeletePost
	tbl.DeletePost(newRecno)
	if rec := tbl.SearchAID(newAID); rec != 0 {
		t.Fatalf("expected 0 after DeletePost, got %d", rec)
	}

	// 9. Test /digest naming
	tmpBoardDir := t.TempDir()
	namesDir := filepath.Join(tmpBoardDir, ".Names")
	svc := newService(tmpBoardDir, filepath.Join(tmpBoardDir, "test.sock"), nil)

	// Verify resolve name
	if got := svc.ResolveBoardName(namesDir, 1); !strings.HasSuffix(got, "/digest") {
		t.Fatalf("expected name ending in /digest, got %s", got)
	}
}

func TestFileheaderModeHelpers(t *testing.T) {
	var fh [128]byte

	// Initial check on empty/nil
	if MatchFileheaderMode(nil, 0) {
		t.Errorf("expected MatchFileheaderMode(nil) == false")
	}
	if MatchFileheaderMode(&fh, 0) {
		t.Errorf("expected MatchFileheaderMode(&fh with zero filename) == false")
	}
	if FileheaderFilemode(nil) != 0 {
		t.Errorf("expected FileheaderFilemode(nil) == 0")
	}

	// Normal valid filename
	copy(fh[:], "M.1700000001.A.001")
	copy(fh[FHDR_OFF_OWNER:], "tester")
	binary.NativeEndian.PutUint16(fh[FHDR_OFF_FILEMODE:], uint16(FILE_MARKED))

	if !MatchFileheaderMode(&fh, 0) {
		t.Errorf("expected MatchFileheaderMode with valid file == true")
	}
	if !MatchFileheaderMode(&fh, int32(FILE_MARKED)) {
		t.Errorf("expected MatchFileheaderMode with FILE_MARKED == true")
	}
	if MatchFileheaderMode(&fh, int32(FILE_BOTTOM)) {
		t.Errorf("expected MatchFileheaderMode with FILE_BOTTOM == false")
	}
	if FileheaderFilemode(&fh) != FILE_MARKED {
		t.Errorf("expected FileheaderFilemode == %d, got %d", FILE_MARKED, FileheaderFilemode(&fh))
	}

	// Dot filename
	fh[0] = '.'
	if MatchFileheaderMode(&fh, 0) {
		t.Errorf("expected dot filename == false")
	}
	fh[0] = 'M'

	// Deleted owner '-'
	fh[FHDR_OFF_OWNER] = '-'
	if !MatchFileheaderMode(&fh, 0) {
		t.Errorf("expected MatchFileheaderMode with requiredMode=0 and deleted owner == true")
	}
	if MatchFileheaderMode(&fh, int32(FILE_MARKED)) {
		t.Errorf("expected MatchFileheaderMode with requiredMode!=0 and deleted owner == false")
	}
	fh[FHDR_OFF_OWNER] = 't'

	// Test recommend
	SetFileheaderRecommend(nil, 50) // should not panic
	SetFileheaderRecommend(&fh, 77)
	if fh[FHDR_OFF_RECOMMEND] != 77 {
		t.Errorf("expected recommend=77, got %d", fh[FHDR_OFF_RECOMMEND])
	}
}

func BenchmarkBoardAIDTable(b *testing.B) {
	// Gossiping scale: 800,000 articles
	total := 800000
	aids := make([]uint64, total)
	baseTS := uint32(1700000000)

	for i := 0; i < total; i++ {
		ts := baseTS + uint32(i*2)
		hex := uint32(i % 4096)
		aids[i] = (uint64(ts) << 12) | uint64(hex)
	}

	tbl := &BoardAIDTable{
		direct:       "/tmp/benchmark.DIR",
		bid:          1,
		maxBacktrack: 150,
		maxTimeDiff:  300,
		minTS:        baseTS,
		maxTS:        baseTS + uint32(total*2),
		aids:         aids,
	}

	targetHit := aids[400000]
	targetMiss := (uint64(baseTS+uint32(400000*2)) << 12) | 0xFFE

	b.Run("Hit-Middle", func(b *testing.B) {
		for i := 0; i < b.N; i++ {
			_ = tbl.SearchAID(targetHit)
		}
	})

	b.Run("Miss-Range", func(b *testing.B) {
		for i := 0; i < b.N; i++ {
			_ = tbl.SearchAID(targetMiss)
		}
	})

	b.Run("Miss-OutOfRange", func(b *testing.B) {
		targetOutOfRange := (uint64(baseTS-5000) << 12) | 0x123
		for i := 0; i < b.N; i++ {
			_ = tbl.SearchAID(targetOutOfRange)
		}
	})
}



func queryAIDClientWithSource(t *testing.T, socketPath string, bid int32, direct string, aidu uint64, requiredMode int32, source int32) int32 {
	t.Helper()
	conn, err := net.Dial("unix", socketPath)
	if err != nil {
		t.Fatalf("failed to dial %s: %v", socketPath, err)
	}
	defer conn.Close()

	var req binaryAIDReqHeader
	req.Magic = SearchAIDMagicV2
	req.Bid = bid
	req.AIDU = aidu
	req.RequiredMode = requiredMode
	req.Source = source
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

func queryBinaryClientWithSource(t *testing.T, socketPath string, bid int32, direct string, preds [][]byte, offset, limit int32, source int32) ([]int32, int32) {
	t.Helper()
	conn, err := net.Dial("unix", socketPath)
	if err != nil {
		t.Fatalf("failed to dial %s: %v", socketPath, err)
	}
	defer conn.Close()

	var hdr binaryReqHeader
	hdr.Magic = SearchSvcMagicV2
	hdr.Bid = bid
	hdr.Offset = offset
	hdr.Limit = limit
	hdr.NumPreds = int32(len(preds))
	hdr.Source = source
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

func TestQuerySourcesAttribution(t *testing.T) {
	tmpDir := t.TempDir()
	socketPath := filepath.Join(tmpDir, "run", "search.svc.sock")
	boardDir := filepath.Join(tmpDir, "boards", "G", "Gossiping")
	if err := os.MkdirAll(boardDir, 0755); err != nil {
		t.Fatalf("mkdir boardDir: %v", err)
	}
	dirPath := filepath.Join(boardDir, ".DIR")
	relDir := filepath.Join("boards", "G", "Gossiping", ".DIR")

	if err := AppendTestFileheader(dirPath, "M.1700000001.A.001", "user1", "Gossiping post 1", 0, 10, 100); err != nil {
		t.Fatalf("append rec 1: %v", err)
	}
	if err := AppendTestFileheader(dirPath, "M.1700000002.A.002", "user2", "Gossiping post 2", 0, 20, 200); err != nil {
		t.Fatalf("append rec 2: %v", err)
	}

	svc := newService(tmpDir, socketPath, nil)
	svc.verbose.Store(1)
	go func() {
		_ = svc.Start()
	}()
	defer svc.Stop()

	for i := 0; i < 50; i++ {
		if IsSocketOccupied(socketPath) {
			break
		}
		time.Sleep(10 * time.Millisecond)
	}

	// AID hit with SrcMbbsdHash
	aidu1 := (uint64(1700000001) << 12) | 0x001
	idx := queryAIDClientWithSource(t, socketPath, 569, relDir, aidu1, 0, SrcMbbsdHash)
	if idx != 1 {
		t.Fatalf("expected idx 1, got %d", idx)
	}

	// AID miss with SrcMbbsdHash
	fakeAIDU := (uint64(1700999999) << 12) | 0x999
	idxMiss := queryAIDClientWithSource(t, socketPath, 569, relDir, fakeAIDU, 0, SrcMbbsdHash)
	if idxMiss != 0 {
		t.Fatalf("expected idx 0, got %d", idxMiss)
	}

	// AID hit with SrcMbbsdLua
	idxLua := queryAIDClientWithSource(t, socketPath, 569, relDir, aidu1, 0, SrcMbbsdLua)
	if idxLua != 1 {
		t.Fatalf("expected idxLua 1, got %d", idxLua)
	}

	// AID hit with SrcBoarddWeb
	idxWeb := queryAIDClientWithSource(t, socketPath, 569, relDir, aidu1, 0, SrcBoarddWeb)
	if idxWeb != 1 {
		t.Fatalf("expected idxWeb 1, got %d", idxWeb)
	}

	// Search query with SrcMbbsdSR
	predBytes := MakePredBytes(RS_KEYWORD, "Gossiping", 0, 0)
	indices, total := queryBinaryClientWithSource(t, socketPath, 569, relDir, [][]byte{predBytes}, 0, 10, SrcMbbsdSR)
	if len(indices) != 2 || total != 2 {
		t.Fatalf("expected 2 matches, got %d (total %d)", len(indices), total)
	}

	// Check status response
	statusResp := sendControlClient(t, socketPath, ControlRequest{Action: "status"})
	statsBytes, _ := json.Marshal(statusResp.Data)
	var stats ServiceStats
	if err := json.Unmarshal(statsBytes, &stats); err != nil {
		t.Fatalf("unmarshal stats: %v", err)
	}

	if stats.AIDSources["mbbsd_hash"] != 2 {
		t.Errorf("expected 2 mbbsd_hash AID queries, got %d", stats.AIDSources["mbbsd_hash"])
	}
	if stats.AIDMissSources["mbbsd_hash"] != 1 {
		t.Errorf("expected 1 mbbsd_hash AID miss, got %d", stats.AIDMissSources["mbbsd_hash"])
	}
	if stats.AIDSources["mbbsd_lua"] != 1 {
		t.Errorf("expected 1 mbbsd_lua AID query, got %d", stats.AIDSources["mbbsd_lua"])
	}
	if stats.AIDSources["web_boardd"] != 1 {
		t.Errorf("expected 1 web_boardd AID query, got %d", stats.AIDSources["web_boardd"])
	}
	if stats.SearchSources["mbbsd_sr"] != 1 {
		t.Errorf("expected 1 mbbsd_sr search query, got %d", stats.SearchSources["mbbsd_sr"])
	}

	// Check top boards
	topResp := sendControlClient(t, socketPath, ControlRequest{Action: "top", Limit: 10, SortBy: "queries"})
	boardsBytes, _ := json.Marshal(topResp.Data)
	var boards []BoardStats
	if err := json.Unmarshal(boardsBytes, &boards); err != nil {
		t.Fatalf("unmarshal top boards: %v", err)
	}
	if len(boards) == 0 {
		t.Fatalf("expected at least 1 board in top")
	}
	b := boards[0]
	if b.AIDHashQueries != 2 || b.AIDHashMisses != 1 {
		t.Errorf("expected AIDHashQueries=2, AIDHashMisses=1, got %d, %d", b.AIDHashQueries, b.AIDHashMisses)
	}
	if b.AIDLuaQueries != 1 {
		t.Errorf("expected AIDLuaQueries=1, got %d", b.AIDLuaQueries)
	}
	if b.AIDWebQueries != 1 {
		t.Errorf("expected AIDWebQueries=1, got %d", b.AIDWebQueries)
	}
	if b.SearchSR != 1 {
		t.Errorf("expected SearchSR=1, got %d", b.SearchSR)
	}
}

func TestQuerySearchNegativeOffset(t *testing.T) {
	tmpDir := t.TempDir()
	socketPath := filepath.Join(tmpDir, "run", "search.svc.sock")
	boardDir := filepath.Join(tmpDir, "boards", "T", "TestBoard")
	if err := os.MkdirAll(boardDir, 0755); err != nil {
		t.Fatalf("mkdir boardDir: %v", err)
	}
	dirPath := filepath.Join(boardDir, ".DIR")
	relDir := filepath.Join("boards", "T", "TestBoard", ".DIR")

	for i := 1; i <= 5; i++ {
		fn := fmt.Sprintf("M.170000000%d.A.00%d", i, i)
		if err := AppendTestFileheader(dirPath, fn, "tester", fmt.Sprintf("title post %d", i), 0, 10, 100); err != nil {
			t.Fatalf("append rec %d: %v", i, err)
		}
	}

	svc := newService(tmpDir, socketPath, nil)
	go func() {
		_ = svc.Start()
	}()
	defer svc.Stop()

	for i := 0; i < 50; i++ {
		if IsSocketOccupied(socketPath) {
			break
		}
		time.Sleep(10 * time.Millisecond)
	}

	predBytes := MakePredBytes(RS_KEYWORD, "title", 0, 0)

	// offset = -2, limit = 2 -> should return last 2 posts (recno 4 and 5)
	indices, total := queryBinaryClientWithSource(t, socketPath, 1, relDir, [][]byte{predBytes}, -2, 2, SrcBoarddWeb)
	if total != 5 {
		t.Fatalf("expected total 5, got %d", total)
	}
	if len(indices) != 2 || indices[0] != 4 || indices[1] != 5 {
		t.Fatalf("expected [4, 5], got %v", indices)
	}

	// offset = -4, limit = 2 -> should return recno 2 and 3
	indices, total = queryBinaryClientWithSource(t, socketPath, 1, relDir, [][]byte{predBytes}, -4, 2, SrcBoarddWeb)
	if total != 5 {
		t.Fatalf("expected total 5, got %d", total)
	}
	if len(indices) != 2 || indices[0] != 2 || indices[1] != 3 {
		t.Fatalf("expected [2, 3], got %v", indices)
	}

	// offset = -10, limit = 2 -> clamp to 0 -> should return recno 1 and 2
	indices, total = queryBinaryClientWithSource(t, socketPath, 1, relDir, [][]byte{predBytes}, -10, 2, SrcBoarddWeb)
	if total != 5 {
		t.Fatalf("expected total 5, got %d", total)
	}
	if len(indices) != 2 || indices[0] != 1 || indices[1] != 2 {
		t.Fatalf("expected [1, 2], got %v", indices)
	}
}

