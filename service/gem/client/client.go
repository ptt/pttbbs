// Package client talks to gem.svc (gem.ctl, tests).
package client

import (
	"bufio"
	"fmt"
	"io"
	"net"
	"strconv"
	"strings"
	"time"

	"pttbbs/gem/protocol"
	"pttbbs/gem/storage"
)

// Client sends commands to the gem.svc at Socket.
type Client struct {
	Socket  string
	Timeout time.Duration // 0: none (imports take long)
}

// Error is an ERR reply.
type Error struct{ Line string }

func (e *Error) Error() string { return e.Line }

// Code is the error code of the reply.
func (e *Error) Code() string {
	code, _, _ := strings.Cut(e.Line, " ")
	return code
}

// Reply is an OK reply: its words and the connection to read its payload.
type Reply struct {
	Words []string
	R     *bufio.Reader
	conn  net.Conn
}

// Close closes the connection of the reply.
func (r *Reply) Close() { r.conn.Close() }

// Int is word i as a number.
func (r *Reply) Int(i int) int64 {
	if i >= len(r.Words) {
		return 0
	}
	n, _ := strconv.ParseInt(r.Words[i], 10, 64)
	return n
}

// Do sends the command line and payload; the caller closes the reply.
func (c *Client) Do(line string, payload ...[]byte) (*Reply, error) {
	conn, err := net.Dial("unix", c.Socket)
	if err != nil {
		return nil, err
	}
	if c.Timeout > 0 {
		conn.SetDeadline(time.Now().Add(c.Timeout))
	}
	w := bufio.NewWriter(conn)
	w.WriteString(line + "\n")
	for _, p := range payload {
		w.Write(p)
	}
	if err := w.Flush(); err != nil {
		conn.Close()
		return nil, err
	}
	r := bufio.NewReader(conn)
	resp, err := r.ReadString('\n')
	if err != nil {
		conn.Close()
		return nil, fmt.Errorf("%s: %v", strings.Fields(line)[0], err)
	}
	resp = strings.TrimRight(resp, "\r\n")
	if rest, ok := strings.CutPrefix(resp, "ERR "); ok {
		conn.Close()
		return nil, &Error{Line: rest}
	}
	if resp == "PONG" {
		return &Reply{Words: []string{"PONG"}, R: r, conn: conn}, nil
	}
	if resp != "OK" && !strings.HasPrefix(resp, "OK ") {
		conn.Close()
		return nil, fmt.Errorf("bad reply %q", resp)
	}
	return &Reply{Words: strings.Fields(resp)[1:], R: r, conn: conn}, nil
}

// Simple sends a command without payload and returns the words of the reply.
func (c *Client) Simple(line string, payload ...[]byte) ([]string, error) {
	r, err := c.Do(line, payload...)
	if err != nil {
		return nil, err
	}
	defer r.Close()
	return r.Words, nil
}

// Ping checks that gem.svc answers.
func (c *Client) Ping() error {
	_, err := c.Simple("PING")
	return err
}

// Listing is a LIST reply.
type Listing struct {
	Count   int // entries in the directory
	Totals  storage.Totals
	Records []protocol.Record
}

// List lists directory path of board from offset (limit < 0: all).
func (c *Client) List(board string, path []string, offset, limit int) (*Listing, error) {
	r, err := c.Do(fmt.Sprintf("LIST %s %s %d %d", board, protocol.Path(path), offset, limit))
	if err != nil {
		return nil, err
	}
	defer r.Close()
	l := &Listing{Count: int(r.Int(0)), Totals: storage.Totals{
		Files: uint64(r.Int(2)), Dirs: uint64(r.Int(3)), Links: uint64(r.Int(4)),
		Missing: uint64(r.Int(5)), Bytes: uint64(r.Int(6)), Depth: uint64(r.Int(7)),
	}}
	buf := make([]byte, protocol.RecordSize)
	for i := int64(0); i < r.Int(1); i++ {
		if _, err := io.ReadFull(r.R, buf); err != nil {
			return nil, err
		}
		l.Records = append(l.Records, protocol.Decode(buf))
	}
	return l, nil
}

// Get returns the content of a file.
func (c *Client) Get(board string, path []string, index int, name string) ([]byte, error) {
	r, err := c.Do(fmt.Sprintf("GET %s %s %d %s", board, protocol.Path(path), index, name))
	if err != nil {
		return nil, err
	}
	defer r.Close()
	data := make([]byte, r.Int(0))
	_, err = io.ReadFull(r.R, data)
	return data, err
}

// Entry returns an entry, its index and (links) its target.
func (c *Client) Entry(board string, path []string, index int, name string) (protocol.Record, int, string, error) {
	r, err := c.Do(fmt.Sprintf("ENTRY %s %s %d %s", board, protocol.Path(path), index, name))
	if err != nil {
		return protocol.Record{}, -1, "", err
	}
	defer r.Close()
	buf := make([]byte, protocol.RecordSize+int(r.Int(1)))
	if _, err := io.ReadFull(r.R, buf); err != nil {
		return protocol.Record{}, -1, "", err
	}
	return protocol.Decode(buf), int(r.Int(0)), string(buf[protocol.RecordSize:]), nil
}

func readRecord(r *Reply) (protocol.Record, error) {
	buf := make([]byte, protocol.RecordSize)
	if _, err := io.ReadFull(r.R, buf); err != nil {
		return protocol.Record{}, err
	}
	return protocol.Decode(buf), nil
}

// Add adds an entry of kind (file, dir, link) at pos; data is the content
// or the link target.
func (c *Client) Add(kind, board string, path []string, pos int, h storage.Header, data []byte) (protocol.Record, int, error) {
	r, err := c.Do(fmt.Sprintf("ADD %s %s %s %d %d", kind, board, protocol.Path(path), pos, len(data)), h[:], data)
	if err != nil {
		return protocol.Record{}, -1, err
	}
	defer r.Close()
	rec, err := readRecord(r)
	return rec, int(r.Int(0)), err
}

// CopyOrMove sends COPY or MOVE (cmd); h nil keeps the source's header.
func (c *Client) CopyOrMove(cmd string, src storage.Ref, board string, path []string, pos int, h *storage.Header) (protocol.Record, int, error) {
	with := 0
	var payload [][]byte
	if h != nil {
		with = 1
		payload = append(payload, h[:])
	}
	r, err := c.Do(fmt.Sprintf("%s %s %s %d %s %s %s %d %d", cmd, src.Root, protocol.Path(src.Path), src.Index, src.Name,
		board, protocol.Path(path), pos, with), payload...)
	if err != nil {
		return protocol.Record{}, -1, err
	}
	defer r.Close()
	rec, err := readRecord(r)
	return rec, int(r.Int(0)), err
}

// Lines reads the n lines that follow a reply.
func (r *Reply) Lines(n int) ([]string, error) {
	var out []string
	for i := 0; i < n; i++ {
		l, err := r.R.ReadString('\n')
		if err != nil {
			return out, err
		}
		out = append(out, strings.TrimRight(l, "\n"))
	}
	return out, nil
}
