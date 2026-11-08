package daemon

import (
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
	"time"

	"pttbbs/big5uao"
)

func writeConf(t *testing.T, s *Server, content string) {
	t.Helper()
	if err := os.WriteFile(s.cfg.ConfFile, []byte(content), 0644); err != nil {
		t.Fatal(err)
	}
	// Make sure a rewrite within the same second is seen as a change.
	future := time.Now().Add(time.Duration(len(content)) * time.Second)
	os.Chtimes(s.cfg.ConfFile, future, future)
}

func TestPlainList(t *testing.T) {
	s, _ := newTestServer(t, "")
	if s.cfg.ConfFile != filepath.Join(s.cfg.BBSHome, "etc", "mail.svc.conf") {
		t.Errorf("ConfFile = %q", s.cfg.ConfFile)
	}
	if s.plain.match("a@ptt.cc") {
		t.Errorf("matched without a list")
	}

	writeConf(t, s, "# BBSes\n\nsmtp = 127.0.0.1:25\n  plain = @PTT.cc \n"+
		"PLAIN ptt2.cc\nplain = .example.net, someone@gmail.com\n")
	for addr, want := range map[string]bool{
		"user.bbs@ptt.cc":     true,
		"User.BBS@Ptt.CC":     true,
		"user.bbs@x.ptt.cc":   false,
		"user.bbs@ptt2.cc":    true,
		"a@mail.example.net":  true,
		"a@example.net":       false,
		"someone@gmail.com":   true,
		"someone2@gmail.com":  false,
		"a@notptt.cc":         false,
		"ptt.cc@elsewhere.cc": false,
	} {
		if got := s.plain.match(addr); got != want {
			t.Errorf("match(%q) = %v, want %v", addr, got, want)
		}
	}

	// Read again when changed
	writeConf(t, s, "plain = @ptt2.cc\n")
	if s.plain.match("a@ptt.cc") || !s.plain.match("a@ptt2.cc") {
		t.Errorf("list not reloaded")
	}

	writeConf(t, s, "plain = .bbs@ bot@\n")
	for addr, want := range map[string]bool{
		"user.bbs@ptt.cc":       true,
		"User.BBS@other.org":    true,
		"user.bbsx@ptt.cc":      false,
		"userbbs@ptt.cc":        false,
		"bot@ptt.cc":            true,
		"robot@ptt.cc":          false,
		"a@user.bbs.example.cc": false,
	} {
		if got := s.plain.match(addr); got != want {
			t.Errorf("match(%q) = %v, want %v", addr, got, want)
		}
	}
	os.Remove(s.cfg.ConfFile)
	if s.plain.match("a@ptt2.cc") {
		t.Errorf("list kept after removed")
	}
}

func TestParseConfig(t *testing.T) {
	path := filepath.Join(t.TempDir(), "mail.svc.conf")
	os.WriteFile(path, []byte("# comment\n; comment\n\nZip_Limit = 10\nsmtp \"relay:25\"\n"+
		"v\nplain=a@b, c@d\n"), 0644)
	conf, err := ParseConfig(path)
	if err != nil {
		t.Fatal(err)
	}
	want := []ConfigEntry{{"zip-limit", "10"}, {"smtp", "relay:25"}, {"v", ""}, {"plain", "a@b, c@d"}}
	if len(conf) != len(want) {
		t.Fatalf("got %+v", conf)
	}
	for i := range want {
		if conf[i] != want[i] {
			t.Errorf("entry %d = %+v, want %+v", i, conf[i], want[i])
		}
	}
	if got := strings.Join(plainPatterns(conf), " "); got != "a@b c@d" {
		t.Errorf("patterns %q", got)
	}
}

func TestQEncode(t *testing.T) {
	// Same as qp_encode(..., "big5") of libcmsys
	got := encodePlainHeader(big5uao.Encode("Re: 測試_a.b"), "big5")
	if want := "=?big5?Q?Re=3A_=B4=FA=B8=D5=5Fa=2Eb?="; got != want {
		t.Errorf("got %q, want %q", got, want)
	}
	if got := encodePlainHeader([]byte("Re: hello"), "big5"); got != "Re: hello" {
		t.Errorf("ASCII encoded: %q", got)
	}
}

var reMessageID = regexp.MustCompile(`^<\d+\.[0-9a-f]+\.outmail@bbs\.example\.org>$`)

func TestSendPlain(t *testing.T) {
	relay := newFakeRelay(t)
	s, _ := newTestServer(t, relay.addr())
	writeConf(t, s, "plain = @ptt.cc\n")

	content := append(big5uao.Encode("\x1b[1;31m紅字\x1b[m\n.dot\r\nCR\rNUL"),
		[]byte("\x00x\nlast")...)
	title := big5uao.Encode("轉寄: 測試")
	if err := s.out.send("Tester", "Friend.bbs@ptt.cc", title, content); err != nil {
		t.Fatal(err)
	}
	if err := s.out.send("Tester", "friend@example.com", title, content); err != nil {
		t.Fatal(err)
	}
	if n := s.out.flush(); n != 2 {
		t.Fatalf("flush sent %d", n)
	}

	var plain, multi string
	for _, m := range relay.msgs {
		if m.from != "Tester.bbs@bbs.example.org" {
			t.Errorf("envelope from %s", m.from)
		}
		switch m.rcpt {
		case "Friend.bbs@ptt.cc":
			plain = m.data
		case "friend@example.com":
			multi = m.data
		}
	}
	if !strings.Contains(multi, "multipart/alternative") {
		t.Errorf("not in the plain list but got %q", multi)
	}

	head, body, ok := strings.Cut(plain, "\r\n\r\n")
	if !ok {
		t.Fatalf("no header end in %q", plain)
	}
	lines := strings.Split(head, "\r\n")
	want := []string{
		"From: Tester.bbs@bbs.example.org <Tester.bbs@bbs.example.org>",
		"To: Friend.bbs@ptt.cc",
		"Subject: =?big5?Q?=C2=E0=B1H=3A_=B4=FA=B8=D5?=",
		"X-Sender: outmail of pttbbs (mail.svc)",
		"Mime-Version: 1.0",
		`Content-Type: text/plain; charset="big5"`,
		"Content-Transfer-Encoding: 8bit",
		"Message-Id: ",
		"X-Disclaimer: bbs.example.org is not responsible for the content of this mail.",
	}
	if len(lines) != len(want) {
		t.Fatalf("headers:\n%s", head)
	}
	for i, l := range lines {
		if w := want[i]; w == "Message-Id: " {
			if id, ok := strings.CutPrefix(l, w); !ok || !reMessageID.MatchString(id) {
				t.Errorf("header %d = %q", i, l)
			}
		} else if l != w {
			t.Errorf("header %d = %q, want %q", i, l, w)
		}
	}
	// As it is on disk, lines ended by CRLF and without NUL
	wantBody := string(big5uao.Encode("\x1b[1;31m紅字\x1b[m\r\n.dot\r\nCR\r\nNULx\r\nlast\r\n"))
	if body != wantBody {
		t.Errorf("body = %q, want %q", body, wantBody)
	}
}

func TestComposePlainUTF8(t *testing.T) {
	s, _ := newTestServer(t, "")
	s.codec = codec{}
	var b strings.Builder
	s.out.composePlain(&b, "Tester", "a@ptt.cc", "測試", []byte("內文\n"))
	got := b.String()
	for _, w := range []string{
		"Subject: =?utf-8?Q?=E6=B8=AC=E8=A9=A6?=\r\n",
		`Content-Type: text/plain; charset="utf-8"` + "\r\n",
		"\r\n\r\n內文\r\n",
	} {
		if !strings.Contains(got, w) {
			t.Errorf("no %q in %q", w, got)
		}
	}
}
