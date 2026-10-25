package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"strconv"

	"pttbbs/bbs"
	"pttbbs/stat/model"
)

func sendRequest(socketPath string, req model.Request) (*model.Response, error) {
	conn, err := net.Dial("unix", socketPath)
	if err != nil {
		return nil, fmt.Errorf("failed to connect to %s: %w", socketPath, err)
	}
	defer conn.Close()

	if err := json.NewEncoder(conn).Encode(req); err != nil {
		return nil, err
	}

	var resp model.Response
	if err := json.NewDecoder(conn).Decode(&resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

func parseUserTarget(target string, req *model.Request) {
	req.UserID = target
}

func printUsage() {
	fmt.Println("Usage: stat.ctl [-socket path] [-json] <action> [options...]")
	fmt.Println("\nActions:")
	fmt.Println("  status                         Show service status, tracked users, uptime")
	fmt.Println("  get <userid>               Show today's stats for a user")
	fmt.Println("  inc_post <userid>          Increment today's post count for a user")
	fmt.Println("  add_mail <userid> [count]  Add to today's mail count for a user (default: 1)")
	fmt.Println("  set <userid> <post> <mail> Set user's post and mail counts")
	fmt.Println("  reset <userid>             Reset user's today stats to 0")
	fmt.Println("  save                           Trigger immediate flush to disk")
	fmt.Println("  prune                          Prune expired entries for previous dates")
}

func main() {
	bbsHome := bbs.BBSHome()
	defaultSocket := filepath.Join(bbsHome, "run", "stat.svc.sock")

	socketPath := flag.String("socket", defaultSocket, "Path to UNIX domain socket")
	jsonOutput := flag.Bool("json", false, "Output results in JSON format")

	flag.Parse()

	args := flag.Args()
	if len(args) == 0 {
		printUsage()
		os.Exit(1)
	}

	action := args[0]
	var req model.Request
	req.Action = action

	switch action {
	case "status", "stat":
		req.Action = "status"

	case "get":
		if len(args) < 2 {
			fmt.Println("Usage: stat.ctl get <userid>")
			os.Exit(1)
		}
		parseUserTarget(args[1], &req)

	case "inc_post":
		if len(args) < 2 {
			fmt.Println("Usage: stat.ctl inc_post <userid>")
			os.Exit(1)
		}
		parseUserTarget(args[1], &req)

	case "inc_song":
		if len(args) < 2 {
			fmt.Println("Usage: stat.ctl inc_song <userid>")
			os.Exit(1)
		}
		parseUserTarget(args[1], &req)

	case "add_mail":
		if len(args) < 2 {
			fmt.Println("Usage: stat.ctl add_mail <userid> [count]")
			os.Exit(1)
		}
		parseUserTarget(args[1], &req)
		count := 1
		if len(args) >= 3 {
			if n, err := strconv.Atoi(args[2]); err == nil && n > 0 {
				count = n
			}
		}
		req.Count = count

	case "set":
		if len(args) < 4 {
			fmt.Println("Usage: stat.ctl set <userid> <post_count> <mail_count>")
			os.Exit(1)
		}
		parseUserTarget(args[1], &req)
		p, err1 := strconv.Atoi(args[2])
		m, err2 := strconv.Atoi(args[3])
		if err1 != nil || err2 != nil {
			fmt.Println("Error: post_count and mail_count must be integers")
			os.Exit(1)
		}
		p32 := int32(p)
		m32 := int32(m)
		req.PostCount = &p32
		req.MailCount = &m32

	case "reset":
		if len(args) < 2 {
			fmt.Println("Usage: stat.ctl reset <userid>")
			os.Exit(1)
		}
		parseUserTarget(args[1], &req)
		req.Action = "set"
		zero := int32(0)
		req.PostCount = &zero
		req.MailCount = &zero

	case "save", "flush":
		req.Action = "save"

	case "prune":
		req.Action = "prune"

	default:
		fmt.Printf("Unknown action: %s\n\n", action)
		printUsage()
		os.Exit(1)
	}

	resp, err := sendRequest(*socketPath, req)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}

	if *jsonOutput {
		out, _ := json.MarshalIndent(resp, "", "  ")
		fmt.Println(string(out))
		return
	}

	if !resp.Success {
		fmt.Fprintf(os.Stderr, "Error: %s\n", resp.Error)
		os.Exit(1)
	}

	switch action {
	case "status", "stat":
		st := resp.Status
		if st != nil {
			fmt.Printf("Date:          %s (%d)\n", model.FormatDate(st.Date), st.Date)
			fmt.Printf("Tracked Users: %d\n", st.TrackedUsers)
			fmt.Printf("Uptime:        %s\n", st.Uptime)
			fmt.Printf("Last Save:     %s\n", st.LastSave)
			fmt.Printf("Server Time:   %s\n", st.Now)
		}

	case "get":
		st := resp.Stats
		if st != nil {
			fmt.Printf("User:          %s\n", st.UserID)
			fmt.Printf("Date:          %s (%d)\n", model.FormatDate(st.Date), st.Date)
			fmt.Printf("Post Count:    %d\n", st.PostCount)
			fmt.Printf("Mail Count:    %d\n", st.MailCount)
			fmt.Printf("Song Count:    %d\n", st.SongCount)
		}

	case "inc_post":
		fmt.Printf("Post Count updated: %d\n", resp.PostCount)

	case "add_mail":
		fmt.Printf("Mail Count updated: %d\n", resp.MailCount)

	case "inc_song":
		fmt.Printf("Song Count updated: %d\n", resp.SongCount)

	default:
		if resp.Message != "" {
			fmt.Println(resp.Message)
		} else {
			fmt.Println("Success")
		}
	}
}
