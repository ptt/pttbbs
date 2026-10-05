package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"text/tabwriter"
	"time"

	"pttbbs/bbs"
	"pttbbs/search/daemon"
)

func sendControlRequest(socketPath string, req daemon.ControlRequest, timeout time.Duration) (*daemon.ControlResponse, error) {
	conn, err := net.Dial("unix", socketPath)
	if err != nil {
		return nil, fmt.Errorf("failed to connect to %s: %w", socketPath, err)
	}
	defer conn.Close()

	if timeout > 0 {
		_ = conn.SetDeadline(time.Now().Add(timeout))
	}

	if err := json.NewEncoder(conn).Encode(req); err != nil {
		return nil, err
	}

	var resp daemon.ControlResponse
	if err := json.NewDecoder(conn).Decode(&resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

func formatScanDuration(ms float64) string {
	if ms <= 0.0 {
		return "0s"
	}
	if ms < 1.0 {
		return fmt.Sprintf("%.2fms", ms)
	}
	if ms < 1000.0 {
		return fmt.Sprintf("%.1fms", ms)
	}
	sec := ms / 1000.0
	if sec < 60.0 {
		return fmt.Sprintf("%.2fs", sec)
	}
	m := int(sec) / 60
	s := sec - float64(m*60)
	return fmt.Sprintf("%dm%.1fs", m, s)
}

func formatTimeDiff(secs int64) string {
	if secs <= 0 {
		return "0s"
	}
	if secs < 60 {
		return fmt.Sprintf("%ds", secs)
	}
	if secs < 3600 {
		return fmt.Sprintf("%dm%ds", secs/60, secs%60)
	}
	h := secs / 3600
	m := (secs % 3600) / 60
	return fmt.Sprintf("%dh%dm", h, m)
}

func formatTimeAgo(t time.Time) string {
	if t.IsZero() {
		return "-"
	}
	d := time.Since(t)
	if d < time.Minute {
		return fmt.Sprintf("%ds ago", int(d.Seconds()))
	}
	if d < time.Hour {
		return fmt.Sprintf("%dm ago", int(d.Minutes()))
	}
	return fmt.Sprintf("%dh ago", int(d.Hours()))
}

func handleTop(socketPath string, args []string) {
	limit := 20
	sortBy := "misses"
	jsonOutput := false
	showSources := false

	for i := 0; i < len(args); i++ {
		arg := args[i]
		if arg == "-sources" || arg == "--sources" {
			showSources = true
		} else if arg == "-json" || arg == "--json" {
			jsonOutput = true
		} else if arg == "-n" && i+1 < len(args) {
			i++
			if n, err := strconv.Atoi(args[i]); err == nil && n > 0 {
				limit = n
			}
		} else if arg == "-by" && i+1 < len(args) {
			i++
			sortBy = args[i]
		} else if n, err := strconv.Atoi(arg); err == nil && n > 0 {
			limit = n
		} else if arg == "misses" || arg == "queries" || arg == "time" || arg == "indices" || arg == "entries" || arg == "aid" || arg == "backtrack" {
			sortBy = arg
		}
	}

	req := daemon.ControlRequest{
		Action: "top",
		Limit:  limit,
		SortBy: sortBy,
	}

	resp, err := sendControlRequest(socketPath, req, 10*time.Second)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
	if resp.Status != "ok" {
		fmt.Fprintf(os.Stderr, "Error: %s\n", resp.Message)
		os.Exit(1)
	}

	if jsonOutput {
		out, _ := json.MarshalIndent(resp, "", "  ")
		fmt.Println(string(out))
		return
	}

	dataBytes, _ := json.Marshal(resp.Data)
	var items []daemon.BoardStats
	if err := json.Unmarshal(dataBytes, &items); err != nil {
		fmt.Fprintf(os.Stderr, "Error parsing response: %v\n", err)
		os.Exit(1)
	}

	if len(items) == 0 {
		fmt.Println("No board activity or cached entries yet.")
		return
	}

	w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
	if showSources {
		fmt.Fprintln(w, "RANK\tBOARD\tBID\tHASH_Q\tHASH_MISS\tLUA_Q\tWEB_Q\tSEARCH_SR\tSEARCH_WEB\tSCAN_TIME")
		for i, item := range items {
			scanTimeStr := formatScanDuration(item.ScanDurationMs)
			fmt.Fprintf(w, "%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n",
				i+1, item.Board, item.Bid,
				item.AIDHashQueries, item.AIDHashMisses,
				item.AIDLuaQueries, item.AIDWebQueries,
				item.SearchSR, item.SearchWeb,
				scanTimeStr)
		}
	} else {
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
				scanTimeStr,
				btrackStr,
				item.CachedEntries, item.CachedIndices, item.CachedAID)
		}
	}
	w.Flush()
}

func handleSources(socketPath string, args []string) {
	limit := 50
	jsonOutput := false
	var filterBoard string

	for i := 0; i < len(args); i++ {
		arg := args[i]
		if arg == "-json" || arg == "--json" {
			jsonOutput = true
		} else if arg == "-n" && i+1 < len(args) {
			i++
			if n, err := strconv.Atoi(args[i]); err == nil && n > 0 {
				limit = n
			}
		} else if strings.HasPrefix(arg, "-") {
			// ignore unknown flags
		} else {
			filterBoard = arg
		}
	}

	statusReq := daemon.ControlRequest{Action: "status"}
	statusResp, err := sendControlRequest(socketPath, statusReq, 5*time.Second)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error fetching status: %v\n", err)
		os.Exit(1)
	}

	topReq := daemon.ControlRequest{
		Action: "top",
		Limit:  limit,
		SortBy: "queries",
	}
	topResp, err := sendControlRequest(socketPath, topReq, 10*time.Second)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error fetching top boards: %v\n", err)
		os.Exit(1)
	}

	if jsonOutput {
		combined := map[string]interface{}{
			"global": statusResp.Data,
			"boards": topResp.Data,
		}
		out, _ := json.MarshalIndent(combined, "", "  ")
		fmt.Println(string(out))
		return
	}

	var stats daemon.ServiceStats
	statusBytes, _ := json.Marshal(statusResp.Data)
	_ = json.Unmarshal(statusBytes, &stats)

	var boards []daemon.BoardStats
	topBytes, _ := json.Marshal(topResp.Data)
	_ = json.Unmarshal(topBytes, &boards)

	fmt.Println("================================================================================")
	fmt.Println("GLOBAL QUERY BREAKDOWN BY SOURCE")
	fmt.Println("================================================================================")
	w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
	fmt.Fprintln(w, "SOURCE\tDESCRIPTION\tAID_QUERIES\tAID_MISSES\tSEARCH_QUERIES")

	srcOrder := []int32{
		daemon.SrcMbbsdHash,
		daemon.SrcMbbsdSR,
		daemon.SrcMbbsdLua,
		daemon.SrcBoarddWeb,
		daemon.SrcExternal,
		daemon.SrcUnknown,
	}
	srcDesc := map[int32]string{
		daemon.SrcMbbsdHash: "mbbsd # (manual/app/bot)",
		daemon.SrcMbbsdSR:   "mbbsd select_read (title/author/etc)",
		daemon.SrcMbbsdLua:  "bbslua banner template",
		daemon.SrcBoarddWeb: "web / boardd",
		daemon.SrcExternal:  "external / maintenance",
		daemon.SrcUnknown:   "unknown / legacy untagged",
	}

	var totAIDQ, totAIDM, totSearchQ int64
	for _, src := range srcOrder {
		name := daemon.SourceName(src)
		aidQ := stats.AIDSources[name]
		aidM := stats.AIDMissSources[name]
		searchQ := stats.SearchSources[name]
		totAIDQ += aidQ
		totAIDM += aidM
		totSearchQ += searchQ
		fmt.Fprintf(w, "%s\t%s\t%d\t%d\t%d\n", name, srcDesc[src], aidQ, aidM, searchQ)
	}
	fmt.Fprintf(w, "----------------\t---------------------------\t-----------\t-----------\t--------------\n")
	fmt.Fprintf(w, "TOTAL\t\t%d\t%d\t%d\n", totAIDQ, totAIDM, totSearchQ)
	w.Flush()

	fmt.Println("\n================================================================================")
	if filterBoard != "" {
		fmt.Printf("BOARD QUERY BREAKDOWN (%s)\n", filterBoard)
	} else {
		fmt.Println("BOARD QUERY BREAKDOWN")
	}
	fmt.Println("================================================================================")
	w2 := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
	fmt.Fprintln(w2, "RANK\tBOARD\tBID\tHASH_Q\tHASH_MISS\tLUA_Q\tWEB_Q\tSEARCH_SR\tSEARCH_WEB")
	rank := 1
	for _, b := range boards {
		if filterBoard != "" && !strings.EqualFold(b.Board, filterBoard) {
			continue
		}
		fmt.Fprintf(w2, "%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
			rank, b.Board, b.Bid,
			b.AIDHashQueries, b.AIDHashMisses,
			b.AIDLuaQueries, b.AIDWebQueries,
			b.SearchSR, b.SearchWeb)
		rank++
	}
	w2.Flush()
}

func handleBacktrack(socketPath string, args []string) {
	req := daemon.ControlRequest{
		Action: "backtrack",
	}
	resp, err := sendControlRequest(socketPath, req, 10*time.Second)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
	if resp.Status != "ok" {
		fmt.Fprintf(os.Stderr, "Error: %s\n", resp.Message)
		os.Exit(1)
	}

	jsonOutput := false
	var filterBoard string
	for _, arg := range args {
		if arg == "-json" || arg == "--json" {
			jsonOutput = true
		} else {
			filterBoard = arg
		}
	}

	if jsonOutput {
		out, _ := json.MarshalIndent(resp, "", "  ")
		fmt.Println(string(out))
		return
	}

	dataBytes, _ := json.Marshal(resp.Data)
	var items []daemon.BoardBacktrackInfo
	if err := json.Unmarshal(dataBytes, &items); err != nil {
		fmt.Fprintf(os.Stderr, "Error parsing response: %v\n", err)
		os.Exit(1)
	}

	if len(items) == 0 {
		fmt.Println("No board backtrack info cached yet.")
		return
	}

	w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
	fmt.Fprintln(w, "BOARD\tBID\tTOTAL_RECS\tMAX_BACKTRACK\tMAX_TIME_DIFF\tLAST_SCANNED")
	for _, item := range items {
		if filterBoard != "" && !strings.EqualFold(item.Board, filterBoard) && !strings.Contains(item.Direct, filterBoard) {
			continue
		}
		tdiffStr := formatTimeDiff(item.MaxTimeDiffSecs)
		timeAgo := formatTimeAgo(item.LastScanned)
		fmt.Fprintf(w, "%s\t%d\t%d\t%d\t%s\t%s\n",
			item.Board, item.Bid, item.TotalRecs,
			item.MaxBacktrack, tdiffStr, timeAgo)
	}
	w.Flush()
}

func handleEntries(socketPath string, args []string) {
	limit := 50
	jsonOutput := false
	var filterBoard string

	for i := 0; i < len(args); i++ {
		arg := args[i]
		if arg == "-json" || arg == "--json" {
			jsonOutput = true
		} else if arg == "-n" && i+1 < len(args) {
			i++
			if n, err := strconv.Atoi(args[i]); err == nil && n > 0 {
				limit = n
			}
		} else if n, err := strconv.Atoi(arg); err == nil && n > 0 && filterBoard != "" {
			limit = n
		} else if !strings.HasPrefix(arg, "-") && filterBoard == "" {
			filterBoard = arg
		}
	}

	req := daemon.ControlRequest{
		Action: "entries",
		Board:  filterBoard,
		Limit:  limit,
	}

	resp, err := sendControlRequest(socketPath, req, 10*time.Second)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
	if resp.Status != "ok" {
		fmt.Fprintf(os.Stderr, "Error: %s\n", resp.Message)
		os.Exit(1)
	}

	if jsonOutput {
		out, _ := json.MarshalIndent(resp, "", "  ")
		fmt.Println(string(out))
		return
	}

	dataBytes, _ := json.Marshal(resp.Data)
	var items []daemon.CachedEntryInfo
	if err := json.Unmarshal(dataBytes, &items); err != nil {
		fmt.Fprintf(os.Stderr, "Error parsing response: %v\n", err)
		os.Exit(1)
	}

	if len(items) == 0 {
		fmt.Println("No cached search entries found.")
		return
	}

	w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
	fmt.Fprintln(w, "BOARD\tBID\tMATCHES\tSCANNED\tAGE\tPREDICATES")
	for _, item := range items {
		ageStr := formatTimeAgo(item.CreatedAt)
		fmt.Fprintf(w, "%s\t%d\t%d\t%d\t%s\t%s\n",
			item.Board, item.Bid, item.Matches, item.ScannedRecs, ageStr, item.Predicates)
	}
	w.Flush()
}

func handleVerbose(socketPath string, args []string) {
	req := daemon.ControlRequest{
		Action: "verbose",
	}
	jsonOutput := false
	for _, arg := range args {
		if arg == "-json" || arg == "--json" {
			jsonOutput = true
		} else if n, err := strconv.Atoi(arg); err == nil {
			req.Level = &n
		}
	}

	resp, err := sendControlRequest(socketPath, req, 5*time.Second)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
	if resp.Status != "ok" {
		fmt.Fprintf(os.Stderr, "Error: %s\n", resp.Message)
		os.Exit(1)
	}

	if jsonOutput {
		out, _ := json.MarshalIndent(resp, "", "  ")
		fmt.Println(string(out))
		return
	}

	dataMap, _ := resp.Data.(map[string]interface{})
	currentLevel := 0
	if dataMap != nil {
		if v, ok := dataMap["verbose"].(float64); ok {
			currentLevel = int(v)
		}
	}

	if req.Level != nil {
		fmt.Printf("Verbose level set to %d\n", currentLevel)
	} else {
		fmt.Printf("Current verbose level: %d\n", currentLevel)
	}
}

func handlePprof(socketPath string, args []string) {
	if len(args) == 0 {
		fmt.Println("Usage: search.ctl pprof <cpu|heap|goroutine|block|mutex> [seconds] [-out file] [-debug 0|1|2]")
		os.Exit(1)
	}

	profileType := args[0]
	seconds := 10
	debug := 0
	outFile := ""

	if profileType == "goroutine" {
		debug = 2 // default to text dump
	}

	for i := 1; i < len(args); i++ {
		arg := args[i]
		if arg == "-out" && i+1 < len(args) {
			i++
			outFile = args[i]
		} else if arg == "-debug" && i+1 < len(args) {
			i++
			_, _ = fmt.Sscanf(args[i], "%d", &debug)
		} else if n, err := strconv.Atoi(arg); err == nil && n > 0 {
			seconds = n
		}
	}

	req := daemon.ControlRequest{
		Action:  "pprof",
		Profile: profileType,
		Seconds: seconds,
		Debug:   debug,
	}

	timeout := time.Duration(seconds+20) * time.Second
	if profileType == "cpu" {
		fmt.Printf("Sampling CPU profile for %d seconds from search.svc...\n", seconds)
	}

	resp, err := sendControlRequest(socketPath, req, timeout)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
	if resp.Status != "ok" {
		fmt.Fprintf(os.Stderr, "Error: %s\n", resp.Message)
		os.Exit(1)
	}

	dataBytes, _ := json.Marshal(resp.Data)
	var res daemon.PprofResult
	if err := json.Unmarshal(dataBytes, &res); err != nil {
		fmt.Fprintf(os.Stderr, "Error parsing pprof response: %v\n", err)
		os.Exit(1)
	}

	if res.IsText {
		if outFile != "" {
			if err := os.WriteFile(outFile, []byte(res.Text), 0644); err != nil {
				fmt.Fprintf(os.Stderr, "Failed to write %s: %v\n", outFile, err)
				os.Exit(1)
			}
			fmt.Printf("Saved %s dump to %s\n", profileType, outFile)
		} else {
			fmt.Print(res.Text)
		}
		return
	}

	if outFile == "" {
		outFile = fmt.Sprintf("%s.pprof", profileType)
	}

	if err := os.WriteFile(outFile, res.Bytes, 0644); err != nil {
		fmt.Fprintf(os.Stderr, "Failed to write %s: %v\n", outFile, err)
		os.Exit(1)
	}
	fmt.Printf("Saved %s profile to %s (analyze with: go tool pprof %s)\n", profileType, outFile, outFile)
}

func printUsage() {
	fmt.Println("Usage: search.ctl [-socket path] <action> [args...]")
	fmt.Println("Actions:")
	fmt.Println("  status, stats                        Show cache statistics, memory usage, and uptime")
	fmt.Println("  sources [board] [-n limit] [-json]   Show query sources breakdown (mbbsd hash, sr, lua, web)")
	fmt.Println("  top [-n limit] [-by sort] [-sources] Show top boards by activity / cache usage")
	fmt.Println("                                       (sort: misses, queries, time, indices, entries, aid, backtrack)")
	fmt.Println("  entries [board] [-n limit] [-json]   Show cached search predicate entries and matches")
	fmt.Println("  backtrack [board] [-json]            Show cached max backtrack ranges for boards")
	fmt.Println("  verbose [level] [-json]              Get or set verbose logging level (0=off, 1=miss/slow/inval, 2=all)")
	fmt.Println("  pprof <type> [seconds] [-out file]   Collect diagnostic profile (cpu, heap, goroutine, block, mutex)")
	fmt.Println("  flush [bid]                          Flush all cached search indices (or for a specific board ID)")
}

func main() {
	bbsHome := os.Getenv("BBSHOME")
	if bbsHome == "" {
		bbsHome = bbs.BBSHome()
	}
	defaultSocket := filepath.Join(bbsHome, "run", "search.svc.sock")

	socketPath := flag.String("socket", defaultSocket, "Path to UNIX domain socket")
	flag.Parse()

	args := flag.Args()
	if len(args) == 0 {
		printUsage()
		os.Exit(1)
	}

	action := args[0]
	subArgs := args[1:]

	switch action {
	case "status", "stats":
		req := daemon.ControlRequest{Action: action}
		resp, err := sendControlRequest(*socketPath, req, 5*time.Second)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(1)
		}
		out, _ := json.MarshalIndent(resp, "", "  ")
		fmt.Println(string(out))
		if resp.Status != "ok" {
			os.Exit(1)
		}

	case "flush":
		req := daemon.ControlRequest{Action: "flush"}
		if len(subArgs) >= 1 {
			var bid int
			if _, err := fmt.Sscanf(subArgs[0], "%d", &bid); err == nil {
				req.Bid = int32(bid)
			}
		}
		resp, err := sendControlRequest(*socketPath, req, 5*time.Second)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(1)
		}
		out, _ := json.MarshalIndent(resp, "", "  ")
		fmt.Println(string(out))
		if resp.Status != "ok" {
			os.Exit(1)
		}

	case "sources":
		handleSources(*socketPath, subArgs)

	case "top":
		handleTop(*socketPath, subArgs)

	case "entries":
		handleEntries(*socketPath, subArgs)

	case "backtrack":
		handleBacktrack(*socketPath, subArgs)

	case "verbose":
		handleVerbose(*socketPath, subArgs)

	case "pprof":
		handlePprof(*socketPath, subArgs)

	default:
		fmt.Fprintf(os.Stderr, "Unknown action: %s\n", action)
		printUsage()
		os.Exit(1)
	}
}
