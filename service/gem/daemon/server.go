// Package daemon serves gem.svc's commands on a UNIX socket (see package
// protocol for the format).
package daemon

import (
	"bufio"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	"pttbbs/gem/legacy"
	"pttbbs/gem/protocol"
	"pttbbs/gem/storage"
)

// Config of a Server.
type Config struct {
	BBSHome    string
	UnixSocket string
	DataDir    string // where BACKUP <name> writes (DataDir/backup/<name>)
	Workers    int    // readers per import
	Limits     func(board string) storage.Limits
}

// Server answers the commands of gem.svc.
type Server struct {
	cfg Config
	st  *storage.Store

	ln       net.Listener
	quit     chan struct{}
	wg       sync.WaitGroup
	connsMu  sync.Mutex
	conns    map[net.Conn]struct{}
	importMu sync.Mutex
	imports  map[string]bool // lower(board)
}

// New makes a Server on st.
func New(cfg Config, st *storage.Store) *Server {
	if cfg.Workers <= 0 {
		cfg.Workers = 8
	}
	return &Server{cfg: cfg, st: st, quit: make(chan struct{}), conns: map[net.Conn]struct{}{}, imports: map[string]bool{}}
}

// IsSocketOccupied tells whether a server answers on socketPath.
func IsSocketOccupied(socketPath string) bool {
	conn, err := net.DialTimeout("unix", socketPath, 100*time.Millisecond)
	if err == nil {
		conn.Close()
		return true
	}
	return false
}

// Start listens and serves in the background.
func (s *Server) Start() error {
	if IsSocketOccupied(s.cfg.UnixSocket) {
		return fmt.Errorf("unix socket %s is already served by another instance", s.cfg.UnixSocket)
	}
	os.Remove(s.cfg.UnixSocket)
	if err := os.MkdirAll(filepath.Dir(s.cfg.UnixSocket), 0755); err != nil {
		return err
	}
	ln, err := net.Listen("unix", s.cfg.UnixSocket)
	if err != nil {
		return err
	}
	_ = os.Chmod(s.cfg.UnixSocket, 0660)
	s.ln = ln
	s.wg.Add(1)
	go s.acceptLoop()
	log.Printf("[gem.svc] listening on %s", s.cfg.UnixSocket)
	// A new site: what util/initbbs made in man/ is not here yet.
	if roots, err := s.st.Roots(); err == nil && len(roots) == 0 {
		log.Printf("[gem.svc] no gem area yet: run gem.ctl import-all once to import man/ (docs/INSTALL)")
	}
	return nil
}

// Stop stops accepting, closes the connections and waits for the commands
// running (an import is cut at its next directory).
func (s *Server) Stop() {
	close(s.quit)
	if s.ln != nil {
		s.ln.Close()
	}
	s.connsMu.Lock()
	for c := range s.conns {
		c.Close()
	}
	s.connsMu.Unlock()
	s.wg.Wait()
}

func (s *Server) acceptLoop() {
	defer s.wg.Done()
	for {
		conn, err := s.ln.Accept()
		if err != nil {
			select {
			case <-s.quit:
				return
			default:
			}
			log.Printf("[gem.svc] accept: %v", err)
			time.Sleep(100 * time.Millisecond)
			continue
		}
		s.connsMu.Lock()
		s.conns[conn] = struct{}{}
		s.connsMu.Unlock()
		s.wg.Add(1)
		go func() {
			defer s.wg.Done()
			defer func() {
				s.connsMu.Lock()
				delete(s.conns, conn)
				s.connsMu.Unlock()
				conn.Close()
			}()
			s.handle(conn)
		}()
	}
}

type request struct {
	args []string
	r    *bufio.Reader
	w    *bufio.Writer
}

func (q *request) ok(format string, args ...any) {
	q.w.WriteString("OK")
	if format != "" {
		q.w.WriteString(" ")
		fmt.Fprintf(q.w, format, args...)
	}
	q.w.WriteString("\n")
}

func (q *request) fail(err error) {
	var se *storage.Error
	if errors.As(err, &se) {
		fmt.Fprintf(q.w, "ERR %s\n", oneLine(se.Error()))
		return
	}
	fmt.Fprintf(q.w, "ERR bbs_gem_internal %s\n", oneLine(err.Error()))
}

func oneLine(s string) string { return strings.ReplaceAll(s, "\n", " ") }

func (q *request) payload(n int) ([]byte, error) {
	if n < 0 || n > protocol.MaxPayload {
		return nil, &storage.Error{Code: "bbs_gem_bad_request", Detail: fmt.Sprintf("payload %d", n)}
	}
	b := make([]byte, n)
	_, err := io.ReadFull(q.r, b)
	return b, err
}

func (q *request) header() (storage.Header, error) {
	var h storage.Header
	_, err := io.ReadFull(q.r, h[:])
	return h, err
}

func badRequest(format string, args ...any) error {
	return &storage.Error{Code: "bbs_gem_bad_request", Detail: fmt.Sprintf(format, args...)}
}

// need returns args[1:] when there are at least n of them.
func (q *request) need(n int, usage string) ([]string, error) {
	if len(q.args)-1 < n {
		return nil, badRequest("usage: %s", usage)
	}
	return q.args[1:], nil
}

func atoi(s string) (int, error) {
	n, err := strconv.Atoi(s)
	if err != nil {
		return 0, badRequest("not a number: %q", s)
	}
	return n, nil
}

// dir reads "<root> <path>"; a bare board name is the root of its area.
func dir(root, path string) (string, []string, error) {
	root = storage.NormalizeRoot(root)
	if !storage.ValidRoot(root) {
		return "", nil, badRequest("bad root %q", root)
	}
	p, err := storage.SplitPath(path)
	return root, p, err
}

// readDir is dir for reading: the trash of a root may be read too.
func readDir(root, path string) (string, []string, error) {
	root = storage.NormalizeRoot(root)
	if !storage.ValidReadRoot(root) {
		return "", nil, badRequest("bad root %q", root)
	}
	p, err := storage.SplitPath(path)
	return root, p, err
}

// readRef is ref for reading: an entry of the trash of a root too.
func readRef(a []string) (storage.Ref, error) {
	board, path, err := readDir(a[0], a[1])
	if err != nil {
		return storage.Ref{}, err
	}
	idx, err := atoi(a[2])
	if err != nil {
		return storage.Ref{}, err
	}
	return storage.Ref{Root: board, Path: path, Index: idx, Name: a[3]}, nil
}

// rootArg reads a root argument.
func rootArg(s string) (string, error) {
	r := storage.NormalizeRoot(s)
	if !storage.ValidRoot(r) {
		return "", badRequest("bad root %q", s)
	}
	return r, nil
}

// ensureTop makes the area of root when something is added at its top and
// it has none yet (as mbbsd made the directory of a mail folder on the
// first paste).
func (s *Server) ensureTop(root string, path []string) error {
	if len(path) == 0 {
		return s.st.EnsureRoot(root)
	}
	return nil
}

// ref reads "<root> <path> <index> <name>" from a.
func ref(a []string) (storage.Ref, error) {
	board, path, err := dir(a[0], a[1])
	if err != nil {
		return storage.Ref{}, err
	}
	idx, err := atoi(a[2])
	if err != nil {
		return storage.Ref{}, err
	}
	return storage.Ref{Root: board, Path: path, Index: idx, Name: a[3]}, nil
}

func (s *Server) handle(conn net.Conn) {
	r := bufio.NewReader(conn)
	w := bufio.NewWriter(conn)
	defer w.Flush()
	line, err := r.ReadString('\n')
	if err != nil {
		return
	}
	args := strings.Fields(strings.TrimRight(line, "\r\n"))
	if len(args) == 0 {
		return
	}
	q := &request{args: args, r: r, w: w}
	if err := s.dispatch(q); err != nil {
		q.fail(err)
	}
}

func (s *Server) dispatch(q *request) error {
	cmd := strings.ToUpper(q.args[0])
	switch cmd {
	case "PING":
		q.w.WriteString("PONG\n")
		return nil

	case "ROOTS":
		// ROOTS -> OK <n>\n<root>\n...
		boards, err := s.st.Roots()
		if err != nil {
			return err
		}
		q.ok("%d", len(boards))
		for _, b := range boards {
			q.w.WriteString(b + "\n")
		}
		return nil

	case "STAT":
		// STAT <root> -> OK <files> <dirs> <links> <missing> <bytes> <depth> <nodes>
		a, err := q.need(1, "STAT <root>")
		if err != nil {
			return err
		}
		root, err := rootArg(a[0])
		if err != nil {
			return err
		}
		t, err := s.st.RootTotals(root)
		if err != nil {
			return err
		}
		q.ok("%d %d %d %d %d %d %d", t.Files, t.Dirs, t.Links, t.Missing, t.Bytes, t.Depth, t.Nodes())
		return nil

	case "QUOTA":
		// QUOTA <root> -> OK <nodes> <dirs> <bytes> <width> <depth> <copy> (0: none)
		a, err := q.need(1, "QUOTA <root>")
		if err != nil {
			return err
		}
		root, err := rootArg(a[0])
		if err != nil {
			return err
		}
		var l storage.Limits
		if s.cfg.Limits != nil {
			l = s.cfg.Limits(root)
		}
		q.ok("%d %d %d %d %d %d", l.Nodes, l.Dirs, l.Bytes, l.Width, l.Depth, l.Copy)
		return nil

	case "LIST":
		// LIST <root> <path> [<offset> [<limit>]]
		//   -> OK <count> <n> <files> <dirs> <links> <missing> <bytes> <depth>\n
		//      then n records from offset (count: entries in the directory).
		a, err := q.need(2, "LIST <root> <path> [<offset> [<limit>]]")
		if err != nil {
			return err
		}
		board, path, err := readDir(a[0], a[1])
		if err != nil {
			return err
		}
		offset, limit := 0, -1
		if len(a) > 2 {
			if offset, err = atoi(a[2]); err != nil {
				return err
			}
		}
		if len(a) > 3 {
			if limit, err = atoi(a[3]); err != nil {
				return err
			}
		}
		var l *storage.Listing
		var infos []storage.TrashInfo
		if root, ok := storage.SplitTrashRoot(board); ok && len(path) == 0 {
			l, infos, err = s.st.Trash(root)
		} else {
			l, err = s.st.List(board, path)
		}
		if err != nil {
			return err
		}
		ents := l.Entries
		offset = max(0, min(offset, len(ents)))
		ents = ents[offset:]
		if limit >= 0 && limit < len(ents) {
			ents = ents[:limit]
		}
		t := l.Totals
		q.ok("%d %d %d %d %d %d %d %d", len(l.Entries), len(ents), t.Files, t.Dirs, t.Links, t.Missing, t.Bytes, t.Depth)
		buf := make([]byte, 0, protocol.RecordSize)
		for i := range ents {
			rec := protocol.FromEntry(&ents[i])
			// The top of a trash: who deleted each entry, and when, in
			// place of its owner and time (see TRASH for the rest).
			if infos != nil {
				ti := &infos[offset+i]
				rec.Header.SetOwner(ti.By)
				rec.Mtime = ti.At
			}
			q.w.Write(rec.Append(buf[:0]))
		}
		return nil

	case "ENTRY":
		// ENTRY <root> <path> <index> <name>
		//   -> OK <index> <target_bytes>\n<record><link target>
		a, err := q.need(4, "ENTRY <root> <path> <index> <name>")
		if err != nil {
			return err
		}
		rf, err := readRef(a)
		if err != nil {
			return err
		}
		e, i, err := s.st.Stat(rf.Root, rf.Path, rf.Index, rf.Name)
		if err != nil {
			return err
		}
		q.ok("%d %d", i, len(e.Target))
		rec := protocol.FromEntry(&e)
		q.w.Write(rec.Append(nil))
		q.w.WriteString(e.Target)
		return nil

	case "GET":
		// GET <root> <path> <index> <name> -> OK <bytes>\n<content>
		a, err := q.need(4, "GET <root> <path> <index> <name>")
		if err != nil {
			return err
		}
		rf, err := readRef(a)
		if err != nil {
			return err
		}
		data, _, err := s.st.ReadFile(rf.Root, rf.Path, rf.Index, rf.Name)
		if err != nil {
			return err
		}
		q.ok("%d", len(data))
		q.w.Write(data)
		return nil

	case "ADD":
		// ADD <file|dir> <root> <path> <pos> <bytes>\n<header><content>
		// (no links, see RESOLVE_LINKS)
		//   -> OK <index>\n<record>. An empty name in the header is made up.
		a, err := q.need(5, "ADD <file|dir> <root> <path> <pos> <bytes>")
		if err != nil {
			return err
		}
		board, path, err := dir(a[1], a[2])
		if err != nil {
			return err
		}
		pos, err := atoi(a[3])
		if err != nil {
			return err
		}
		n, err := atoi(a[4])
		if err != nil {
			return err
		}
		h, err := q.header()
		if err != nil {
			return err
		}
		data, err := q.payload(n)
		if err != nil {
			return err
		}
		if err := s.ensureTop(board, path); err != nil {
			return err
		}
		var e storage.Entry
		var at int
		switch strings.ToLower(a[0]) {
		case "file":
			e, at, err = s.st.AddFile(board, path, pos, h, data)
		case "dir":
			e, at, err = s.st.AddDir(board, path, pos, h)
		default:
			return badRequest("kind %q", a[0])
		}
		if err != nil {
			return err
		}
		q.ok("%d", at)
		rec := protocol.FromEntry(&e)
		q.w.Write(rec.Append(nil))
		return nil

	case "SET_HEADER":
		// SET_HEADER <root> <path> <index> <name>\n<header> -> OK\n<record>
		a, err := q.need(4, "SET_HEADER <root> <path> <index> <name>")
		if err != nil {
			return err
		}
		rf, err := ref(a)
		if err != nil {
			return err
		}
		h, err := q.header()
		if err != nil {
			return err
		}
		e, err := s.st.SetHeader(rf, h)
		if err != nil {
			return err
		}
		q.ok("")
		rec := protocol.FromEntry(&e)
		q.w.Write(rec.Append(nil))
		return nil

	case "PUT":
		// PUT <root> <path> <index> <name> <bytes>\n<content> -> OK
		a, err := q.need(5, "PUT <root> <path> <index> <name> <bytes>")
		if err != nil {
			return err
		}
		rf, err := ref(a)
		if err != nil {
			return err
		}
		n, err := atoi(a[4])
		if err != nil {
			return err
		}
		data, err := q.payload(n)
		if err != nil {
			return err
		}
		if _, err := s.st.PutFile(rf, data); err != nil {
			return err
		}
		q.ok("")
		return nil

	case "REMOVE":
		// REMOVE <root> <path> <index> <name> -> OK
		a, err := q.need(4, "REMOVE <root> <path> <index> <name>")
		if err != nil {
			return err
		}
		rf, err := ref(a)
		if err != nil {
			return err
		}
		if _, err := s.st.Remove(rf); err != nil {
			return err
		}
		q.ok("")
		return nil

	case "REORDER":
		// REORDER <root> <path> <index> <name> <to> -> OK <index>
		a, err := q.need(5, "REORDER <root> <path> <index> <name> <to>")
		if err != nil {
			return err
		}
		rf, err := ref(a)
		if err != nil {
			return err
		}
		to, err := atoi(a[4])
		if err != nil {
			return err
		}
		at, err := s.st.Reorder(rf, to)
		if err != nil {
			return err
		}
		q.ok("%d", at)
		return nil

	case "COPY", "MOVE":
		// COPY|MOVE <root> <path> <index> <name> <to_root> <to_path> <pos> <with_header>\n[<header>]
		//   -> OK <index>\n<record>. COPY of a directory shares it (copy on
		//   write); with_header 0 keeps the header of the source.
		a, err := q.need(8, cmd+" <root> <path> <index> <name> <to_root> <to_path> <pos> <with_header>")
		if err != nil {
			return err
		}
		// Copied out of a trash, not moved (RESTORE moves back).
		rf, err := readRef(a)
		if err != nil {
			return err
		}
		if _, trash := storage.SplitTrashRoot(rf.Root); trash && cmd == "MOVE" {
			return badRequest("MOVE out of %s: RESTORE it", rf.Root)
		}
		dboard, dpath, err := dir(a[4], a[5])
		if err != nil {
			return err
		}
		pos, err := atoi(a[6])
		if err != nil {
			return err
		}
		var hp *storage.Header
		if a[7] == "1" {
			h, err := q.header()
			if err != nil {
				return err
			}
			hp = &h
		}
		if err := s.ensureTop(dboard, dpath); err != nil {
			return err
		}
		var e storage.Entry
		var at int
		if cmd == "COPY" {
			e, at, err = s.st.Copy(rf, dboard, dpath, pos, hp)
		} else {
			e, at, err = s.st.Move(rf, dboard, dpath, pos, hp)
		}
		if err != nil {
			return err
		}
		q.ok("%d", at)
		rec := protocol.FromEntry(&e)
		q.w.Write(rec.Append(nil))
		return nil

	case "CREATE_ROOT":
		a, err := q.need(1, "CREATE_ROOT <root>")
		if err != nil {
			return err
		}
		root, err := rootArg(a[0])
		if err != nil {
			return err
		}
		if err := s.st.CreateRoot(root); err != nil {
			return err
		}
		q.ok("")
		return nil

	case "DELETE_ROOT":
		a, err := q.need(1, "DELETE_ROOT <root>")
		if err != nil {
			return err
		}
		root, err := rootArg(a[0])
		if err != nil {
			return err
		}
		if err := s.st.DeleteRoot(root); err != nil {
			return err
		}
		q.ok("")
		return nil

	case "RENAME_ROOT":
		a, err := q.need(2, "RENAME_ROOT <from> <to>")
		if err != nil {
			return err
		}
		from, err := rootArg(a[0])
		if err != nil {
			return err
		}
		to, err := rootArg(a[1])
		if err != nil {
			return err
		}
		if err := s.st.RenameRoot(from, to); err != nil {
			return err
		}
		q.ok("")
		return nil

	case "IMPORT":
		// IMPORT <root> [replace] [<dir>] -> OK <stats>
		// From its files (legacy.RootDir), or dir (absolute).
		a, err := q.need(1, "IMPORT <root> [replace] [<dir>]")
		if err != nil {
			return err
		}
		root, err := rootArg(a[0])
		if err != nil {
			return err
		}
		opts := legacy.ImportOptions{BBSHome: s.cfg.BBSHome, Root: root, Workers: s.cfg.Workers, Stop: s.quit,
			Logf: func(f string, args ...any) { log.Printf("[gem.svc] import %s: "+f, append([]any{a[0]}, args...)...) }}
		for _, x := range a[1:] {
			switch {
			case x == "replace":
				opts.Replace = true
			case filepath.IsAbs(x):
				opts.Dir = x
			default:
				return badRequest("IMPORT: %q", x)
			}
		}
		key := strings.ToLower(opts.Root)
		s.importMu.Lock()
		if s.imports[key] {
			s.importMu.Unlock()
			return &storage.Error{Code: "bbs_gem_import_running", Detail: opts.Root}
		}
		s.imports[key] = true
		s.importMu.Unlock()
		defer func() {
			s.importMu.Lock()
			delete(s.imports, key)
			s.importMu.Unlock()
		}()
		start := time.Now()
		stats, err := legacy.ImportRoot(s.st, opts)
		if err != nil {
			return err
		}
		log.Printf("[gem.svc] imported %s in %v: %v", opts.Root, time.Since(start), stats)
		q.ok("%d %s", time.Since(start).Milliseconds(), stats)
		return nil

	case "EXPORT":
		// EXPORT <root> <dir> -> OK <stats>; dir absolute, new or empty.
		a, err := q.need(2, "EXPORT <root> <dir>")
		if err != nil {
			return err
		}
		root, err := rootArg(a[0])
		if err != nil {
			return err
		}
		if !filepath.IsAbs(a[1]) {
			return badRequest("EXPORT: %q is not absolute", a[1])
		}
		stats, err := legacy.ExportRoot(s.st, root, a[1])
		if err != nil {
			return err
		}
		q.ok("%s", stats)
		return nil

	case "VERIFY":
		// VERIFY [fix] -> OK <ok> <listings> <blobs> <entries> <stored_bytes> <pending> <n>\n<problem>...
		fix := len(q.args) > 1 && q.args[1] == "fix"
		if fix {
			s.importMu.Lock()
			running := len(s.imports)
			s.importMu.Unlock()
			// The listings of an import in progress are not reachable
			// yet: fixing would drop them.
			if running > 0 {
				return &storage.Error{Code: "bbs_gem_import_running", Detail: "VERIFY fix waits for the imports"}
			}
		}
		rep, err := s.st.Verify(fix)
		if err != nil {
			return err
		}
		okv := 0
		if rep.OK() {
			okv = 1
		}
		q.ok("%d %d %d %d %d %d %d", okv, rep.Listings, rep.Blobs, rep.Entries, rep.StoredBytes, rep.Pending, len(rep.Problems))
		for _, p := range rep.Problems {
			q.w.WriteString(oneLine(p) + "\n")
		}
		return nil

	case "DELETE":
		// DELETE <root> <path> <index> <name> <by> -> OK <name in the trash>
		// Moves the entry to the trash of its area (storage.Delete).
		a, err := q.need(5, "DELETE <root> <path> <index> <name> <by>")
		if err != nil {
			return err
		}
		rf, err := ref(a)
		if err != nil {
			return err
		}
		name, err := s.st.Delete(rf, a[4])
		if err != nil {
			return err
		}
		q.ok("%s", name)
		return nil

	case "TRASH":
		// TRASH <root> -> OK <n>\n then n lines:
		//   <name> <kind> <deleted at> <by> <index> <name it had> <path>\t<title>
		// (path "." for the top; the title in the bytes of the BBS).
		a, err := q.need(1, "TRASH <root>")
		if err != nil {
			return err
		}
		root, err := rootArg(a[0])
		if err != nil {
			return err
		}
		l, infos, err := s.st.Trash(root)
		if err != nil {
			return err
		}
		q.ok("%d", len(l.Entries))
		for i := range l.Entries {
			e, ti := &l.Entries[i], &infos[i]
			by := ti.By
			if by == "" {
				by = "-"
			}
			fmt.Fprintf(q.w, "%s %s %d %s %d %s %s\t%s\n", e.Name(), e.Kind, ti.At, by, ti.Index,
				ti.Name, protocol.Path(ti.Path), oneLine(string(e.Header.Title())))
		}
		return nil

	case "TRASH_ENTRY":
		// TRASH_ENTRY <root> <name> -> OK <index> <deleted at> <by> <name it had>\n<path>\n
		a, err := q.need(2, "TRASH_ENTRY <root> <name>")
		if err != nil {
			return err
		}
		root, err := rootArg(a[0])
		if err != nil {
			return err
		}
		_, ti, err := s.st.TrashEntry(root, a[1])
		if err != nil {
			return err
		}
		by := ti.By
		if by == "" {
			by = "-"
		}
		q.ok("%d %d %s %s", ti.Index, ti.At, by, ti.Name)
		q.w.WriteString(protocol.Path(ti.Path) + "\n")
		return nil

	case "RESTORE":
		// RESTORE <root> <name in the trash> -> OK <index> <name>\n<path>\n
		// Back where it was (storage.Restore).
		a, err := q.need(2, "RESTORE <root> <name>")
		if err != nil {
			return err
		}
		root, err := rootArg(a[0])
		if err != nil {
			return err
		}
		r, err := s.st.Restore(root, a[1])
		if err != nil {
			return err
		}
		q.ok("%d %s", r.Index, r.Name)
		q.w.WriteString(protocol.Path(r.Path) + "\n")
		return nil

	case "EXPIRE":
		// EXPIRE <root> <path> <days> -> OK <n>: removes for good the
		// files of the directory older than days days (storage.Expire).
		a, err := q.need(3, "EXPIRE <root> <path> <days>")
		if err != nil {
			return err
		}
		board, path, err := dir(a[0], a[1])
		if err != nil {
			return err
		}
		days, err := atoi(a[2])
		if err != nil || days < 0 {
			return badRequest("EXPIRE: days %q", a[2])
		}
		n, err := s.st.Expire(board, path, time.Now().Add(-time.Duration(days)*24*time.Hour).Unix())
		if err != nil {
			return err
		}
		if n > 0 {
			log.Printf("[gem.svc] expired %d files of %s/%s older than %d days", n, board, protocol.Path(path), days)
		}
		q.ok("%d", n)
		return nil

	case "PURGE":
		// PURGE <days> -> OK <n>: removes for good what every trash has
		// had for more than days days. PURGE <root> <name>: one entry.
		a, err := q.need(1, "PURGE <days> | PURGE <root> <name>")
		if err != nil {
			return err
		}
		if len(a) >= 2 {
			root, err := rootArg(a[0])
			if err != nil {
				return err
			}
			if err := s.st.PurgeEntry(root, a[1]); err != nil {
				return err
			}
			q.ok("1")
			return nil
		}
		days, err := atoi(a[0])
		if err != nil || days < 0 {
			return badRequest("PURGE: days %q", a[0])
		}
		n, err := s.st.Purge(time.Now().Add(-time.Duration(days) * 24 * time.Hour).Unix())
		if err != nil {
			return err
		}
		log.Printf("[gem.svc] purged %d deleted entries older than %d days", n, days)
		q.ok("%d", n)
		return nil

	case "RESOLVE_LINKS":
		// RESOLVE_LINKS [<old BBSHOME>...]
		//   -> OK <dirs> <files> <indexes> <broken> <loops> <n>\n<problem>...
		// Follows absolute targets under BBSHOME or an old BBSHOME
		// (default legacy.DefaultOldHomes).
		olds := legacy.DefaultOldHomes
		if len(q.args) > 1 {
			olds = q.args[1:]
			for _, o := range olds {
				if !filepath.IsAbs(o) {
					return badRequest("RESOLVE_LINKS: %q is not absolute", o)
				}
			}
		}
		s.importMu.Lock()
		running := len(s.imports)
		s.importMu.Unlock()
		// Links to an area being imported would point nowhere yet.
		if running > 0 {
			return &storage.Error{Code: "bbs_gem_import_running", Detail: "RESOLVE_LINKS waits for the imports"}
		}
		var problems []string
		start := time.Now()
		stats, err := legacy.ResolveLinks(s.st, s.cfg.BBSHome, olds, func(f string, args ...any) {
			p := fmt.Sprintf(f, args...)
			log.Printf("[gem.svc] resolve links: %s", p)
			problems = append(problems, p)
		})
		if err != nil {
			return err
		}
		log.Printf("[gem.svc] resolved links in %v: %+v", time.Since(start), stats)
		q.ok("%d %d %d %d %d %d", stats.Dirs, stats.Files, stats.Indexes, stats.Broken, stats.Loops, len(problems))
		for _, p := range problems {
			q.w.WriteString(oneLine(p) + "\n")
		}
		return nil

	case "INDEX":
		// INDEX <root> -> OK <bytes>\n<text>: the table of contents of the
		// area, as util/mandex wrote it to .index.
		a, err := q.need(1, "INDEX <root>")
		if err != nil {
			return err
		}
		root := storage.NormalizeRoot(a[0])
		if !storage.ValidReadRoot(root) {
			return badRequest("bad root %q", a[0])
		}
		if err != nil {
			return err
		}
		text, err := legacy.Index(s.st, root)
		if err != nil {
			return err
		}
		q.ok("%d", len(text))
		q.w.Write(text)
		return nil

	case "METRICS":
		// METRICS -> OK <bytes>\n<text>: Pebble's metrics (levels,
		// compactions, write amplification, caches).
		text := s.st.Metrics()
		q.ok("%d", len(text))
		q.w.WriteString(text)
		return nil

	case "PENDING":
		// PENDING -> OK <listings waiting to be freed>
		n, err := s.st.Pending()
		if err != nil {
			return err
		}
		q.ok("%d", n)
		return nil

	case "BACKUP":
		// BACKUP [<name> | <absolute path>] -> OK <dir>
		// A Pebble checkpoint: DataDir/backup/<name> (default: the time).
		dest := time.Now().Format("20060102-150405")
		if len(q.args) > 1 {
			dest = q.args[1]
		}
		if !filepath.IsAbs(dest) {
			if strings.ContainsAny(dest, "/\\") || dest == "." || dest == ".." {
				return badRequest("BACKUP: %q", dest)
			}
			dest = filepath.Join(s.cfg.DataDir, "backup", dest)
		}
		if err := os.MkdirAll(filepath.Dir(dest), 0755); err != nil {
			return err
		}
		if err := s.st.Checkpoint(dest); err != nil {
			return err
		}
		q.ok("%s", dest)
		return nil
	}
	return badRequest("unknown command %s", cmd)
}
