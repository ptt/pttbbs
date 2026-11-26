package model

import (
	"math/rand"
	"testing"
)

// TestStripANSIMatchesRegexp checks StripANSI against ansiRegex on
// typical lines and on random strings made of the bytes that matter.
func TestStripANSIMatchesRegexp(t *testing.T) {
	cases := []string{
		"", "plain", "\x1b[1;37m推 \x1b[33muser\x1b[m\x1b[33m: text\x1b[m 07/30 22:12",
		"\x1b[", "\x1b[1;2", "\x1b[1;2;", "\x1b]0;title\x07rest", "\x1b]0;title\x1b\\rest",
		"\x1b]0;no end\nnext", "\x1b]", "\x1b", "a\x1b", "\x1b(B", "\x1b( ", "\x1b\x1b[m",
		"\x1b[?25h", "\x1b[1;37;", "\x1b]\x1b\\", "\x1b] \x1b", "\x1b*\x1b[0m",
	}
	alphabet := []byte("\x1b[];?\x07\\\n 09(/@~mA\x80\xff")
	r := rand.New(rand.NewSource(1))
	for i := 0; i < 200000; i++ {
		b := make([]byte, r.Intn(12))
		for j := range b {
			b[j] = alphabet[r.Intn(len(alphabet))]
		}
		cases = append(cases, string(b))
	}
	for _, s := range cases {
		if got, want := StripANSI(s), ansiRegex.ReplaceAllString(s, ""); got != want {
			t.Fatalf("StripANSI(%q) = %q, want %q", s, got, want)
		}
	}
}
