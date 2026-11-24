package main

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"text/tabwriter"
	"time"
	"unicode/utf8"

	"pttbbs/bbs"
	"pttbbs/big5uao"
	"pttbbs/svcctl"
)

var (
	flagSocket = flag.String("socket", "", "Path to post.svc UNIX socket (default $BBSHOME/run/post.svc.sock)")
)

func getSocketPath() string {
	if *flagSocket != "" {
		return *flagSocket
	}
	env := os.Getenv("POSTSVC_SOCK")
	if env != "" {
		return env
	}
	bbshome := bbs.BBSHome()
	if bbshome != "" {
		candidate := filepath.Join(bbshome, "run", "post.svc.sock")
		if _, err := os.Stat(candidate); err == nil {
			return candidate
		}
	}
	if _, err := os.Stat("/tmp/post.sock"); err == nil {
		return "/tmp/post.sock"
	}
	if bbshome != "" {
		return filepath.Join(bbshome, "run", "post.svc.sock")
	}
	return "/tmp/post.sock"
}

// ping expects PONG from the daemon.
func ping(socket string) error {
	resp, err := svcctl.LineRequest(socket, "PING")
	if err != nil {
		return err
	}
	if resp != "PONG" {
		return fmt.Errorf("unexpected reply %q", resp)
	}
	return nil
}

func connectDaemon() (net.Conn, error) {
	sockPath := getSocketPath()
	return net.Dial("unix", sockPath)
}

func sendCmd(header string, payload []byte) (string, []byte, error) {
	sockPath := getSocketPath()
	conn, err := connectDaemon()
	if err != nil {
		return "", nil, fmt.Errorf("connect to %s failed: %w", sockPath, err)
	}
	defer conn.Close()

	if !strings.HasSuffix(header, "\n") {
		header += "\n"
	}
	if _, err := conn.Write([]byte(header)); err != nil {
		return "", nil, err
	}
	if len(payload) > 0 {
		if _, err := conn.Write(payload); err != nil {
			return "", nil, err
		}
	}

	reader := bufio.NewReader(conn)
	respLine, err := reader.ReadString('\n')
	if err != nil {
		return "", nil, err
	}
	respLine = strings.TrimRight(respLine, "\r\n")

	rest, _ := io.ReadAll(reader)
	return respLine, rest, nil
}

// -----------------------------------------------------------------------------
// Unified Post Target (<post_id> or <post_file>@<community>)
// -----------------------------------------------------------------------------

type postTarget struct {
	postID    uint64
	community string
	postFile  string
	isFile    bool
}

func (t postTarget) String() string {
	if t.isFile {
		return fmt.Sprintf("%s@%s", t.postFile, t.community)
	}
	return strconv.FormatUint(t.postID, 10)
}

// parsePostArg parses the post argument from args starting at index `idx`.
// Supports:
//  1. "<post_file>@<community>" (e.g. M.111.A.001@Marginalman)
//  2. "<post_id>" (e.g. 123)
//  3. 2-argument legacy fallback: "<community> <post_file>" (e.g. "Marginalman M.111.A.001")
func parsePostArg(args []string, idx int) (postTarget, int, error) {
	if idx >= len(args) {
		return postTarget{}, 0, fmt.Errorf("missing post argument (<post_id> or <post_file>@<community>)")
	}

	arg := args[idx]

	// 1. post_file@community
	if atIdx := strings.Index(arg, "@"); atIdx != -1 {
		postFile := arg[:atIdx]
		community := arg[atIdx+1:]
		if postFile == "" || community == "" {
			return postTarget{}, 0, fmt.Errorf("invalid post format '%s', expected <post_file>@<community>", arg)
		}
		return postTarget{
			community: community,
			postFile:  postFile,
			isFile:    true,
		}, 1, nil
	}

	// 2. numeric post_id
	if id, err := strconv.ParseUint(arg, 10, 64); err == nil {
		return postTarget{
			postID: id,
			isFile: false,
		}, 1, nil
	}

	// 3. 2-argument legacy fallback: args[idx] is community, args[idx+1] is post_file
	if idx+1 < len(args) {
		community := args[idx]
		postFile := args[idx+1]
		if strings.HasPrefix(postFile, "M.") || strings.HasPrefix(postFile, "G.") || strings.Contains(postFile, ".") {
			return postTarget{
				community: community,
				postFile:  postFile,
				isFile:    true,
			}, 2, nil
		}
	}

	return postTarget{}, 0, fmt.Errorf("invalid post identifier '%s': must be numeric <post_id> or <post_file>@<community>", arg)
}

func resolveToCommunityFile(target *postTarget) error {
	if target.isFile {
		return nil
	}
	pResp, _, err := sendCmd(fmt.Sprintf("GET_POST %d\n", target.postID), nil)
	if err != nil || !strings.HasPrefix(pResp, "OK") {
		return fmt.Errorf("post %d not found", target.postID)
	}
	parts := strings.Fields(pResp)
	if len(parts) >= 5 {
		target.community = parts[3]
		target.postFile = parts[4]
		target.isFile = true
		return nil
	}
	return fmt.Errorf("invalid response for post %d: %s", target.postID, pResp)
}

func main() {
	flag.Parse()
	args := flag.Args()

	if len(args) == 0 {
		printUsage()
		return
	}

	cmd := args[0]
	switch cmd {
	case "restart":
		os.Exit(svcctl.Main(svcctl.Options{Name: "post.svc", Socket: getSocketPath(), Ping: ping}, args[1:]))

	case "ping":
		resp, _, err := sendCmd("PING\n", nil)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		fmt.Printf("Ping response: %s\n", resp)

	case "daily-backup", "inc-backup":
		os.Exit(dailyBackup(args[1:]))

	case "backup":
		// post.ctl backup [name | dir]
		//   name: each engine writes to <DataDir>/backup/<name> on its own disk.
		//   dir:  anything containing a '/' is a directory, resolved here because
		//         the daemon has its own working directory.
		dest := ""
		if len(args) >= 2 {
			dest = args[1]
		}
		if strings.Contains(dest, "/") {
			abs, err := filepath.Abs(dest)
			if err != nil {
				log.Fatalf("Error: %v", err)
			}
			dest = abs
		}
		resp, payload, err := sendCmd(strings.TrimRight("BACKUP "+dest, " ")+"\n", nil)
		if err != nil {
			log.Fatalf("backup failed: %v", err)
		}
		if !strings.HasPrefix(resp, "OK") {
			log.Fatalf("backup failed: %s", resp)
		}
		ms := 0
		if parts := strings.Fields(resp); len(parts) >= 2 {
			ms, _ = strconv.Atoi(parts[1])
		}
		fmt.Printf("Backup completed in %v:\n", time.Duration(ms)*time.Millisecond)
		fmt.Print(string(payload))
		fmt.Println("To restore: stop post.svc and copy each directory's *.pebble and meta.sqlite3")
		fmt.Println("(and communities.db, in the first one) back into its engine's data dir.")
		fmt.Println("Do not run `post.svc -rebuild` unless meta.sqlite3 is lost: SQLite keeps")
		fmt.Println("some data that Pebble does not. communities.db cannot be rebuilt at all.")

	case "add-file":
		if len(args) < 5 {
			log.Fatalf("Usage: post.ctl add-file <community> <post_file> <author> <title> [creator_id]")
		}
		var uid uint64
		if len(args) >= 6 {
			val, _ := strconv.ParseUint(args[5], 10, 64)
			uid = val
		}
		titleB := []byte(args[4])
		header := fmt.Sprintf("CREATE_POST_FILE %s %s %s %d 0 0 %d %d\n", args[1], args[2], args[3], uid, time.Now().Unix(), len(titleB))
		resp, _, err := sendCmd(header, titleB)
		if err != nil {
			log.Fatalf("add-file failed: %v", err)
		}
		fmt.Println(resp)

	case "add":
		if len(args) < 5 {
			log.Fatalf("Usage: post.ctl add <community> <author> <title> <content>")
		}
		titleB := []byte(args[3])
		contentB := []byte(args[4])
		header := fmt.Sprintf("CREATE_POST %s - %s 0 0 %d 0 %d %d\n", args[1], args[2], time.Now().Unix(), len(titleB), len(contentB))
		payload := append(titleB, contentB...)
		resp, _, err := sendCmd(header, payload)
		if err != nil {
			log.Fatalf("add failed: %v", err)
		}
		fmt.Println(resp)

	case "comment":
		if len(args) < 4 {
			log.Fatalf("Usage: post.ctl comment <post> <author> <content> [ip] [creator_id]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx+1 {
			log.Fatalf("Usage: post.ctl comment <post> <author> <content> [ip] [creator_id]")
		}
		author := args[nextIdx]
		contentB := []byte(args[nextIdx+1])
		ip := "127.0.0.1"
		if len(args) > nextIdx+2 {
			ip = args[nextIdx+2]
		}
		var uid uint64
		if len(args) > nextIdx+3 {
			val, _ := strconv.ParseUint(args[nextIdx+3], 10, 64)
			uid = val
		}
		var legType uint8 = 3 // default 箭頭
		if len(args) > nextIdx+4 {
			val, _ := strconv.Atoi(args[nextIdx+4])
			legType = uint8(val)
		}
		if err := resolveToCommunityFile(&target); err != nil {
			log.Fatalf("Error: %v", err)
		}
		header := fmt.Sprintf("CREATE_COMMENT %s %s %s %d %s %d %d %d\n", target.community, target.postFile, author, uid, ip, time.Now().Unix(), len(contentB), legType)
		resp, _, err := sendCmd(header, contentB)
		if err != nil {
			log.Fatalf("comment failed: %v", err)
		}
		fmt.Println(resp)

	case "api":
		// Lemmy's API, e.g. post.ctl api GetPosts '{"community_name":"Test"}'
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl api <method> [json]")
		}
		req := []byte("{}")
		if len(args) > 2 {
			req = []byte(args[2])
		}
		resp, payload, err := sendCmd(fmt.Sprintf("API %s %d\n", args[1], len(req)), req)
		if err != nil {
			log.Fatalf("api failed: %v", err)
		}
		if !strings.HasPrefix(resp, "OK") {
			log.Fatalf("api failed: %s", resp)
		}
		fmt.Println(string(payload))

	case "get-comment":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl get-comment <comment_id>")
		}
		if _, err := strconv.ParseUint(args[1], 10, 64); err != nil {
			log.Fatalf("Invalid comment ID: %s", args[1])
		}
		resp, payload, err := sendCmd(fmt.Sprintf("GET_COMMENT %s\n", args[1]), nil)
		if err != nil {
			log.Fatalf("get-comment failed: %v", err)
		}
		parts := strings.Fields(resp)
		if len(parts) < 3 || parts[0] != "OK" {
			log.Fatalf("get-comment failed: %s", resp)
		}
		fmt.Printf("Comment #%s is floor %s of post %s:\n", args[1], parts[2], parts[1])
		fmt.Print(string(payload))

	case "vote", "vote-file":
		if len(args) < 3 {
			log.Fatalf("Usage: post.ctl vote <post> <user> [1|-1|0] [creator_id] [ip] [ctime]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx {
			log.Fatalf("Usage: post.ctl vote <post> <user> [1|-1|0] [creator_id] [ip] [ctime]")
		}
		user := args[nextIdx]
		voteVal := 1
		if len(args) > nextIdx+1 {
			voteVal, _ = strconv.Atoi(args[nextIdx+1])
		}
		var uid uint64
		if len(args) > nextIdx+2 {
			v, _ := strconv.ParseUint(args[nextIdx+2], 10, 64)
			uid = v
		}
		var ip string
		var ctime int64
		if len(args) > nextIdx+3 {
			ip = args[nextIdx+3]
		}
		if len(args) > nextIdx+4 {
			ctime, _ = strconv.ParseInt(args[nextIdx+4], 10, 64)
		}
		var header string
		if target.isFile {
			if ip != "" || ctime != 0 {
				if ip == "" {
					ip = "-"
				}
				header = fmt.Sprintf("CREATE_POST_LIKE_FILE %s %s %s %d %d %s %d\n", target.community, target.postFile, user, uid, voteVal, ip, ctime)
			} else {
				header = fmt.Sprintf("CREATE_POST_LIKE_FILE %s %s %s %d %d\n", target.community, target.postFile, user, uid, voteVal)
			}
		} else {
			if ip != "" || ctime != 0 {
				if ip == "" {
					ip = "-"
				}
				header = fmt.Sprintf("CREATE_POST_LIKE %d %s %d %d %s %d\n", target.postID, user, uid, voteVal, ip, ctime)
			} else {
				header = fmt.Sprintf("CREATE_POST_LIKE %d %s %d %d\n", target.postID, user, uid, voteVal)
			}
		}
		resp, _, err := sendCmd(header, nil)
		if err != nil {
			log.Fatalf("vote failed: %v", err)
		}
		fmt.Println(resp)

	case "votes", "votes-file":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl votes <post>\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, _, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		var header string
		if target.isFile {
			header = fmt.Sprintf("LIST_POST_LIKES_FILE %s %s\n", target.community, target.postFile)
		} else {
			header = fmt.Sprintf("LIST_POST_LIKES %d\n", target.postID)
		}
		conn, err := connectDaemon()
		if err != nil {
			log.Fatalf("connect daemon failed: %v", err)
		}
		defer conn.Close()

		if _, err := conn.Write([]byte(header)); err != nil {
			log.Fatalf("send votes cmd failed: %v", err)
		}
		reader := bufio.NewReader(conn)
		statusLine, err := reader.ReadString('\n')
		if err != nil {
			log.Fatalf("read response failed: %v", err)
		}
		statusLine = strings.TrimSpace(statusLine)
		if !strings.HasPrefix(statusLine, "OK") {
			log.Fatalf("votes failed: %s", statusLine)
		}
		var total int
		fmt.Sscanf(statusLine, "OK %d", &total)
		if total == 0 {
			fmt.Println("No votes")
			return
		}
		w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
		fmt.Fprintln(w, "USER\tTOKEN\tVOTE\tTIME\tIP")
		for i := 0; i < total; i++ {
			line, err := reader.ReadString('\n')
			if err != nil {
				break
			}
			line = strings.TrimRight(line, "\r\n")
			cols := strings.Split(line, "\t")
			if len(cols) >= 5 {
				u := cols[0]
				tok := cols[1]
				vVal := cols[2]
				tInt, _ := strconv.ParseInt(cols[3], 10, 64)
				timeStr := time.Unix(tInt, 0).Format("01/02 15:04:05")
				uIP := cols[4]
				voteStr := "+1"
				if vVal == "-1" {
					voteStr = "-1"
				}
				fmt.Fprintf(w, "%s\t%s\t%s\t%s\t%s\n", u, tok, voteStr, timeStr, uIP)
			}
		}
		w.Flush()

	case "get", "get-post":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl get <post>\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, _, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		var header string
		if target.isFile {
			header = fmt.Sprintf("GET_POST_FILE %s %s\n", target.community, target.postFile)
		} else {
			header = fmt.Sprintf("GET_POST %d\n", target.postID)
		}
		resp, payload, err := sendCmd(header, nil)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		fmt.Println(resp)
		if len(payload) > 0 {
			fmt.Println(string(payload))
		}

	case "purge-comments":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl purge-comments <author> [days=7]")
		}
		author := args[1]
		days := 7
		if len(args) >= 3 {
			days, _ = strconv.Atoi(args[2])
		}
		header := fmt.Sprintf("PURGE_PERSON_COMMENTS %s %d\n", author, days)
		resp, _, err := sendCmd(header, nil)
		if err != nil {
			log.Fatalf("purge-comments failed: %v", err)
		}
		fmt.Println(resp)

	case "purge-posts":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl purge-posts <author> [days=7]")
		}
		author := args[1]
		days := 7
		if len(args) >= 3 {
			days, _ = strconv.Atoi(args[2])
		}
		header := fmt.Sprintf("PURGE_PERSON_POSTS %s %d\n", author, days)
		resp, _, err := sendCmd(header, nil)
		if err != nil {
			log.Fatalf("purge-posts failed: %v", err)
		}
		fmt.Println(resp)

	case "list":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl list <community> [count [start]]")
		}
		comm := args[1]
		count := 20
		if len(args) >= 3 {
			if c, err := strconv.Atoi(args[2]); err == nil && c > 0 {
				count = c
			}
		}
		start := 0
		if len(args) >= 4 {
			if s, err := strconv.Atoi(args[3]); err == nil && s >= 0 {
				start = s
			}
		}
		filterMode := ""
		if len(args) >= 5 {
			filterMode = args[4]
		}
		header := fmt.Sprintf("GET_POSTS %s %d %d %s\n", comm, count, start, filterMode)
		resp, payload, err := sendCmd(header, nil)
		if err != nil {
			log.Fatalf("List failed: %v", err)
		}
		if !strings.HasPrefix(resp, "OK") {
			log.Fatalf("List failed: %s", resp)
		}

		respParts := strings.Fields(resp)
		returnedCount := 0
		totalCount := 0
		if len(respParts) >= 2 {
			returnedCount, _ = strconv.Atoi(respParts[1])
		}
		if len(respParts) >= 3 {
			totalCount, _ = strconv.Atoi(respParts[2])
		}

		if returnedCount == 0 {
			fmt.Printf("No posts found in %s\n", comm)
			return
		}

		w := tabwriter.NewWriter(os.Stdout, 0, 8, 2, ' ', 0)
		fmt.Fprintf(w, "ID\tPOST_FILE\tAUTHOR\tDATE\tSCORE\tCOMMENTS\tTITLE\n")
		lines := strings.Split(strings.TrimRight(string(payload), "\n"), "\n")
		for _, line := range lines {
			if line == "" {
				continue
			}
			cols := strings.Split(line, "\t")
			if len(cols) < 10 {
				continue
			}
			id := cols[0]
			postFile := cols[1]
			author := cols[2]
			cTime, _ := strconv.ParseInt(cols[3], 10, 64)
			dateStr := time.Unix(cTime, 0).Format("01/02 15:04")
			up, _ := strconv.Atoi(cols[5])
			down, _ := strconv.Atoi(cols[6])
			score := up - down
			comments := cols[7]
			title := cols[9]
			fmt.Fprintf(w, "%s\t%s\t%s\t%s\t%d\t%s\t%s\n", id, postFile, author, dateStr, score, comments, title)
		}
		w.Flush()
		fmt.Printf("(Showing %d of %d posts)\n", returnedCount, totalCount)

	case "render", "render-file":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl render <post> [output] [--legacy-format]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		outPath := "-"
		legacyFormat := false
		for i := 1 + consumed; i < len(args); i++ {
			arg := args[i]
			if arg == "--legacy-format" || arg == "-legacy-format" || arg == "--legacy" {
				legacyFormat = true
			} else if !strings.HasPrefix(arg, "-") && outPath == "-" {
				outPath = arg
				if outPath != "-" && !filepath.IsAbs(outPath) {
					if abs, err := filepath.Abs(outPath); err == nil {
						outPath = abs
					}
				}
			}
		}
		fmtStr := "modern"
		if legacyFormat {
			fmtStr = "legacy"
		}
		var header string
		if target.isFile {
			header = fmt.Sprintf("RENDER_FILE %s %s %s %s\n", target.community, target.postFile, outPath, fmtStr)
		} else {
			header = fmt.Sprintf("RENDER %d %s %s\n", target.postID, outPath, fmtStr)
		}
		resp, payload, err := sendCmd(header, nil)
		if err != nil {
			log.Fatalf("Render failed: %v", err)
		}
		if !strings.HasPrefix(resp, "OK") {
			log.Fatalf("Render failed: %s", resp)
		}

		if outPath == "-" {
			if !utf8.Valid(payload) {
				os.Stdout.WriteString(big5uao.DecodeSGR66(payload))
			} else {
				os.Stdout.Write(payload)
			}
		} else {
			fmt.Println(resp)
		}

	case "import-board":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl import-board <board>|--all [render_target] [--overwrite] [--dry-run] [--workers N] [--limit N] [--offset N] [--no-merge] [--commentd-db PATH]")
		}
		comm := args[1]
		renderTarget := "-"
		overwrite := false
		limit := 0
		offset := 0
		noMerge := false
		commentdDB := "-"
		legacyFormat := false
		dryRun := false
		workers := 0

		for i := 2; i < len(args); i++ {
			arg := args[i]
			if arg == "--overwrite" || arg == "-overwrite" {
				overwrite = true
			} else if arg == "--dry-run" || arg == "-dry-run" || arg == "--dryrun" || arg == "-n" {
				dryRun = true
			} else if (arg == "--workers" || arg == "-workers" || arg == "-w") && i+1 < len(args) {
				workers, _ = strconv.Atoi(args[i+1])
				i++
			} else if strings.HasPrefix(arg, "--workers=") {
				workers, _ = strconv.Atoi(strings.TrimPrefix(arg, "--workers="))
			} else if strings.HasPrefix(arg, "-workers=") {
				workers, _ = strconv.Atoi(strings.TrimPrefix(arg, "-workers="))
			} else if strings.HasPrefix(arg, "-w=") {
				workers, _ = strconv.Atoi(strings.TrimPrefix(arg, "-w="))
			} else if arg == "--limit" && i+1 < len(args) {
				limit, _ = strconv.Atoi(args[i+1])
				i++
			} else if strings.HasPrefix(arg, "--limit=") {
				limit, _ = strconv.Atoi(strings.TrimPrefix(arg, "--limit="))
			} else if arg == "--offset" && i+1 < len(args) {
				offset, _ = strconv.Atoi(args[i+1])
				i++
			} else if strings.HasPrefix(arg, "--offset=") {
				offset, _ = strconv.Atoi(strings.TrimPrefix(arg, "--offset="))
			} else if arg == "--no-merge" || arg == "--no-merge-consecutive" {
				noMerge = true
			} else if (arg == "--commentd-db" || arg == "-commentd-db") && i+1 < len(args) {
				commentdDB = args[i+1]
				i++
			} else if strings.HasPrefix(arg, "--commentd-db=") {
				commentdDB = strings.TrimPrefix(arg, "--commentd-db=")
			} else if strings.HasPrefix(arg, "-commentd-db=") {
				commentdDB = strings.TrimPrefix(arg, "-commentd-db=")
			} else if arg == "--re-render" || arg == "-re-render" {
				if i+1 < len(args) && !strings.HasPrefix(args[i+1], "-") {
					renderTarget = args[i+1]
					i++
				} else {
					renderTarget = "boards"
				}
			} else if (arg == "--render-target" || arg == "-render-target") && i+1 < len(args) {
				renderTarget = args[i+1]
				i++
			} else if strings.HasPrefix(arg, "--render-target=") {
				renderTarget = strings.TrimPrefix(arg, "--render-target=")
			} else if arg == "--legacy-format" || arg == "-legacy-format" || arg == "--legacy" {
				legacyFormat = true
			} else if !strings.HasPrefix(arg, "-") && renderTarget == "-" {
				renderTarget = arg
				if abs, err := filepath.Abs(renderTarget); err == nil {
					renderTarget = abs
				}
			}
		}

		if commentdDB != "-" && commentdDB != "" {
			if abs, err := filepath.Abs(commentdDB); err == nil {
				commentdDB = abs
			}
		}

		ovStr := "0"
		if overwrite {
			ovStr = "1"
		}
		nmStr := "0"
		if noMerge {
			nmStr = "1"
		}
		legStr := "0"
		if legacyFormat {
			legStr = "1"
		}
		dryStr := "0"
		if dryRun {
			dryStr = "1"
		}
		mkHeader := func(board string) string {
			return fmt.Sprintf("IMPORT_BOARD %s %s %s %d %d %s %s %s %s %d\n", board, renderTarget, ovStr, limit, offset, nmStr, commentdDB, legStr, dryStr, workers)
		}
		printMode := func() {
			if dryRun {
				fmt.Println("    Mode: dry-run (simulation only, database and disk will not be modified)")
			}
			if workers > 0 {
				fmt.Printf("    Workers: %d\n", workers)
			}
			if renderTarget != "-" && renderTarget != "" {
				fmt.Printf("    Render target: %s\n", renderTarget)
			} else {
				fmt.Println("    Re-render: disabled (default, import to DB only)")
			}
			if overwrite {
				fmt.Println("    Mode: overwrite (clearing existing board data)")
			}
			if commentdDB != "-" && commentdDB != "" {
				fmt.Printf("    Commentd DB: %s\n", commentdDB)
			}
		}

		if comm == "--all" || comm == "-all" {
			// Every board in .BRD, one after another, e.g. before
			// switching the backend to post.svc.
			if renderTarget != "-" && renderTarget != "boards" && renderTarget != "cache" {
				log.Fatalf("import-board --all: the render target must be boards or cache (one directory per board), not %s", renderTarget)
			}
			boards, skipped, err := boardsToImport(bbs.BBSHome(), filepath.Join(bbs.BBSHome(), ".BRD"))
			if err != nil {
				log.Fatalf("import-board --all: read .BRD failed: %v", err)
			}
			fmt.Printf("[*] Importing %d boards from .BRD (%d without .DIR skipped)...\n", len(boards), len(skipped))
			printMode()
			var failed []string
			for i, board := range boards {
				prefix := fmt.Sprintf("[%d/%d] %s: ", i+1, len(boards), board)
				fmt.Print(prefix)
				r, err := runImportBoard(mkHeader(board), prefix)
				if err != nil {
					fmt.Printf("[!] %s: %v\n", board, err)
					failed = append(failed, board)
					continue
				}
				printImportBoardResult(r, dryRun)
			}
			fmt.Printf("[+] %d boards done, %d failed\n", len(boards)-len(failed), len(failed))
			if len(failed) > 0 {
				fmt.Printf("    Failed: %s\n", strings.Join(failed, " "))
				os.Exit(1)
			}
			break
		}

		fmt.Printf("[*] Starting fast Go migration for board '%s'...\n", comm)
		printMode()
		r, err := runImportBoard(mkHeader(comm), "")
		if err != nil {
			log.Fatalf("[!] Migration failed: %v", err)
		}
		printImportBoardResult(r, dryRun)

	case "render-community":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl render-community <community> [output_dir] [--legacy-format]")
		}
		comm := args[1]
		outDir := filepath.Join(bbs.BBSHome(), "cache", comm)
		legacyFormat := false

		for i := 2; i < len(args); i++ {
			arg := args[i]
			if arg == "--legacy-format" || arg == "-legacy-format" || arg == "--legacy" {
				legacyFormat = true
			} else if !strings.HasPrefix(arg, "-") {
				outDir = arg
				if abs, err := filepath.Abs(outDir); err == nil {
					outDir = abs
				}
			}
		}
		_ = os.MkdirAll(outDir, 0755)

		fmtStr := "modern"
		if legacyFormat {
			fmtStr = "legacy"
		}

		header := fmt.Sprintf("RENDER_COMMUNITY %s %s %s\n", comm, outDir, fmtStr)
		sockPath := getSocketPath()
		conn, err := net.Dial("unix", sockPath)
		if err != nil {
			log.Fatalf("connect to %s failed: %v\n(Is post.svc running?)", sockPath, err)
		}
		defer conn.Close()

		if _, err := conn.Write([]byte(header)); err != nil {
			log.Fatalf("send command failed: %v", err)
		}

		fmt.Printf("[*] Rendering community '%s' into %s (format: %s)...\n", comm, outDir, fmtStr)
		reader := bufio.NewReader(conn)
		for {
			line, err := reader.ReadString('\n')
			if err != nil {
				if err == io.EOF {
					break
				}
				log.Fatalf("\nRead error: %v", err)
			}
			line = strings.TrimRight(line, "\r\n")
			if strings.HasPrefix(line, "PROGRESS ") {
				parts := strings.Fields(line)
				if len(parts) >= 3 {
					cur, _ := strconv.Atoi(parts[1])
					total, _ := strconv.Atoi(parts[2])
					pct := 0
					if total > 0 {
						pct = cur * 100 / total
					}
					fmt.Printf("\r%d/%d (%d%%)", cur, total, pct)
				}
			} else if strings.HasPrefix(line, "OK") {
				fmt.Println()
				parts := strings.Fields(line)
				if len(parts) >= 3 {
					fmt.Printf("[+] Render completed in %ss: %s posts rendered into %s (format: %s)\n",
						parts[2], parts[1], outDir, fmtStr)
				} else {
					fmt.Println(line)
				}
				break
			} else if strings.HasPrefix(line, "ERR") {
				fmt.Println()
				log.Fatalf("[!] Render failed: %s", line)
			}
		}

	case "update", "update-file", "edit", "edit-file":
		if len(args) < 4 {
			log.Fatalf("Usage: post.ctl update <post> <title> <content> [editor]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) < nextIdx+2 {
			log.Fatalf("Usage: post.ctl update <post> <title> <content> [editor]")
		}
		titleB := []byte(args[nextIdx])
		contentB := []byte(args[nextIdx+1])
		editor := "sysop"
		if len(args) >= nextIdx+3 {
			editor = args[nextIdx+2]
		}
		var header string
		if target.isFile {
			header = fmt.Sprintf("EDIT_POST_FILE %s %s %s 0 0 %d %d\n", target.community, target.postFile, editor, len(titleB), len(contentB))
		} else {
			header = fmt.Sprintf("EDIT_POST %d %s 0 %d %d\n", target.postID, editor, len(titleB), len(contentB))
		}
		resp, _, err := sendCmd(header, append(titleB, contentB...))
		if err != nil {
			log.Fatalf("update failed: %v", err)
		}
		fmt.Println(resp)

	case "update-title", "update-title-file", "edit-title", "edit-title-file":
		if len(args) < 3 {
			log.Fatalf("Usage: post.ctl update-title <post> <title> [editor]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx {
			log.Fatalf("Usage: post.ctl update-title <post> <title> [editor]")
		}
		titleB := []byte(args[nextIdx])
		editor := "sysop"
		if len(args) >= nextIdx+2 {
			editor = args[nextIdx+1]
		}
		var header string
		if target.isFile {
			header = fmt.Sprintf("EDIT_POST_TITLE_FILE %s %s %s 0 %d\n", target.community, target.postFile, editor, len(titleB))
		} else {
			header = fmt.Sprintf("EDIT_POST_TITLE %d %s 0 %d\n", target.postID, editor, len(titleB))
		}
		resp, _, err := sendCmd(header, titleB)
		if err != nil {
			log.Fatalf("update-title failed: %v", err)
		}
		fmt.Println(resp)

	case "comments", "comments-file":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl comments <post> [start [limit]]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		nextIdx := 1 + consumed
		start := 1
		limit := 1000
		if len(args) > nextIdx {
			if s, err := strconv.Atoi(args[nextIdx]); err == nil && s > 0 {
				start = s
			}
		}
		if len(args) > nextIdx+1 {
			if l, err := strconv.Atoi(args[nextIdx+1]); err == nil && l > 0 {
				limit = l
			}
		}
		var header string
		if target.isFile {
			header = fmt.Sprintf("GET_COMMENTS_FILE %s %s %d %d\n", target.community, target.postFile, start, limit)
		} else {
			header = fmt.Sprintf("GET_COMMENTS %d %d %d\n", target.postID, start, limit)
		}
		resp, payload, err := sendCmd(header, nil)
		if err != nil {
			log.Fatalf("comments failed: %v", err)
		}
		printCommentsTable(resp, payload)

	case "update-comment", "update-comment-file", "edit-comment", "edit-comment-file":
		if len(args) < 4 {
			log.Fatalf("Usage: post.ctl update-comment <post> <seq> <content> [editor]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx+1 {
			log.Fatalf("Usage: post.ctl update-comment <post> <seq> <content> [editor]")
		}
		seq := args[nextIdx]
		contentB := []byte(args[nextIdx+1])
		editor := "sysop"
		if len(args) > nextIdx+2 {
			editor = args[nextIdx+2]
		}
		if err := resolveToCommunityFile(&target); err != nil {
			log.Fatalf("Error: %v", err)
		}
		header := fmt.Sprintf("EDIT_COMMENT_FILE %s %s %s %s 0 %d\n", target.community, target.postFile, seq, editor, len(contentB))
		resp, _, err := sendCmd(header, contentB)
		if err != nil {
			log.Fatalf("update-comment failed: %v", err)
		}
		fmt.Println(resp)

	case "history", "history-file", "post-history", "post-history-file":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl history <post> [rev]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		nextIdx := 1 + consumed
		rev := 0
		if len(args) > nextIdx {
			rev, _ = strconv.Atoi(args[nextIdx])
		}
		var header string
		if target.isFile {
			header = fmt.Sprintf("GET_POST_REVISIONS_FILE %s %s %d\n", target.community, target.postFile, rev)
		} else {
			header = fmt.Sprintf("GET_POST_REVISIONS %d %d\n", target.postID, rev)
		}
		resp, payload, err := sendCmd(header, nil)
		if err != nil {
			log.Fatalf("history failed: %v", err)
		}
		printPostHistory(resp, payload, rev)

	case "comment-history", "comment-history-file":
		if len(args) < 3 {
			log.Fatalf("Usage: post.ctl comment-history <post> <seq> [rev]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx {
			log.Fatalf("Usage: post.ctl comment-history <post> <seq> [rev]")
		}
		seq := args[nextIdx]
		rev := 0
		if len(args) > nextIdx+1 {
			rev, _ = strconv.Atoi(args[nextIdx+1])
		}
		if err := resolveToCommunityFile(&target); err != nil {
			log.Fatalf("Error: %v", err)
		}
		header := fmt.Sprintf("GET_COMMENT_REVISIONS_FILE %s %s %s %d\n", target.community, target.postFile, seq, rev)
		resp, payload, err := sendCmd(header, nil)
		if err != nil {
			log.Fatalf("comment-history failed: %v", err)
		}
		printCommentHistory(resp, payload, rev)

	case "crosspost", "share":
		if len(args) < 3 {
			log.Fatalf("Usage: post.ctl crosspost <src_post> <target_post> <operator> [person_id]\n  <post>: <post_id> or <post_file>@<community>")
		}
		srcTarget, srcConsumed, srcErr := parsePostArg(args, 1)
		if srcErr != nil {
			log.Fatalf("Error parsing src_post: %v", srcErr)
		}
		tgtTarget, tgtConsumed, tgtErr := parsePostArg(args, 1+srcConsumed)
		if tgtErr != nil {
			log.Fatalf("Error parsing target_post: %v", tgtErr)
		}
		_ = resolveToCommunityFile(&srcTarget)
		_ = resolveToCommunityFile(&tgtTarget)
		nextIdx := 1 + srcConsumed + tgtConsumed
		operator := "sysop"
		if len(args) > nextIdx {
			operator = args[nextIdx]
		}
		token := "0"
		if len(args) > nextIdx+1 {
			token = args[nextIdx+1]
		}
		now := strconv.FormatInt(time.Now().Unix(), 10)
		cmd := fmt.Sprintf("CROSSPOST %s %s %s %s %s %s %s\n",
			srcTarget.community, srcTarget.postFile, tgtTarget.community, tgtTarget.postFile, operator, token, now)
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			log.Fatalf("crosspost failed: %v", err)
		}
		fmt.Println(resp)

	case "crossposts", "shares", "list-crossposts":
		if len(args) < 2 {
			fmt.Fprintf(os.Stderr, "Usage: post.ctl crossposts <post>\n  <post>: <post_id> or <post_file>@<community>\n")
			os.Exit(1)
		}
		target, _, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		if err := resolveToCommunityFile(&target); err != nil {
			log.Fatalf("Error: %v", err)
		}
		cmd := fmt.Sprintf("GET_CROSSPOSTS %s %s\n", target.community, target.postFile)
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			log.Fatalf("get crossposts failed: %v", err)
		}
		lines := strings.Split(strings.TrimRight(resp, "\n"), "\n")
		if len(lines) == 0 || !strings.HasPrefix(lines[0], "OK") {
			fmt.Println(resp)
			return
		}
		w := tabwriter.NewWriter(os.Stdout, 0, 8, 2, ' ', 0)
		fmt.Fprintf(w, "COMMUNITY\tPOST_FILE\tOPERATOR\tCREATED_AT\n")
		for _, line := range lines[1:] {
			cols := strings.Split(line, "\t")
			if len(cols) < 4 {
				continue
			}
			tVal, _ := strconv.ParseInt(cols[3], 10, 64)
			dateStr := time.Unix(tVal, 0).Format("01/02/2006 15:04:05")
			fmt.Fprintf(w, "%s\t%s\t%s\t%s\n", cols[0], cols[1], cols[2], dateStr)
		}
		w.Flush()

	case "origin", "source":
		if len(args) < 2 {
			fmt.Fprintf(os.Stderr, "Usage: post.ctl origin <post>\n  <post>: <post_id> or <post_file>@<community>\n")
			os.Exit(1)
		}
		target, _, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		if err := resolveToCommunityFile(&target); err != nil {
			log.Fatalf("Error: %v", err)
		}
		cmd := fmt.Sprintf("GET_CROSSPOST_PARENT %s %s\n", target.community, target.postFile)
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			log.Fatalf("get origin failed: %v", err)
		}
		if !strings.HasPrefix(resp, "OK ") {
			fmt.Print(resp)
			return
		}
		cols := strings.Split(strings.TrimPrefix(strings.TrimRight(resp, "\n"), "OK "), "\t")
		if len(cols) >= 6 {
			tVal, _ := strconv.ParseInt(cols[5], 10, 64)
			dateStr := time.Unix(tVal, 0).Format("01/02/2006 15:04:05")
			fmt.Printf("Origin Community : %s\n", cols[0])
			fmt.Printf("Origin Post File : %s\n", cols[1])
			fmt.Printf("Original CreatorName  : %s\n", cols[2])
			fmt.Printf("Original Title   : %s\n", cols[3])
			fmt.Printf("Crossposted By   : %s\n", cols[4])
			fmt.Printf("Crossposted At   : %s\n", dateStr)
		} else {
			fmt.Print(resp)
		}

	case "fetch", "fetch-file", "pull", "pull-file":
		if len(args) < 3 {
			fmt.Fprintf(os.Stderr, "Usage: post.ctl fetch <post> <output_path>\n  <post>: <post_id> or <post_file>@<community>\n")
			os.Exit(1)
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx {
			log.Fatalf("Usage: post.ctl fetch <post> <output_path>")
		}
		outPath := args[nextIdx]
		if err := resolveToCommunityFile(&target); err != nil {
			log.Fatalf("Error: %v", err)
		}
		cmd := fmt.Sprintf("FETCH_POST_FILE %s %s %s\n", target.community, target.postFile, outPath)
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			log.Fatalf("fetch failed: %v", err)
		}
		fmt.Println(resp)

	case "sync-dir":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl sync-dir <board> [--only-scores | --full | --rebuild] [--prune] [--add-missing] [--dry-run]")
		}
		comm := args[1]
		dryRun := false
		var modes []string
		for _, arg := range args[2:] {
			switch strings.TrimLeft(arg, "-") {
			case "dry-run", "dryrun", "n":
				dryRun = true
			case "only-scores":
				// default
			case "full":
				modes = append(modes, "full")
			case "prune":
				modes = append(modes, "prune")
			case "add-missing":
				modes = append(modes, "add-missing")
			case "rebuild":
				modes = append(modes, "rebuild")
			default:
				log.Fatalf("sync-dir: unknown argument: %s", arg)
			}
		}
		dryStr := "0"
		if dryRun {
			dryStr = "1"
		}
		modeStr := "-"
		if len(modes) > 0 {
			modeStr = strings.Join(modes, ",")
		}
		resp, _, err := sendCmd(fmt.Sprintf("SYNC_DIR %s %s %s\n", comm, dryStr, modeStr), nil)
		if err != nil {
			log.Fatalf("sync-dir failed: %v", err)
		}
		parts := strings.Fields(resp)
		if len(parts) < 8 || parts[0] != "OK" {
			log.Fatalf("sync-dir failed: %s", resp)
		}
		if strings.Contains(modeStr, "rebuild") {
			if dryRun {
				fmt.Printf("[+] Dry-run of rebuilding .DIR of '%s' from post.svc (nothing written):\n", comm)
			} else {
				fmt.Printf("[+] Rebuilt .DIR of '%s' from post.svc:\n", comm)
			}
			fmt.Printf("    Records before:     %s (%s known by post.svc, %s dropped)\n", parts[1], parts[2], parts[4])
			if len(parts) >= 10 {
				fmt.Printf("    Legacy posts:       %s imported first, %s records kept as they are\n", parts[8], parts[9])
			}
			fmt.Printf("    Records written:    %s (%s new)\n", parts[3], parts[5])
			fmt.Printf("    Articles rendered:  %s (%s failed)\n", parts[7], parts[6])
			if !dryRun {
				fmt.Println("    The previous .DIR is kept as .DIR.bak-<time>.")
			}
			break
		}
		what := "score and comments"
		if strings.Contains(modeStr, "full") {
			what = "all fields"
		}
		if dryRun {
			fmt.Printf("[+] Dry-run of syncing %s of '%s' to .DIR (nothing written):\n", what, comm)
		} else {
			fmt.Printf("[+] Synced %s of '%s' to .DIR:\n", what, comm)
		}
		fmt.Printf("    Records in .DIR:    %s\n    Known by post.svc:  %s\n    Records updated:    %s\n", parts[1], parts[2], parts[3])
		if strings.Contains(modeStr, "prune") {
			fmt.Printf("    Records pruned:     %s\n", parts[4])
		}
		if strings.Contains(modeStr, "add-missing") {
			fmt.Printf("    Records added:      %s (%s without article file)\n", parts[5], parts[6])
		}
		if !dryRun && (strings.Contains(modeStr, "prune") || strings.Contains(modeStr, "add-missing")) {
			fmt.Println("    The previous .DIR is kept as .DIR.bak-<time>.")
		}

	case "remove-community":
		// Lemmy's RemoveCommunity, by the system (mod 0), as mbbsd does
		// when a board is deleted.
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl remove-community <community> [reason]")
		}
		comm := args[1]
		reason := strings.Join(args[2:], " ")
		cmdLine := fmt.Sprintf("REMOVE_COMMUNITY %s", comm)
		if reason != "" {
			cmdLine += " 0 " + reason
		}
		resp, _, err := sendCmd(cmdLine+"\n", nil)
		if err != nil {
			log.Fatalf("remove-community failed: %v", err)
		}
		parts := strings.Fields(resp)
		if len(parts) < 2 || parts[0] != "OK" {
			log.Fatalf("remove-community failed: %s", resp)
		}
		if parts[1] == "0" {
			fmt.Printf("Community '%s' is unknown to post.svc; nothing removed.\n", comm)
		} else {
			fmt.Printf("Community '%s' (#%s) removed; its posts are kept until 'post.ctl purge-community #%s'.\n", comm, parts[1], parts[1])
		}

	case "purge-community":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl purge-community <community>|#<id>")
		}
		ref := args[1]
		resp, _, err := sendCmd(fmt.Sprintf("PURGE_COMMUNITY %s\n", ref), nil)
		if err != nil {
			log.Fatalf("purge-community failed: %v", err)
		}
		parts := strings.Fields(resp)
		if len(parts) < 4 || parts[0] != "OK" {
			log.Fatalf("purge-community failed: %s", resp)
		}
		fmt.Printf("Community '%s' (#%s) purged: %s posts and %s comments removed.\n", ref, parts[1], parts[2], parts[3])

	case "rename-community":
		if len(args) < 3 {
			log.Fatalf("Usage: post.ctl rename-community <old> <new>")
		}
		resp, _, err := sendCmd(fmt.Sprintf("RENAME_COMMUNITY %s %s\n", args[1], args[2]), nil)
		if err != nil {
			log.Fatalf("rename-community failed: %v", err)
		}
		parts := strings.Fields(resp)
		if len(parts) < 2 || parts[0] != "OK" {
			log.Fatalf("rename-community failed: %s", resp)
		}
		if parts[1] == "0" {
			fmt.Printf("Community '%s' is unknown to post.svc; nothing renamed.\n", args[1])
		} else {
			fmt.Printf("Community #%s renamed from '%s' to '%s'.\n", parts[1], args[1], args[2])
		}

	case "merge-community":
		if len(args) < 3 {
			log.Fatalf("Usage: post.ctl merge-community <from> <to>")
		}
		resp, _, err := sendCmd(fmt.Sprintf("MERGE_COMMUNITY %s %s\n", args[1], args[2]), nil)
		if err != nil {
			log.Fatalf("merge-community failed: %v", err)
		}
		parts := strings.Fields(resp)
		if len(parts) < 3 || parts[0] != "OK" {
			log.Fatalf("merge-community failed: %s", resp)
		}
		fmt.Printf("Moved %s posts from '%s' to '%s'.\n", parts[1], args[1], args[2])
		if parts[2] != "0" {
			fmt.Printf("%s posts were left in '%s' as '%s' already has their post files.\n", parts[2], args[1], args[2])
		}

	case "move-community":
		if len(args) < 3 {
			log.Fatalf("Usage: post.ctl move-community <community> <engine>")
		}
		resp, _, err := sendCmd(fmt.Sprintf("MOVE_COMMUNITY %s %s\n", args[1], args[2]), nil)
		if err != nil {
			log.Fatalf("move-community failed: %v", err)
		}
		parts := strings.Fields(resp)
		if len(parts) < 2 || parts[0] != "OK" {
			log.Fatalf("move-community failed: %s", resp)
		}
		fmt.Printf("Moved '%s' with %s posts to engine %s.\n", args[1], parts[1], args[2])

	case "communities", "list-communities":
		resp, payload, err := sendCmd("LIST_COMMUNITIES\n", nil)
		if err != nil {
			log.Fatalf("communities failed: %v", err)
		}
		if !strings.HasPrefix(resp, "OK") {
			log.Fatalf("communities failed: %s", resp)
		}
		w := tabwriter.NewWriter(os.Stdout, 0, 8, 2, ' ', 0)
		fmt.Fprintf(w, "ID\tNAME\tCATEGORY\tFLAGS\tCREATED\tDELETED\tMODERATORS\tTITLE\n")
		for _, line := range strings.Split(strings.TrimRight(string(payload), "\n"), "\n") {
			cols := strings.Split(line, "\t")
			if len(cols) < 8 {
				continue
			}
			created, _ := strconv.ParseInt(cols[2], 10, 64)
			deleted, _ := strconv.ParseInt(cols[3], 10, 64)
			deletedStr := "-"
			if deleted != 0 {
				deletedStr = time.Unix(deleted, 0).Format("2006-01-02 15:04")
			}
			flags, _ := strconv.ParseUint(cols[4], 10, 32)
			fmt.Fprintf(w, "%s\t%s\t%s\t0x%08x\t%s\t%s\t%s\t%s\n", cols[0], cols[1], cols[5], flags,
				time.Unix(created, 0).Format("2006-01-02 15:04"), deletedStr, cols[7], cols[6])
		}
		w.Flush()

	case "community", "get-community":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl community <community>|#<id>")
		}
		resp, payload, err := sendCmd(fmt.Sprintf("GET_COMMUNITY %s\n", args[1]), nil)
		if err != nil {
			log.Fatalf("community failed: %v", err)
		}
		if !strings.HasPrefix(resp, "OK") {
			log.Fatalf("community failed: %s", resp)
		}
		os.Stdout.Write(payload)
		if len(payload) > 0 && payload[len(payload)-1] != '\n' {
			fmt.Println()
		}

	case "import-boards":
		dryRun := len(args) >= 2 && strings.TrimLeft(args[1], "-") == "dry-run"
		dryStr := "0"
		if dryRun {
			dryStr = "1"
		}
		first, rest, err := sendCmd(fmt.Sprintf("IMPORT_BOARDS %s\n", dryStr), nil)
		if err != nil {
			log.Fatalf("import-boards failed: %v", err)
		}
		// ERR_DETAIL lines (boards that failed) come before the result.
		var resp string
		for _, line := range strings.Split(first+"\n"+string(rest), "\n") {
			if detail, ok := strings.CutPrefix(line, "ERR_DETAIL "); ok {
				fmt.Printf("[!] %s\n", detail)
			} else if line != "" {
				resp = line
			}
		}
		parts := strings.Fields(resp)
		if len(parts) < 5 || parts[0] != "OK" {
			log.Fatalf("import-boards failed: %s", resp)
		}
		verb := "Imported"
		if dryRun {
			verb = "Would import"
		}
		fmt.Printf("%s %s boards of %s records in .BRD (%s empty or links skipped, %s failed).\n", verb, parts[2], parts[1], parts[3], parts[4])

	case "purge", "purge-post", "purge-post-file":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl purge <post>\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, _, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		var cmd string
		if target.isFile {
			cmd = fmt.Sprintf("PURGE_POST_FILE %s %s\n", target.community, target.postFile)
		} else {
			cmd = fmt.Sprintf("PURGE_POST %d\n", target.postID)
		}
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			log.Fatalf("purge failed: %v", err)
		}
		fmt.Print(resp)

	case "remove", "remove-post":
		// Lemmy's RemovePost, as the site's operator (or mod): post.svc
		// keeps the post as removed, with the reason.
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl remove <post> [mod] [reason]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		nextIdx := 1 + consumed
		mod := "sysop"
		if len(args) > nextIdx {
			mod = args[nextIdx]
		}
		reason := ""
		if len(args) > nextIdx+1 {
			reason = strings.Join(args[nextIdx+1:], " ")
		}
		var cmd string
		if target.isFile {
			cmd = fmt.Sprintf("REMOVE_POST_FILE %s %s %s 0 %s\n", target.community, target.postFile, mod, reason)
		} else {
			cmd = fmt.Sprintf("REMOVE_POST %d %s 0 %s\n", target.postID, mod, reason)
		}
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			log.Fatalf("remove failed: %v", err)
		}
		fmt.Print(resp)

	case "recover", "recover-post":
		// Puts a deleted or removed post back (Discourse's recover).
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl recover <post>\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, _, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		var cmd string
		if target.isFile {
			cmd = fmt.Sprintf("RECOVER_POST_FILE %s %s\n", target.community, target.postFile)
		} else {
			cmd = fmt.Sprintf("RECOVER_POST %d\n", target.postID)
		}
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			log.Fatalf("recover failed: %v", err)
		}
		fmt.Print(resp)

	case "remove-comment":
		// Lemmy's RemoveComment, as the site's operator (or mod).
		if len(args) < 3 {
			log.Fatalf("Usage: post.ctl remove-comment <post> <seq> [mod] [reason]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx {
			log.Fatalf("Usage: post.ctl remove-comment <post> <seq> [mod] [reason]")
		}
		seq := args[nextIdx]
		mod := "sysop"
		if len(args) > nextIdx+1 {
			mod = args[nextIdx+1]
		}
		reason := ""
		if len(args) > nextIdx+2 {
			reason = strings.Join(args[nextIdx+2:], " ")
		}
		if err := resolveToCommunityFile(&target); err != nil {
			log.Fatalf("Error: %v", err)
		}
		cmd := fmt.Sprintf("REMOVE_COMMENT_FILE %s %s %s %s 0 %s\n", target.community, target.postFile, seq, mod, reason)
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			log.Fatalf("remove-comment failed: %v", err)
		}
		fmt.Print(resp)

	case "recover-comment":
		if len(args) < 3 {
			log.Fatalf("Usage: post.ctl recover-comment <post> <seq>\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx {
			log.Fatalf("Usage: post.ctl recover-comment <post> <seq>")
		}
		seq := args[nextIdx]
		if err := resolveToCommunityFile(&target); err != nil {
			log.Fatalf("Error: %v", err)
		}
		cmd := fmt.Sprintf("RECOVER_COMMENT_FILE %s %s %s\n", target.community, target.postFile, seq)
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			log.Fatalf("recover-comment failed: %v", err)
		}
		fmt.Print(resp)

	default:
		printUsage()
	}
}

func printUsage() {
	fmt.Printf(`post.ctl - CLI administration tool for post.svc

Post Identifiers:
  <post> can be specified as:
    <post_file>@<community>  (e.g. M.1479981956.A.D0D@Marginalman)
    <post_id>                (e.g. 1, 79986)

Usage:
  post.ctl ping
  post.ctl restart [-timeout D] [--] [svc args...]
  post.ctl backup [name | dir]   (name: <engine data dir>/backup/<name>; default: timestamp)
  post.ctl daily-backup [-dest DIR | -cross] [-keep N] [-keep-local N] [-verify] [name]
  post.ctl list <community> [count [start]] [deleted]
  post.ctl get <post>
  post.ctl render <post> [output] [--legacy-format]
  post.ctl render-community <community> [output_dir] [--legacy-format]
  post.ctl import-board <board>|--all [--overwrite] [--dry-run] [--workers N] [--limit N] [--offset N] [--no-merge] [--commentd-db PATH] [--re-render [target_dir]] [--legacy-format]
  post.ctl sync-dir <board> [--only-scores | --full | --rebuild] [--prune] [--add-missing] [--dry-run]
  post.ctl fetch <post> <output_path>      (output under $BBSHOME; relative to it)
  post.ctl update <post> <title> <content> [editor]
  post.ctl update-title <post> <title> [editor]
  post.ctl remove <post> [mod] [reason]     (Lemmy's RemovePost)
  post.ctl recover <post>                   (puts a deleted or removed post back)
  post.ctl purge <post>
  post.ctl communities
  post.ctl community <community>|#<id>
  post.ctl import-boards [--dry-run]
  post.ctl rename-community <old> <new>
  post.ctl merge-community <from> <to>
  post.ctl move-community <community> <engine> (move to another engine)
  post.ctl remove-community <community> [reason] (posts are kept)
  post.ctl purge-community <community>|#<id>  (removes all posts)
  post.ctl vote <post> <user> [1|-1|0] [person_id] [ip] [ctime]
  post.ctl votes <post>
  post.ctl comments <post> [start [limit]]
  post.ctl get-comment <comment_id>
  post.ctl api <method> [json]               (Lemmy API: GetPosts, GetPost, GetComments, GetComment, LockPost)
  post.ctl comment <post> <author> <content> [ip] [person_id]
  post.ctl remove-comment <post> <seq> [mod] [reason]
  post.ctl recover-comment <post> <seq>
  post.ctl update-comment <post> <seq> <content> [editor]
  post.ctl history <post> [rev]
  post.ctl comment-history <post> <seq> [rev]
  post.ctl crosspost <src_post> <target_post> <operator> [person_id]
  post.ctl crossposts <post>
  post.ctl origin <post>
  post.ctl add <community> <author> <title> <content>
  post.ctl add-file <community> <post_file> <author> <title> [person_id]
  post.ctl purge-posts <author> [days=7]
  post.ctl purge-comments <author> [days=7]
`)
}

func printCommentsTable(resp string, payload []byte) {
	if !strings.HasPrefix(resp, "OK") {
		fmt.Print(resp)
		return
	}
	respParts := strings.Fields(resp)
	count := 0
	total := 0
	if len(respParts) >= 2 {
		count, _ = strconv.Atoi(respParts[1])
	}
	if len(respParts) >= 3 {
		total, _ = strconv.Atoi(respParts[2])
	}
	if count == 0 {
		fmt.Printf("No comments (total: %d)\n", total)
		return
	}
	w := tabwriter.NewWriter(os.Stdout, 0, 8, 2, ' ', 0)
	fmt.Fprintf(w, "SEQ\tID\tAUTHOR\tDATE\tIP\tSTATUS\tCONTENT\n")
	lines := strings.Split(strings.TrimRight(string(payload), "\n"), "\n")
	for _, l := range lines {
		if l == "" {
			continue
		}
		cols := strings.Split(l, "\t")
		if len(cols) < 8 {
			continue
		}
		seq := cols[0]
		commentID := cols[6]
		author := cols[1]
		// <created_at>[/<modified_at>]
		created, modified, edited := strings.Cut(cols[2], "/")
		ts, _ := strconv.ParseInt(created, 10, 64)
		dateStr := time.Unix(ts, 0).Format("01/02 15:04")
		if mts, _ := strconv.ParseInt(modified, 10, 64); edited && mts > 0 {
			dateStr += " (edited " + time.Unix(mts, 0).Format("01/02 15:04") + ")"
		}
		ip := cols[3]
		typeVal, _ := strconv.Atoi(cols[4])
		isDel := (typeVal < 0)
		reason := cols[5]
		content := strings.Join(cols[7:], " / ")
		status := "ACTIVE"
		if isDel {
			status = "DELETED"
			if reason != "" {
				content = "[" + reason + "] " + content
			}
		}
		fmt.Fprintf(w, "[%s]\t#%s\t%s\t%s\t%s\t%s\t%s\n", seq, commentID, author, dateStr, ip, status, content)
	}
	w.Flush()
	fmt.Printf("(Showing %d of %d comments)\n", count, total)
}

func printPostHistory(resp string, payload []byte, rev int) {
	if !strings.HasPrefix(resp, "OK") {
		log.Fatalf("History failed: %s", resp)
	}
	if rev == 0 {
		w := tabwriter.NewWriter(os.Stdout, 0, 8, 2, ' ', 0)
		fmt.Fprintf(w, "REV\tEDITED_AT\tEDITOR\tTITLE\n")
		lines := strings.Split(strings.TrimRight(string(payload), "\n"), "\n")
		for _, line := range lines {
			if line == "" {
				continue
			}
			cols := strings.Split(line, "\t")
			if len(cols) < 4 {
				continue
			}
			rNum := cols[0]
			eTime, _ := strconv.ParseInt(cols[1], 10, 64)
			dateStr := time.Unix(eTime, 0).Format("01/02/2006 15:04:05")
			ed := cols[2]
			tStr := cols[3]
			fmt.Fprintf(w, "%s\t%s\t%s\t%s\n", rNum, dateStr, ed, tStr)
		}
		w.Flush()
	} else {
		parts := strings.Fields(resp)
		if len(parts) >= 7 {
			titleLen, _ := strconv.Atoi(parts[5])
			contentLen, _ := strconv.Atoi(parts[6])
			tBytes := payload[:titleLen]
			cBytes := payload[titleLen : titleLen+contentLen]
			eTime, _ := strconv.ParseInt(parts[3], 10, 64)
			dateStr := time.Unix(eTime, 0).Format("01/02/2006 15:04:05")
			fmt.Printf("[Revision %s by %s at %s]\nTitle: %s\n\n%s\n",
				parts[2], parts[4], dateStr, string(tBytes), string(cBytes))
		}
	}
}

func printCommentHistory(resp string, payload []byte, rev int) {
	if !strings.HasPrefix(resp, "OK") {
		log.Fatalf("Comment history failed: %s", resp)
	}
	if rev == 0 {
		w := tabwriter.NewWriter(os.Stdout, 0, 8, 2, ' ', 0)
		fmt.Fprintf(w, "REV\tEDITED_AT\tEDITOR\tCONTENT\n")
		lines := strings.Split(strings.TrimRight(string(payload), "\n"), "\n")
		for _, line := range lines {
			if line == "" {
				continue
			}
			cols := strings.Split(line, "\t")
			if len(cols) < 4 {
				continue
			}
			rNum := cols[0]
			eTime, _ := strconv.ParseInt(cols[1], 10, 64)
			dateStr := time.Unix(eTime, 0).Format("01/02/2006 15:04:05")
			ed := cols[2]
			cStr := cols[3]
			fmt.Fprintf(w, "%s\t%s\t%s\t%s\n", rNum, dateStr, ed, cStr)
		}
		w.Flush()
	} else {
		parts := strings.Fields(resp)
		if len(parts) >= 6 {
			contentLen, _ := strconv.Atoi(parts[5])
			cBytes := payload[:contentLen]
			eTime, _ := strconv.ParseInt(parts[3], 10, 64)
			dateStr := time.Unix(eTime, 0).Format("01/02/2006 15:04:05")
			fmt.Printf("[Comment Revision %s by %s at %s]\n%s\n",
				parts[2], parts[4], dateStr, string(cBytes))
		}
	}
}

// -----------------------------------------------------------------------------
// Legacy Man (精華區) Importer
