package daemon

import (
	"encoding/base64"
	"strings"
	"testing"

	"pttbbs/big5uao"
)

func b64(s string) string { return base64.StdEncoding.EncodeToString([]byte(s)) }

func TestParseMail(t *testing.T) {
	big5Subject := string(big5uao.Encode("中文標題"))
	big5Body := string(big5uao.Encode("這是 Big5 內文\n"))

	tests := []struct {
		name      string
		raw       string
		sender    string
		subject   string
		body      string // must be in the body
		notInBody string // must not be in the body
	}{
		{
			name: "gmail multipart/alternative",
			raw: "From: =?UTF-8?B?" + b64("王小明") + "?= <ming@gmail.com>\r\n" +
				"Subject: =?UTF-8?B?" + b64("你好嗎") + "?=\r\n" +
				"MIME-Version: 1.0\r\n" +
				"Content-Type: multipart/alternative; boundary=\"XX\"\r\n\r\n" +
				"--XX\r\nContent-Type: text/plain; charset=\"UTF-8\"\r\n" +
				"Content-Transfer-Encoding: base64\r\n\r\n" +
				b64("純文字內容\r\n第二行\r\n") + "\r\n" +
				"--XX\r\nContent-Type: text/html; charset=\"UTF-8\"\r\n\r\n" +
				"<div>HTML 內容</div>\r\n--XX--\r\n",
			sender:    "ming@gmail.com (王小明)",
			subject:   "你好嗎",
			body:      "純文字內容\n第二行\n",
			notInBody: "HTML",
		},
		{
			name: "quoted-printable latin1",
			raw: "From: <a@example.com>\nSubject: =?iso-8859-1?q?caf=E9?=\n" +
				"Content-Type: text/plain; charset=iso-8859-1\n" +
				"Content-Transfer-Encoding: quoted-printable\n\n" +
				"caf=E9 au lait=\nsoft break\n",
			sender:  "a@example.com (unknown)",
			subject: "café",
			body:    "café au laitsoft break\n",
		},
		{
			name: "legacy raw big5",
			raw: "From: user.bbs@ptt.cc\nSubject: " + big5Subject + "\n\n" +
				big5Body,
			sender:  "user.bbs@ptt.cc",
			subject: "中文標題",
			body:    "這是 Big5 內文\n",
		},
		{
			name:    "mbox envelope only",
			raw:     "From someone@example.org Fri Nov  6 12:00:00 2026\nSubject: hi\n\nbody\n",
			sender:  "someone@example.org",
			subject: "hi",
			body:    "body\n",
		},
		{
			name: "html only",
			raw: "From: a@b.c\nSubject: x\nContent-Type: text/html; charset=utf-8\n\n" +
				"<html><head><style>p{}</style></head><body><p>第一段</p>" +
				"<p>A&amp;B<br>next</p><script>evil()</script></body></html>",
			sender:    "a@b.c",
			body:      "第一段\nA&B\nnext\n",
			notInBody: "evil",
		},
		{
			name: "mixed with attachment",
			raw: "From: a@b.c\nSubject: file\nContent-Type: multipart/mixed; boundary=B\n\n" +
				"--B\nContent-Type: text/plain; charset=utf-8\n\nsee attached\n" +
				"--B\nContent-Type: application/pdf; name=\"=?UTF-8?B?" + b64("報告.pdf") + "?=\"\n" +
				"Content-Disposition: attachment; filename=\"report.pdf\"\n" +
				"Content-Transfer-Encoding: base64\n\nJVBERi0xLjQK\n--B--\n",
			sender:    "a@b.c",
			subject:   "file",
			body:      "see attached\n\n※ 已略過的附件: report.pdf (application/pdf)\n",
			notInBody: "JVBER",
		},
		{
			name: "bounce report",
			raw: "From: MAILER-DAEMON@mx.example.com (Mail Delivery System)\n" +
				"Subject: Undelivered Mail Returned to Sender\n" +
				"Content-Type: multipart/report; report-type=delivery-status; boundary=R\n\n" +
				"--R\nContent-Type: text/plain\n\nI'm sorry to have to inform you\n" +
				"--R\nContent-Type: message/delivery-status\n\nStatus: 5.1.1\n" +
				"--R\nContent-Type: message/rfc822\n\nFrom: me@ptt.cc\nSubject: orig\n\noriginal body\n" +
				"--R--\n",
			sender:  "MAILER-DAEMON@mx.example.com (Mail Delivery System)",
			subject: "Undelivered Mail Returned to Sender",
			body:    "Status: 5.1.1\n\n----- 原始信件 -----\nFrom: me@ptt.cc\nSubject: orig\n\noriginal body\n",
		},
		{
			name:    "malformed header",
			raw:     "From: a@b.c\nthis is not a header\nSubject: s\n\nbody\n",
			sender:  "a@b.c",
			subject: "s",
			body:    "body\n",
		},
		{
			name:    "control characters in headers",
			raw:     "From: a@b.c\nSubject: =?UTF-8?B?" + b64("\x1b[1;31mred\x1b[m\ttab") + "?=\n\nbody\n",
			sender:  "a@b.c",
			subject: "red tab",
			body:    "body\n",
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			m := parseMail([]byte(tc.raw))
			if m.Sender != tc.sender {
				t.Errorf("sender = %q, want %q", m.Sender, tc.sender)
			}
			if tc.subject != "" && m.Subject != tc.subject {
				t.Errorf("subject = %q, want %q", m.Subject, tc.subject)
			}
			if !strings.Contains(m.Body, tc.body) {
				t.Errorf("body = %q, want %q in it", m.Body, tc.body)
			}
			if tc.notInBody != "" && strings.Contains(m.Body, tc.notInBody) {
				t.Errorf("body = %q, must not have %q", m.Body, tc.notInBody)
			}
		})
	}
}

func TestOwnerOf(t *testing.T) {
	tests := map[string]string{
		"ming@gmail.com (王小明)":        "ming.",
		"user.bbs@ptt.cc":             "user.",
		"averyveryverylongname@x.com": "averyveryver.",
		"@@@":                         "@@@",
		"first last <a@b>":            "first.",
	}
	for in, want := range tests {
		if got := ownerOf(in); got != want {
			t.Errorf("ownerOf(%q) = %q, want %q", in, got, want)
		}
	}
}

func TestCharsets(t *testing.T) {
	tests := []struct {
		cs   string
		in   []byte
		want string
	}{
		{"gb2312", []byte{0xc4, 0xe3, 0xba, 0xc3}, "你好"},
		{"Big5", big5uao.Encode("測試"), "測試"},
		{"utf_8*en", []byte("測試"), "測試"},
		{"", big5uao.Encode("沒有字集"), "沒有字集"},
		{"us-ascii", big5uao.Encode("亂標"), "亂標"},
		{"iso-8859-1", []byte{'c', 'a', 'f', 0xe9}, "café"},
		{"x-unknown", []byte("plain"), "plain"},
	}
	for _, tc := range tests {
		if got := charsetToUTF8(tc.cs, tc.in); got != tc.want {
			t.Errorf("charsetToUTF8(%q) = %q, want %q", tc.cs, got, tc.want)
		}
	}
	c := codec{storageBig5: true, mbBig5: true}
	if got := c.truncateMB(big5uao.Encode("中文字"), 5); len(got) != 4 {
		t.Errorf("truncateMB cut a Big5 character: %q", got)
	}
	c = codec{}
	if got := c.truncate("中文字", 7); got != "中文" {
		t.Errorf("truncate cut a UTF-8 character: %q", got)
	}
}
