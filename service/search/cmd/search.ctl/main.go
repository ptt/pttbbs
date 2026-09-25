package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"net"
	"os"
	"path/filepath"

	"pttbbs/bbs"
	"pttbbs/search/daemon"
)

func sendControlRequest(socketPath string, req daemon.ControlRequest) (*daemon.ControlResponse, error) {
	conn, err := net.Dial("unix", socketPath)
	if err != nil {
		return nil, fmt.Errorf("failed to connect to %s: %w", socketPath, err)
	}
	defer conn.Close()

	if err := json.NewEncoder(conn).Encode(req); err != nil {
		return nil, err
	}

	var resp daemon.ControlResponse
	if err := json.NewDecoder(conn).Decode(&resp); err != nil {
		return nil, err
	}
	return &resp, nil
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
		fmt.Println("Usage: search.ctl [-socket path] <action> [args...]")
		fmt.Println("Actions:")
		fmt.Println("  status             Show cache statistics and uptime")
		fmt.Println("  flush [bid]        Flush all cached search indices (or for a specific board ID)")
		os.Exit(1)
	}

	action := args[0]
	req := daemon.ControlRequest{
		Action: action,
	}

	if action == "flush" && len(args) >= 2 {
		var bid int
		if _, err := fmt.Sscanf(args[1], "%d", &bid); err == nil {
			req.Bid = int32(bid)
		}
	}

	resp, err := sendControlRequest(*socketPath, req)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}

	out, _ := json.MarshalIndent(resp, "", "  ")
	fmt.Println(string(out))
	if resp.Status != "ok" {
		os.Exit(1)
	}
}
