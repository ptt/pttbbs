// Package daemon is mail.svc: it delivers mails from the Internet to the
// mailboxes of BBS users, sends mails to the Internet, and packs homes.
//
// Two UNIX sockets, so the MTA side can deliver mails in without being
// able to send mails out:
//
// mail.svc.in.sock, used by util/bbsmail (run by the MTA):
//
//	DELIVER <receiver>\n
//	<the mail, as the MTA gives it, until EOF>
//
//	→ OK\n | REJECT <why>\n | ERR <why>\n
//
// REJECT means the mail is not delivered and should not be tried again
// (e.g. no such user); ERR means it may be tried again later.
//
// mail.svc.out.sock, used by mbbsd:
//
//	SEND\n
//	Rcpt: <address>\n
//	From: <BBS user id>\n
//	Subject: <title, in the memory encoding>\n
//	Length: <bytes>\n
//	\n
//	<content, in the storage encoding>
//
//	ZIPHOME\n
//	User: <BBS user id>\n
//	Rcpt: <address>\n
//	\n
//
//	→ OK\n | ERR <why>\n
//
// A mail is copied into the spool before OK, and sent (and sent again if
// needed) later. A home is packed later; if it can't be mailed, the user
// is told in a BBS mail.
package daemon

import (
	"bufio"
	"fmt"
	"io"
	"log"
	"net"
	"net/textproto"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"time"
)

const (
	// Larger mails are not taken, in or out.
	MaxMailSize = 32 << 20

	maxConns    = 64
	inTimeout   = 60 * time.Second
	outTimeout  = 30 * time.Second
	sysopOwner  = "SYSOP"
	idLen       = 12 // IDLEN
	ttLen       = 64 // TTLEN
	ownerLen    = idLen + 1
	mailLogPath = "etc/mailog"
)

// BBS is what the service needs from libcmbbs, so tests can fake it.
type BBS interface {
	// LoadMailUser finds userid (case insensitive): the uid (0 if there is
	// no such user), the userid as registered, and if the user rejects
	// mails from outside.
	LoadMailUser(userid string) (uid int, registered string, rejects bool)
	// DeliverHomeMail puts a mail in the mailbox of userid. owner and
	// title are in the memory encoding, content in the storage encoding.
	DeliverHomeMail(userid string, owner, title, content []byte) error
}

type Config struct {
	BBSHome     string
	InSocket    string
	OutSocket   string
	SpoolDir    string
	Relay       string // SMTP relay, host:port
	Hostname    string // MYHOSTNAME
	BBSName     string // BBSNAME, in UTF-8
	StorageBig5 bool
	MBBig5      bool

	ZipLimit   int64         // largest home backup to mail
	ZipWorkers int           // homes packed at the same time
	MaxAge     time.Duration // give up a mail not sent after this long
	Verbose    bool
}

type Server struct {
	cfg   Config
	bbs   BBS
	codec codec
	out   *outbox
	zip   *zipper
	stop  chan struct{}
}

func NewServer(cfg Config, bbs BBS) (*Server, error) {
	if cfg.SpoolDir == "" {
		cfg.SpoolDir = filepath.Join(cfg.BBSHome, "out", "mail.svc")
	}
	if cfg.MaxAge <= 0 {
		cfg.MaxAge = 24 * time.Hour
	}
	if cfg.ZipWorkers <= 0 {
		cfg.ZipWorkers = 1
	}
	if cfg.ZipLimit <= 0 {
		cfg.ZipLimit = defaultZipLimit
	}
	if err := os.MkdirAll(cfg.SpoolDir, 0755); err != nil {
		return nil, err
	}
	s := &Server{
		cfg:   cfg,
		bbs:   bbs,
		codec: codec{storageBig5: cfg.StorageBig5, mbBig5: cfg.MBBig5},
		stop:  make(chan struct{}),
	}
	s.out = newOutbox(s)
	s.zip = newZipper(s)
	return s, nil
}

// IsSocketOccupied tells if another instance serves socketPath.
func IsSocketOccupied(socketPath string) bool {
	conn, err := net.DialTimeout("unix", socketPath, 200*time.Millisecond)
	if err == nil {
		conn.Close()
		return true
	}
	return false
}

func listen(path string) (net.Listener, error) {
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		return nil, err
	}
	if IsSocketOccupied(path) {
		return nil, fmt.Errorf("another mail.svc is running on %s", path)
	}
	_ = os.Remove(path)
	l, err := net.Listen("unix", path)
	if err != nil {
		return nil, err
	}
	// Only the BBS (and its group) may deliver or send mails.
	if err := os.Chmod(path, 0660); err != nil {
		log.Printf("[mail.svc] Warning: chmod %s: %v", path, err)
	}
	return l, nil
}

// Start serves until SIGINT or SIGTERM.
func (s *Server) Start() error {
	lin, err := listen(s.cfg.InSocket)
	if err != nil {
		return err
	}
	defer lin.Close()
	lout, err := listen(s.cfg.OutSocket)
	if err != nil {
		return err
	}
	defer lout.Close()
	log.Printf("[mail.svc] Listening on %s and %s, relay %s",
		s.cfg.InSocket, s.cfg.OutSocket, s.cfg.Relay)

	go s.out.run(s.stop)
	s.zip.start(s.stop)

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, os.Interrupt, syscall.SIGTERM)
	go func() {
		sig := <-sigCh
		log.Printf("[mail.svc] Received signal %v, shutting down...", sig)
		close(s.stop)
		lin.Close()
		lout.Close()
	}()

	done := make(chan error, 2)
	go func() { done <- s.serve(lin, s.handleIn) }()
	go func() { done <- s.serve(lout, s.handleOut) }()
	err = <-done
	<-done
	return err
}

func (s *Server) serve(l net.Listener, handle func(net.Conn)) error {
	sem := make(chan struct{}, maxConns)
	for {
		conn, err := l.Accept()
		if err != nil {
			select {
			case <-s.stop:
				return nil
			default:
			}
			log.Printf("[mail.svc] Accept error: %v", err)
			time.Sleep(50 * time.Millisecond)
			continue
		}
		select {
		case sem <- struct{}{}:
			go func() {
				defer func() { <-sem }()
				defer conn.Close()
				handle(conn)
			}()
		default:
			fmt.Fprintf(conn, "ERR service overloaded\n")
			conn.Close()
		}
	}
}

func reply(conn net.Conn, format string, args ...any) {
	msg := fmt.Sprintf(format, args...)
	msg = strings.NewReplacer("\r", " ", "\n", " ").Replace(msg)
	fmt.Fprintf(conn, "%s\n", msg)
}

// handleIn serves util/bbsmail.
func (s *Server) handleIn(conn net.Conn) {
	_ = conn.SetDeadline(time.Now().Add(inTimeout))
	r := bufio.NewReader(conn)
	line, err := r.ReadString('\n')
	if err != nil {
		reply(conn, "ERR bad request")
		return
	}
	cmd, arg, _ := strings.Cut(strings.TrimRight(line, "\r\n"), " ")
	if cmd != "DELIVER" {
		reply(conn, "ERR unknown command")
		return
	}
	raw, err := io.ReadAll(io.LimitReader(r, MaxMailSize+1))
	if err != nil {
		reply(conn, "ERR %v", err)
		return
	}
	if len(raw) > MaxMailSize {
		s.mailog(fmt.Sprintf("Mail to <%s> is too large", arg))
		reply(conn, "REJECT too large")
		return
	}
	status, msg := s.deliver(arg, raw)
	if msg != "" {
		reply(conn, "%s %s", status, msg)
	} else {
		reply(conn, "%s", status)
	}
}

// handleOut serves mbbsd.
func (s *Server) handleOut(conn net.Conn) {
	_ = conn.SetDeadline(time.Now().Add(outTimeout))
	tr := textproto.NewReader(bufio.NewReader(conn))
	cmd, err := tr.ReadLine()
	if err != nil {
		reply(conn, "ERR bad request")
		return
	}
	h, err := tr.ReadMIMEHeader()
	if err != nil {
		reply(conn, "ERR bad header: %v", err)
		return
	}
	switch cmd {
	case "SEND":
		var n int64
		if _, err := fmt.Sscan(h.Get("Length"), &n); err != nil || n < 0 || n > MaxMailSize {
			reply(conn, "ERR bad length")
			return
		}
		content := make([]byte, n)
		if _, err := io.ReadFull(tr.R, content); err != nil {
			reply(conn, "ERR short content")
			return
		}
		if err := s.out.send(h.Get("From"), h.Get("Rcpt"),
			s.codec.fromMB([]byte(h.Get("Subject"))), s.codec.fromStorageArticle(content)); err != nil {
			reply(conn, "ERR %v", err)
			return
		}
		reply(conn, "OK")
	case "ZIPHOME":
		if err := s.zip.add(h.Get("User"), h.Get("Rcpt")); err != nil {
			reply(conn, "ERR %v", err)
			return
		}
		reply(conn, "OK")
	default:
		reply(conn, "ERR unknown command")
	}
}

// mailog logs like the old bbsmail did, in etc/mailog.
func (s *Server) mailog(msg string) {
	t := time.Now()
	line := fmt.Sprintf("%02d/%02d/%02d %02d:%02d:%02d <bbsmail> %s\n",
		t.Year()%100, int(t.Month()), t.Day(), t.Hour(), t.Minute(), t.Second(),
		msg)
	f, err := os.OpenFile(filepath.Join(s.cfg.BBSHome, mailLogPath),
		os.O_WRONLY|os.O_APPEND|os.O_CREATE, 0644)
	if err != nil {
		return
	}
	defer f.Close()
	f.Write(s.codec.toStorage(line))
}
