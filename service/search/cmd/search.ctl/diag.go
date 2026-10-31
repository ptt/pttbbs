package main

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"encoding/json"
	"fmt"
	"os"
	"sort"
	"strconv"
	"strings"
	"text/tabwriter"
	"time"

	"pttbbs/search/daemon"
)

type diagSnapshot struct {
	CollectedAt   time.Time                   `json:"collected_at"`
	Hostname      string                      `json:"hostname"`
	SocketPath    string                      `json:"socket_path"`
	WindowSeconds float64                     `json:"window_seconds"`
	VerboseLevel  int                         `json:"verbose_level"`
	StatusBefore  *daemon.ServiceStats        `json:"status_before"`
	StatusAfter   *daemon.ServiceStats        `json:"status_after"`
	BoardsBefore  []daemon.BoardStats         `json:"boards_before"`
	BoardsAfter   []daemon.BoardStats         `json:"boards_after"`
	TopByMisses   []daemon.BoardStats         `json:"top_by_misses"`
	TopByTime     []daemon.BoardStats         `json:"top_by_time"`
	TopByAID      []daemon.BoardStats         `json:"top_by_aid"`
	TopByIndices  []daemon.BoardStats         `json:"top_by_indices"`
	Entries       []daemon.CachedEntryInfo    `json:"entries"`
	Backtrack     []daemon.BoardBacktrackInfo `json:"backtrack"`
}

type boardDelta struct {
	Board       string
	Bid         int32
	Direct      string
	SearchQ     int64
	SearchMiss  int64
	SearchSR    int64
	SearchWeb   int64
	AIDQ        int64
	AIDMiss     int64
	AIDHashQ    int64
	AIDHashMiss int64
	ScanMs      float64
}

func fetchTypedControl[T any](socketPath string, req daemon.ControlRequest, timeout time.Duration) (T, error) {
	var zero T
	resp, err := sendControlRequest(socketPath, req, timeout)
	if err != nil {
		return zero, err
	}
	if resp.Status != "ok" {
		return zero, fmt.Errorf("%s", resp.Message)
	}
	raw, err := json.Marshal(resp.Data)
	if err != nil {
		return zero, err
	}
	var out T
	if err := json.Unmarshal(raw, &out); err != nil {
		return zero, err
	}
	return out, nil
}

func fetchVerboseLevel(socketPath string) int {
	resp, err := sendControlRequest(socketPath, daemon.ControlRequest{Action: "verbose"}, 5*time.Second)
	if err != nil || resp.Status != "ok" {
		return -1
	}
	if m, ok := resp.Data.(map[string]interface{}); ok {
		if v, ok := m["verbose"].(float64); ok {
			return int(v)
		}
	}
	return -1
}

func computeBoardDeltas(before, after []daemon.BoardStats) []boardDelta {
	prevMap := make(map[string]daemon.BoardStats, len(before))
	for _, b := range before {
		key := b.Direct
		if key == "" {
			key = fmt.Sprintf("%s#%d", b.Board, b.Bid)
		}
		prevMap[key] = b
	}

	var deltas []boardDelta
	for _, cur := range after {
		key := cur.Direct
		if key == "" {
			key = fmt.Sprintf("%s#%d", cur.Board, cur.Bid)
		}
		prev, ok := prevMap[key]
		var d boardDelta
		d.Board = cur.Board
		d.Bid = cur.Bid
		d.Direct = cur.Direct
		if ok {
			d.SearchQ = cur.SearchQueries - prev.SearchQueries
			d.SearchMiss = cur.SearchMisses - prev.SearchMisses
			d.SearchSR = cur.SearchSR - prev.SearchSR
			d.SearchWeb = cur.SearchWeb - prev.SearchWeb
			d.AIDQ = cur.AIDQueries - prev.AIDQueries
			d.AIDMiss = cur.AIDMisses - prev.AIDMisses
			d.AIDHashQ = cur.AIDHashQueries - prev.AIDHashQueries
			d.AIDHashMiss = cur.AIDHashMisses - prev.AIDHashMisses
			d.ScanMs = cur.ScanDurationMs - prev.ScanDurationMs
		} else {
			d.SearchQ = cur.SearchQueries
			d.SearchMiss = cur.SearchMisses
			d.SearchSR = cur.SearchSR
			d.SearchWeb = cur.SearchWeb
			d.AIDQ = cur.AIDQueries
			d.AIDMiss = cur.AIDMisses
			d.AIDHashQ = cur.AIDHashQueries
			d.AIDHashMiss = cur.AIDHashMisses
			d.ScanMs = cur.ScanDurationMs
		}
		if d.SearchQ > 0 || d.AIDQ > 0 || d.ScanMs > 0.01 || d.SearchMiss > 0 || d.AIDMiss > 0 {
			deltas = append(deltas, d)
		}
	}

	sort.Slice(deltas, func(i, j int) bool {
		if deltas[i].ScanMs != deltas[j].ScanMs {
			return deltas[i].ScanMs > deltas[j].ScanMs
		}
		ti := deltas[i].SearchQ + deltas[i].AIDQ
		tj := deltas[j].SearchQ + deltas[j].AIDQ
		return ti > tj
	})
	return deltas
}

func formatBoardTable(buf *bytes.Buffer, title string, items []daemon.BoardStats) {
	fmt.Fprintf(buf, "\n================================================================================\n")
	fmt.Fprintf(buf, "%s\n", title)
	fmt.Fprintf(buf, "================================================================================\n")
	if len(items) == 0 {
		fmt.Fprintln(buf, "(none)")
		return
	}
	w := tabwriter.NewWriter(buf, 0, 0, 2, ' ', 0)
	fmt.Fprintln(w, "RANK\tBOARD\tBID\tMISSES\tQUERIES\tAID_MISS\tAID_QUERIES\tSCAN_TIME\tBACKTRACK\tENTRIES\tINDICES\tAID_CACHE")
	for i, item := range items {
		scanTimeStr := formatScanDuration(item.ScanDurationMs)
		btrackStr := "-"
		if item.MaxBacktrack >= 0 {
			btrackStr = fmt.Sprintf("%d", item.MaxBacktrack)
		}
		fmt.Fprintf(w, "%d\t%s\t%d\t%d\t%d\t%d\t%d\t%s\t%s\t%d\t%d\t%d\n",
			i+1, item.Board, item.Bid,
			item.SearchMisses, item.SearchQueries,
			item.AIDMisses, item.AIDQueries,
			scanTimeStr, btrackStr,
			item.CachedEntries, item.CachedIndices, item.CachedAID)
	}
	_ = w.Flush()
}

func renderLiveDeltaSection(buf *bytes.Buffer, snap *diagSnapshot) {
	secs := snap.WindowSeconds
	if secs <= 0 || snap.StatusBefore == nil || snap.StatusAfter == nil {
		return
	}
	b := snap.StatusBefore
	a := snap.StatusAfter

	dHits := a.Hits - b.Hits
	dMiss := a.Misses - b.Misses
	dIncr := a.IncrementalUpdates - b.IncrementalUpdates
	dChain := a.ChainedHits - b.ChainedHits
	dInval := a.Invalidations - b.Invalidations
	dEvict := a.Evictions - b.Evictions
	dSearchTotal := dHits + dMiss + dIncr + dChain

	dAIDHits := a.AIDHits - b.AIDHits
	dAIDNeg := a.AIDNegativeHits - b.AIDNegativeHits
	dAIDMiss := a.AIDMisses - b.AIDMisses
	dAIDTblHits := a.AIDTableHits - b.AIDTableHits
	dAIDTblEvict := a.AIDTableEvictions - b.AIDTableEvictions
	dAIDTotal := dAIDHits + dAIDNeg + dAIDMiss

	searchHitPct := 0.0
	if dSearchTotal > 0 {
		searchHitPct = float64(dHits+dIncr+dChain) * 100.0 / float64(dSearchTotal)
	}
	aidHitPct := 0.0
	if dAIDTotal > 0 {
		aidHitPct = float64(dAIDHits+dAIDNeg+dAIDTblHits) * 100.0 / float64(dAIDTotal)
	}

	fmt.Fprintf(buf, "================================================================================\n")
	fmt.Fprintf(buf, "LIVE WINDOW ACTIVITY (%.2fs sample)\n", secs)
	fmt.Fprintf(buf, "================================================================================\n")
	fmt.Fprintf(buf, "Search QPS:    %8.1f/s  (total=%d, hit+incr=%.1f%%, hits=%.1f/s, incr=%.1f/s, chain=%.1f/s, full_miss=%.2f/s)\n",
		float64(dSearchTotal)/secs, dSearchTotal, searchHitPct,
		float64(dHits)/secs, float64(dIncr)/secs, float64(dChain)/secs, float64(dMiss)/secs)
	fmt.Fprintf(buf, "Cache Churn:   inval=%.2f/s (%d), evict=%.2f/s (%d), aid_tbl_evict=%.2f/s (%d)\n",
		float64(dInval)/secs, dInval, float64(dEvict)/secs, dEvict, float64(dAIDTblEvict)/secs, dAIDTblEvict)
	fmt.Fprintf(buf, "AID QPS:       %8.1f/s  (total=%d, eff_hit=%.1f%%, lru_hit=%.1f/s, neg_hit=%.1f/s, tbl_hit=%.1f/s, disk_miss=%.2f/s)\n",
		float64(dAIDTotal)/secs, dAIDTotal, aidHitPct,
		float64(dAIDHits)/secs, float64(dAIDNeg)/secs, float64(dAIDTblHits)/secs, float64(dAIDMiss-dAIDTblHits)/secs)

	fmt.Fprintf(buf, "\n--- Live Window Breakdown by Source (per second) ---\n")
	w := tabwriter.NewWriter(buf, 0, 0, 2, ' ', 0)
	fmt.Fprintln(w, "SOURCE\tAID_Q/s\tAID_MISS/s\tSEARCH_Q/s\tAID_DELTA\tSEARCH_DELTA")
	srcOrder := []int32{
		daemon.SrcMbbsdHash,
		daemon.SrcMbbsdSR,
		daemon.SrcMbbsdLua,
		daemon.SrcBoarddWeb,
		daemon.SrcExternal,
		daemon.SrcUnknown,
	}
	for _, src := range srcOrder {
		name := daemon.SourceName(src)
		dqAID := a.AIDSources[name] - b.AIDSources[name]
		dmAID := a.AIDMissSources[name] - b.AIDMissSources[name]
		dqSearch := a.SearchSources[name] - b.SearchSources[name]
		if dqAID == 0 && dmAID == 0 && dqSearch == 0 {
			continue
		}
		fmt.Fprintf(w, "%s\t%.1f\t%.2f\t%.1f\t%d\t%d\n",
			name, float64(dqAID)/secs, float64(dmAID)/secs, float64(dqSearch)/secs, dqAID, dqSearch)
	}
	_ = w.Flush()

	deltas := computeBoardDeltas(snap.BoardsBefore, snap.BoardsAfter)
	fmt.Fprintf(buf, "\n--- Active Boards in Live Window (top %d of %d active) ---\n", minInt(30, len(deltas)), len(deltas))
	if len(deltas) == 0 {
		fmt.Fprintln(buf, "(no board activity observed in window)")
		return
	}
	w2 := tabwriter.NewWriter(buf, 0, 0, 2, ' ', 0)
	fmt.Fprintln(w2, "RANK\tBOARD\tBID\tSR_Q/s\tWEB_Q/s\tMISS/s\tAID_Q/s\tAID_MISS/s\tSCAN_MS/s\tSCAN_TOTAL")
	for i, d := range deltas {
		if i >= 30 {
			break
		}
		fmt.Fprintf(w2, "%d\t%s\t%d\t%.1f\t%.1f\t%.2f\t%.1f\t%.2f\t%.1fms/s\t%s\n",
			i+1, d.Board, d.Bid,
			float64(d.SearchSR)/secs,
			float64(d.SearchWeb)/secs,
			float64(d.SearchMiss)/secs,
			float64(d.AIDQ)/secs,
			float64(d.AIDMiss)/secs,
			d.ScanMs/secs,
			formatScanDuration(d.ScanMs))
	}
	_ = w2.Flush()
}

func minInt(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func buildDiagSummaryText(snap *diagSnapshot, heapText, goroutineText string) string {
	var buf bytes.Buffer

	fmt.Fprintf(&buf, "================================================================================\n")
	fmt.Fprintf(&buf, "SEARCH.SVC DIAGNOSTIC REPORT\n")
	fmt.Fprintf(&buf, "================================================================================\n")
	fmt.Fprintf(&buf, "Collected At:   %s\n", snap.CollectedAt.Format(time.RFC3339))
	fmt.Fprintf(&buf, "Hostname:       %s\n", snap.Hostname)
	fmt.Fprintf(&buf, "Socket:         %s\n", snap.SocketPath)
	fmt.Fprintf(&buf, "Sample Window:  %.2fs\n", snap.WindowSeconds)
	fmt.Fprintf(&buf, "Verbose Level:  %d\n\n", snap.VerboseLevel)

	renderLiveDeltaSection(&buf, snap)

	fmt.Fprintf(&buf, "\n================================================================================\n")
	fmt.Fprintf(&buf, "DAEMON STATUS (AFTER WINDOW)\n")
	fmt.Fprintf(&buf, "================================================================================\n")
	if statusJSON, err := json.MarshalIndent(snap.StatusAfter, "", "  "); err == nil {
		buf.Write(statusJSON)
		buf.WriteByte('\n')
	}

	if snap.StatusAfter != nil {
		fmt.Fprintf(&buf, "\n================================================================================\n")
		fmt.Fprintf(&buf, "CUMULATIVE GLOBAL QUERY BREAKDOWN BY SOURCE\n")
		fmt.Fprintf(&buf, "================================================================================\n")
		w := tabwriter.NewWriter(&buf, 0, 0, 2, ' ', 0)
		fmt.Fprintln(w, "SOURCE\tAID_QUERIES\tAID_MISSES\tSEARCH_QUERIES")
		srcOrder := []int32{
			daemon.SrcMbbsdHash,
			daemon.SrcMbbsdSR,
			daemon.SrcMbbsdLua,
			daemon.SrcBoarddWeb,
			daemon.SrcExternal,
			daemon.SrcUnknown,
		}
		var totAIDQ, totAIDM, totSearchQ int64
		for _, src := range srcOrder {
			name := daemon.SourceName(src)
			aq := snap.StatusAfter.AIDSources[name]
			am := snap.StatusAfter.AIDMissSources[name]
			sq := snap.StatusAfter.SearchSources[name]
			totAIDQ += aq
			totAIDM += am
			totSearchQ += sq
			fmt.Fprintf(w, "%s\t%d\t%d\t%d\n", name, aq, am, sq)
		}
		fmt.Fprintf(w, "TOTAL\t%d\t%d\t%d\n", totAIDQ, totAIDM, totSearchQ)
		_ = w.Flush()

		fmt.Fprintf(&buf, "\n================================================================================\n")
		fmt.Fprintf(&buf, "CUMULATIVE BOARD SOURCE BREAKDOWN (TOP 50 BY QUERIES)\n")
		fmt.Fprintf(&buf, "================================================================================\n")
		w2 := tabwriter.NewWriter(&buf, 0, 0, 2, ' ', 0)
		fmt.Fprintln(w2, "RANK\tBOARD\tBID\tHASH_Q\tHASH_MISS\tLUA_Q\tWEB_Q\tSEARCH_SR\tSEARCH_WEB\tSCAN_TIME")
		for i, b := range snap.BoardsAfter {
			if i >= 50 {
				break
			}
			fmt.Fprintf(w2, "%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n",
				i+1, b.Board, b.Bid,
				b.AIDHashQueries, b.AIDHashMisses,
				b.AIDLuaQueries, b.AIDWebQueries,
				b.SearchSR, b.SearchWeb,
				formatScanDuration(b.ScanDurationMs))
		}
		_ = w2.Flush()
	}

	formatBoardTable(&buf, "TOP BOARDS BY SCAN TIME (TOP 30)", snap.TopByTime)
	formatBoardTable(&buf, "TOP BOARDS BY SEARCH MISSES (TOP 30)", snap.TopByMisses)
	formatBoardTable(&buf, "TOP BOARDS BY AID QUERIES (TOP 30)", snap.TopByAID)
	formatBoardTable(&buf, "TOP BOARDS BY CACHED INDICES MEMORY (TOP 30)", snap.TopByIndices)

	fmt.Fprintf(&buf, "\n================================================================================\n")
	fmt.Fprintf(&buf, "TOP CACHED PREDICATE ENTRIES (TOP %d)\n", len(snap.Entries))
	fmt.Fprintf(&buf, "================================================================================\n")
	if len(snap.Entries) == 0 {
		fmt.Fprintln(&buf, "(none)")
	} else {
		we := tabwriter.NewWriter(&buf, 0, 0, 2, ' ', 0)
		fmt.Fprintln(we, "BOARD\tBID\tMATCHES\tSCANNED\tAGE\tPREDICATES")
		for _, item := range snap.Entries {
			fmt.Fprintf(we, "%s\t%d\t%d\t%d\t%s\t%s\n",
				item.Board, item.Bid, item.Matches, item.ScannedRecs, formatTimeAgo(item.CreatedAt), item.Predicates)
		}
		_ = we.Flush()
	}

	fmt.Fprintf(&buf, "\n================================================================================\n")
	fmt.Fprintf(&buf, "CACHED BOARD BACKTRACK INFO (%d boards)\n", len(snap.Backtrack))
	fmt.Fprintf(&buf, "================================================================================\n")
	if len(snap.Backtrack) == 0 {
		fmt.Fprintln(&buf, "(none)")
	} else {
		wb := tabwriter.NewWriter(&buf, 0, 0, 2, ' ', 0)
		fmt.Fprintln(wb, "BOARD\tBID\tTOTAL_RECS\tMAX_BACKTRACK\tMAX_TIME_DIFF\tLAST_SCANNED")
		for i, item := range snap.Backtrack {
			if i >= 50 {
				break
			}
			fmt.Fprintf(wb, "%s\t%d\t%d\t%d\t%s\t%s\n",
				item.Board, item.Bid, item.TotalRecs, item.MaxBacktrack,
				formatTimeDiff(item.MaxTimeDiffSecs), formatTimeAgo(item.LastScanned))
		}
		_ = wb.Flush()
	}

	if heapText != "" {
		fmt.Fprintf(&buf, "\n================================================================================\n")
		fmt.Fprintf(&buf, "HEAP PROFILE SUMMARY (debug=1)\n")
		fmt.Fprintf(&buf, "================================================================================\n")
		buf.WriteString(heapText)
		if !strings.HasSuffix(heapText, "\n") {
			buf.WriteByte('\n')
		}
	}

	if goroutineText != "" {
		fmt.Fprintf(&buf, "\n================================================================================\n")
		fmt.Fprintf(&buf, "GOROUTINE DUMP (debug=2)\n")
		fmt.Fprintf(&buf, "================================================================================\n")
		buf.WriteString(goroutineText)
		if !strings.HasSuffix(goroutineText, "\n") {
			buf.WriteByte('\n')
		}
	}

	return buf.String()
}

func writeTarFile(tw *tar.Writer, name string, data []byte, now time.Time) error {
	hdr := &tar.Header{
		Name:    name,
		Mode:    0644,
		Size:    int64(len(data)),
		ModTime: now,
	}
	if err := tw.WriteHeader(hdr); err != nil {
		return err
	}
	_, err := tw.Write(data)
	return err
}

func runDiag(socketPath string, seconds int, outFile string, collectPprof bool) (string, string, error) {
	hostname, _ := os.Hostname()
	now := time.Now()
	if outFile == "" {
		outFile = fmt.Sprintf("search-diag-%s.tar.gz", now.Format("20060102-150405"))
	}

	verboseLevel := fetchVerboseLevel(socketPath)

	statusBefore, err := fetchTypedControl[daemon.ServiceStats](socketPath, daemon.ControlRequest{Action: "status"}, 10*time.Second)
	if err != nil {
		return "", "", fmt.Errorf("fetch initial status: %w", err)
	}
	boardsBefore, _ := fetchTypedControl[[]daemon.BoardStats](socketPath, daemon.ControlRequest{
		Action: "top",
		Limit:  500,
		SortBy: "queries",
	}, 10*time.Second)

	tStart := time.Now()
	var cpuBytes []byte
	if seconds > 0 {
		if collectPprof {
			fmt.Printf("Collecting %ds CPU profile + live QPS delta from search.svc...\n", seconds)
			cpuRes, err := fetchTypedControl[daemon.PprofResult](socketPath, daemon.ControlRequest{
				Action:  "pprof",
				Profile: "cpu",
				Seconds: seconds,
			}, time.Duration(seconds+20)*time.Second)
			if err != nil {
				fmt.Fprintf(os.Stderr, "Warning: CPU profile failed (%v), falling back to sleep window\n", err)
				if rem := time.Duration(seconds)*time.Second - time.Since(tStart); rem > 0 {
					time.Sleep(rem)
				}
			} else {
				cpuBytes = cpuRes.Bytes
			}
		} else {
			fmt.Printf("Sampling %ds live QPS delta from search.svc...\n", seconds)
			time.Sleep(time.Duration(seconds) * time.Second)
		}
	}
	windowSecs := time.Since(tStart).Seconds()

	statusAfter, err := fetchTypedControl[daemon.ServiceStats](socketPath, daemon.ControlRequest{Action: "status"}, 10*time.Second)
	if err != nil {
		return "", "", fmt.Errorf("fetch post-window status: %w", err)
	}
	boardsAfter, _ := fetchTypedControl[[]daemon.BoardStats](socketPath, daemon.ControlRequest{
		Action: "top",
		Limit:  500,
		SortBy: "queries",
	}, 10*time.Second)

	topByTime, _ := fetchTypedControl[[]daemon.BoardStats](socketPath, daemon.ControlRequest{Action: "top", Limit: 30, SortBy: "time"}, 10*time.Second)
	topByMisses, _ := fetchTypedControl[[]daemon.BoardStats](socketPath, daemon.ControlRequest{Action: "top", Limit: 30, SortBy: "misses"}, 10*time.Second)
	topByAID, _ := fetchTypedControl[[]daemon.BoardStats](socketPath, daemon.ControlRequest{Action: "top", Limit: 30, SortBy: "aid"}, 10*time.Second)
	topByIndices, _ := fetchTypedControl[[]daemon.BoardStats](socketPath, daemon.ControlRequest{Action: "top", Limit: 30, SortBy: "indices"}, 10*time.Second)
	entries, _ := fetchTypedControl[[]daemon.CachedEntryInfo](socketPath, daemon.ControlRequest{Action: "entries", Limit: 200}, 10*time.Second)
	backtrack, _ := fetchTypedControl[[]daemon.BoardBacktrackInfo](socketPath, daemon.ControlRequest{Action: "backtrack"}, 10*time.Second)

	var goroutineText, heapText string
	var heapBytes, mutexBytes []byte
	if gr, err := fetchTypedControl[daemon.PprofResult](socketPath, daemon.ControlRequest{Action: "pprof", Profile: "goroutine", Debug: 2}, 15*time.Second); err == nil {
		goroutineText = gr.Text
	}
	if hpTxt, err := fetchTypedControl[daemon.PprofResult](socketPath, daemon.ControlRequest{Action: "pprof", Profile: "heap", Debug: 1}, 15*time.Second); err == nil {
		heapText = hpTxt.Text
	}
	if collectPprof {
		if hpBin, err := fetchTypedControl[daemon.PprofResult](socketPath, daemon.ControlRequest{Action: "pprof", Profile: "heap", Debug: 0}, 15*time.Second); err == nil {
			heapBytes = hpBin.Bytes
		}
		if muBin, err := fetchTypedControl[daemon.PprofResult](socketPath, daemon.ControlRequest{Action: "pprof", Profile: "mutex", Debug: 0}, 15*time.Second); err == nil {
			mutexBytes = muBin.Bytes
		}
	}

	snap := &diagSnapshot{
		CollectedAt:   now,
		Hostname:      hostname,
		SocketPath:    socketPath,
		WindowSeconds: windowSecs,
		VerboseLevel:  verboseLevel,
		StatusBefore:  &statusBefore,
		StatusAfter:   &statusAfter,
		BoardsBefore:  boardsBefore,
		BoardsAfter:   boardsAfter,
		TopByMisses:   topByMisses,
		TopByTime:     topByTime,
		TopByAID:      topByAID,
		TopByIndices:  topByIndices,
		Entries:       entries,
		Backtrack:     backtrack,
	}

	summaryText := buildDiagSummaryText(snap, heapText, goroutineText)

	var livePreview bytes.Buffer
	renderLiveDeltaSection(&livePreview, snap)

	lowerOut := strings.ToLower(outFile)
	if strings.HasSuffix(lowerOut, ".txt") || strings.HasSuffix(lowerOut, ".log") {
		if err := os.WriteFile(outFile, []byte(summaryText), 0644); err != nil {
			return "", "", err
		}
		return outFile, livePreview.String(), nil
	}

	var archiveBuf bytes.Buffer
	gzw := gzip.NewWriter(&archiveBuf)
	tw := tar.NewWriter(gzw)

	if err := writeTarFile(tw, "summary.txt", []byte(summaryText), now); err != nil {
		return "", "", err
	}
	if snapJSON, err := json.MarshalIndent(snap, "", "  "); err == nil {
		if err := writeTarFile(tw, "snapshot.json", snapJSON, now); err != nil {
			return "", "", err
		}
	}
	if len(cpuBytes) > 0 {
		if err := writeTarFile(tw, "cpu.pprof", cpuBytes, now); err != nil {
			return "", "", err
		}
	}
	if len(heapBytes) > 0 {
		if err := writeTarFile(tw, "heap.pprof", heapBytes, now); err != nil {
			return "", "", err
		}
	}
	if len(mutexBytes) > 0 {
		if err := writeTarFile(tw, "mutex.pprof", mutexBytes, now); err != nil {
			return "", "", err
		}
	}
	if err := tw.Close(); err != nil {
		return "", "", err
	}
	if err := gzw.Close(); err != nil {
		return "", "", err
	}
	if err := os.WriteFile(outFile, archiveBuf.Bytes(), 0644); err != nil {
		return "", "", err
	}
	return outFile, livePreview.String(), nil
}

func handleDiag(socketPath string, args []string) {
	seconds := 10
	outFile := ""
	collectPprof := true

	for i := 0; i < len(args); i++ {
		arg := args[i]
		if (arg == "-out" || arg == "--out" || arg == "-o") && i+1 < len(args) {
			i++
			outFile = args[i]
		} else if (arg == "-seconds" || arg == "--seconds" || arg == "-s") && i+1 < len(args) {
			i++
			if n, err := strconv.Atoi(args[i]); err == nil && n >= 0 {
				seconds = n
			}
		} else if arg == "-no-pprof" || arg == "--no-pprof" {
			collectPprof = false
		} else if n, err := strconv.Atoi(arg); err == nil && n >= 0 {
			seconds = n
		}
	}

	savedPath, preview, err := runDiag(socketPath, seconds, outFile, collectPprof)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error collecting diagnostics: %v\n", err)
		os.Exit(1)
	}
	if preview != "" {
		fmt.Println()
		fmt.Print(preview)
	}
	fmt.Printf("\nSaved diagnostic bundle to: %s\n", savedPath)
}
