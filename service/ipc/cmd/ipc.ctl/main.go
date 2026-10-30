package main

import (
	"flag"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"pttbbs/bbs"
	"pttbbs/ipc/daemon"
)

func main() {
	bbsHome := os.Getenv("BBSHOME")
	if bbsHome == "" {
		bbsHome = bbs.BBSHome()
	}

	defaultSocket := filepath.Join(bbsHome, "run", "ipc.svc.sock")
	sockPath := flag.String("socket", defaultSocket, "Path to ipc.svc UNIX domain socket")
	flag.Parse()

	args := flag.Args()
	if len(args) == 0 {
		printUsage()
		os.Exit(1)
	}

	cmd := args[0]
	switch cmd {
	case "ping":
		doPing(*sockPath)
	case "wmsg":
		if len(args) < 3 {
			fmt.Fprintf(os.Stderr, "Usage: ipc.ctl wmsg <to_userid> <message>\n")
			os.Exit(1)
		}
		doWmsg(*sockPath, args[1], args[2])
	case "talk":
		if len(args) < 2 {
			fmt.Fprintf(os.Stderr, "Usage: ipc.ctl talk <to_userid> [sig]\n")
			os.Exit(1)
		}
		sig := 1
		if len(args) >= 3 {
			if s, err := strconv.Atoi(args[2]); err == nil {
				sig = s
			}
		}
		doTalk(*sockPath, args[1], sig)
	case "live-pub":
		if len(args) < 3 {
			fmt.Fprintf(os.Stderr, "Usage: ipc.ctl live-pub <board:file|bid:aid> <comment> OR ipc.ctl live-pub <board|bid> <file|aid> <comment>\n")
			os.Exit(1)
		}
		var bid uint32
		var aid uint64
		var comment string
		var err error
		if len(args) >= 4 {
			bid, aid, err = resolveBidAndAid(args[1], args[2])
			comment = args[3]
		} else {
			bid, aid, err = parseTarget(args[1])
			comment = args[2]
		}
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(1)
		}
		doLivePub(*sockPath, bid, aid, comment)
	case "live-sub":
		if len(args) < 2 {
			fmt.Fprintf(os.Stderr, "Usage: ipc.ctl live-sub <board:file|bid:aid> OR ipc.ctl live-sub <board|bid> <file|aid>\n")
			os.Exit(1)
		}
		var bid uint32
		var aid uint64
		var err error
		if len(args) >= 3 {
			bid, aid, err = resolveBidAndAid(args[1], args[2])
		} else {
			bid, aid, err = parseTarget(args[1])
		}
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(1)
		}
		doLiveSub(*sockPath, bid, aid)
	case "conns", "connections", "clients":
		doConns(*sockPath)
	case "status":
		doStatus(*sockPath)
	default:
		fmt.Fprintf(os.Stderr, "Unknown command: %s\n", cmd)
		printUsage()
		os.Exit(1)
	}
}

func printUsage() {
	fmt.Printf("PTT BBS IPC Control Utility (ipc.ctl)\n\n")
	fmt.Printf("Usage: ipc.ctl [options] <command> [arguments]\n\n")
	fmt.Printf("Commands:\n")
	fmt.Printf("  ping                        Check connectivity to ipc.svc\n")
	fmt.Printf("  wmsg <to_userid> <msg>      Send a test waterball message\n")
	fmt.Printf("  talk <to_userid> [sig]      Send a test talk/page request (sig: 1=Talk, 2=Gomo/Urgent, 3=CHC)\n")
	fmt.Printf("  live-pub <topic> <comment>  Publish a live comment to a topic\n")
	fmt.Printf("  live-sub <topic>            Subscribe and stream live comments for a topic\n")
	fmt.Printf("  conns                       Query active connections count\n")
	fmt.Printf("  status                      Query service status and active connections\n\n")
	fmt.Printf("Options:\n")
	fmt.Printf("  -socket <path>              UNIX socket path (default: $BBSHOME/run/ipc.svc.sock)\n")
}

func connect(sockPath string) net.Conn {
	conn, err := net.Dial("unix", sockPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: cannot connect to ipc.svc at %s: %v\n", sockPath, err)
		os.Exit(1)
	}
	return conn
}

func doPing(sockPath string) {
	conn := connect(sockPath)
	defer conn.Close()

	hdr := &daemon.Header{
		Magic:   daemon.IPCMagic,
		Version: daemon.IPCVersion,
		Type:    daemon.IPCTypePing,
		Seq:     1,
		Len:     0,
	}
	if _, err := conn.Write(hdr.Encode()); err != nil {
		fmt.Fprintf(os.Stderr, "Write error: %v\n", err)
		os.Exit(1)
	}

	respHdr, err := daemon.ReadHeader(conn)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Read error: %v\n", err)
		os.Exit(1)
	}

	if respHdr.Type == daemon.IPCTypePong {
		fmt.Println("PONG (ipc.svc is alive and healthy)")
	} else {
		fmt.Printf("Received unexpected response type: 0x%04x\n", respHdr.Type)
	}
}

func doWmsg(sockPath string, toUser, msg string) {
	conn := connect(sockPath)
	defer conn.Close()

	// 1. Register as ctl
	reg := &daemon.RegisterPayload{PID: uint32(os.Getpid()), USlot: 0}
	copy(reg.UserID[:], "SYSOP_CTL")
	regPkt := append((&daemon.Header{
		Magic:   daemon.IPCMagic,
		Version: daemon.IPCVersion,
		Type:    daemon.IPCTypeRegister,
		Seq:     1,
		Len:     uint32(len(reg.Encode())),
	}).Encode(), reg.Encode()...)
	if _, err := conn.Write(regPkt); err != nil {
		fmt.Fprintf(os.Stderr, "Register error: %v\n", err)
		os.Exit(1)
	}
	ackHdr, err := daemon.ReadHeader(conn)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Register ACK read error: %v\n", err)
		os.Exit(1)
	}
	ackBuf := make([]byte, ackHdr.Len)
	_, _ = io.ReadFull(conn, ackBuf)

	// 2. Send WMSG
	wmsg := &daemon.WmsgPayload{
		FromPID:   uint32(os.Getpid()),
		FromUSlot: 0,
		MsgMode:   0,
		Timestamp: uint32(time.Now().Unix()),
	}
	copy(wmsg.FromUserID[:], "SYSOP_CTL")
	copy(wmsg.ToUserID[:], toUser)
	copy(wmsg.Text[:], msg)

	wmsgEncoded := wmsg.Encode()
	wmsgPkt := append((&daemon.Header{
		Magic:   daemon.IPCMagic,
		Version: daemon.IPCVersion,
		Type:    daemon.IPCTypeWmsgSend,
		Seq:     2,
		Len:     uint32(len(wmsgEncoded)),
	}).Encode(), wmsgEncoded...)

	if _, err := conn.Write(wmsgPkt); err != nil {
		fmt.Fprintf(os.Stderr, "Send wmsg error: %v\n", err)
		os.Exit(1)
	}

	respHdr, err := daemon.ReadHeader(conn)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Read response error: %v\n", err)
		os.Exit(1)
	}

	payload := make([]byte, respHdr.Len)
	_, _ = io.ReadFull(conn, payload)
	ack, _ := daemon.DecodeAckPayload(payload)
	if ack.Code == 0 {
		fmt.Printf("Success: Waterball sent to %s: \"%s\"\n", toUser, msg)
	} else {
		fmt.Printf("Failed: Code=%d, Message=%s\n", ack.Code, ack.MessageString())
	}
}

func doTalk(sockPath string, toUser string, sig int) {
	conn := connect(sockPath)
	defer conn.Close()

	// 1. Register as ctl
	reg := &daemon.RegisterPayload{PID: uint32(os.Getpid()), USlot: 0}
	copy(reg.UserID[:], "SYSOP_CTL")
	regPkt := append((&daemon.Header{
		Magic:   daemon.IPCMagic,
		Version: daemon.IPCVersion,
		Type:    daemon.IPCTypeRegister,
		Seq:     1,
		Len:     uint32(len(reg.Encode())),
	}).Encode(), reg.Encode()...)
	if _, err := conn.Write(regPkt); err != nil {
		fmt.Fprintf(os.Stderr, "Register error: %v\n", err)
		os.Exit(1)
	}
	ackHdr, err := daemon.ReadHeader(conn)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Register ACK read error: %v\n", err)
		os.Exit(1)
	}
	ackBuf := make([]byte, ackHdr.Len)
	_, _ = io.ReadFull(conn, ackBuf)

	// 2. Send TALK
	talk := &daemon.TalkPayload{
		FromPID:   uint32(os.Getpid()),
		FromUSlot: 0,
		Sig:       int32(sig),
		IsUrgent:  0,
	}
	copy(talk.FromUserID[:], "SYSOP_CTL")
	copy(talk.ToUserID[:], toUser)

	talkEncoded := talk.Encode()
	talkPkt := append((&daemon.Header{
		Magic:   daemon.IPCMagic,
		Version: daemon.IPCVersion,
		Type:    daemon.IPCTypeTalkReq,
		Seq:     2,
		Len:     uint32(len(talkEncoded)),
	}).Encode(), talkEncoded...)

	if _, err := conn.Write(talkPkt); err != nil {
		fmt.Fprintf(os.Stderr, "Send talk error: %v\n", err)
		os.Exit(1)
	}

	respHdr, err := daemon.ReadHeader(conn)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Read response error: %v\n", err)
		os.Exit(1)
	}

	payload := make([]byte, respHdr.Len)
	_, _ = io.ReadFull(conn, payload)
	ack, _ := daemon.DecodeAckPayload(payload)
	if ack.Code == 0 {
		fmt.Printf("Success: Talk request (sig=%d) sent to %s\n", sig, toUser)
	} else {
		fmt.Printf("Failed: Code=%d, Message=%s\n", ack.Code, ack.MessageString())
	}
}

func parseTarget(targetStr string) (uint32, uint64, error) {
	target := strings.TrimPrefix(targetStr, "live:")
	parts := strings.Split(target, ":")
	if len(parts) == 2 {
		return resolveBidAndAid(parts[0], parts[1])
	}
	return 0, 0, fmt.Errorf("invalid target format: %q (expected <board>:<filename> or <bid>:<aid>)", targetStr)
}

func resolveBidAndAid(boardStr, fileStr string) (uint32, uint64, error) {
	var bid uint32
	if b, err := strconv.ParseUint(boardStr, 10, 32); err == nil && b > 0 {
		bid = uint32(b)
	} else {
		shm, err := bbs.AttachSHM()
		if err == nil {
			if binfo, ok := shm.GetBoardByName(boardStr); ok {
				bid = uint32(binfo.BID)
			}
		}
	}
	if bid == 0 {
		return 0, 0, fmt.Errorf("unknown board or bid: %q", boardStr)
	}

	var aid uint64
	if strings.HasPrefix(fileStr, "M.") || strings.HasPrefix(fileStr, "G.") {
		aid = bbs.Fn2Aidu(fileStr)
	} else {
		a, err := strconv.ParseUint(fileStr, 0, 64)
		if err == nil {
			aid = a
		}
	}
	if aid == 0 {
		return 0, 0, fmt.Errorf("invalid post filename or aid: %q", fileStr)
	}
	return bid, aid, nil
}

func doLivePub(sockPath string, bid uint32, aid uint64, comment string) {
	conn := connect(sockPath)
	defer conn.Close()

	ps := &daemon.PubSubPayload{
		Bid:       bid,
		Timestamp: uint32(time.Now().Unix()),
		Aid:       aid,
	}
	copy(ps.Text[:], comment)

	psEncoded := ps.Encode()
	pkt := append((&daemon.Header{
		Magic:   daemon.IPCMagic,
		Version: daemon.IPCVersion,
		Type:    daemon.IPCTypePub,
		Seq:     1,
		Len:     uint32(len(psEncoded)),
	}).Encode(), psEncoded...)

	if _, err := conn.Write(pkt); err != nil {
		fmt.Fprintf(os.Stderr, "Publish error: %v\n", err)
		os.Exit(1)
	}

	respHdr, err := daemon.ReadHeader(conn)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Read response error: %v\n", err)
		os.Exit(1)
	}
	payload := make([]byte, respHdr.Len)
	_, _ = io.ReadFull(conn, payload)
	ack, _ := daemon.DecodeAckPayload(payload)
	if ack.Code == 0 {
		fmt.Printf("Success: Live comment published to bid=%d aid=0x%016x: \"%s\"\n", bid, aid, comment)
	} else {
		fmt.Printf("Failed: Code=%d, Message=%s\n", ack.Code, ack.MessageString())
	}
}

func doLiveSub(sockPath string, bid uint32, aid uint64) {
	conn := connect(sockPath)
	defer conn.Close()

	// 1. Register as subscriber
	reg := &daemon.RegisterPayload{PID: uint32(os.Getpid()), USlot: 0}
	copy(reg.UserID[:], fmt.Sprintf("SUB_%d", os.Getpid()))
	regPkt := append((&daemon.Header{
		Magic:   daemon.IPCMagic,
		Version: daemon.IPCVersion,
		Type:    daemon.IPCTypeRegister,
		Seq:     1,
		Len:     uint32(len(reg.Encode())),
	}).Encode(), reg.Encode()...)
	if _, err := conn.Write(regPkt); err != nil {
		fmt.Fprintf(os.Stderr, "Register error: %v\n", err)
		os.Exit(1)
	}
	ackHdr, _ := daemon.ReadHeader(conn)
	ackBuf := make([]byte, ackHdr.Len)
	_, _ = io.ReadFull(conn, ackBuf)

	// 2. Subscribe
	ps := &daemon.PubSubPayload{
		Bid: bid,
		Aid: aid,
	}
	psEncoded := ps.Encode()
	pkt := append((&daemon.Header{
		Magic:   daemon.IPCMagic,
		Version: daemon.IPCVersion,
		Type:    daemon.IPCTypeSub,
		Seq:     2,
		Len:     uint32(len(psEncoded)),
	}).Encode(), psEncoded...)

	if _, err := conn.Write(pkt); err != nil {
		fmt.Fprintf(os.Stderr, "Subscribe error: %v\n", err)
		os.Exit(1)
	}
	respHdr, err := daemon.ReadHeader(conn)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Subscribe ACK error: %v\n", err)
		os.Exit(1)
	}
	subAckBuf := make([]byte, respHdr.Len)
	_, _ = io.ReadFull(conn, subAckBuf)

	fmt.Printf("Subscribed to live target: bid=%d aid=0x%016x. Streaming comments (Ctrl-C to stop)...\n", bid, aid)

	// 3. Stream loop
	for {
		hdr, err := daemon.ReadHeader(conn)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Connection closed: %v\n", err)
			return
		}
		payload := make([]byte, hdr.Len)
		if _, err := io.ReadFull(conn, payload); err != nil {
			fmt.Fprintf(os.Stderr, "Read payload error: %v\n", err)
			return
		}
		if hdr.Type == daemon.IPCTypeLiveComment {
			comment, err := daemon.DecodePubSubPayload(payload)
			if err == nil {
				fmt.Printf("[%s] (bid=%d aid=0x%016x) %s\n", time.Now().Format("15:04:05"), comment.Bid, comment.Aid, comment.TextString())
			}
		}
	}
}

func queryStats(conn net.Conn) (*daemon.StatsPayload, error) {
	hdr := &daemon.Header{
		Magic:   daemon.IPCMagic,
		Version: daemon.IPCVersion,
		Type:    daemon.IPCTypeStatsReq,
		Seq:     1,
		Len:     0,
	}
	if _, err := conn.Write(hdr.Encode()); err != nil {
		return nil, fmt.Errorf("write error: %w", err)
	}

	respHdr, err := daemon.ReadHeader(conn)
	if err != nil {
		return nil, fmt.Errorf("read header error: %w", err)
	}
	if respHdr.Type != daemon.IPCTypeStatsResp && respHdr.Type != daemon.IPCTypePong {
		return nil, fmt.Errorf("unexpected response type: 0x%04x", respHdr.Type)
	}

	if respHdr.Len == 0 {
		return &daemon.StatsPayload{}, nil
	}

	payload := make([]byte, respHdr.Len)
	if _, err := io.ReadFull(conn, payload); err != nil {
		return nil, fmt.Errorf("read payload error: %w", err)
	}

	return daemon.DecodeStatsPayload(payload)
}

func formatDuration(sec uint32) string {
	d := time.Duration(sec) * time.Second
	days := d / (24 * time.Hour)
	d -= days * 24 * time.Hour
	hours := d / time.Hour
	d -= hours * time.Hour
	mins := d / time.Minute
	d -= mins * time.Minute
	s := d / time.Second

	if days > 0 {
		return fmt.Sprintf("%dd %02dh %02dm %02ds", days, hours, mins, s)
	}
	if hours > 0 {
		return fmt.Sprintf("%02dh %02dm %02ds", hours, mins, s)
	}
	return fmt.Sprintf("%02dm %02ds", mins, s)
}

func doConns(sockPath string) {
	conn := connect(sockPath)
	defer conn.Close()

	stats, err := queryStats(conn)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error querying stats: %v\n", err)
		os.Exit(1)
	}

	fmt.Printf("Active connections: %d (registered sessions: %d, unique users: %d)\n",
		stats.ActiveClients, stats.ActiveSessions, stats.RegisteredUsers)
}

func doStatus(sockPath string) {
	conn := connect(sockPath)
	defer conn.Close()

	stats, err := queryStats(conn)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Failed to query stats from service: %v\n", err)
		os.Exit(1)
	}

	xchatSock := filepath.Join(filepath.Dir(sockPath), "xchat.ipc.sock")
	chessSock := filepath.Join(filepath.Dir(sockPath), "chess.ipc.sock")
	fmt.Printf("ipc.svc is running on %s (Protocol v%d, Magic: 'IPCV' 0x%08x)\n",
		sockPath, daemon.IPCVersion, daemon.IPCMagic)
	fmt.Printf("  Uptime:                %s\n", formatDuration(stats.UptimeSec))
	fmt.Printf("  Active connections:    %d (registered sessions: %d, unique users: %d)\n",
		stats.ActiveClients, stats.ActiveSessions, stats.RegisteredUsers)
	fmt.Printf("  Active Live topics:    %d\n", stats.ActiveTopics)
	fmt.Printf("  Active chess games:    %d\n", stats.ActiveChess)
	fmt.Printf("  XChat bridge clients:  %d (backend: xchatd)\n", stats.ActiveXChat)
	fmt.Printf("  Total waterballs:      %d\n", stats.TotalWmsg)
	fmt.Printf("  Total talk requests:   %d\n", stats.TotalTalk)
	fmt.Printf("  Total live comments:   %d\n", stats.TotalLive)
	fmt.Printf("xchat bridge is active on %s (backend: xchatd)\n", xchatSock)
	fmt.Printf("chess service is active on %s\n", chessSock)
}
