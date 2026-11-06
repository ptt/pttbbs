package daemon

// MIME decoding of incoming mails: turns a mail into a sender, a subject
// and a plain text body, all in UTF-8.

import (
	"bufio"
	"bytes"
	"encoding/base64"
	"fmt"
	"html"
	"io"
	"mime"
	"mime/multipart"
	"mime/quotedprintable"
	"net/mail"
	"net/textproto"
	"regexp"
	"strings"
	"unicode/utf8"

	"golang.org/x/text/encoding/htmlindex"

	"pttbbs/big5uao"
)

const (
	maxMIMEDepth = 8   // nested multiparts
	maxMIMEParts = 100 // parts in one multipart
)

type parsedMail struct {
	Sender  string
	Subject string
	Body    string
}

// charsetToUTF8 converts b in charset cs to UTF-8. Text without a charset
// (or claiming to be ASCII or UTF-8) that is not UTF-8 is taken as Big5,
// which is what local mails and old mail programs give.
func charsetToUTF8(cs string, b []byte) string {
	if i := strings.IndexByte(cs, '*'); i >= 0 { // RFC 2231 language
		cs = cs[:i]
	}
	key := strings.ToLower(strings.NewReplacer("-", "", "_", "").Replace(
		strings.TrimSpace(cs)))
	switch key {
	case "big5", "cp950", "xxbig5", "big5uao", "big5hkscs":
		return big5uao.Decode(b)
	case "", "usascii", "ascii", "utf8":
		return utf8OrBig5(b)
	}
	if enc, err := htmlindex.Get(cs); err == nil {
		if d, err := enc.NewDecoder().Bytes(b); err == nil {
			return strings.ToValidUTF8(string(d), "?")
		}
	}
	return utf8OrBig5(b)
}

func utf8OrBig5(b []byte) string {
	if utf8.Valid(b) {
		return string(b)
	}
	return big5uao.Decode(b)
}

var wordDecoder = &mime.WordDecoder{
	CharsetReader: func(cs string, r io.Reader) (io.Reader, error) {
		b, err := io.ReadAll(r)
		if err != nil {
			return nil, err
		}
		return strings.NewReader(charsetToUTF8(cs, b)), nil
	},
}

// decodeHeader decodes the encoded words (RFC 2047) in a header. Raw 8-bit
// text that is not UTF-8 is taken as Big5.
func decodeHeader(s string) string {
	if !utf8.ValidString(s) {
		s = big5uao.Decode([]byte(s))
	}
	if d, err := wordDecoder.DecodeHeader(s); err == nil {
		s = d
	}
	return s
}

var controlSequence = regexp.MustCompile(`\x1b(\[[0-9;?]*[ -/]*[@-~]|[@-Z\\-_])`)

// sanitizeHeader strips control sequences, and turns other control
// characters into spaces, so the text stays on one line.
func sanitizeHeader(s string) string {
	s = controlSequence.ReplaceAllString(s, "")
	return strings.Map(func(r rune) rune {
		if r < ' ' || r == 0x7f || (r >= 0x80 && r < 0xa0) {
			return ' '
		}
		return r
	}, s)
}

// formatSender formats a From header as the old bbsmail did:
// "addr (name)", "addr (unknown)" for "<addr>", or just "addr".
func formatSender(from string) string {
	from = strings.TrimSpace(from)
	if from == "" {
		return ""
	}
	parser := mail.AddressParser{WordDecoder: wordDecoder}
	if !utf8.ValidString(from) {
		from = big5uao.Decode([]byte(from))
	}
	if a, err := parser.Parse(from); err == nil {
		switch {
		case a.Name != "":
			return fmt.Sprintf("%s (%s)", a.Address, a.Name)
		case strings.Contains(from, "<"):
			return fmt.Sprintf("%s (unknown)", a.Address)
		default:
			return a.Address
		}
	}
	// Not a valid address: do as the old bbsmail did.
	if l := strings.IndexByte(from, '<'); l >= 0 {
		if r := strings.LastIndexByte(from[l:], '>'); r >= 0 {
			name := strings.TrimSpace(decodeHeader(from[:l]))
			if name == "" {
				name = "unknown"
			}
			return fmt.Sprintf("%s (%s)", from[l+1:l+r], name)
		}
	}
	if f := strings.Fields(from); len(f) > 0 {
		return f[0]
	}
	return ""
}

// splitEnvelope removes the "From sender date" line that some MTAs put
// before the headers, and returns its sender.
func splitEnvelope(raw []byte) (string, []byte) {
	if !bytes.HasPrefix(raw, []byte("From ")) {
		return "", raw
	}
	line, rest, _ := bytes.Cut(raw, []byte("\n"))
	f := strings.Fields(string(line))
	if len(f) > 1 {
		return f[1], rest
	}
	return "", rest
}

func parseMail(raw []byte) parsedMail {
	envSender, raw := splitEnvelope(raw)
	var m parsedMail

	msg, err := mail.ReadMessage(bufio.NewReader(bytes.NewReader(raw)))
	if err != nil {
		m = parseLegacy(raw)
	} else {
		from := msg.Header.Get("From")
		m.Sender = formatSender(from)
		m.Subject = strings.TrimSpace(decodeHeader(msg.Header.Get("Subject")))
		var att []string
		m.Body = extractText(textproto.MIMEHeader(msg.Header), msg.Body, 0, &att)
		m.Body = normalizeBody(m.Body)
		if len(att) > 0 {
			m.Body += "\n※ 已略過的附件: " + strings.Join(att, ", ") + "\n"
		}
	}
	if m.Sender == "" {
		m.Sender = envSender
	}
	m.Sender = sanitizeHeader(m.Sender)
	m.Subject = sanitizeHeader(m.Subject)
	return m
}

// parseLegacy handles mails that net/mail can't read, as the old bbsmail
// did: the headers end at the first empty line and the body is kept.
func parseLegacy(raw []byte) parsedMail {
	var m parsedMail
	head, body, found := bytes.Cut(raw, []byte("\n\n"))
	if !found {
		if h, b, ok := bytes.Cut(raw, []byte("\r\n\r\n")); ok {
			head, body = h, b
		} else {
			head, body = raw, nil
		}
	}
	for _, line := range strings.Split(string(head), "\n") {
		line = strings.TrimRight(line, "\r")
		if v, ok := strings.CutPrefix(line, "Subject: "); ok {
			m.Subject = strings.TrimSpace(decodeHeader(v))
		} else if v, ok := strings.CutPrefix(line, "From:"); ok {
			m.Sender = formatSender(v)
		}
	}
	m.Body = normalizeBody(charsetToUTF8("", body))
	return m
}

// decodeTransfer undoes the Content-Transfer-Encoding.
func decodeTransfer(r io.Reader, cte string) io.Reader {
	switch strings.ToLower(strings.TrimSpace(cte)) {
	case "base64":
		return base64.NewDecoder(base64.StdEncoding, &base64Filter{r: r})
	case "quoted-printable":
		return quotedprintable.NewReader(r)
	}
	return r
}

// base64Filter drops what is not base64 (spaces, line breaks) as most
// mail readers do, so a sloppy encoder does not stop the decoding.
type base64Filter struct{ r io.Reader }

func (f *base64Filter) Read(p []byte) (int, error) {
	for {
		n, err := f.r.Read(p)
		j := 0
		for _, c := range p[:n] {
			if c >= 'A' && c <= 'Z' || c >= 'a' && c <= 'z' ||
				c >= '0' && c <= '9' || c == '+' || c == '/' || c == '=' {
				p[j] = c
				j++
			}
		}
		if j > 0 || err != nil {
			return j, err
		}
	}
}

func partName(h textproto.MIMEHeader, params map[string]string) string {
	if _, p, err := mime.ParseMediaType(h.Get("Content-Disposition")); err == nil &&
		p["filename"] != "" {
		return decodeHeader(p["filename"])
	}
	if params["name"] != "" {
		return decodeHeader(params["name"])
	}
	return ""
}

func isAttachment(h textproto.MIMEHeader) bool {
	d, _, err := mime.ParseMediaType(h.Get("Content-Disposition"))
	return err == nil && d == "attachment"
}

// extractText returns the readable text of a part (in UTF-8); parts that
// are not text are skipped and listed in att.
func extractText(h textproto.MIMEHeader, body io.Reader, depth int,
	att *[]string) string {
	mediatype, params, err := mime.ParseMediaType(h.Get("Content-Type"))
	if err != nil || mediatype == "" {
		mediatype, params = "text/plain", map[string]string{}
	}

	if strings.HasPrefix(mediatype, "multipart/") {
		if depth >= maxMIMEDepth || params["boundary"] == "" {
			return ""
		}
		return extractMultipart(mediatype, params["boundary"], body, depth, att)
	}

	r := decodeTransfer(body, h.Get("Content-Transfer-Encoding"))
	if isAttachment(h) && !strings.HasPrefix(mediatype, "message/") {
		*att = append(*att, describePart(h, mediatype, params))
		return ""
	}

	switch {
	case mediatype == "text/html":
		b, _ := io.ReadAll(r)
		return htmlToText(charsetToUTF8(params["charset"], b))
	case mediatype == "message/rfc822" || mediatype == "message/global":
		if depth >= maxMIMEDepth {
			return ""
		}
		return extractMessage(r, depth, att)
	case strings.HasPrefix(mediatype, "text/"),
		mediatype == "message/delivery-status",
		mediatype == "message/global-delivery-status",
		mediatype == "message/rfc822-headers":
		b, _ := io.ReadAll(r)
		return charsetToUTF8(params["charset"], b)
	}
	*att = append(*att, describePart(h, mediatype, params))
	return ""
}

func describePart(h textproto.MIMEHeader, mediatype string,
	params map[string]string) string {
	if name := sanitizeHeader(partName(h, params)); name != "" {
		return fmt.Sprintf("%s (%s)", name, mediatype)
	}
	return mediatype
}

// extractMessage shows a mail inside a mail, like the original mail in a
// bounce.
func extractMessage(r io.Reader, depth int, att *[]string) string {
	msg, err := mail.ReadMessage(bufio.NewReader(r))
	if err != nil {
		return ""
	}
	var sb strings.Builder
	sb.WriteString("----- 原始信件 -----\n")
	for _, k := range []string{"From", "To", "Date", "Subject"} {
		if v := msg.Header.Get(k); v != "" {
			fmt.Fprintf(&sb, "%s: %s\n", k, sanitizeHeader(decodeHeader(v)))
		}
	}
	sb.WriteString("\n")
	sb.WriteString(extractText(textproto.MIMEHeader(msg.Header), msg.Body,
		depth+1, att))
	return sb.String()
}

func extractMultipart(mediatype, boundary string, body io.Reader, depth int,
	att *[]string) string {
	mr := multipart.NewReader(body, boundary)
	var texts []string
	var plain, rich string
	for i := 0; i < maxMIMEParts; i++ {
		p, err := mr.NextRawPart()
		if err != nil {
			break
		}
		if mediatype == "multipart/alternative" {
			// Prefer text/plain; fall back to text/html.
			pt, _, _ := mime.ParseMediaType(p.Header.Get("Content-Type"))
			var sub []string
			t := extractText(p.Header, p, depth+1, &sub)
			switch {
			case pt == "text/plain" || pt == "":
				if plain == "" {
					plain = t
				}
			case rich == "":
				rich = t
			}
			continue
		}
		if t := extractText(p.Header, p, depth+1, att); strings.TrimSpace(t) != "" {
			texts = append(texts, strings.TrimRight(t, "\r\n"))
		}
	}
	if mediatype == "multipart/alternative" {
		if strings.TrimSpace(plain) != "" {
			return plain
		}
		return rich
	}
	if len(texts) == 0 {
		return ""
	}
	// A blank line between the parts
	return strings.Join(texts, "\n\n") + "\n"
}

var (
	reHTMLDrop  = regexp.MustCompile(`(?is)<(script|style|head)\b.*?</(script|style|head)\s*>|<!--.*?-->`)
	reHTMLBreak = regexp.MustCompile(`(?i)<br\s*/?>|</(p|div|tr|li|h[1-6]|table|blockquote)\s*>`)
	reHTMLItem  = regexp.MustCompile(`(?i)<li\b[^>]*>`)
	reHTMLTag   = regexp.MustCompile(`(?s)<[^>]*>`)
	reBlankRuns = regexp.MustCompile(`\n{3,}`)
)

// htmlToText gives a rough text version of a HTML mail.
func htmlToText(s string) string {
	s = reHTMLDrop.ReplaceAllString(s, "")
	s = strings.NewReplacer("\r", "", "\n", " ").Replace(s)
	s = reHTMLBreak.ReplaceAllString(s, "\n")
	s = reHTMLItem.ReplaceAllString(s, "* ")
	s = reHTMLTag.ReplaceAllString(s, "")
	s = html.UnescapeString(s)
	s = strings.ReplaceAll(s, "\u00a0", " ")
	lines := strings.Split(s, "\n")
	for i, l := range lines {
		lines[i] = strings.TrimSpace(l)
	}
	return strings.TrimSpace(reBlankRuns.ReplaceAllString(strings.Join(lines, "\n"), "\n\n")) + "\n"
}

// normalizeBody ends lines with LF and drops NUL.
func normalizeBody(s string) string {
	s = strings.ReplaceAll(s, "\r\n", "\n")
	s = strings.ReplaceAll(s, "\r", "\n")
	s = strings.ReplaceAll(s, "\x00", "")
	if s != "" && !strings.HasSuffix(s, "\n") {
		s += "\n"
	}
	return s
}
