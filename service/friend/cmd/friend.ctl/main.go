package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"net"
	"os"
	"path/filepath"

	"pttbbs/bbs"
	"pttbbs/friend/daemon"
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
	bbsHome := os.Getenv("BBSHOME")
	if bbsHome == "" {
		bbsHome = bbs.BBSHome()
	}
	defaultSocket := filepath.Join(bbsHome, "run", "friend.svc.sock")

	socketPath := flag.String("socket", defaultSocket, "Path to UNIX domain socket")
	flag.Parse()

	args := flag.Args()
	if len(args) == 0 {
		fmt.Println("Usage: friend.ctl [-socket path] <action> [args...]")
		fmt.Println("Actions:")
		fmt.Println("  status")
		fmt.Println("  query <userid>             (or user / info <userid>)")
		fmt.Println("  hbfl <bid|brdname>         (inspect hidden board visable list)")
		fmt.Println("  hbfl_user <uid|userid>     (list hidden boards accessible by user)")
		fmt.Println("  hbfl_reload [bid|brdname]  (reload hidden board visable list)")
		fmt.Println("  login <userid> <pid> <sid>")
		fmt.Println("  friend_sync <userid> <uid> <pid> <sid>")
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

	case "query", "user", "info":
		if len(args) < 2 {
			fmt.Println("Usage: friend.ctl query <userid>")
			os.Exit(1)
		}
		req.UserID = args[1]

	case "hbfl", "hbfl_board":
		if len(args) < 2 {
			fmt.Println("Usage: friend.ctl hbfl <bid|brdname>")
			os.Exit(1)
		}
		if n, err := fmt.Sscanf(args[1], "%d", &req.BID); err != nil || n != 1 {
			req.BrdName = args[1]
		}

	case "hbfl_user":
		if len(args) < 2 {
			fmt.Println("Usage: friend.ctl hbfl_user <uid|userid>")
			os.Exit(1)
		}
		if n, err := fmt.Sscanf(args[1], "%d", &req.UID); err != nil || n != 1 {
			req.UserID = args[1]
		}

	case "hbfl_reload":
		if len(args) >= 2 {
			if n, err := fmt.Sscanf(args[1], "%d", &req.BID); err != nil || n != 1 {
				req.BrdName = args[1]
			}
		}

	case "login":
		if len(args) < 4 {
			fmt.Println("Usage: friend.ctl login <userid> <pid> <sid>")
			os.Exit(1)
		}
		req.UserID = args[1]
		fmt.Sscanf(args[2], "%d", &req.PID)
		fmt.Sscanf(args[3], "%d", &req.SID)

	case "friend_sync":
		if len(args) < 5 {
			fmt.Println("Usage: friend.ctl friend_sync <userid> <uid> <pid> <sid>")
			os.Exit(1)
		}
		req.UserID = args[1]
		fmt.Sscanf(args[2], "%d", &req.UID)
		fmt.Sscanf(args[3], "%d", &req.PID)
		fmt.Sscanf(args[4], "%d", &req.SID)

	case "logout":
		if len(args) < 3 {
			fmt.Println("Usage: friend.ctl logout <userid> <pid>")
			os.Exit(1)
		}
		req.UserID = args[1]
		fmt.Sscanf(args[2], "%d", &req.PID)

	case "reload":
		if len(args) < 2 {
			fmt.Println("Usage: friend.ctl reload <userid>")
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
