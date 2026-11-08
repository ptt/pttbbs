package daemon

import (
	"bufio"
	"crypto/rand"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"mime"
	"net/mail"
	"net/smtp"
	"net/textproto"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

const (
	sendInterval = 30 * time.Second
	minBackoff   = time.Minute
	maxBackoff   = time.Hour
)

// envelope is the spooled state of a mail; the mail itself is in <id>.eml.
type envelope struct {
	ID       string    `json:"id"`
	From     string    `json:"from"` // envelope sender
	Rcpt     string    `json:"rcpt"`
	Subject  string    `json:"subject"` // for logs
	Created  time.Time `json:"created"`
	Next     time.Time `json:"next"`
	Attempts int       `json:"attempts"`
	LastErr  string    `json:"last_error,omitempty"`
}

// outbox keeps mails to send in the spool and sends them to the relay.
type outbox struct {
	s    *Server
	dir  string
	kick chan struct{}
	seq  atomic.Uint64
	mu   sync.Mutex // one round of sending at a time
	// dial connects to the relay (replaced in tests)
	dial func(addr string) (*smtp.Client, error)
}

func newOutbox(s *Server) *outbox {
	return &outbox{
		s:    s,
		dir:  s.cfg.SpoolDir,
		kick: make(chan struct{}, 1),
		dial: smtp.Dial,
	}
}

var validUserID = regexp.MustCompile(`^[A-Za-z][A-Za-z0-9._-]{0,15}$`)

// checkRcpt checks an address from mbbsd and gives it without a name.
func checkRcpt(rcpt string) (string, error) {
	a, err := mail.ParseAddress(strings.TrimSpace(rcpt))
	if err != nil || !strings.Contains(a.Address, "@") {
		return "", fmt.Errorf("bad recipient")
	}
	return a.Address, nil
}

// sender is the address of a BBS user.
func (o *outbox) sender(from string) string {
	return from + ".bbs@" + o.s.cfg.Hostname
}

// writeHeaders writes the common headers of mails sent out.
func (o *outbox) writeHeaders(w io.Writer, from, rcpt, subject string) {
	host := o.s.cfg.Hostname
	var id [8]byte
	rand.Read(id[:])
	fmt.Fprintf(w, "From: %s\r\n", (&mail.Address{Name: from, Address: o.sender(from)}).String())
	fmt.Fprintf(w, "To: %s\r\n", rcpt)
	fmt.Fprintf(w, "Subject: %s\r\n", mime.BEncoding.Encode("UTF-8", subject))
	fmt.Fprintf(w, "Date: %s\r\n", time.Now().Format(time.RFC1123Z))
	fmt.Fprintf(w, "Message-ID: <%d.%s.mailsvc@%s>\r\n", time.Now().Unix(),
		hex.EncodeToString(id[:]), host)
	fmt.Fprintf(w, "MIME-Version: 1.0\r\n")
	fmt.Fprintf(w, "X-Mailer: mail.svc of pttbbs\r\n")
	fmt.Fprintf(w, "X-Disclaimer: %s\r\n", o.disclaimer())
}

// disclaimer is the X-Disclaimer header, in ASCII (MYHOSTNAME), so it
// needs no encoding.
func (o *outbox) disclaimer() string {
	return sanitizeHeader(o.s.cfg.Hostname) + " is not responsible for the content of this mail."
}

// lineWriter breaks base64 into lines of 76 characters.
type lineWriter struct {
	w   io.Writer
	col int
}

func (l *lineWriter) Write(p []byte) (int, error) {
	n := 0
	for len(p) > 0 {
		k := 76 - l.col
		if k > len(p) {
			k = len(p)
		}
		if _, err := l.w.Write(p[:k]); err != nil {
			return n, err
		}
		n += k
		p = p[k:]
		l.col += k
		if l.col == 76 {
			if _, err := io.WriteString(l.w, "\r\n"); err != nil {
				return n, err
			}
			l.col = 0
		}
	}
	return n, nil
}

func (l *lineWriter) end() {
	if l.col > 0 {
		io.WriteString(l.w, "\r\n")
		l.col = 0
	}
}

func writeBase64(w io.Writer, r io.Reader) error {
	lw := &lineWriter{w: w}
	enc := base64.NewEncoder(base64.StdEncoding, lw)
	if _, err := io.Copy(enc, r); err != nil {
		return err
	}
	if err := enc.Close(); err != nil {
		return err
	}
	lw.end()
	return nil
}

const textType = "Content-Type: text/plain; charset=UTF-8\r\n" +
	"Content-Transfer-Encoding: base64\r\n"

const htmlType = "Content-Type: text/html; charset=UTF-8\r\n" +
	"Content-Transfer-Encoding: base64\r\n"

func newBoundary() string {
	var b [12]byte
	rand.Read(b[:])
	return "mailsvc-" + hex.EncodeToString(b[:])
}

// The renderers of articles (replaced in tests).
var (
	renderPlain = plainText
	renderHTML  = htmlText
)

// composeText writes a BBS article as a mail in UTF-8, like most mail
// programs: plain text without the ANSI sequences, and HTML with the
// colors.
func (o *outbox) composeText(w io.Writer, from, rcpt, subject, body string) error {
	return o.composeRendered(w, from, rcpt, subject, renderPlain(body), renderHTML(body))
}

// composeRendered writes a mail with text and HTML, or only text if html
// is empty.
func (o *outbox) composeRendered(w io.Writer, from, rcpt, subject, text, html string) error {
	o.writeHeaders(w, from, rcpt, subject)
	if html == "" {
		io.WriteString(w, textType+"\r\n")
		return writeBase64(w, strings.NewReader(text))
	}
	boundary := newBoundary()
	fmt.Fprintf(w, "Content-Type: multipart/alternative; boundary=\"%s\"\r\n\r\n", boundary)
	fmt.Fprintf(w, "--%s\r\n%s\r\n", boundary, textType)
	if err := writeBase64(w, strings.NewReader(text)); err != nil {
		return err
	}
	fmt.Fprintf(w, "--%s\r\n%s\r\n", boundary, htmlType)
	if err := writeBase64(w, strings.NewReader(html)); err != nil {
		return err
	}
	fmt.Fprintf(w, "--%s--\r\n", boundary)
	return nil
}

// composeAttachment writes a mail with a text and a file.
func (o *outbox) composeAttachment(w io.Writer, from, rcpt, subject, body,
	filename, ctype string, data io.Reader) error {
	boundary := newBoundary()
	o.writeHeaders(w, from, rcpt, subject)
	fmt.Fprintf(w, "Content-Type: multipart/mixed; boundary=\"%s\"\r\n\r\n", boundary)
	fmt.Fprintf(w, "--%s\r\n%s\r\n", boundary, textType)
	if err := writeBase64(w, strings.NewReader(plainText(body))); err != nil {
		return err
	}
	fmt.Fprintf(w, "--%s\r\nContent-Type: %s\r\n", boundary, ctype)
	fmt.Fprintf(w, "Content-Disposition: attachment; filename=\"%s\"\r\n", filename)
	fmt.Fprintf(w, "Content-Transfer-Encoding: base64\r\n\r\n")
	if err := writeBase64(w, data); err != nil {
		return err
	}
	fmt.Fprintf(w, "--%s--\r\n", boundary)
	return nil
}

// send spools a mail from the BBS user from to rcpt. subject is in the
// memory encoding and content in the storage encoding. Receivers in the
// plain list get it as the old outmail sent it; others get UTF-8 text and
// HTML.
func (o *outbox) send(from, rcpt string, subject, content []byte) error {
	if !validUserID.MatchString(from) {
		return fmt.Errorf("bad sender")
	}
	addr, err := checkRcpt(rcpt)
	if err != nil {
		return err
	}
	title := sanitizeHeader(o.s.codec.fromMB(subject))
	if o.s.plain.match(addr) {
		return o.enqueue(o.sender(from), addr, title, func(w io.Writer) error {
			return o.composePlain(w, from, addr, title, content)
		})
	}
	// Render before spooling, so a failure doesn't leave a partial mail;
	// and degrade instead of failing: without HTML if it can't be made,
	// or as the old outmail did if not even the text can be made.
	what := fmt.Sprintf("the mail from %s to %s", from, addr)
	body, ok := safely("decoding "+what, func() string {
		return o.s.codec.fromStorageArticle(content)
	})
	var text string
	if ok {
		text, ok = safely("rendering the text of "+what, func() string {
			return renderPlain(body)
		})
	}
	if !ok {
		return o.enqueue(o.sender(from), addr, title, func(w io.Writer) error {
			return o.composePlain(w, from, addr, title, content)
		})
	}
	html, _ := safely("rendering the HTML of "+what, func() string {
		return renderHTML(body)
	})
	return o.enqueue(o.sender(from), addr, title, func(w io.Writer) error {
		return o.composeRendered(w, from, addr, title, text, html)
	})
}

func (o *outbox) newID() string {
	return fmt.Sprintf("%d.%d.%d", time.Now().UnixNano(), os.Getpid(), o.seq.Add(1))
}

func writeFileSync(path string, write func(io.Writer) error) error {
	tmp := path + ".tmp"
	f, err := os.OpenFile(tmp, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0600)
	if err != nil {
		return err
	}
	done := false
	defer func() {
		// also when write panics
		if !done {
			f.Close()
			os.Remove(tmp)
		}
	}()
	bw := bufio.NewWriter(f)
	err = write(bw)
	if err == nil {
		err = bw.Flush()
	}
	if err == nil {
		err = f.Sync()
	}
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	done = true
	if err == nil {
		err = os.Rename(tmp, path)
	}
	if err != nil {
		os.Remove(tmp)
	}
	return err
}

// enqueue writes a mail and its envelope in the spool. The envelope is
// written last, so a mail is sent only when it is complete.
func (o *outbox) enqueue(from, rcpt, subject string, compose func(io.Writer) error) error {
	env := envelope{
		ID: o.newID(), From: from, Rcpt: rcpt, Subject: subject,
		Created: time.Now(),
	}
	env.Next = env.Created
	eml := filepath.Join(o.dir, env.ID+".eml")
	if err := writeFileSync(eml, compose); err != nil {
		log.Printf("[mail.svc] Spool %s: %v", env.ID, err)
		return fmt.Errorf("cannot spool")
	}
	if err := o.saveEnvelope(&env); err != nil {
		os.Remove(eml)
		log.Printf("[mail.svc] Spool %s: %v", env.ID, err)
		return fmt.Errorf("cannot spool")
	}
	log.Printf("[mail.svc] Queued %s: %s -> %s: %s", env.ID, from, rcpt, subject)
	select {
	case o.kick <- struct{}{}:
	default:
	}
	return nil
}

func (o *outbox) saveEnvelope(env *envelope) error {
	return writeFileSync(filepath.Join(o.dir, env.ID+".json"), func(w io.Writer) error {
		return json.NewEncoder(w).Encode(env)
	})
}

func (o *outbox) remove(env *envelope) {
	os.Remove(filepath.Join(o.dir, env.ID+".json"))
	os.Remove(filepath.Join(o.dir, env.ID+".eml"))
}

// run sends the spooled mails when kicked, and every sendInterval.
func (o *outbox) run(stop <-chan struct{}) {
	t := time.NewTicker(sendInterval)
	defer t.Stop()
	guard("sending mails", func() { o.flush() })
	for {
		select {
		case <-stop:
			return
		case <-t.C:
		case <-o.kick:
		}
		guard("sending mails", func() { o.flush() })
	}
}

func (o *outbox) pending() []*envelope {
	names, _ := filepath.Glob(filepath.Join(o.dir, "*.json"))
	sort.Strings(names)
	var envs []*envelope
	for _, name := range names {
		b, err := os.ReadFile(name)
		if err != nil {
			continue
		}
		var env envelope
		if err := json.Unmarshal(b, &env); err != nil || env.ID == "" ||
			filepath.Join(o.dir, env.ID+".json") != name {
			log.Printf("[mail.svc] Bad envelope %s, removed", name)
			os.Remove(name)
			continue
		}
		envs = append(envs, &env)
	}
	return envs
}

// permanent tells if the relay refuses a mail for good.
func permanent(err error) bool {
	var te *textproto.Error
	return errors.As(err, &te) && te.Code >= 500
}

// flush tries to send the mails that are due. It returns the number sent.
func (o *outbox) flush() int {
	o.mu.Lock()
	defer o.mu.Unlock()

	var c *smtp.Client
	defer func() {
		if c != nil {
			c.Quit()
		}
	}()
	sent := 0
	now := time.Now()
	for _, env := range o.pending() {
		if env.Next.After(now) {
			continue
		}
		if c == nil {
			var err error
			if c, err = o.dial(o.s.cfg.Relay); err == nil {
				err = c.Hello(o.s.cfg.Hostname)
			}
			if err != nil {
				log.Printf("[mail.svc] Relay %s: %v", o.s.cfg.Relay, err)
				if c != nil {
					c.Close()
					c = nil
				}
				return sent
			}
		}
		err := o.deliver(c, env)
		switch {
		case err == nil:
			log.Printf("[mail.svc] Sent %s: %s -> %s: %s", env.ID, env.From, env.Rcpt, env.Subject)
			o.remove(env)
			sent++
		case permanent(err):
			log.Printf("[mail.svc] Refused %s: %s -> %s: %v", env.ID, env.From, env.Rcpt, err)
			o.remove(env)
		default:
			o.retry(env, err)
			// The connection may be broken: start again next round.
			c.Close()
			c = nil
			return sent
		}
	}
	return sent
}

func (o *outbox) deliver(c *smtp.Client, env *envelope) error {
	f, err := os.Open(filepath.Join(o.dir, env.ID+".eml"))
	if err != nil {
		return &textproto.Error{Code: 550, Msg: "spooled mail lost: " + err.Error()}
	}
	defer f.Close()
	if err := c.Reset(); err != nil {
		return err
	}
	if err := c.Mail(env.From); err != nil {
		return err
	}
	if err := c.Rcpt(env.Rcpt); err != nil {
		return err
	}
	w, err := c.Data()
	if err != nil {
		return err
	}
	if _, err := io.Copy(w, f); err != nil {
		w.Close()
		return err
	}
	return w.Close()
}

func (o *outbox) retry(env *envelope, err error) {
	env.Attempts++
	env.LastErr = err.Error()
	if time.Since(env.Created) > o.s.cfg.MaxAge {
		log.Printf("[mail.svc] Gave up %s after %d tries: %s -> %s: %v",
			env.ID, env.Attempts, env.From, env.Rcpt, err)
		o.remove(env)
		return
	}
	backoff := minBackoff << min(env.Attempts-1, 6)
	if backoff > maxBackoff {
		backoff = maxBackoff
	}
	env.Next = time.Now().Add(backoff)
	log.Printf("[mail.svc] Retry %s in %v: %v", env.ID, backoff, err)
	if err := o.saveEnvelope(env); err != nil {
		log.Printf("[mail.svc] Save %s: %v", env.ID, err)
	}
}
