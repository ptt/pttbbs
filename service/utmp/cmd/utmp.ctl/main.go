package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"time"

	"pttbbs/bbs"
	"pttbbs/utmp/daemon"
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

func fallbackDirectSHM(bbsHome string, req daemon.Request) (*daemon.Response, error) {
	shm, err := bbs.AttachSHM()
	if err != nil {
		return nil, fmt.Errorf("failed to attach to SHM directly: %w", err)
	}

	switch req.Action {
	case "status":
		now := time.Now()
		st := shm.GetUtmpStatus()
		status := &daemon.UTMPStatus{
			Now:             now.Format(time.ANSIC),
			Uptime:          st.Uptime.Format(time.ANSIC),
			UptimeTimestamp: st.Uptime.Unix(),
			Number:          st.Number,
			Busystate:       st.Busystate,
			NeedUpdate:      st.NeedUpdate,
		}
		bids := shm.GetHotBoardBIDs(128)
		for _, bid := range bids {
			if b, ok := shm.GetBoardByBID(bid); ok {
				status.HotBoards = append(status.HotBoards, daemon.HotBoardInfo{
					BID:     bid,
					BrdName: b.BrdName,
					NUsers:  shm.GetBoardNUser(bid),
				})
			}
		}
		return &daemon.Response{Success: true, Message: "ok (direct SHM)", Status: status}, nil

	case "num":
		num := shm.GetUtmpNumber()
		return &daemon.Response{Success: true, Message: fmt.Sprintf("%d.0", num), Number: &num}, nil

	case "reset":
		shm.ResetUtmpBusystate()
		return fallbackDirectSHM(bbsHome, daemon.Request{Action: "status"})

	case "rebuild":
		shm.RebuildUtmpUser()
		num := shm.GetUtmpNumber()
		return &daemon.Response{
			Success: true,
			Message: fmt.Sprintf("utmp_user table rebuilt in-place from active sessions (%d online) (direct SHM)", num),
		}, nil

	case "update":
		shm.UtmpUpdate()
		shm.SetUtmpNeedUpdate(0)
		return fallbackDirectSHM(bbsHome, daemon.Request{Action: "status"})

	case "fix":
		res := daemon.RunFix(shm)
		return &daemon.Response{
			Success: true,
			Message: fmt.Sprintf("utmpfix finished in %s, cleaned %d dead sessions (direct SHM)", res.Duration, res.TotalCleaned),
			Fix:     &res,
		}, nil

	default:
		return nil, fmt.Errorf("daemon is not running and action '%s' requires daemon", req.Action)
	}
}

func printUsage() {
	fmt.Println("Usage: utmp.ctl [-socket path] [-json] <action> [options...]")
	fmt.Println("\nActions:")
	fmt.Println("  status                Show UTMP status, uptime, online users and hotboards")
	fmt.Println("  num                   Print online number for snmpd (<number>.0)")
	fmt.Println("  update                Trigger immediate utmp_update and recalculate hotboards")
	fmt.Println("  reset                 Reset SHM->UTMPbusystate = 0")
	fmt.Println("  rebuild               Rebuild utmp_user table in-place from active sessions")
	fmt.Println("  fix                   Clear dead userlist entries and fix broken table links")
	fmt.Println("  kick <userid|pid>     Kick a specific online user session")
	fmt.Println("  watch                 Check and fix stuck busystate")
}

func main() {
	bbsHome := os.Getenv("BBSHOME")
	if bbsHome == "" {
		bbsHome = bbs.BBSHome()
	}
	defaultSocket := filepath.Join(bbsHome, "run", "utmp.svc.sock")

	socketPath := flag.String("socket", defaultSocket, "Path to UNIX domain socket")
	jsonOutput := flag.Bool("json", false, "Output results in JSON format")

	flag.Parse()

	args := flag.Args()
	if len(args) == 0 {
		printUsage()
		os.Exit(1)
	}

	action := args[0]
	var req daemon.Request
	req.Action = action

	switch action {
	case "status", "stat":
		req.Action = "status"

	case "num":
		req.Action = "num"

	case "update":
		req.Action = "update"

	case "reset":
		req.Action = "reset"

	case "rebuild", "rebuild_utmp":
		req.Action = "rebuild"

	case "watch":
		req.Action = "watch"

	case "kick":
		if len(args) < 2 {
			fmt.Println("Usage: utmp.ctl kick <userid|pid>")
			os.Exit(1)
		}
		req.Action = "kick"
		req.Target = args[1]

	case "fix", "utmpfix":
		req.Action = "fix"

	default:
		fmt.Printf("Unknown action: %s\n\n", action)
		printUsage()
		os.Exit(1)
	}

	resp, err := sendRequest(*socketPath, req)
	if err != nil {
		// Attempt direct SHM fallback
		directResp, directErr := fallbackDirectSHM(bbsHome, req)
		if directErr == nil {
			resp = directResp
		} else {
			fmt.Fprintf(os.Stderr, "Error: %v (direct SHM fallback failed: %v)\n", err, directErr)
			os.Exit(1)
		}
	}

	if *jsonOutput {
		out, _ := json.MarshalIndent(resp, "", "  ")
		fmt.Println(string(out))
		return
	}

	// Human-readable formatting matching traditional shmctl output
	switch req.Action {
	case "status":
		if resp.Status != nil {
			fmt.Printf("now:        %s\n", resp.Status.Now)
			fmt.Printf("uptime:     %s\n", resp.Status.Uptime)
			fmt.Printf("number:     %d\n", resp.Status.Number)
			fmt.Printf("busystate:  %d\n", resp.Status.Busystate)
			fmt.Printf("needupdate: %d\n", resp.Status.NeedUpdate)
			if len(resp.Status.HotBoards) > 0 {
				fmt.Println("\nHot Boards:")
				for idx, b := range resp.Status.HotBoards {
					fmt.Printf("  #%02d [%s] (bid %d) - %d users\n", idx+1, b.BrdName, b.BID, b.NUsers)
				}
			}
		} else {
			fmt.Println(resp.Message)
		}

	case "num":
		if resp.Number != nil {
			fmt.Printf("%d.0\n", *resp.Number)
		} else {
			fmt.Println(resp.Message)
		}

	case "reset":
		if resp.Status != nil {
			fmt.Printf("now:        %s\n", resp.Status.Now)
			fmt.Printf("uptime:     %s\n", resp.Status.Uptime)
			fmt.Printf("number:     %d\n", resp.Status.Number)
			fmt.Printf("busystate:  %d\n", resp.Status.Busystate)
		} else {
			fmt.Println(resp.Message)
		}

	case "fix":
		if resp.Fix != nil {
			fmt.Println("starting scaning...")
			for idx, d := range resp.Fix.Details {
				fmt.Printf("clean %06d(%s), userid: %s, pid: %d\n", idx, d.Reason, d.UserID, d.PID)
			}
			fmt.Printf("utmpfix finished in %s. Cleaned: %d, Online: %d\n",
				resp.Fix.Duration, resp.Fix.TotalCleaned, resp.Fix.OnlineAfter)
		} else {
			fmt.Println(resp.Message)
		}

	case "rebuild", "update", "watch", "kick":
		fmt.Println(resp.Message)
		if resp.Fix != nil {
			for _, d := range resp.Fix.Details {
				fmt.Printf("  cleaned slot %d, userid: %s, pid: %d (%s)\n", d.Slot, d.UserID, d.PID, d.Reason)
			}
		}

	default:
		if resp.Success {
			fmt.Println(resp.Message)
		} else {
			fmt.Fprintf(os.Stderr, "Error: %s\n", resp.Error)
			os.Exit(1)
		}
	}
}
