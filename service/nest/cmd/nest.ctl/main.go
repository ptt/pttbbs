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
	flagSocket = flag.String("socket", "", "Path to nest.svc UNIX socket (default $BBSHOME/run/nest.svc.sock)")
)

func getSocketPath() string {
	if *flagSocket != "" {
		return *flagSocket
	}
	env := os.Getenv("NEST_SOCK")
	if env != "" {
		return env
	}
	bbshome := bbs.BBSHome()
	if bbshome != "" {
		candidate := filepath.Join(bbshome, "run", "nest.svc.sock")
		if _, err := os.Stat(candidate); err == nil {
			return candidate
		}
	}
	if _, err := os.Stat("/tmp/post.sock"); err == nil {
		return "/tmp/post.sock"
	}
	if bbshome != "" {
		return filepath.Join(bbshome, "run", "nest.svc.sock")
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

// splitOpts separates the -options in args (anywhere) from the rest.
func splitOpts(args []string) (pos []string, opts map[string]bool) {
	opts = map[string]bool{}
	for _, a := range args {
		if len(a) > 1 && strings.HasPrefix(a, "-") {
			opts[a] = true
		} else {
			pos = append(pos, a)
		}
	}
	return pos, opts
}

// getBody returns the body and the ID of the post in args[0] (GET_POST).
func getBody(args []string) ([]byte, uint64, error) {
	target, _, err := parsePostArg(args, 0)
	if err != nil {
		return nil, 0, err
	}
	header := fmt.Sprintf("GET_POST %d\n", target.postID)
	if target.isFile {
		header = fmt.Sprintf("GET_POST_FILE %s %s\n", target.community, target.postFile)
	}
	resp, payload, err := sendCmd(header, nil)
	if err != nil {
		return nil, 0, err
	}
	// OK <id> ... <title_bytes> <content_bytes>, then the title and body.
	f := strings.Fields(resp)
	if len(f) < 14 || f[0] != "OK" {
		return nil, 0, fmt.Errorf("%s", resp)
	}
	id, _ := strconv.ParseUint(f[1], 10, 64)
	titleLen, _ := strconv.Atoi(f[len(f)-2])
	contentLen, _ := strconv.Atoi(f[len(f)-1])
	if titleLen < 0 || contentLen < 0 || titleLen+contentLen > len(payload) {
		return nil, 0, fmt.Errorf("short reply: %s", resp)
	}
	return payload[titleLen : titleLen+contentLen], id, nil
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
	svcctl.Exit(run())
}

// fail prints the message with log and returns exit code 1.
func fail(format string, a ...any) int {
	log.Printf(format, a...)
	return 1
}

func run() int {
	flag.Parse()
	args := flag.Args()

	if len(args) == 0 {
		printUsage()
		return 0
	}

	cmd := args[0]
	switch cmd {
	case "restart":
		return svcctl.Main(svcctl.Options{Name: "nest.svc", Socket: getSocketPath(), Ping: ping}, args[1:])

	case "ping":
		resp, _, err := sendCmd("PING\n", nil)
		if err != nil {
			return fail("Error: %v", err)
		}
		fmt.Printf("Ping response: %s\n", resp)

	case "daily-backup", "inc-backup":
		return dailyBackup(args[1:])

	case "extract-backup", "export-community":
		return extractBackup(args[0], args[1:])

	case "backup":
		// nest.ctl backup [name | dir]
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
				return fail("Error: %v", err)
			}
			dest = abs
		}
		resp, payload, err := sendCmd(strings.TrimRight("BACKUP "+dest, " ")+"\n", nil)
		if err != nil {
			return fail("backup failed: %v", err)
		}
		if !strings.HasPrefix(resp, "OK") {
			return fail("backup failed: %s", resp)
		}
		ms := 0
		if parts := strings.Fields(resp); len(parts) >= 2 {
			ms, _ = strconv.Atoi(parts[1])
		}
		fmt.Printf("Backup completed in %v:\n", time.Duration(ms)*time.Millisecond)
		fmt.Print(string(payload))
		fmt.Println("To restore: stop nest.svc, copy each directory's *.pebble (and communities.db,")
		fmt.Println("in the first one) back into its engine's data dir, remove meta.sqlite3*")
		fmt.Println("there and start nest.svc: it rebuilds meta.sqlite3 (about 25 minutes for")
		fmt.Println("48M posts) before serving. communities.db cannot be rebuilt at all.")

	case "add-file":
		if len(args) < 5 {
			return fail("Usage: nest.ctl add-file <community> <post_file> <author> <title> [creator_id]")
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
			return fail("add-file failed: %v", err)
		}
		fmt.Println(resp)

	case "add":
		if len(args) < 5 {
			return fail("Usage: nest.ctl add <community> <author> <title> <content>")
		}
		titleB := []byte(args[3])
		contentB := []byte(args[4])
		header := fmt.Sprintf("CREATE_POST %s - %s 0 0 %d 0 %d %d\n", args[1], args[2], time.Now().Unix(), len(titleB), len(contentB))
		payload := append(titleB, contentB...)
		resp, _, err := sendCmd(header, payload)
		if err != nil {
			return fail("add failed: %v", err)
		}
		fmt.Println(resp)

	case "comment":
		if len(args) < 4 {
			return fail("Usage: nest.ctl comment <post> <author> <content> [ip] [creator_id]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			return fail("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx+1 {
			return fail("Usage: nest.ctl comment <post> <author> <content> [ip] [creator_id]")
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
			return fail("Error: %v", err)
		}
		header := fmt.Sprintf("CREATE_COMMENT %s %s %s %d %s %d %d %d\n", target.community, target.postFile, author, uid, ip, time.Now().Unix(), len(contentB), legType)
		resp, _, err := sendCmd(header, contentB)
		if err != nil {
			return fail("comment failed: %v", err)
		}
		fmt.Println(resp)

	case "api":
		// Lemmy's API, e.g. nest.ctl api GetPosts '{"community_name":"Test"}'
		if len(args) < 2 {
			return fail("Usage: nest.ctl api <method> [json]")
		}
		req := []byte("{}")
		if len(args) > 2 {
			req = []byte(args[2])
		}
		resp, payload, err := sendCmd(fmt.Sprintf("API %s %d\n", args[1], len(req)), req)
		if err != nil {
			return fail("api failed: %v", err)
		}
		if !strings.HasPrefix(resp, "OK") {
			return fail("api failed: %s", resp)
		}
		fmt.Println(string(payload))

	case "get-comment":
		if len(args) < 2 {
			return fail("Usage: nest.ctl get-comment <comment_id>")
		}
		if _, err := strconv.ParseUint(args[1], 10, 64); err != nil {
			return fail("Invalid comment ID: %s", args[1])
		}
		resp, payload, err := sendCmd(fmt.Sprintf("GET_COMMENT %s\n", args[1]), nil)
		if err != nil {
			return fail("get-comment failed: %v", err)
		}
		parts := strings.Fields(resp)
		if len(parts) < 3 || parts[0] != "OK" {
			return fail("get-comment failed: %s", resp)
		}
		fmt.Printf("Comment #%s is floor %s of post %s:\n", args[1], parts[2], parts[1])
		fmt.Print(string(payload))

	case "vote", "vote-file":
		if len(args) < 3 {
			return fail("Usage: nest.ctl vote <post> <user> [1|-1|0] [creator_id] [ip] [ctime]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			return fail("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx {
			return fail("Usage: nest.ctl vote <post> <user> [1|-1|0] [creator_id] [ip] [ctime]")
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
			return fail("vote failed: %v", err)
		}
		fmt.Println(resp)

	case "votes", "votes-file":
		if len(args) < 2 {
			return fail("Usage: nest.ctl votes <post>\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, _, err := parsePostArg(args, 1)
		if err != nil {
			return fail("Error: %v", err)
		}
		var header string
		if target.isFile {
			header = fmt.Sprintf("LIST_POST_LIKES_FILE %s %s\n", target.community, target.postFile)
		} else {
			header = fmt.Sprintf("LIST_POST_LIKES %d\n", target.postID)
		}
		conn, err := connectDaemon()
		if err != nil {
			return fail("connect daemon failed: %v", err)
		}
		defer conn.Close()

		if _, err := conn.Write([]byte(header)); err != nil {
			return fail("send votes cmd failed: %v", err)
		}
		reader := bufio.NewReader(conn)
		statusLine, err := reader.ReadString('\n')
		if err != nil {
			return fail("read response failed: %v", err)
		}
		statusLine = strings.TrimSpace(statusLine)
		if !strings.HasPrefix(statusLine, "OK") {
			return fail("votes failed: %s", statusLine)
		}
		var total int
		fmt.Sscanf(statusLine, "OK %d", &total)
		if total == 0 {
			fmt.Println("No votes")
			return 0
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
			return fail("Usage: nest.ctl get <post>\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, _, err := parsePostArg(args, 1)
		if err != nil {
			return fail("Error: %v", err)
		}
		var header string
		if target.isFile {
			header = fmt.Sprintf("GET_POST_FILE %s %s\n", target.community, target.postFile)
		} else {
			header = fmt.Sprintf("GET_POST %d\n", target.postID)
		}
		resp, payload, err := sendCmd(header, nil)
		if err != nil {
			return fail("Error: %v", err)
		}
		fmt.Println(resp)
		if len(payload) > 0 {
			fmt.Println(string(payload))
		}

	case "get-content":
		// The content as nest.svc keeps it (UTF-8), not the rendered article.
		pos, opts := splitOpts(args[1:])
		if len(pos) < 1 || len(pos) > 2 {
			return fail("Usage: nest.ctl get-content <post> [file] [-big5]")
		}
		body, _, err := getBody(pos)
		if err != nil {
			return fail("get-content failed: %v", err)
		}
		if opts["-big5"] {
			body = big5uao.Encode(string(body))
		}
		if len(pos) == 2 && pos[1] != "-" {
			if err := os.WriteFile(pos[1], body, 0644); err != nil {
				return fail("get-content failed: %v", err)
			}
		} else {
			os.Stdout.Write(body)
		}

	case "overwrite-content":
		pos, opts := splitOpts(args[1:])
		if len(pos) != 2 {
			return fail("Usage: nest.ctl overwrite-content <post> <file|-> [-touch] [-big5]")
		}
		var body []byte
		var err error
		if pos[1] == "-" {
			body, err = io.ReadAll(os.Stdin)
		} else {
			body, err = os.ReadFile(pos[1])
		}
		if err != nil {
			return fail("overwrite-content failed: %v", err)
		}
		if opts["-big5"] {
			body = []byte(big5uao.DecodeSGR66(body))
		}
		_, id, err := getBody(pos[:1])
		if err != nil {
			return fail("overwrite-content failed: %v", err)
		}
		touch := 0
		if opts["-touch"] {
			touch = 1
		}
		resp, _, err := sendCmd(fmt.Sprintf("SET_POST_CONTENT %d %d %d\n", id, touch, len(body)), body)
		if err != nil {
			return fail("overwrite-content failed: %v", err)
		}
		if !strings.HasPrefix(resp, "OK") {
			return fail("overwrite-content failed: %s", resp)
		}
		fmt.Println(resp)

	case "purge-comments":
		if len(args) < 2 {
			return fail("Usage: nest.ctl purge-comments <author> [days=7]")
		}
		author := args[1]
		days := 7
		if len(args) >= 3 {
			days, _ = strconv.Atoi(args[2])
		}
		header := fmt.Sprintf("PURGE_PERSON_COMMENTS %s %d\n", author, days)
		resp, _, err := sendCmd(header, nil)
		if err != nil {
			return fail("purge-comments failed: %v", err)
		}
		fmt.Println(resp)

	case "purge-posts":
		if len(args) < 2 {
			return fail("Usage: nest.ctl purge-posts <author> [days=7]")
		}
		author := args[1]
		days := 7
		if len(args) >= 3 {
			days, _ = strconv.Atoi(args[2])
		}
		header := fmt.Sprintf("PURGE_PERSON_POSTS %s %d\n", author, days)
		resp, _, err := sendCmd(header, nil)
		if err != nil {
			return fail("purge-posts failed: %v", err)
		}
		fmt.Println(resp)

	case "list":
		if len(args) < 2 {
			return fail("Usage: nest.ctl list <community> [count [start]]")
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
			return fail("List failed: %v", err)
		}
		if !strings.HasPrefix(resp, "OK") {
			return fail("List failed: %s", resp)
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
			return 0
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
			return fail("Usage: nest.ctl render <post> [output] [--legacy-format]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			return fail("Error: %v", err)
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
			return fail("Render failed: %v", err)
		}
		if !strings.HasPrefix(resp, "OK") {
			return fail("Render failed: %s", resp)
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
			return fail("Usage: nest.ctl import-board <board>|--all [render_target] [--overwrite] [--dry-run] [--workers N] [--jobs N] [--limit N] [--offset N] [--no-merge] [--commentd-db PATH]")
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
		jobs := 1

		for i := 2; i < len(args); i++ {
			arg := args[i]
			if (arg == "--jobs" || arg == "-jobs" || arg == "-j") && i+1 < len(args) {
				jobs, _ = strconv.Atoi(args[i+1])
				i++
			} else if strings.HasPrefix(arg, "--jobs=") {
				jobs, _ = strconv.Atoi(strings.TrimPrefix(arg, "--jobs="))
			} else if arg == "--overwrite" || arg == "-overwrite" {
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
			// switching the backend to nest.svc.
			if renderTarget != "-" && renderTarget != "boards" && renderTarget != "cache" {
				return fail("import-board --all: the render target must be boards or cache (one directory per board), not %s", renderTarget)
			}
			boards, skipped, err := boardsToImport(bbs.BBSHome(), filepath.Join(bbs.BBSHome(), ".BRD"))
			if err != nil {
				return fail("import-board --all: read .BRD failed: %v", err)
			}
			fmt.Printf("[*] Importing %d boards from .BRD (%d without .DIR skipped)...\n", len(boards), len(skipped))
			printMode()
			if jobs > 1 {
				fmt.Printf("    Jobs: %d boards at a time\n", jobs)
			}
			failed := importBoards(boards, jobs, mkHeader, dryRun)
			fmt.Printf("[+] %d boards done, %d failed\n", len(boards)-len(failed), len(failed))
			if !dryRun {
				// Copies imported before their sources.
				if err := linkCrossposts(); err != nil {
					fmt.Printf("[!] link-crossposts failed: %v\n", err)
				}
				// Also communities imported before (with the import time).
				if err := fixCommunityPublished(); err != nil {
					fmt.Printf("[!] fix-published failed: %v\n", err)
				}
			}
			if len(failed) > 0 {
				fmt.Printf("    Failed: %s\n", strings.Join(failed, " "))
				return 1
			}
			break
		}

		fmt.Printf("[*] Starting fast Go migration for board '%s'...\n", comm)
		printMode()
		r, err := runImportBoard(mkHeader(comm), "", os.Stdout)
		if err != nil {
			return fail("[!] Migration failed: %v", err)
		}
		printImportBoardResult(os.Stdout, r, dryRun)

	case "render-community":
		// render-community <community>|--all [output_dir|boards|cache] [--legacy-format] [--jobs N]
		//   boards: into the board directories, the cache that mbbsd reads.
		//   cache (the default for one community): $BBSHOME/cache/<community>.
		if len(args) < 2 {
			return fail("Usage: nest.ctl render-community <community>|--all [output_dir|boards|cache] [--legacy-format] [--jobs N]")
		}
		comm := args[1]
		target := ""
		legacyFormat := false
		jobs := 1
		for i := 2; i < len(args); i++ {
			arg := args[i]
			if arg == "--legacy-format" || arg == "-legacy-format" || arg == "--legacy" {
				legacyFormat = true
			} else if (arg == "--jobs" || arg == "-jobs" || arg == "-j") && i+1 < len(args) {
				jobs, _ = strconv.Atoi(args[i+1])
				i++
			} else if strings.HasPrefix(arg, "--jobs=") {
				jobs, _ = strconv.Atoi(strings.TrimPrefix(arg, "--jobs="))
			} else if !strings.HasPrefix(arg, "-") {
				target = arg
			}
		}
		fmtStr := "modern"
		if legacyFormat {
			fmtStr = "legacy"
		}
		// outDir is where the files of board go, as sent to nest.svc.
		outDir := func(board string) string {
			switch target {
			case "boards":
				return "boards"
			case "", "cache":
				return filepath.Join(bbs.BBSHome(), "cache", board)
			}
			if abs, err := filepath.Abs(target); err == nil {
				return abs
			}
			return target
		}
		render := func(board, prefix string, out io.Writer) (func(io.Writer), error) {
			dir := outDir(board)
			if dir != "boards" {
				_ = os.MkdirAll(dir, 0755)
			}
			n, secs, err := runRenderCommunity(fmt.Sprintf("RENDER_COMMUNITY %s %s %s\n", board, dir, fmtStr), prefix, out)
			if err != nil {
				return nil, err
			}
			return func(w io.Writer) {
				fmt.Fprintf(w, "[+] Render completed in %ss: %s posts rendered into %s (format: %s)\n", secs, n, dir, fmtStr)
			}, nil
		}

		if comm == "--all" || comm == "-all" {
			// Every board in .BRD that has a .DIR, e.g. after import-board
			// --all and sync-dir --all when switching to nest.svc.
			if target != "boards" && target != "cache" {
				return fail("render-community --all: the target must be boards or cache (one directory per board), not %q", target)
			}
			boards, skipped, err := boardsToImport(bbs.BBSHome(), filepath.Join(bbs.BBSHome(), ".BRD"))
			if err != nil {
				return fail("render-community --all: read .BRD failed: %v", err)
			}
			fmt.Printf("[*] Rendering %d boards from .BRD into %s (format: %s; %d without .DIR skipped)...\n", len(boards), target, fmtStr, len(skipped))
			if jobs > 1 {
				fmt.Printf("    Jobs: %d boards at a time\n", jobs)
			}
			failed := eachBoard(boards, jobs, render)
			fmt.Printf("[+] %d boards done, %d failed\n", len(boards)-len(failed), len(failed))
			if len(failed) > 0 {
				fmt.Printf("    Failed: %s\n", strings.Join(failed, " "))
				return 1
			}
			break
		}

		fmt.Printf("[*] Rendering community '%s' into %s (format: %s)...\n", comm, outDir(comm), fmtStr)
		report, err := render(comm, "", os.Stdout)
		if err != nil {
			return fail("[!] Render failed: %v", err)
		}
		report(os.Stdout)

	case "update", "update-file", "edit", "edit-file":
		if len(args) < 4 {
			return fail("Usage: nest.ctl update <post> <title> <content> [editor]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			return fail("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) < nextIdx+2 {
			return fail("Usage: nest.ctl update <post> <title> <content> [editor]")
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
			return fail("update failed: %v", err)
		}
		fmt.Println(resp)

	case "update-title", "update-title-file", "edit-title", "edit-title-file":
		if len(args) < 3 {
			return fail("Usage: nest.ctl update-title <post> <title> [editor]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			return fail("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx {
			return fail("Usage: nest.ctl update-title <post> <title> [editor]")
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
			return fail("update-title failed: %v", err)
		}
		fmt.Println(resp)

	case "comments", "comments-file":
		if len(args) < 2 {
			return fail("Usage: nest.ctl comments <post> [start [limit]]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			return fail("Error: %v", err)
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
			return fail("comments failed: %v", err)
		}
		printCommentsTable(resp, payload)

	case "update-comment", "update-comment-file", "edit-comment", "edit-comment-file":
		if len(args) < 4 {
			return fail("Usage: nest.ctl update-comment <post> <seq> <content> [editor]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			return fail("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx+1 {
			return fail("Usage: nest.ctl update-comment <post> <seq> <content> [editor]")
		}
		seq := args[nextIdx]
		contentB := []byte(args[nextIdx+1])
		editor := "sysop"
		if len(args) > nextIdx+2 {
			editor = args[nextIdx+2]
		}
		if err := resolveToCommunityFile(&target); err != nil {
			return fail("Error: %v", err)
		}
		header := fmt.Sprintf("EDIT_COMMENT_FILE %s %s %s %s 0 %d\n", target.community, target.postFile, seq, editor, len(contentB))
		resp, _, err := sendCmd(header, contentB)
		if err != nil {
			return fail("update-comment failed: %v", err)
		}
		fmt.Println(resp)

	case "history", "history-file", "post-history", "post-history-file":
		if len(args) < 2 {
			return fail("Usage: nest.ctl history <post> [rev]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			return fail("Error: %v", err)
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
			return fail("history failed: %v", err)
		}
		if err := printPostHistory(resp, payload, rev); err != nil {
			return fail("%v", err)
		}

	case "comment-history", "comment-history-file":
		if len(args) < 3 {
			return fail("Usage: nest.ctl comment-history <post> <seq> [rev]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			return fail("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx {
			return fail("Usage: nest.ctl comment-history <post> <seq> [rev]")
		}
		seq := args[nextIdx]
		rev := 0
		if len(args) > nextIdx+1 {
			rev, _ = strconv.Atoi(args[nextIdx+1])
		}
		if err := resolveToCommunityFile(&target); err != nil {
			return fail("Error: %v", err)
		}
		header := fmt.Sprintf("GET_COMMENT_REVISIONS_FILE %s %s %s %d\n", target.community, target.postFile, seq, rev)
		resp, payload, err := sendCmd(header, nil)
		if err != nil {
			return fail("comment-history failed: %v", err)
		}
		if err := printCommentHistory(resp, payload, rev); err != nil {
			return fail("%v", err)
		}

	case "crosspost", "share":
		if len(args) < 3 {
			return fail("Usage: nest.ctl crosspost <src_post> <target_post> <operator> [person_id]\n  <post>: <post_id> or <post_file>@<community>")
		}
		srcTarget, srcConsumed, srcErr := parsePostArg(args, 1)
		if srcErr != nil {
			return fail("Error parsing src_post: %v", srcErr)
		}
		tgtTarget, tgtConsumed, tgtErr := parsePostArg(args, 1+srcConsumed)
		if tgtErr != nil {
			return fail("Error parsing target_post: %v", tgtErr)
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
			return fail("crosspost failed: %v", err)
		}
		fmt.Println(resp)

	case "link-crossposts":
		if err := linkCrossposts(); err != nil {
			return fail("link-crossposts failed: %v", err)
		}

	case "fix-published":
		if err := fixCommunityPublished(args[1:]...); err != nil {
			return fail("fix-published failed: %v", err)
		}

	case "crossposts", "shares", "list-crossposts":
		if len(args) < 2 {
			fmt.Fprintf(os.Stderr, "Usage: nest.ctl crossposts <post>\n  <post>: <post_id> or <post_file>@<community>\n")
			return 1
		}
		target, _, err := parsePostArg(args, 1)
		if err != nil {
			return fail("Error: %v", err)
		}
		if err := resolveToCommunityFile(&target); err != nil {
			return fail("Error: %v", err)
		}
		cmd := fmt.Sprintf("GET_CROSSPOSTS %s %s\n", target.community, target.postFile)
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			return fail("get crossposts failed: %v", err)
		}
		lines := strings.Split(strings.TrimRight(resp, "\n"), "\n")
		if len(lines) == 0 || !strings.HasPrefix(lines[0], "OK") {
			fmt.Println(resp)
			return 0
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
			fmt.Fprintf(os.Stderr, "Usage: nest.ctl origin <post>\n  <post>: <post_id> or <post_file>@<community>\n")
			return 1
		}
		target, _, err := parsePostArg(args, 1)
		if err != nil {
			return fail("Error: %v", err)
		}
		if err := resolveToCommunityFile(&target); err != nil {
			return fail("Error: %v", err)
		}
		cmd := fmt.Sprintf("GET_CROSSPOST_PARENT %s %s\n", target.community, target.postFile)
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			return fail("get origin failed: %v", err)
		}
		if !strings.HasPrefix(resp, "OK ") {
			fmt.Print(resp)
			return 0
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
			fmt.Fprintf(os.Stderr, "Usage: nest.ctl fetch <post> <output_path>\n  <post>: <post_id> or <post_file>@<community>\n")
			return 1
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			return fail("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx {
			return fail("Usage: nest.ctl fetch <post> <output_path>")
		}
		outPath := args[nextIdx]
		if err := resolveToCommunityFile(&target); err != nil {
			return fail("Error: %v", err)
		}
		cmd := fmt.Sprintf("FETCH_POST_FILE %s %s %s\n", target.community, target.postFile, outPath)
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			return fail("fetch failed: %v", err)
		}
		fmt.Println(resp)

	case "sync-dir":
		if len(args) < 2 {
			return fail("Usage: nest.ctl sync-dir <board>|--all [--only-scores | --full | --rebuild] [--prune] [--add-missing] [--dry-run]")
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
				return fail("sync-dir: unknown argument: %s", arg)
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
		// --all: every board of .BRD with posts of its own, e.g. to write
		// the scores and numbers of comments of nest.svc into the .DIR of
		// every board after import-board --all.
		if comm == "--all" || comm == "-all" {
			boards, _, err := boardsToImport(bbs.BBSHome(), filepath.Join(bbs.BBSHome(), ".BRD"))
			if err != nil {
				return fail("sync-dir --all: read .BRD failed: %v", err)
			}
			var failed []string
			for i, b := range boards {
				fmt.Printf("[%d/%d] ", i+1, len(boards))
				if err := syncDir(b, dryRun, dryStr, modeStr); err != nil {
					fmt.Printf("[!] %s: %v\n", b, err)
					failed = append(failed, b)
				}
			}
			if len(failed) > 0 {
				return fail("sync-dir --all: %d board(s) failed: %s", len(failed), strings.Join(failed, " "))
			}
			break
		}
		if err := syncDir(comm, dryRun, dryStr, modeStr); err != nil {
			return fail("sync-dir failed: %v", err)
		}

	case "remove-community":
		// Lemmy's RemoveCommunity, by the system (mod 0), as mbbsd does
		// when a board is deleted.
		if len(args) < 2 {
			return fail("Usage: nest.ctl remove-community <community> [reason]")
		}
		comm := args[1]
		reason := strings.Join(args[2:], " ")
		cmdLine := fmt.Sprintf("REMOVE_COMMUNITY %s", comm)
		if reason != "" {
			cmdLine += " 0 " + reason
		}
		resp, _, err := sendCmd(cmdLine+"\n", nil)
		if err != nil {
			return fail("remove-community failed: %v", err)
		}
		parts := strings.Fields(resp)
		if len(parts) < 2 || parts[0] != "OK" {
			return fail("remove-community failed: %s", resp)
		}
		if parts[1] == "0" {
			fmt.Printf("Community '%s' is unknown to nest.svc; nothing removed.\n", comm)
		} else {
			fmt.Printf("Community '%s' (#%s) removed; its posts are kept until 'nest.ctl purge-community #%s'.\n", comm, parts[1], parts[1])
		}

	case "purge-community":
		if len(args) < 2 {
			return fail("Usage: nest.ctl purge-community <community>|#<id>")
		}
		ref := args[1]
		resp, _, err := sendCmd(fmt.Sprintf("PURGE_COMMUNITY %s\n", ref), nil)
		if err != nil {
			return fail("purge-community failed: %v", err)
		}
		parts := strings.Fields(resp)
		if len(parts) < 4 || parts[0] != "OK" {
			return fail("purge-community failed: %s", resp)
		}
		fmt.Printf("Community '%s' (#%s) purged: %s posts and %s comments removed.\n", ref, parts[1], parts[2], parts[3])

	case "rename-community":
		if len(args) < 3 {
			return fail("Usage: nest.ctl rename-community <old> <new>")
		}
		resp, _, err := sendCmd(fmt.Sprintf("RENAME_COMMUNITY %s %s\n", args[1], args[2]), nil)
		if err != nil {
			return fail("rename-community failed: %v", err)
		}
		parts := strings.Fields(resp)
		if len(parts) < 2 || parts[0] != "OK" {
			return fail("rename-community failed: %s", resp)
		}
		if parts[1] == "0" {
			fmt.Printf("Community '%s' is unknown to nest.svc; nothing renamed.\n", args[1])
		} else {
			fmt.Printf("Community #%s renamed from '%s' to '%s'.\n", parts[1], args[1], args[2])
		}

	case "merge-community":
		if len(args) < 3 {
			return fail("Usage: nest.ctl merge-community <from> <to>")
		}
		resp, _, err := sendCmd(fmt.Sprintf("MERGE_COMMUNITY %s %s\n", args[1], args[2]), nil)
		if err != nil {
			return fail("merge-community failed: %v", err)
		}
		parts := strings.Fields(resp)
		if len(parts) < 3 || parts[0] != "OK" {
			return fail("merge-community failed: %s", resp)
		}
		fmt.Printf("Moved %s posts from '%s' to '%s'.\n", parts[1], args[1], args[2])
		if parts[2] != "0" {
			fmt.Printf("%s posts were left in '%s' as '%s' already has their post files.\n", parts[2], args[1], args[2])
		}

	case "move-community":
		if len(args) < 3 {
			return fail("Usage: nest.ctl move-community <community> <engine>")
		}
		resp, _, err := sendCmd(fmt.Sprintf("MOVE_COMMUNITY %s %s\n", args[1], args[2]), nil)
		if err != nil {
			return fail("move-community failed: %v", err)
		}
		parts := strings.Fields(resp)
		if len(parts) < 2 || parts[0] != "OK" {
			return fail("move-community failed: %s", resp)
		}
		fmt.Printf("Moved '%s' with %s posts to engine %s.\n", args[1], parts[1], args[2])

	case "communities", "list-communities":
		resp, payload, err := sendCmd("LIST_COMMUNITIES\n", nil)
		if err != nil {
			return fail("communities failed: %v", err)
		}
		if !strings.HasPrefix(resp, "OK") {
			return fail("communities failed: %s", resp)
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
			return fail("Usage: nest.ctl community <community>|#<id>")
		}
		resp, payload, err := sendCmd(fmt.Sprintf("GET_COMMUNITY %s\n", args[1]), nil)
		if err != nil {
			return fail("community failed: %v", err)
		}
		if !strings.HasPrefix(resp, "OK") {
			return fail("community failed: %s", resp)
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
			return fail("import-boards failed: %v", err)
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
			return fail("import-boards failed: %s", resp)
		}
		verb := "Imported"
		if dryRun {
			verb = "Would import"
		}
		fmt.Printf("%s %s boards of %s records in .BRD (%s empty or links skipped, %s failed).\n", verb, parts[2], parts[1], parts[3], parts[4])

	case "purge", "purge-post", "purge-post-file":
		if len(args) < 2 {
			return fail("Usage: nest.ctl purge <post>\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, _, err := parsePostArg(args, 1)
		if err != nil {
			return fail("Error: %v", err)
		}
		var cmd string
		if target.isFile {
			cmd = fmt.Sprintf("PURGE_POST_FILE %s %s\n", target.community, target.postFile)
		} else {
			cmd = fmt.Sprintf("PURGE_POST %d\n", target.postID)
		}
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			return fail("purge failed: %v", err)
		}
		fmt.Print(resp)

	case "remove", "remove-post":
		// Lemmy's RemovePost, as the site's operator (or mod): nest.svc
		// keeps the post as removed, with the reason.
		if len(args) < 2 {
			return fail("Usage: nest.ctl remove <post> [mod] [reason]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			return fail("Error: %v", err)
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
			return fail("remove failed: %v", err)
		}
		fmt.Print(resp)

	case "recover", "recover-post":
		// Puts a deleted or removed post back (Discourse's recover).
		if len(args) < 2 {
			return fail("Usage: nest.ctl recover <post>\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, _, err := parsePostArg(args, 1)
		if err != nil {
			return fail("Error: %v", err)
		}
		var cmd string
		if target.isFile {
			cmd = fmt.Sprintf("RECOVER_POST_FILE %s %s\n", target.community, target.postFile)
		} else {
			cmd = fmt.Sprintf("RECOVER_POST %d\n", target.postID)
		}
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			return fail("recover failed: %v", err)
		}
		fmt.Print(resp)

	case "remove-comment":
		// Lemmy's RemoveComment, as the site's operator (or mod).
		if len(args) < 3 {
			return fail("Usage: nest.ctl remove-comment <post> <seq> [mod] [reason]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			return fail("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx {
			return fail("Usage: nest.ctl remove-comment <post> <seq> [mod] [reason]")
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
			return fail("Error: %v", err)
		}
		cmd := fmt.Sprintf("REMOVE_COMMENT_FILE %s %s %s %s 0 %s\n", target.community, target.postFile, seq, mod, reason)
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			return fail("remove-comment failed: %v", err)
		}
		fmt.Print(resp)

	case "recover-comment":
		if len(args) < 3 {
			return fail("Usage: nest.ctl recover-comment <post> <seq>\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			return fail("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx {
			return fail("Usage: nest.ctl recover-comment <post> <seq>")
		}
		seq := args[nextIdx]
		if err := resolveToCommunityFile(&target); err != nil {
			return fail("Error: %v", err)
		}
		cmd := fmt.Sprintf("RECOVER_COMMENT_FILE %s %s %s\n", target.community, target.postFile, seq)
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			return fail("recover-comment failed: %v", err)
		}
		fmt.Print(resp)

	default:
		printUsage()
	}
	return 0
}

func printUsage() {
	fmt.Printf(`nest.ctl - CLI administration tool for nest.svc

Post Identifiers:
  <post> can be specified as:
    <post_file>@<community>  (e.g. M.1479981956.A.D0D@Marginalman)
    <post_id>                (e.g. 1, 79986)

Usage:
  nest.ctl ping
  nest.ctl restart [-timeout D] [--] [svc args...]
  nest.ctl backup [name | dir]   (name: <engine data dir>/backup/<name>; default: timestamp)
  nest.ctl daily-backup [-dest DIR | -cross] [-keep N] [-keep-local N] [-verify] [name]
  nest.ctl list <community> [count [start]] [deleted]
  nest.ctl get <post>
  nest.ctl get-content <post> [file] [-big5]   (the content as kept, UTF-8; for overwrite-content)
  nest.ctl overwrite-content <post> <file|-> [-touch] [-big5]   (replaces the content: no revision, no modlog; -touch moves modified)
  nest.ctl render <post> [output] [--legacy-format]
  nest.ctl render-community <community>|--all [output_dir|boards|cache] [--legacy-format] [--jobs N]
  nest.ctl extract-backup [-o DIR] [-id N] [-deleted] [-utf8] [-legacy-format] <community> <backup_dir>...   (files of a community from a backup; read only, without nest.svc)
  nest.ctl extract-backup -list <backup_dir>...   (the communities in a backup)
  nest.ctl import-board <board>|--all [--overwrite] [--dry-run] [--workers N] [--jobs N (with --all: boards at a time)] [--limit N] [--offset N] [--no-merge] [--commentd-db PATH] [--re-render [target_dir]] [--legacy-format]
  nest.ctl sync-dir <board>|--all [--only-scores | --full | --rebuild] [--prune] [--add-missing] [--dry-run]
  nest.ctl fetch <post> <output_path>      (output under $BBSHOME; relative to it)
  nest.ctl update <post> <title> <content> [editor]
  nest.ctl update-title <post> <title> [editor]
  nest.ctl remove <post> [mod] [reason]     (Lemmy's RemovePost)
  nest.ctl recover <post>                   (puts a deleted or removed post back)
  nest.ctl purge <post>
  nest.ctl communities
  nest.ctl community <community>|#<id>
  nest.ctl import-boards [--dry-run]
  nest.ctl rename-community <old> <new>
  nest.ctl merge-community <from> <to>
  nest.ctl move-community <community> <engine> (move to another engine)
  nest.ctl remove-community <community> [reason] (posts are kept)
  nest.ctl purge-community <community>|#<id>  (removes all posts)
  nest.ctl vote <post> <user> [1|-1|0] [person_id] [ip] [ctime]
  nest.ctl votes <post>
  nest.ctl comments <post> [start [limit]]
  nest.ctl get-comment <comment_id>
  nest.ctl api <method> [json]               (Lemmy API: GetPosts, GetPost, GetComments, GetComment, LockPost)
  nest.ctl comment <post> <author> <content> [ip] [person_id]
  nest.ctl remove-comment <post> <seq> [mod] [reason]
  nest.ctl recover-comment <post> <seq>
  nest.ctl update-comment <post> <seq> <content> [editor]
  nest.ctl history <post> [rev]
  nest.ctl comment-history <post> <seq> [rev]
  nest.ctl crosspost <src_post> <target_post> <operator> [person_id]
  nest.ctl crossposts <post>
  nest.ctl link-crossposts                   (record the imported copies at their sources)
  nest.ctl fix-published [community...]      (date communities from their oldest post)
  nest.ctl origin <post>
  nest.ctl add <community> <author> <title> <content>
  nest.ctl add-file <community> <post_file> <author> <title> [person_id]
  nest.ctl purge-posts <author> [days=7]
  nest.ctl purge-comments <author> [days=7]
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
			// Taken back by its creator (DeleteComment, no reason), or
			// removed by a moderator (RemoveComment, with one).
			status = "DELETED"
			if reason != "" && reason != "自刪" {
				status = "REMOVED"
				content = "[" + reason + "] " + content
			}
		}
		fmt.Fprintf(w, "[%s]\t#%s\t%s\t%s\t%s\t%s\t%s\n", seq, commentID, author, dateStr, ip, status, content)
	}
	w.Flush()
	fmt.Printf("(Showing %d of %d comments)\n", count, total)
}

func printPostHistory(resp string, payload []byte, rev int) error {
	if !strings.HasPrefix(resp, "OK") {
		return fmt.Errorf("History failed: %s", resp)
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
	return nil
}

func printCommentHistory(resp string, payload []byte, rev int) error {
	if !strings.HasPrefix(resp, "OK") {
		return fmt.Errorf("Comment history failed: %s", resp)
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
	return nil
}

// syncDir sends SYNC_DIR for board comm (dryStr "0" or "1", modeStr the
// modes or "-") and prints what nest.svc did.
func syncDir(comm string, dryRun bool, dryStr, modeStr string) error {
	resp, _, err := sendCmd(fmt.Sprintf("SYNC_DIR %s %s %s\n", comm, dryStr, modeStr), nil)
	if err != nil {
		return err
	}
	parts := strings.Fields(resp)
	if len(parts) < 8 || parts[0] != "OK" {
		return fmt.Errorf("%s", strings.TrimSpace(resp))
	}
	if strings.Contains(modeStr, "rebuild") {
		if dryRun {
			fmt.Printf("[+] Dry-run of rebuilding .DIR of '%s' from nest.svc (nothing written):\n", comm)
		} else {
			fmt.Printf("[+] Rebuilt .DIR of '%s' from nest.svc:\n", comm)
		}
		fmt.Printf("    Records before:     %s (%s known by nest.svc, %s dropped)\n", parts[1], parts[2], parts[4])
		if len(parts) >= 10 {
			fmt.Printf("    Legacy posts:       %s imported first, %s records kept as they are\n", parts[8], parts[9])
		}
		fmt.Printf("    Records written:    %s (%s new)\n", parts[3], parts[5])
		fmt.Printf("    Articles rendered:  %s (%s failed)\n", parts[7], parts[6])
		if !dryRun {
			fmt.Println("    The previous .DIR is kept as .DIR.bak-<time>.")
		}
		return nil
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
	fmt.Printf("    Records in .DIR:    %s\n    Known by nest.svc:  %s\n    Records updated:    %s\n", parts[1], parts[2], parts[3])
	if strings.Contains(modeStr, "prune") {
		fmt.Printf("    Records pruned:     %s\n", parts[4])
	}
	if strings.Contains(modeStr, "add-missing") {
		fmt.Printf("    Records added:      %s (%s without article file)\n", parts[5], parts[6])
	}
	if !dryRun && (strings.Contains(modeStr, "prune") || strings.Contains(modeStr, "add-missing")) {
		fmt.Println("    The previous .DIR is kept as .DIR.bak-<time>.")
	}
	return nil
}

// -----------------------------------------------------------------------------
// Legacy Man (精華區) Importer
