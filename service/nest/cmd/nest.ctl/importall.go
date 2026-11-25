package main

import (
	"bufio"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
)

// Layout of boardheader_t (include/pttstruct.h) that import-board --all
// reads: sizeof(boardheader_t), brdname[BRDNAME_SIZE] and brdattr.
const (
	brdRecordSize = 256
	brdNameLen    = 13
	brdAttrOff    = 104
	brdSymbolic   = 0x00008000 // BRD_SYMBOLIC: a link to another board
)

// boardsToImport returns the boards in the .BRD at brdPath that have posts
// of their own to import, in .BRD order: not empty (deleted) slots, not
// links to other boards (BRD_SYMBOLIC), and with a .DIR under
// bbsHome/boards (group boards have none).
func boardsToImport(bbsHome, brdPath string) (boards, skipped []string, err error) {
	data, err := os.ReadFile(brdPath)
	if err != nil {
		return nil, nil, err
	}
	for off := 0; off+brdRecordSize <= len(data); off += brdRecordSize {
		rec := data[off : off+brdRecordSize]
		name := string(rec[:brdNameLen])
		if i := strings.IndexByte(name, 0); i >= 0 {
			name = name[:i]
		}
		if name == "" || binary.LittleEndian.Uint32(rec[brdAttrOff:])&brdSymbolic != 0 {
			continue
		}
		if !hasDir(bbsHome, name) {
			skipped = append(skipped, name)
			continue
		}
		boards = append(boards, name)
	}
	return boards, skipped, nil
}

// hasDir tells whether board has a .DIR (setbpath: boards/<c>/<board>).
func hasDir(bbsHome, board string) bool {
	for _, dir := range []string{
		filepath.Join(bbsHome, "boards", board[:1], board),
		filepath.Join(bbsHome, "boards", board),
	} {
		if fi, err := os.Stat(filepath.Join(dir, ".DIR")); err == nil && fi.Mode().IsRegular() {
			return true
		}
	}
	return false
}

// importBoardResult is the reply of IMPORT_BOARD.
type importBoardResult struct {
	posts, comments, crossposts, errors string
	seconds                             string
	deletedImported, deletedMarked      string
	details                             []string // ERR_DETAIL lines, when not shown as they came
}

// runImportBoard sends one IMPORT_BOARD header to nest.svc, shows its
// progress (prefixed by prefix) on out and returns its result. With out nil
// (several boards at once) the progress is not shown, and the errors of
// single posts (ERR_DETAIL) are returned in the result instead.
func runImportBoard(header, prefix string, out io.Writer) (*importBoardResult, error) {
	sockPath := getSocketPath()
	conn, err := net.Dial("unix", sockPath)
	if err != nil {
		return nil, fmt.Errorf("connect to %s failed: %v (is nest.svc running?)", sockPath, err)
	}
	defer conn.Close()
	if _, err := conn.Write([]byte(header)); err != nil {
		return nil, fmt.Errorf("send command failed: %v", err)
	}
	newline := func() {
		if out != nil {
			fmt.Fprintln(out)
		}
	}
	var details []string
	reader := bufio.NewReader(conn)
	for {
		line, err := reader.ReadString('\n')
		if err != nil {
			newline()
			if err == io.EOF {
				return nil, fmt.Errorf("connection closed prematurely by nest.svc (service crashed or stopped)")
			}
			return nil, fmt.Errorf("read error: %v", err)
		}
		line = strings.TrimRight(line, "\r\n")
		switch {
		case strings.HasPrefix(line, "PROGRESS "):
			parts := strings.Fields(line)
			if out != nil && len(parts) >= 3 {
				cur, _ := strconv.Atoi(parts[1])
				total, _ := strconv.Atoi(parts[2])
				pct := 0
				if total > 0 {
					pct = cur * 100 / total
				}
				fmt.Fprintf(out, "\r%s%d/%d (%d%%)", prefix, cur, total, pct)
			}
		case strings.HasPrefix(line, "ERR_DETAIL "):
			detail := strings.TrimPrefix(line, "ERR_DETAIL ")
			if out != nil {
				fmt.Fprintf(out, "\n[!] Error: %s", detail)
			} else {
				details = append(details, detail)
			}
		case strings.HasPrefix(line, "OK"):
			newline()
			parts := strings.Fields(line)
			if len(parts) < 6 {
				return nil, fmt.Errorf("unexpected reply: %s", line)
			}
			r := &importBoardResult{posts: parts[1], comments: parts[2], crossposts: parts[3], errors: parts[4], seconds: parts[5], details: details}
			if len(parts) >= 8 {
				r.deletedImported, r.deletedMarked = parts[6], parts[7]
			}
			return r, nil
		case strings.HasPrefix(line, "ERR"):
			newline()
			return nil, fmt.Errorf("%s", line)
		}
	}
}

// printImportBoardResult shows the result of importing one board.
func printImportBoardResult(w io.Writer, r *importBoardResult, dryRun bool) {
	for _, d := range r.details {
		fmt.Fprintf(w, "[!] Error: %s\n", d)
	}
	if dryRun {
		fmt.Fprintf(w, "[+] Dry-run simulation completed in %ss:\n    Posts parsed:       %s\n    Comments parsed:    %s\n    Crossposts found:   %s\n    Errors encountered: %s\n",
			r.seconds, r.posts, r.comments, r.crossposts, r.errors)
	} else {
		fmt.Fprintf(w, "[+] Migration completed in %ss:\n    Posts imported:     %s\n    Comments imported:  %s\n    Crossposts logged:  %s\n    Errors encountered: %s\n",
			r.seconds, r.posts, r.comments, r.crossposts, r.errors)
	}
	if r.deletedImported != "" {
		fmt.Fprintf(w, "    Deleted posts:      %s imported, %s marked deleted\n", r.deletedImported, r.deletedMarked)
	}
}

// importBoards imports boards with header(board), jobs boards at a time
// (nest.svc imports different boards at once; the boards of one shard
// only take turns to commit), and returns the boards that failed. With
// one job the progress of each board is shown as it goes; with more, the
// result of each board is shown when it is done.
func importBoards(boards []string, jobs int, header func(string) string, dryRun bool) []string {
	var failed []string
	if jobs <= 1 {
		for i, board := range boards {
			prefix := fmt.Sprintf("[%d/%d] %s: ", i+1, len(boards), board)
			fmt.Print(prefix)
			r, err := runImportBoard(header(board), prefix, os.Stdout)
			if err != nil {
				fmt.Printf("[!] %s: %v\n", board, err)
				failed = append(failed, board)
				continue
			}
			printImportBoardResult(os.Stdout, r, dryRun)
		}
		return failed
	}

	var mu sync.Mutex // guards done, failed and stdout
	done := 0
	next := make(chan string)
	var wg sync.WaitGroup
	for j := 0; j < jobs; j++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for board := range next {
				r, err := runImportBoard(header(board), "", nil)
				mu.Lock()
				done++
				if err != nil {
					fmt.Printf("[%d/%d] %s: [!] %v\n", done, len(boards), board, err)
					failed = append(failed, board)
				} else {
					fmt.Printf("[%d/%d] %s:\n", done, len(boards), board)
					printImportBoardResult(os.Stdout, r, dryRun)
				}
				mu.Unlock()
			}
		}()
	}
	for _, board := range boards {
		next <- board
	}
	close(next)
	wg.Wait()
	return failed
}
