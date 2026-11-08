package daemon

import (
	"archive/tar"
	"bufio"
	"bytes"
	"compress/gzip"
	"encoding/base64"
	"fmt"
	"io"
	"mime"
	"mime/multipart"
	"net"
	"net/mail"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	"pttbbs/big5uao"
)

type delivered struct {
	userid, owner, title, content string // in UTF-8
}

// fakeBBS keeps users and their mails in memory (Big5 storage and memory).
type fakeBBS struct {
	mu      sync.Mutex
	users   map[string]bool // registered id -> rejects outside mails
	mails   []delivered
	failing bool
}

func (f *fakeBBS) LoadMailUser(userid string) (int, string, bool) {
	for id, rejects := range f.users {
		if strings.EqualFold(id, userid) {
			return 1, id, rejects
		}
	}
	return 0, "", false
}

func (f *fakeBBS) DeliverHomeMail(userid string, owner, title, content []byte) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	if f.failing {
		return fmt.Errorf("disk full")
	}
	f.mails = append(f.mails, delivered{userid, big5uao.Decode(owner),
		big5uao.Decode(title), big5uao.Decode(content)})
	return nil
}

func (f *fakeBBS) last(t *testing.T) delivered {
	t.Helper()
	f.mu.Lock()
	defer f.mu.Unlock()
	if len(f.mails) == 0 {
		t.Fatal("no mail delivered")
	}
	return f.mails[len(f.mails)-1]
}

func newTestServer(t testing.TB, relay string) (*Server, *fakeBBS) {
	t.Helper()
	home := t.TempDir()
	os.MkdirAll(filepath.Join(home, "etc"), 0755)
	bbs := &fakeBBS{users: map[string]bool{"Tester": false, "Hermit": true}}
	s, err := NewServer(Config{
		BBSHome:     home,
		InSocket:    filepath.Join(home, "in.sock"),
		OutSocket:   filepath.Join(home, "out.sock"),
		Relay:       relay,
		Hostname:    "bbs.example.org",
		StorageBig5: true,
		MBBig5:      true,
		ZipLimit:    1 << 20,
	}, bbs)
	if err != nil {
		t.Fatal(err)
	}
	return s, bbs
}

func TestDeliver(t *testing.T) {
	s, bbs := newTestServer(t, "")
	raw := "From: =?UTF-8?B?" + b64("王小明") + "?= <ming@gmail.com>\n" +
		"Subject: Undelivered Mail Returned to Sender\n\nbody\n"

	if st, msg := s.deliver("tester.bbs", []byte(raw)); st != statusOK {
		t.Fatalf("deliver = %s %s", st, msg)
	}
	m := bbs.last(t)
	if m.userid != "Tester" || m.owner != "ming." || m.title != "<對方退信通知>" {
		t.Errorf("delivered %+v", m)
	}
	if !strings.HasPrefix(m.content, "作者: ming@gmail.com (王小明)\n"+
		"標題: Undelivered Mail Returned to Sender\n時間: ") ||
		!strings.HasSuffix(m.content, "\n\nbody\n") {
		t.Errorf("content = %q", m.content)
	}

	if st, _ := s.deliver("nobody", []byte(raw)); st != statusReject {
		t.Errorf("unknown user: %s", st)
	}
	if st, _ := s.deliver("hermit", []byte(raw)); st != statusReject {
		t.Errorf("rejecting user: %s", st)
	}
	bbs.failing = true
	if st, _ := s.deliver("tester", []byte(raw)); st != statusErr {
		t.Errorf("failed delivery: %s", st)
	}
	raw2, _ := os.ReadFile(filepath.Join(s.cfg.BBSHome, mailLogPath))
	log := big5uao.Decode(raw2) // in the storage encoding
	if !strings.Contains(log, "<bbsmail> ming@gmail.com (王小明) => Tester\n") ||
		!strings.Contains(log, "<bbsmail> BBS user <nobody> not existed\n") {
		t.Errorf("mailog = %q", log)
	}
}

// fakeRelay is a small SMTP server.
type fakeRelay struct {
	l        net.Listener
	mu       sync.Mutex
	rcptCode int // reply to RCPT
	msgs     []relayed
}

type relayed struct {
	from, rcpt, data string
}

func newFakeRelay(t *testing.T) *fakeRelay {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	r := &fakeRelay{l: l, rcptCode: 250}
	t.Cleanup(func() { l.Close() })
	go func() {
		for {
			c, err := l.Accept()
			if err != nil {
				return
			}
			go r.serve(c)
		}
	}()
	return r
}

func (r *fakeRelay) serve(c net.Conn) {
	defer c.Close()
	br := bufio.NewReader(c)
	fmt.Fprintf(c, "220 fake\r\n")
	var cur relayed
	for {
		line, err := br.ReadString('\n')
		if err != nil {
			return
		}
		cmd := strings.ToUpper(strings.TrimSpace(line))
		switch {
		case strings.HasPrefix(cmd, "EHLO"), strings.HasPrefix(cmd, "HELO"):
			fmt.Fprintf(c, "250 fake\r\n")
		case strings.HasPrefix(cmd, "MAIL FROM:"):
			cur = relayed{from: strings.Trim(strings.TrimSpace(line)[10:], "<>")}
			fmt.Fprintf(c, "250 ok\r\n")
		case strings.HasPrefix(cmd, "RCPT TO:"):
			r.mu.Lock()
			code := r.rcptCode
			r.mu.Unlock()
			cur.rcpt = strings.Trim(strings.TrimSpace(line)[8:], "<>")
			fmt.Fprintf(c, "%d rcpt\r\n", code)
		case cmd == "DATA":
			fmt.Fprintf(c, "354 go\r\n")
			var data strings.Builder
			for {
				l, err := br.ReadString('\n')
				if err != nil {
					return
				}
				if l == ".\r\n" {
					break
				}
				data.WriteString(strings.TrimPrefix(l, "."))
			}
			cur.data = data.String()
			r.mu.Lock()
			r.msgs = append(r.msgs, cur)
			r.mu.Unlock()
			fmt.Fprintf(c, "250 queued\r\n")
		case cmd == "RSET":
			fmt.Fprintf(c, "250 ok\r\n")
		case cmd == "QUIT":
			fmt.Fprintf(c, "221 bye\r\n")
			return
		default:
			fmt.Fprintf(c, "502 what\r\n")
		}
	}
}

func (r *fakeRelay) addr() string { return r.l.Addr().String() }

func spooled(t *testing.T, s *Server) []string {
	t.Helper()
	names, _ := filepath.Glob(filepath.Join(s.cfg.SpoolDir, "*.json"))
	return names
}

func request(t *testing.T, sock, req string) string {
	t.Helper()
	c, err := net.Dial("unix", sock)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	io.WriteString(c, req)
	c.(*net.UnixConn).CloseWrite()
	resp, _ := io.ReadAll(c)
	return string(resp)
}

func startSockets(t *testing.T, s *Server) {
	t.Helper()
	lin, err := listen(s.cfg.InSocket)
	if err != nil {
		t.Fatal(err)
	}
	lout, err := listen(s.cfg.OutSocket)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		close(s.stop)
		lin.Close()
		lout.Close()
	})
	go s.serve(lin, s.handleIn)
	go s.serve(lout, s.handleOut)
}

// alternatives decodes the parts of a multipart/alternative mail in UTF-8.
func alternatives(t *testing.T, m *mail.Message) map[string]string {
	t.Helper()
	mt, params, _ := mime.ParseMediaType(m.Header.Get("Content-Type"))
	if mt != "multipart/alternative" {
		t.Fatalf("Content-Type = %q", m.Header.Get("Content-Type"))
	}
	parts := map[string]string{}
	mr := multipart.NewReader(m.Body, params["boundary"])
	for {
		p, err := mr.NextPart()
		if err != nil {
			break
		}
		ct, cp, _ := mime.ParseMediaType(p.Header.Get("Content-Type"))
		if cp["charset"] != "UTF-8" {
			t.Errorf("%s in %q", ct, cp["charset"])
		}
		b, err := io.ReadAll(base64.NewDecoder(base64.StdEncoding, p))
		if err != nil {
			t.Fatal(err)
		}
		parts[ct] = string(b)
	}
	return parts
}

func TestSockets(t *testing.T) {
	relay := newFakeRelay(t)
	s, bbs := newTestServer(t, relay.addr())
	startSockets(t, s)

	if got := request(t, s.cfg.InSocket, "DELIVER tester.bbs\nSubject: hi\n\nhello\n"); got != "OK\n" {
		t.Errorf("DELIVER = %q", got)
	}
	if m := bbs.last(t); m.title != "hi" {
		t.Errorf("delivered %+v", m)
	}
	if got := request(t, s.cfg.InSocket, "DELIVER nobody\n\nhello\n"); !strings.HasPrefix(got, "REJECT ") {
		t.Errorf("DELIVER to nobody = %q", got)
	}

	content := big5uao.Encode("\x1b[1;31m紅字\x1b[m 內文\n")
	title := string(big5uao.Encode("轉寄: 測試"))
	req := fmt.Sprintf("SEND\nRcpt: friend@example.com\nFrom: Tester\nSubject: %s\nLength: %d\n\n%s",
		title, len(content), content)
	if got := request(t, s.cfg.OutSocket, req); got != "OK\n" {
		t.Fatalf("SEND = %q", got)
	}
	for _, bad := range []string{
		"SEND\nRcpt: not an address\nFrom: Tester\nSubject: x\nLength: 0\n\n",
		"SEND\nRcpt: a@b.c\nFrom: ../x\nSubject: x\nLength: 0\n\n",
		"SEND\nRcpt: a@b.c\nFrom: Tester\nSubject: x\nLength: 10\n\nshort",
		"HELLO\n\n",
	} {
		if got := request(t, s.cfg.OutSocket, bad); !strings.HasPrefix(got, "ERR ") {
			t.Errorf("%q = %q", bad, got)
		}
	}
	if n := len(spooled(t, s)); n != 1 {
		t.Fatalf("%d mails in the spool", n)
	}

	if n := s.out.flush(); n != 1 {
		t.Fatalf("flush sent %d", n)
	}
	if n := len(spooled(t, s)); n != 0 {
		t.Errorf("%d mails left in the spool", n)
	}
	if len(relay.msgs) != 1 {
		t.Fatalf("relay got %d mails", len(relay.msgs))
	}
	got := relay.msgs[0]
	if got.from != "Tester.bbs@bbs.example.org" || got.rcpt != "friend@example.com" {
		t.Errorf("envelope %s -> %s", got.from, got.rcpt)
	}
	m, err := mail.ReadMessage(strings.NewReader(got.data))
	if err != nil {
		t.Fatal(err)
	}
	subject, _ := new(mime.WordDecoder).DecodeHeader(m.Header.Get("Subject"))
	if subject != "轉寄: 測試" {
		t.Errorf("subject = %q", subject)
	}
	parts := alternatives(t, m)
	if parts["text/plain"] != "紅字 內文\n" {
		t.Errorf("text = %q", parts["text/plain"])
	}
	if !strings.Contains(parts["text/html"], `<span style="color:#ff0000">紅字</span> 內文`) {
		t.Errorf("html = %q", parts["text/html"])
	}
}

func TestSendTextOnly(t *testing.T) {
	relay := newFakeRelay(t)
	s, _ := newTestServer(t, relay.addr())
	startSockets(t, s)

	content := big5uao.Encode("\x1b[1;31m查詢\x1b[m 結果\n1.2.3.4\tTester\n")
	req := fmt.Sprintf("SEND\nRcpt: archive@example.com\nFrom: bot@bbs.example.org\nSubject: report\nLength: %d\nFormat: text\n\n%s",
		len(content), content)
	if got := request(t, s.cfg.OutSocket, req); got != "OK\n" {
		t.Fatalf("SEND = %q", got)
	}
	for _, bad := range []string{
		"SEND\nRcpt: a@b.c\nFrom: Tester\nSubject: x\nLength: 0\nFormat: pdf\n\n",
		"SEND\nRcpt: a@b.c\nFrom: bot@example.com\nSubject: x\nLength: 0\n\n",
		"SEND\nRcpt: a@b.c\nFrom: ../x@bbs.example.org\nSubject: x\nLength: 0\n\n",
	} {
		if got := request(t, s.cfg.OutSocket, bad); !strings.HasPrefix(got, "ERR ") {
			t.Errorf("%q = %q", bad, got)
		}
	}
	if n := s.out.flush(); n != 1 {
		t.Fatalf("flush sent %d", n)
	}
	if got := relay.msgs[0]; got.from != "bot@bbs.example.org" || got.rcpt != "archive@example.com" {
		t.Errorf("envelope %s -> %s", got.from, got.rcpt)
	}
	m, err := mail.ReadMessage(strings.NewReader(relay.msgs[0].data))
	if err != nil {
		t.Fatal(err)
	}
	if from := m.Header.Get("From"); from != `"bot" <bot@bbs.example.org>` {
		t.Errorf("From = %q", from)
	}
	mt, params, _ := mime.ParseMediaType(m.Header.Get("Content-Type"))
	if mt != "text/plain" || params["charset"] != "UTF-8" {
		t.Fatalf("Content-Type = %q", m.Header.Get("Content-Type"))
	}
	b, err := io.ReadAll(base64.NewDecoder(base64.StdEncoding, m.Body))
	if err != nil {
		t.Fatal(err)
	}
	if string(b) != "查詢 結果\n1.2.3.4\tTester\n" {
		t.Errorf("text = %q", b)
	}
}

func TestRetry(t *testing.T) {
	relay := newFakeRelay(t)
	s, _ := newTestServer(t, relay.addr())

	relay.rcptCode = 451
	if err := s.out.send("Tester", "a@example.com", []byte("x"), []byte("y")); err != nil {
		t.Fatal(err)
	}
	if n := s.out.flush(); n != 0 {
		t.Errorf("sent %d", n)
	}
	envs := s.out.pending()
	if len(envs) != 1 || envs[0].Attempts != 1 || !envs[0].Next.After(time.Now()) {
		t.Fatalf("after a temporary failure: %+v", envs)
	}
	// Not due yet
	relay.rcptCode = 250
	if n := s.out.flush(); n != 0 {
		t.Errorf("sent %d before it's due", n)
	}

	envs[0].Next = time.Now()
	s.out.saveEnvelope(envs[0])
	relay.rcptCode = 550
	s.out.flush()
	if n := len(spooled(t, s)); n != 0 {
		t.Errorf("a refused mail is kept")
	}

	// Relay down: kept for later
	s.cfg.Relay = "127.0.0.1:1"
	s.out.send("Tester", "a@example.com", []byte("x"), []byte("y"))
	s.out.flush()
	if n := len(spooled(t, s)); n != 1 {
		t.Errorf("%d mails in the spool when the relay is down", n)
	}
}

func makeHome(t *testing.T, s *Server) {
	t.Helper()
	home := filepath.Join(s.cfg.BBSHome, "home", "T", "Tester")
	os.MkdirAll(filepath.Join(home, "sub"), 0755)
	for name, data := range map[string]string{
		".DIR":      "dir",
		".DIR.old":  "old",
		"buf.1":     "buf",
		"sub/M.1.A": strings.Repeat("mail ", 100),
	} {
		os.WriteFile(filepath.Join(home, name), []byte(data), 0644)
	}
	os.WriteFile(filepath.Join(s.cfg.BBSHome, zipExcludeFile), []byte(".DIR.old\nbuf.*\n"), 0644)
}

func TestZipHome(t *testing.T) {
	relay := newFakeRelay(t)
	s, bbs := newTestServer(t, relay.addr())
	makeHome(t, s)

	if err := s.zip.add("Tester", "me@example.com"); err != nil {
		t.Fatal(err)
	}
	if err := s.zip.add("Tester", "me@example.com"); err != errQueued {
		t.Errorf("second add: %v", err)
	}
	if err := s.zip.add("tester", "me@example.com"); err == nil {
		t.Errorf("add takes a userid not as registered")
	}
	if err := s.zip.add("../etc", "me@example.com"); err == nil {
		t.Errorf("add takes a bad userid")
	}
	s.zip.run(<-s.zip.jobs)
	if s.out.flush() != 1 {
		t.Fatal("not sent")
	}

	m, err := mail.ReadMessage(strings.NewReader(relay.msgs[0].data))
	if err != nil {
		t.Fatal(err)
	}
	_, params, _ := mime.ParseMediaType(m.Header.Get("Content-Type"))
	mr := multipart.NewReader(m.Body, params["boundary"])
	mr.NextPart() // the text
	p, err := mr.NextPart()
	if err != nil {
		t.Fatal(err)
	}
	if p.FileName() != "home.Tester.tgz" {
		t.Errorf("file name %q", p.FileName())
	}
	gz, err := gzip.NewReader(base64.NewDecoder(base64.StdEncoding, p))
	if err != nil {
		t.Fatal(err)
	}
	var names []string
	tr := tar.NewReader(gz)
	for {
		h, err := tr.Next()
		if err != nil {
			break
		}
		names = append(names, h.Name)
	}
	want := "home/T/Tester/ home/T/Tester/.DIR home/T/Tester/sub/ home/T/Tester/sub/M.1.A"
	if got := strings.Join(names, " "); got != want {
		t.Errorf("packed %q, want %q", got, want)
	}

	// Too large: told in a BBS mail
	s.cfg.ZipLimit = 100
	s.zip.run(zipJob{user: "Tester", rcpt: "me@example.com"})
	if m := bbs.last(t); m.title != "私人資料打包過大，無法寄出" || m.owner != "SYSOP" {
		t.Errorf("notice %+v", m)
	}
	if s.out.flush() != 0 {
		t.Errorf("a too large backup is sent")
	}
	leftovers, _ := filepath.Glob(filepath.Join(s.cfg.SpoolDir, "*.tgz"))
	if len(leftovers) != 0 {
		t.Errorf("left %v", leftovers)
	}
}

func TestComposeText(t *testing.T) {
	s, _ := newTestServer(t, "")
	var b bytes.Buffer
	s.out.composeText(&b, "Tester", "a@b.c", "主旨", strings.Repeat("長", 100))
	m, err := mail.ReadMessage(&b)
	if err != nil {
		t.Fatal(err)
	}
	if from := m.Header.Get("From"); from != `"Tester" <Tester.bbs@bbs.example.org>` {
		t.Errorf("From = %q", from)
	}
	if d := m.Header.Get("X-Disclaimer"); d != "bbs.example.org is not responsible for the content of this mail." {
		t.Errorf("X-Disclaimer = %q", d)
	}
	raw, _ := io.ReadAll(m.Body)
	for _, l := range strings.Split(string(raw), "\r\n") {
		if len(l) > 76 {
			t.Errorf("line too long: %d", len(l))
		}
	}
}

// panicBBS panics, like a bug on a bad mail.
type panicBBS struct{ fakeBBS }

func (*panicBBS) LoadMailUser(string) (int, string, bool) { panic("bug") }

func TestRequestPanic(t *testing.T) {
	s, _ := newTestServer(t, "")
	s.bbs = &panicBBS{}
	startSockets(t, s)
	if got := request(t, s.cfg.InSocket, "DELIVER tester\n\nhi\n"); got != "ERR internal error\n" {
		t.Errorf("DELIVER = %q", got)
	}
	// still serving
	if got := request(t, s.cfg.OutSocket, "HELLO\n\n"); !strings.HasPrefix(got, "ERR ") {
		t.Errorf("after a panic: %q", got)
	}
}

// A mail that can't be encoded or decoded is still sent or delivered.
func TestDegrade(t *testing.T) {
	relay := newFakeRelay(t)
	s, bbs := newTestServer(t, relay.addr())
	boom := func(string) string { panic("render bug") }
	content := big5uao.Encode("\x1b[31m內文\x1b[m\n")

	defer func(p, h func(string) string) { renderPlain, renderHTML = p, h }(renderPlain, renderHTML)

	// Without HTML
	renderHTML = boom
	if err := s.out.send("Tester", "a@example.com", []byte("x"), content); err != nil {
		t.Fatal(err)
	}
	// As the old outmail, without even the text
	renderPlain = boom
	if err := s.out.send("Tester", "b@example.com", []byte("x"), content); err != nil {
		t.Fatal(err)
	}
	if n := s.out.flush(); n != 2 {
		t.Fatalf("sent %d", n)
	}
	for _, m := range relay.msgs {
		msg, err := mail.ReadMessage(strings.NewReader(m.data))
		if err != nil {
			t.Fatal(err)
		}
		ct := msg.Header.Get("Content-Type")
		switch m.rcpt {
		case "a@example.com":
			if body, _ := io.ReadAll(base64.NewDecoder(base64.StdEncoding, msg.Body)); !strings.HasPrefix(ct, "text/plain") || string(body) != "內文\n" {
				t.Errorf("without HTML: %q %q", ct, body)
			}
		case "b@example.com":
			if body, _ := io.ReadAll(msg.Body); !strings.Contains(ct, "big5") || !strings.Contains(string(body), string(content[:5])) {
				t.Errorf("as outmail: %q %q", ct, body)
			}
		}
	}
	if leftovers, _ := filepath.Glob(filepath.Join(s.cfg.SpoolDir, "*")); len(leftovers) != 0 {
		t.Errorf("left %v", leftovers)
	}

	// Delivered as it is
	defer func(d func([]byte) parsedMail) { decodeMail = d }(decodeMail)
	decodeMail = func([]byte) parsedMail { panic("decode bug") }
	if st, msg := s.deliver("tester", []byte("Subject: hi\n\nbody\n")); st != statusOK {
		t.Fatalf("deliver = %s %s", st, msg)
	}
	if m := bbs.last(t); m.title != "hi" || !strings.HasSuffix(m.content, "\n\nbody\n") {
		t.Errorf("delivered %+v", m)
	}
}

// Mails in and out must be delivered or spooled, whatever they have.
func FuzzDeliverAndSend(f *testing.F) {
	f.Add([]byte("Subject: hi\n\nbody\n"), []byte("title"))
	f.Add([]byte("\x1b可\x1b[1;31m紅\xa5"), []byte("\xa5\x1b"))
	s, _ := newTestServer(f, "")
	f.Fuzz(func(t *testing.T, raw, subject []byte) {
		if st, msg := s.deliver("tester", raw); st != statusOK {
			t.Fatalf("deliver = %s %s", st, msg)
		}
		if err := s.out.send("Tester", "a@example.com", subject, raw); err != nil {
			t.Fatalf("send: %v", err)
		}
		for _, env := range s.out.pending() {
			s.out.remove(env)
		}
	})
}
