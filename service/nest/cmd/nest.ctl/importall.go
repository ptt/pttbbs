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
}

// runImportBoard sends one IMPORT_BOARD header to nest.svc, shows its
// progress (prefixed by prefix) and returns its result.
func runImportBoard(header, prefix string) (*importBoardResult, error) {
	sockPath := getSocketPath()
	conn, err := net.Dial("unix", sockPath)
	if err != nil {
		return nil, fmt.Errorf("connect to %s failed: %v (is nest.svc running?)", sockPath, err)
	}
	defer conn.Close()
	if _, err := conn.Write([]byte(header)); err != nil {
		return nil, fmt.Errorf("send command failed: %v", err)
	}
	reader := bufio.NewReader(conn)
	for {
		line, err := reader.ReadString('\n')
		if err != nil {
			fmt.Println()
			if err == io.EOF {
				return nil, fmt.Errorf("connection closed prematurely by nest.svc (service crashed or stopped)")
			}
			return nil, fmt.Errorf("read error: %v", err)
		}
		line = strings.TrimRight(line, "\r\n")
		switch {
		case strings.HasPrefix(line, "PROGRESS "):
			parts := strings.Fields(line)
			if len(parts) >= 3 {
				cur, _ := strconv.Atoi(parts[1])
				total, _ := strconv.Atoi(parts[2])
				pct := 0
				if total > 0 {
					pct = cur * 100 / total
				}
				fmt.Printf("\r%s%d/%d (%d%%)", prefix, cur, total, pct)
			}
		case strings.HasPrefix(line, "ERR_DETAIL "):
			fmt.Printf("\n[!] Error: %s", strings.TrimPrefix(line, "ERR_DETAIL "))
		case strings.HasPrefix(line, "OK"):
			fmt.Println()
			parts := strings.Fields(line)
			if len(parts) < 6 {
				return nil, fmt.Errorf("unexpected reply: %s", line)
			}
			r := &importBoardResult{posts: parts[1], comments: parts[2], crossposts: parts[3], errors: parts[4], seconds: parts[5]}
			if len(parts) >= 8 {
				r.deletedImported, r.deletedMarked = parts[6], parts[7]
			}
			return r, nil
		case strings.HasPrefix(line, "ERR"):
			fmt.Println()
			return nil, fmt.Errorf("%s", line)
		}
	}
}

// printImportBoardResult shows the result of importing one board.
func printImportBoardResult(r *importBoardResult, dryRun bool) {
	if dryRun {
		fmt.Printf("[+] Dry-run simulation completed in %ss:\n    Posts parsed:       %s\n    Comments parsed:    %s\n    Crossposts found:   %s\n    Errors encountered: %s\n",
			r.seconds, r.posts, r.comments, r.crossposts, r.errors)
	} else {
		fmt.Printf("[+] Migration completed in %ss:\n    Posts imported:     %s\n    Comments imported:  %s\n    Crossposts logged:  %s\n    Errors encountered: %s\n",
			r.seconds, r.posts, r.comments, r.crossposts, r.errors)
	}
	if r.deletedImported != "" {
		fmt.Printf("    Deleted posts:      %s imported, %s marked deleted\n", r.deletedImported, r.deletedMarked)
	}
}
