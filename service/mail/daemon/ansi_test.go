package daemon

import (
	"strings"
	"testing"
	"unicode/utf8"

	"pttbbs/big5uao"
)

func TestPlainText(t *testing.T) {
	in := "\x1b[1;33m標題\x1b[m\r\n\x1b*s 你好\x1b[2J\x07\ttab\n"
	if got := plainText(in); got != "標題\n 你好\ttab\n" {
		t.Errorf("plainText = %q", got)
	}
}

func TestHTMLText(t *testing.T) {
	tests := []struct{ in, want string }{
		{"plain <b>&", "plain &lt;b&gt;&amp;"},
		{"\x1b[31mred\x1b[m", `<span style="color:#800000">red</span>`},
		{"\x1b[1;31mbright\x1b[0m", `<span style="color:#ff0000">bright</span>`},
		{"\x1b[33;44mon blue\x1b[m", `<span style="color:#808000;background-color:#000080">on blue</span>`},
		{"\x1b[7mrev\x1b[m", `<span style="color:#000000;background-color:#c0c0c0">rev</span>`},
		{"\x1b[1;32ma\x1b[;32mb\x1b[37mc", `<span style="color:#00ff00">a</span><span style="color:#008000">b</span>c`},
		{"\x1b[4;31mu\x1b[24mx", `<span style="color:#800000;text-decoration:underline">u</span><span style="color:#800000">x</span>`},
	}
	for _, tc := range tests {
		got := htmlText(tc.in)
		if !strings.Contains(got, ">"+tc.want+"</pre>") {
			t.Errorf("htmlText(%q) = ...%q", tc.in, got[strings.Index(got, "<pre"):])
		}
	}
	// Ambiguous width characters take two columns; CJK and ASCII don't
	// need it.
	if got := htmlText("中▋a—"); !strings.Contains(got, ">中"+wideSpan+"▋</span>a"+wideSpan+"—</span></pre>") {
		t.Errorf("htmlText wide = %q", got)
	}
	if got := htmlText("x"); !strings.Contains(got, `bgcolor="#000000"`) {
		t.Errorf("no black background: %q", got)
	}
}

func TestJoinSplitBig5(t *testing.T) {
	// "紅" with a color change between its two bytes, then "字"
	b := big5uao.Encode("紅字")
	split := append([]byte{b[0]}, []byte("\x1b[1;31m")...)
	split = append(split, b[1:]...)
	got := big5uao.Decode(joinSplitBig5(split))
	if got != "紅\x1b[1;31m字" {
		t.Errorf("joinSplitBig5 = %q", got)
	}
	c := codec{storageBig5: true}
	if got := plainText(c.fromStorageArticle(split)); got != "紅字" {
		t.Errorf("plain = %q", got)
	}
	if got := big5uao.Decode(joinSplitBig5(big5uao.Encode("\x1b[m純中文"))); got != "\x1b[m純中文" {
		t.Errorf("unchanged text = %q", got)
	}
}

// A lone ESC before a non-ASCII character took the first byte of it, and
// the broken UTF-8 crashed htmlText.
func TestLoneEscape(t *testing.T) {
	s := "1490\n\x1b可以開5個Profile\n"
	if got := plainText(s); got != "1490\n可以開5個Profile\n" {
		t.Errorf("plainText = %q", got)
	}
	if got := htmlText(s); !strings.Contains(got, "1490\n可以開5個Profile\n") {
		t.Errorf("htmlText = %q", got)
	}
	// Broken UTF-8 in the input
	if got := htmlText("a\xe5b\x8f▋"); !strings.Contains(got, "a?b?"+wideSpan+"▋</span>") {
		t.Errorf("htmlText broken = %q", got)
	}
}

func FuzzANSI(f *testing.F) {
	for _, s := range []string{"\x1b[1;31m紅\x1b[m", "\x1b可", "\x1b*s", "\x1b[", "\xe5\x1b\x8f▋—"} {
		f.Add([]byte(s))
	}
	c := codec{storageBig5: true, mbBig5: true}
	f.Fuzz(func(t *testing.T, b []byte) {
		for _, s := range []string{string(b), c.fromStorageArticle(b)} {
			if p := plainText(s); !utf8.ValidString(p) {
				t.Fatalf("plainText(%q) = %q, not UTF-8", s, p)
			}
			if h := htmlText(s); !utf8.ValidString(h) {
				t.Fatalf("htmlText(%q) is not UTF-8", s)
			}
		}
	})
}
