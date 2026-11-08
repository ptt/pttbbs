package daemon

// Some receivers, like BBSes whose bbsmail predates MIME, can only take a
// mail as the old outmail sent it: one text/plain part with the article as
// it is on disk (ANSI sequences kept) in the BBS encoding. Their addresses
// are listed in etc/mail.svc.conf as "plain" (which may repeat, and may
// have several patterns separated by commas or spaces):
//
//	plain = user@host   this address
//	plain = @host       any address at host (a bare "host" is the same)
//	plain = .host       any address at a subdomain of host
//	plain = user@       user at any host
//	plain = .suffix@    any user ending with .suffix at any host, e.g.
//	                    ".bbs@" for the users of all BBSes
//
// The file is read again when it changes, so mail.svc doesn't need to be
// restarted for these.

import (
	"crypto/rand"
	"encoding/binary"
	"fmt"
	"io"
	"os"
	"strings"
	"sync"
	"time"
)

// ConfigPath is where mail.svc.conf is, relative to BBSHOME.
const ConfigPath = "etc/mail.svc.conf"

// plainList is the list of receivers that get plain mails.
type plainList struct {
	path string // mail.svc.conf

	mu      sync.Mutex
	loaded  bool
	mtime   time.Time
	size    int64
	entries []string // in lower case
}

func newPlainList(path string) *plainList {
	return &plainList{path: path}
}

// plainPatterns gives the "plain" patterns in the entries of
// mail.svc.conf, in lower case.
func plainPatterns(conf []ConfigEntry) []string {
	var patterns []string
	for _, e := range conf {
		if e.Key != "plain" {
			continue
		}
		for _, p := range strings.FieldsFunc(e.Value, func(r rune) bool {
			return r == ',' || r == ' ' || r == '\t'
		}) {
			patterns = append(patterns, strings.ToLower(p))
		}
	}
	return patterns
}

// reload reads the list again if the file has changed. A missing file is
// an empty list.
func (p *plainList) reload() {
	st, err := os.Stat(p.path)
	if err != nil {
		p.entries, p.loaded = nil, true
		p.mtime, p.size = time.Time{}, 0
		return
	}
	if p.loaded && st.ModTime().Equal(p.mtime) && st.Size() == p.size {
		return
	}
	conf, err := ParseConfig(p.path)
	if err != nil {
		return // keep the old list
	}
	p.entries, p.loaded = plainPatterns(conf), true
	p.mtime, p.size = st.ModTime(), st.Size()
}

// match tells if addr (without a name) gets plain mails.
func (p *plainList) match(addr string) bool {
	p.mu.Lock()
	defer p.mu.Unlock()
	p.reload()

	addr = strings.ToLower(addr)
	at := strings.LastIndexByte(addr, '@')
	if at < 0 {
		return false
	}
	domain := addr[at+1:]
	local := addr[:at]
	for _, e := range p.entries {
		switch {
		case e[len(e)-1] == '@': // the local part, at any host
			if e[0] == '.' && len(e) > 2 {
				if strings.HasSuffix(local, e[:len(e)-1]) {
					return true
				}
			} else if local == e[:len(e)-1] {
				return true
			}
		case e[0] == '.':
			if strings.HasSuffix(domain, e) {
				return true
			}
		case e[0] == '@':
			if domain == e[1:] {
				return true
			}
		case strings.Contains(e, "@"):
			if addr == e {
				return true
			}
		default:
			if domain == e {
				return true
			}
		}
	}
	return false
}

// mimeSpecials are the characters qEncode encodes besides the 8-bit and
// control ones, as qp_encode() of libcmsys.
const mimeSpecials = "@.,;:<>[]\\\"()?/= \t"

// qEncode encodes s as one encoded word (RFC 2047 "Q") the way
// qp_encode() does, for headers like the old outmail sent.
func qEncode(s []byte, charset string) string {
	const hex = "0123456789ABCDEF"
	var sb strings.Builder
	sb.WriteString("=?" + charset + "?Q?")
	for _, c := range s {
		switch {
		case c == ' ':
			sb.WriteByte('_')
		case c >= 0x7f || c < 0x20 || c == '_' || strings.IndexByte(mimeSpecials, c) >= 0:
			sb.WriteByte('=')
			sb.WriteByte(hex[c>>4])
			sb.WriteByte(hex[c&0x0f])
		default:
			sb.WriteByte(c)
		}
	}
	sb.WriteString("?=")
	return sb.String()
}

// encodePlainHeader gives s (in an 8-bit charset) as the old outmail did:
// as it is if it's ASCII, otherwise as one encoded word.
func encodePlainHeader(s []byte, charset string) string {
	for _, c := range s {
		if c >= 0x80 {
			return qEncode(s, charset)
		}
	}
	return string(s)
}

func charsetName(big5 bool) string {
	if big5 {
		return "big5"
	}
	return "utf-8"
}

// writePlainBody writes content with every line (LF, CR or CRLF) ended by
// CRLF and without NUL, as the old outmail did. Dot-stuffing is done by
// the SMTP client.
func writePlainBody(w io.Writer, content []byte) error {
	out := make([]byte, 0, len(content)+len(content)/32+2)
	cr := false
	for _, c := range content {
		if cr && c == '\n' {
			cr = false
			continue
		}
		cr = c == '\r'
		switch c {
		case 0:
		case '\r', '\n':
			out = append(out, '\r', '\n')
		default:
			out = append(out, c)
		}
	}
	_, err := w.Write(out)
	return err
}

// composePlain writes a BBS article as the old outmail did, all in the BBS
// encodings (Big5 on most sites): subject (in UTF-8, as everywhere inside
// the service) is turned back into the memory encoding, and content, in
// the storage encoding, is sent as it is.
func (o *outbox) composePlain(w io.Writer, from, rcpt, subject string, content []byte) error {
	c := o.s.codec
	mbCharset := charsetName(c.mbBig5)
	sender := o.sender(from)
	var id [4]byte
	rand.Read(id[:])

	fmt.Fprintf(w, "From: %s <%s>\r\n", sender, sender)
	fmt.Fprintf(w, "To: %s\r\n", rcpt)
	fmt.Fprintf(w, "Subject: %s\r\n", encodePlainHeader(c.toMB(subject), mbCharset))
	fmt.Fprintf(w, "X-Sender: outmail of pttbbs (mail.svc)\r\n")
	fmt.Fprintf(w, "Mime-Version: 1.0\r\n")
	fmt.Fprintf(w, "Content-Type: text/plain; charset=\"%s\"\r\n", charsetName(c.storageBig5))
	fmt.Fprintf(w, "Content-Transfer-Encoding: 8bit\r\n")
	fmt.Fprintf(w, "Message-Id: <%d.%x.outmail@%s>\r\n", time.Now().Unix(),
		binary.BigEndian.Uint32(id[:]), o.s.cfg.Hostname)
	fmt.Fprintf(w, "X-Disclaimer: %s\r\n", o.disclaimer())
	fmt.Fprintf(w, "\r\n")
	return writePlainBody(w, content)
}
