package daemon

import (
	"fmt"
	"log"
	"strings"
	"time"
)

const (
	statusOK     = "OK"
	statusReject = "REJECT"
	statusErr    = "ERR"
)

// prettySubject replaces the subjects of bounces with a friendly one.
func prettySubject(subject string) string {
	table := []struct{ prefix, title string }{
		{"Undelivered Mail Returned to Sender", "<對方退信通知>"},
		{"Returned mail: see transcript for details", "<對方退信通知>"},
	}
	for _, t := range table {
		if strings.HasPrefix(subject, t.prefix) {
			return t.title
		}
	}
	return subject
}

// ownerOf gives fileheader_t.owner for sender, as the old bbsmail did: the
// part before the first ' ', '.' or '@', with a '.' to tell it's not a
// local user, in IDLEN + 1 bytes.
func ownerOf(sender string) string {
	s := strings.TrimLeft(sender, " .@")
	if s != "" {
		if i := strings.IndexAny(s, " .@"); i >= 0 {
			s = s[:i]
		}
		s += "."
		if len(s) > idLen+1 {
			s = s[:idLen] + "."
		}
	} else if len(sender) > idLen+1 {
		s = sender[:idLen+1]
	} else {
		s = sender
	}
	return s
}

// decodeMail decodes incoming mails (replaced in tests).
var decodeMail = parseMail

// receiverID gives the BBS user id in "userid" or "userid.bbs".
func receiverID(receiver string) string {
	receiver = strings.TrimLeft(receiver, ".")
	if i := strings.IndexByte(receiver, '.'); i >= 0 {
		receiver = receiver[:i]
	}
	return receiver
}

// mailHeader is the header of mails in the BBS.
func mailHeader(author, title string, t time.Time) string {
	return "作者: " + author + "\n標題: " + title + "\n時間: " +
		t.Format("Mon Jan _2 15:04:05 2006") + "\n\n"
}

// deliver puts a mail from the Internet in the mailbox of receiver.
func (s *Server) deliver(receiver string, raw []byte) (string, string) {
	userid := receiverID(receiver)
	uid, registered, rejects := s.bbs.LoadMailUser(userid)
	if uid < 1 {
		msg := fmt.Sprintf("BBS user <%s> not existed", userid)
		s.mailog(msg)
		return statusReject, msg
	}
	if rejects { // 不接受站外信
		return statusReject, "rejects outside mails"
	}

	// A mail that can't be decoded is still delivered, as it is.
	m, ok := safely("decoding a mail to "+registered, func() parsedMail {
		return decodeMail(raw)
	})
	if !ok {
		m, ok = safely("decoding a mail to "+registered+" (legacy)", func() parsedMail {
			return parseLegacy(raw)
		})
	}
	if !ok {
		m = parsedMail{Body: normalizeBody(utf8OrBig5(raw))}
	}
	sender := m.Sender
	if !strings.Contains(sender, "@") { // 由 local host 寄信
		sender += "@" + s.cfg.Hostname
	}
	title := m.Subject
	if title == "" {
		r := []rune(sender)
		if len(r) > 64 {
			r = r[:64]
		}
		title = "來自 " + string(r)
	}
	// The title in the file is cut as in the fileheader.
	title = s.codec.truncate(title, ttLen)

	content := s.codec.toStorage(mailHeader(sender, title, time.Now()) + m.Body)
	owner := s.codec.truncateMB(s.codec.toMB(ownerOf(sender)), ownerLen)
	fhTitle := s.codec.truncateMB(s.codec.toMB(prettySubject(title)), ttLen)
	if err := s.bbs.DeliverHomeMail(registered, owner, fhTitle, content); err != nil {
		log.Printf("[mail.svc] Deliver %s => %s: %v", sender, registered, err)
		return statusErr, "cannot deliver"
	}
	s.mailog(fmt.Sprintf("%s => %s", sender, registered))
	return statusOK, ""
}

// notify tells a BBS user something in a BBS mail from SYSOP.
func (s *Server) notify(userid, title, body string) error {
	uid, registered, _ := s.bbs.LoadMailUser(userid)
	if uid < 1 {
		return fmt.Errorf("no such user %s", userid)
	}
	title = s.codec.truncate(title, ttLen)
	content := s.codec.toStorage(mailHeader(sysopOwner+" (系統通知)", title,
		time.Now()) + body)
	return s.bbs.DeliverHomeMail(registered, []byte(sysopOwner),
		s.codec.toMB(title), content)
}
