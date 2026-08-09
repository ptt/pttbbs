package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"net"
	"os"

	"pttbbs/aloha/daemon"
)

func sendRequest(socketPath string, req daemon.Request) (*daemon.Response, error) {
	conn, err := net.Dial("unix", socketPath)
	if err != nil {
		return nil, fmt.Errorf("failed to connect to %s: %w", socketPath, err)
	}
	defer conn.Close()

	if err := json.NewEncoder(conn).Encode(req); err != nil {
		return nil, err
	}

	var resp daemon.Response
	if err := json.NewDecoder(conn).Decode(&resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

func main() {
	socketPath := flag.String("socket", "/v/bbshome/run/aloha.svc.sock", "Path to UNIX domain socket")
	flag.Parse()

	args := flag.Args()
	if len(args) == 0 {
		fmt.Println("Usage: aloha.ctl [-socket path] <action> [args...]")
		fmt.Println("Actions:")
		fmt.Println("  status")
		fmt.Println("  login <userid> <pid> <sid>")
		fmt.Println("  logout <userid> <pid>")
		fmt.Println("  reload <userid>")
		os.Exit(1)
	}

	action := args[0]
	var req daemon.Request
	req.Action = action

	switch action {
	case "status":
		// no extra args

	case "login":
		if len(args) < 4 {
			fmt.Println("Usage: aloha.ctl login <userid> <pid> <sid>")
			os.Exit(1)
		}
		req.UserID = args[1]
		fmt.Sscanf(args[2], "%d", &req.PID)
		fmt.Sscanf(args[3], "%d", &req.SID)

	case "logout":
		if len(args) < 3 {
			fmt.Println("Usage: aloha.ctl logout <userid> <pid>")
			os.Exit(1)
		}
		req.UserID = args[1]
		fmt.Sscanf(args[2], "%d", &req.PID)

	case "reload":
		if len(args) < 2 {
			fmt.Println("Usage: aloha.ctl reload <userid>")
			os.Exit(1)
		}
		req.UserID = args[1]

	default:
		fmt.Printf("Unknown action: %s\n", action)
		os.Exit(1)
	}

	resp, err := sendRequest(*socketPath, req)
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		os.Exit(1)
	}

	out, _ := json.MarshalIndent(resp, "", "  ")
	fmt.Println(string(out))
}
