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
	flagSocket = flag.String("socket", "", "Path to post.svc UNIX socket")
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
		if len(args) < 5 {
			log.Fatalf("Usage: post.ctl comment <community> <post_file> <author> <content> [ip] [author_token]")
		}
		ip := "127.0.0.1"
		if len(args) >= 6 {
			ip = args[5]
		}
		var token uint32
		if len(args) >= 7 {
			val, _ := strconv.ParseUint(args[6], 10, 32)
			token = uint32(val)
		}
		contentB := []byte(args[4])
		header := fmt.Sprintf("COMMENT %s %s %s %d %s %d %d\n", args[1], args[2], args[3], token, ip, time.Now().Unix(), len(contentB))
		resp, _, err := sendCmd(header, contentB)
		if err != nil {
			log.Fatalf("comment failed: %v", err)
		}
		fmt.Println(resp)

	case "vote":
		if len(args) < 4 {
			log.Fatalf("Usage: post.ctl vote <community> <post_file> <user> [1|-1|0]")
		}
		comm := args[1]
		file := args[2]
		user := args[3]
		voteVal := 1
		if len(args) >= 5 {
			voteVal, _ = strconv.Atoi(args[4])
		}
		header := fmt.Sprintf("VOTE %s %s %s 0 %d\n", comm, file, user, voteVal)
		resp, _, err := sendCmd(header, nil)
		if err != nil {
			log.Fatalf("Vote failed: %v", err)
		}
		fmt.Println(resp)

	case "get":
		if len(args) < 3 {
			log.Fatalf("Usage: post.ctl get <community> <post_file>")
		}
		header := fmt.Sprintf("GET_POST %s %s\n", args[1], args[2])
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
			log.Fatalf("Purge failed: %v", err)
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
			log.Fatalf("Purge failed: %v", err)
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
		header := fmt.Sprintf("LIST %s %d %d\n", comm, count, start)
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
			scoreStr := fmt.Sprintf("%+d", score)
			if score == 0 {
				scoreStr = "0"
			}
			numComments := cols[7]
			title := cols[9]
			fmt.Fprintf(w, "%s\t%s\t%s\t%s\t%s\t%s\t%s\n", id, postFile, author, dateStr, scoreStr, numComments, title)
		}
		w.Flush()
		if totalCount > 0 {
			fmt.Printf("(Showing %d of %d posts in %s, start=%d)\n", returnedCount, totalCount, comm, start)
		}

	case "render":
		if len(args) < 2 {
			log.Fatalf("Usage: post.ctl render <post-id> [output]")
		}
		postID := args[1]
		outPath := "-"
		if len(args) >= 3 {
			outPath = args[2]
			if outPath != "-" && !filepath.IsAbs(outPath) {
				if abs, err := filepath.Abs(outPath); err == nil {
					outPath = abs
				}
			}
		}
		header := fmt.Sprintf("RENDER %s %s\n", postID, outPath)
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

	case "render-file":
		if len(args) < 3 {
			log.Fatalf("Usage: post.ctl render-file <community> <post_file> [output]")
		}
		comm := args[1]
		postFile := args[2]
		outPath := "-"
		if len(args) >= 4 {
			outPath = args[3]
			if outPath != "-" && !filepath.IsAbs(outPath) {
				if abs, err := filepath.Abs(outPath); err == nil {
					outPath = abs
				}
			}
		}
		header := fmt.Sprintf("RENDER_FILE %s %s %s\n", comm, postFile, outPath)
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

	default:
		printUsage()
	}
}

func printUsage() {
	fmt.Printf(`post.ctl - CLI administration tool for post.svc

Usage:
  post.ctl ping
  post.ctl list <community> [count [start]]
  post.ctl render <post-id> [output]
  post.ctl render-file <community> <post_file> [output]
  post.ctl add-file <community> <post_file> <author> <title> [author_token]
  post.ctl add <community> <author> <title> <content>
  post.ctl comment <community> <post_file> <author> <content> [ip] [author_token]
  post.ctl vote <community> <post_file> <user> [1|-1|0]
  post.ctl get <community> <post_file>
  post.ctl purge-comments <author> [days=7]
  post.ctl purge-posts <author> [days=7]
`)
}
