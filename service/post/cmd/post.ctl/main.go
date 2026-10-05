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

	"pttbbs/bbs"
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

func sendCmd(header string, payload []byte) (string, []byte, error) {
	sockPath := getSocketPath()
	conn, err := net.Dial("unix", sockPath)
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
	pResp, _, err := sendCmd(fmt.Sprintf("GET_POST_ID %d\n", target.postID), nil)
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
	case "ping":
		resp, _, err := sendCmd("PING\n", nil)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		fmt.Printf("Ping response: %s\n", resp)

	case "add-file":
		if len(args) < 5 {
			log.Fatalf("Usage: post.ctl add-file <community> <post_file> <author> <title> [author_token]")
		}
		var token uint32
		if len(args) >= 6 {
			val, _ := strconv.ParseUint(args[5], 10, 32)
			token = uint32(val)
		}
		titleB := []byte(args[4])
		header := fmt.Sprintf("POST_FILE %s %s %s %d 0 %d %d\n", args[1], args[2], args[3], token, time.Now().Unix(), len(titleB))
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
		header := fmt.Sprintf("POST %s - %s 0 0 %d 0 %d %d\n", args[1], args[2], time.Now().Unix(), len(titleB), len(contentB))
		payload := append(titleB, contentB...)
		resp, _, err := sendCmd(header, payload)
		if err != nil {
			log.Fatalf("add failed: %v", err)
		}
		fmt.Println(resp)

	case "comment":
		if len(args) < 4 {
			log.Fatalf("Usage: post.ctl comment <post> <author> <content> [ip] [author_token]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx+1 {
			log.Fatalf("Usage: post.ctl comment <post> <author> <content> [ip] [author_token]")
		}
		author := args[nextIdx]
		contentB := []byte(args[nextIdx+1])
		ip := "127.0.0.1"
		if len(args) > nextIdx+2 {
			ip = args[nextIdx+2]
		}
		var token uint32
		if len(args) > nextIdx+3 {
			val, _ := strconv.ParseUint(args[nextIdx+3], 10, 32)
			token = uint32(val)
		}
		var legType uint8 = 3 // default 箭頭
		if len(args) > nextIdx+4 {
			val, _ := strconv.Atoi(args[nextIdx+4])
			legType = uint8(val)
		}
		if err := resolveToCommunityFile(&target); err != nil {
			log.Fatalf("Error: %v", err)
		}
		header := fmt.Sprintf("COMMENT %s %s %s %d %s %d %d %d\n", target.community, target.postFile, author, token, ip, time.Now().Unix(), len(contentB), legType)
		resp, _, err := sendCmd(header, contentB)
		if err != nil {
			log.Fatalf("comment failed: %v", err)
		}
		fmt.Println(resp)

	case "vote", "vote-file":
		if len(args) < 3 {
			log.Fatalf("Usage: post.ctl vote <post> <user> [1|-1|0] [author_token]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx {
			log.Fatalf("Usage: post.ctl vote <post> <user> [1|-1|0] [author_token]")
		}
		user := args[nextIdx]
		voteVal := 1
		if len(args) > nextIdx+1 {
			voteVal, _ = strconv.Atoi(args[nextIdx+1])
		}
		var token uint32
		if len(args) > nextIdx+2 {
			v, _ := strconv.ParseUint(args[nextIdx+2], 10, 32)
			token = uint32(v)
		}
		var header string
		if target.isFile {
			header = fmt.Sprintf("VOTE_FILE %s %s %s %d %d\n", target.community, target.postFile, user, token, voteVal)
		} else {
			header = fmt.Sprintf("VOTE %d %s %d %d\n", target.postID, user, token, voteVal)
		}
		resp, _, err := sendCmd(header, nil)
		if err != nil {
			log.Fatalf("vote failed: %v", err)
		}
		fmt.Println(resp)

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
			header = fmt.Sprintf("GET_POST %s %s\n", target.community, target.postFile)
		} else {
			header = fmt.Sprintf("GET_POST_ID %d\n", target.postID)
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
		header := fmt.Sprintf("PURGE_COMMENTS %s %d\n", author, days)
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
		header := fmt.Sprintf("PURGE_POSTS %s %d\n", author, days)
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
		header := fmt.Sprintf("LIST %s %d %d %s\n", comm, count, start, filterMode)
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
			os.Stdout.Write(payload)
		} else {
			fmt.Println(resp)
		}

	case "import-community", "import-board", "migrate-community", "migrate-board":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl import-community <community> [render_target] [--overwrite] [--dry-run] [--workers N] [--limit N] [--offset N] [--no-merge] [--commentd-addr ADDR]")
		}
		comm := args[1]
		renderTarget := "-"
		overwrite := false
		limit := 0
		offset := 0
		noMerge := false
		commentdAddr := "-"
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
			} else if arg == "--commentd-addr" && i+1 < len(args) {
				commentdAddr = args[i+1]
				i++
			} else if strings.HasPrefix(arg, "--commentd-addr=") {
				commentdAddr = strings.TrimPrefix(arg, "--commentd-addr=")
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
		header := fmt.Sprintf("IMPORT_BOARD %s %s %s %d %d %s %s %s %s %d\n", comm, renderTarget, ovStr, limit, offset, nmStr, commentdAddr, legStr, dryStr, workers)

		sockPath := getSocketPath()
		conn, err := net.Dial("unix", sockPath)
		if err != nil {
			log.Fatalf("connect to %s failed: %v\n(Is post.svc running?)", sockPath, err)
		}
		defer conn.Close()

		if _, err := conn.Write([]byte(header)); err != nil {
			log.Fatalf("send command failed: %v", err)
		}

		fmt.Printf("[*] Starting fast Go migration for board '%s'...\n", comm)
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

		reader := bufio.NewReader(conn)
		for {
			line, err := reader.ReadString('\n')
			if err != nil {
				if err == io.EOF {
					fmt.Println()
					log.Fatalf("[!] Connection closed prematurely by post.svc (service crashed or stopped)")
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
			} else if strings.HasPrefix(line, "ERR_DETAIL ") {
				fmt.Printf("\n[!] Error: %s", strings.TrimPrefix(line, "ERR_DETAIL "))
			} else if strings.HasPrefix(line, "OK") {
				fmt.Println()
				parts := strings.Fields(line)
				if len(parts) >= 6 {
					if dryRun {
						fmt.Printf("[+] Dry-run simulation completed in %ss:\n    Posts parsed:       %s\n    Comments parsed:    %s\n    Crossposts found:   %s\n    Errors encountered: %s\n",
							parts[5], parts[1], parts[2], parts[3], parts[4])
					} else {
						fmt.Printf("[+] Migration completed in %ss:\n    Posts imported:     %s\n    Comments imported:  %s\n    Crossposts logged:  %s\n    Errors encountered: %s\n",
							parts[5], parts[1], parts[2], parts[3], parts[4])
					}
				} else {
					fmt.Println(line)
				}
				break
			} else if strings.HasPrefix(line, "ERR") {
				fmt.Println()
				log.Fatalf("[!] Migration failed: %s", line)
			}
		}

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
			header = fmt.Sprintf("UPDATE_POST_FILE %s %s %s 0 0 %d %d\n", target.community, target.postFile, editor, len(titleB), len(contentB))
		} else {
			header = fmt.Sprintf("UPDATE_POST %d %s %d %d\n", target.postID, editor, len(titleB), len(contentB))
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
			header = fmt.Sprintf("UPDATE_POST_TITLE_FILE %s %s %s %d\n", target.community, target.postFile, editor, len(titleB))
		} else {
			header = fmt.Sprintf("UPDATE_POST_TITLE %d %s %d\n", target.postID, editor, len(titleB))
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
			header = fmt.Sprintf("COMMENTS_FILE %s %s %d %d\n", target.community, target.postFile, start, limit)
		} else {
			header = fmt.Sprintf("COMMENTS %d %d %d\n", target.postID, start, limit)
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
		var header string
		if target.isFile {
			header = fmt.Sprintf("UPDATE_COMMENT_FILE %s %s %s %s %d\n", target.community, target.postFile, seq, editor, len(contentB))
		} else {
			header = fmt.Sprintf("UPDATE_COMMENT %d %s %s %d\n", target.postID, seq, editor, len(contentB))
		}
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
			header = fmt.Sprintf("POST_HISTORY_FILE %s %s %d\n", target.community, target.postFile, rev)
		} else {
			header = fmt.Sprintf("POST_HISTORY %d %d\n", target.postID, rev)
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
		var header string
		if target.isFile {
			header = fmt.Sprintf("COMMENT_HISTORY_FILE %s %s %s %d\n", target.community, target.postFile, seq, rev)
		} else {
			header = fmt.Sprintf("COMMENT_HISTORY %d %s %d\n", target.postID, seq, rev)
		}
		resp, payload, err := sendCmd(header, nil)
		if err != nil {
			log.Fatalf("comment-history failed: %v", err)
		}
		printCommentHistory(resp, payload, rev)

	case "crosspost", "share":
		if len(args) < 3 {
			log.Fatalf("Usage: post.ctl crosspost <src_post> <target_post> <operator> [token]\n  <post>: <post_id> or <post_file>@<community>")
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
		cmd := fmt.Sprintf("CROSSPOSTS %s %s\n", target.community, target.postFile)
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
		cmd := fmt.Sprintf("ORIGIN %s %s\n", target.community, target.postFile)
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
			fmt.Printf("Original Author  : %s\n", cols[2])
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

	case "delete-community", "purge-community":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl delete-community <community>")
		}
		comm := args[1]
		cmd := fmt.Sprintf("DELETE_COMMUNITY %s\n", comm)
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			log.Fatalf("delete-community failed: %v", err)
		}
		if !strings.HasPrefix(resp, "OK") {
			log.Fatalf("delete-community failed: %s", resp)
		}
		parts := strings.Fields(resp)
		postsDel := 0
		commentsDel := 0
		if len(parts) >= 2 {
			postsDel, _ = strconv.Atoi(parts[1])
		}
		if len(parts) >= 3 {
			commentsDel, _ = strconv.Atoi(parts[2])
		}
		fmt.Printf("Community '%s' deleted successfully: %d posts and %d comments removed.\n", comm, postsDel, commentsDel)

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

	case "delete", "delete-file", "delete-post", "delete-post-file":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl delete <post> [deleter] [reason]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		nextIdx := 1 + consumed
		deleter := "sysop"
		if len(args) > nextIdx {
			deleter = args[nextIdx]
		}
		reason := ""
		if len(args) > nextIdx+1 {
			reason = strings.Join(args[nextIdx+1:], " ")
		}
		var cmd string
		if target.isFile {
			cmd = fmt.Sprintf("DELETE_POST_FILE %s %s %s %s\n", target.community, target.postFile, deleter, reason)
		} else {
			cmd = fmt.Sprintf("DELETE_POST %d %s %s\n", target.postID, deleter, reason)
		}
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			log.Fatalf("delete failed: %v", err)
		}
		fmt.Print(resp)

	case "undelete", "undelete-file", "undelete-post", "undelete-post-file":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl undelete <post>\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, _, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		var cmd string
		if target.isFile {
			cmd = fmt.Sprintf("UNDELETE_POST_FILE %s %s\n", target.community, target.postFile)
		} else {
			cmd = fmt.Sprintf("UNDELETE_POST %d\n", target.postID)
		}
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			log.Fatalf("undelete failed: %v", err)
		}
		fmt.Print(resp)

	case "delete-comment", "delete-comment-file":
		if len(args) < 3 {
			log.Fatalf("Usage: post.ctl delete-comment <post> <seq> [deleter] [reason]\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx {
			log.Fatalf("Usage: post.ctl delete-comment <post> <seq> [deleter] [reason]")
		}
		seq := args[nextIdx]
		deleter := "sysop"
		if len(args) > nextIdx+1 {
			deleter = args[nextIdx+1]
		}
		reason := ""
		if len(args) > nextIdx+2 {
			reason = strings.Join(args[nextIdx+2:], " ")
		}
		var cmd string
		if target.isFile {
			cmd = fmt.Sprintf("DELETE_COMMENT_FILE %s %s %s %s %s\n", target.community, target.postFile, seq, deleter, reason)
		} else {
			cmd = fmt.Sprintf("DELETE_COMMENT %d %s %s %s\n", target.postID, seq, deleter, reason)
		}
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			log.Fatalf("delete-comment failed: %v", err)
		}
		fmt.Print(resp)

	case "undelete-comment", "undelete-comment-file":
		if len(args) < 3 {
			log.Fatalf("Usage: post.ctl undelete-comment <post> <seq>\n  <post>: <post_id> or <post_file>@<community>")
		}
		target, consumed, err := parsePostArg(args, 1)
		if err != nil {
			log.Fatalf("Error: %v", err)
		}
		nextIdx := 1 + consumed
		if len(args) <= nextIdx {
			log.Fatalf("Usage: post.ctl undelete-comment <post> <seq>")
		}
		seq := args[nextIdx]
		var cmd string
		if target.isFile {
			cmd = fmt.Sprintf("UNDELETE_COMMENT_FILE %s %s %s\n", target.community, target.postFile, seq)
		} else {
			cmd = fmt.Sprintf("UNDELETE_COMMENT %d %s\n", target.postID, seq)
		}
		resp, _, err := sendCmd(cmd, nil)
		if err != nil {
			log.Fatalf("undelete-comment failed: %v", err)
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
  post.ctl list <community> [count [start]] [deleted]
  post.ctl get <post>
  post.ctl render <post> [output] [--legacy-format]
  post.ctl render-community <community> [output_dir] [--legacy-format]
  post.ctl import-community <community> [--overwrite] [--dry-run] [--workers N] [--limit N] [--offset N] [--no-merge] [--commentd-addr ADDR] [--re-render [target_dir]] [--legacy-format]
  post.ctl fetch <post> <output_path>
  post.ctl update <post> <title> <content> [editor]
  post.ctl update-title <post> <title> [editor]
  post.ctl delete <post> [deleter] [reason]
  post.ctl undelete <post>
  post.ctl purge <post>
  post.ctl delete-community <community>
  post.ctl vote <post> <user> [1|-1|0] [token]
  post.ctl comments <post> [start [limit]]
  post.ctl comment <post> <author> <content> [ip] [token]
  post.ctl delete-comment <post> <seq> [deleter] [reason]
  post.ctl undelete-comment <post> <seq>
  post.ctl update-comment <post> <seq> <content> [editor]
  post.ctl history <post> [rev]
  post.ctl comment-history <post> <seq> [rev]
  post.ctl crosspost <src_post> <target_post> <operator> [token]
  post.ctl crossposts <post>
  post.ctl origin <post>
  post.ctl add <community> <author> <title> <content>
  post.ctl add-file <community> <post_file> <author> <title> [token]
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
	fmt.Fprintf(w, "SEQ\tAUTHOR\tDATE\tIP\tSTATUS\tCONTENT\n")
	lines := strings.Split(strings.TrimRight(string(payload), "\n"), "\n")
	for _, l := range lines {
		if l == "" {
			continue
		}
		cols := strings.Split(l, "\t")
		if len(cols) < 7 {
			continue
		}
		seq := cols[0]
		author := cols[1]
		ts, _ := strconv.ParseInt(cols[2], 10, 64)
		dateStr := time.Unix(ts, 0).Format("01/02 15:04")
		ip := cols[3]
		typeVal, _ := strconv.Atoi(cols[4])
		isDel := (typeVal < 0)
		reason := cols[5]
		content := strings.Join(cols[6:], " / ")
		status := "ACTIVE"
		if isDel {
			status = "DELETED"
			if reason != "" {
				content = "[" + reason + "] " + content
			}
		}
		fmt.Fprintf(w, "[%s]\t%s\t%s\t%s\t%s\t%s\n", seq, author, dateStr, ip, status, content)
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



