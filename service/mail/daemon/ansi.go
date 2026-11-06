package daemon

// BBS articles have ANSI sequences for colors. Mails sent out have them
// as plain text (without the sequences) and as HTML (with the colors).

import (
	"fmt"
	"html"
	"strconv"
	"strings"

	"golang.org/x/text/width"
)

// The colors of BBS terminals: 0-7 normal, 8-15 bright (bold).
var ansiPalette = [16]string{
	"#000000", "#800000", "#008000", "#808000",
	"#000080", "#800080", "#008080", "#c0c0c0",
	"#808080", "#ff0000", "#00ff00", "#ffff00",
	"#0000ff", "#ff00ff", "#00ffff", "#ffffff",
}

const (
	defaultFG = 7
	defaultBG = 0
)

// ansiState is the attributes set by SGR (ESC [ ... m).
type ansiState struct {
	fg, bg    int // 0-7
	bold      bool
	underline bool
	reverse   bool
}

func defaultANSI() ansiState { return ansiState{fg: defaultFG, bg: defaultBG} }

func (a *ansiState) apply(params string) {
	if params == "" {
		*a = defaultANSI()
		return
	}
	for _, p := range strings.Split(params, ";") {
		n, err := strconv.Atoi(p)
		if p == "" {
			n, err = 0, nil
		}
		if err != nil {
			continue
		}
		switch {
		case n == 0:
			*a = defaultANSI()
		case n == 1:
			a.bold = true
		case n == 4:
			a.underline = true
		case n == 7:
			a.reverse = true
		case n == 22:
			a.bold = false
		case n == 24:
			a.underline = false
		case n == 27:
			a.reverse = false
		case n >= 30 && n <= 37:
			a.fg = n - 30
		case n == 39:
			a.fg = defaultFG
		case n >= 40 && n <= 47:
			a.bg = n - 40
		case n == 49:
			a.bg = defaultBG
		}
	}
}

// style gives the CSS of the state, or "" for the default.
func (a ansiState) style() string {
	fg, bg := a.fg, a.bg
	if a.bold {
		fg += 8
	}
	if a.reverse {
		fg, bg = bg, fg
	}
	var css []string
	if fg != defaultFG {
		css = append(css, "color:"+ansiPalette[fg])
	}
	if bg != defaultBG {
		css = append(css, "background-color:"+ansiPalette[bg])
	}
	if a.underline {
		css = append(css, "text-decoration:underline")
	}
	return strings.Join(css, ";")
}

// scanANSI splits s into text and SGR parameters. Other escape sequences
// (cursor moves, PTT's "ESC *x" ...) are dropped, and so are control
// characters but tabs and line breaks.
func scanANSI(s string, text func(string), sgr func(string)) {
	s = strings.ReplaceAll(s, "\r\n", "\n")
	start := 0
	flush := func(end int) {
		if end > start {
			text(s[start:end])
		}
	}
	for i := 0; i < len(s); {
		c := s[i]
		switch {
		case c == 0x1b:
			flush(i)
			i++
			if i < len(s) && s[i] == '[' {
				j := i + 1
				for j < len(s) && (s[j] < 0x40 || s[j] > 0x7e) {
					j++
				}
				if j < len(s) && s[j] == 'm' {
					sgr(s[i+1 : j])
				}
				i = j + 1
			} else if i < len(s) {
				i++ // ESC x, including PTT's ESC * x
				if s[i-1] == '*' && i < len(s) {
					i++
				}
			}
			start = i
		case c < ' ' && c != '\n' && c != '\t', c == 0x7f:
			flush(i)
			i++
			start = i
		default:
			i++
		}
	}
	flush(len(s))
}

// plainText makes a BBS article readable in a mail: no ANSI sequences or
// other control characters but tabs and line breaks.
func plainText(s string) string {
	var sb strings.Builder
	scanANSI(s, func(t string) { sb.WriteString(t) }, func(string) {})
	return sb.String()
}

// htmlText renders a BBS article as HTML, with the colors on a black
// background like a BBS terminal.
func htmlText(s string) string {
	var sb strings.Builder
	state := defaultANSI()
	open := "" // the style of the open span
	scanANSI(s, func(t string) {
		if st := state.style(); st != open {
			if open != "" {
				sb.WriteString("</span>")
			}
			if st != "" {
				fmt.Fprintf(&sb, `<span style="%s">`, st)
			}
			open = st
		}
		writeColumns(&sb, t)
	}, func(p string) { state.apply(p) })
	if open != "" {
		sb.WriteString("</span>")
	}

	// Mail programs drop <style> and some ignore the background of <div>,
	// so it is an inline styled table with bgcolor.
	return `<!DOCTYPE html>
<html><head><meta http-equiv="Content-Type" content="text/html; charset=UTF-8"></head>
<body style="margin:0;padding:0">
<table width="100%" cellpadding="12" cellspacing="0" border="0" bgcolor="#000000" style="background-color:#000000"><tr><td>
<pre style="margin:0;color:#c0c0c0;background-color:#000000;font-family:MingLiU,'細明體','Noto Sans Mono CJK TC','Sarasa Mono TC','WenQuanYi Zen Hei Mono',monospace;font-size:15px;line-height:1;white-space:pre-wrap;word-break:break-all">` +
		sb.String() + "</pre>\n</td></tr></table>\n</body></html>\n"
}

// wideSpan keeps a character in two columns, as BBS terminals show the
// characters of Big5. Without it, fonts show the East Asian ambiguous
// width ones (box drawing, blocks, Greek, ...) in one column and break
// the art. There is no CSS to make them wide, so each takes a box of two
// columns ("ch" is the width of a column in monospace fonts).
const wideSpan = `<span style="display:inline-block;width:2ch">`

// isNarrowWide tells if r is in two columns in BBS terminals, but may be
// in one with the fonts of mail programs.
func isNarrowWide(r rune) bool {
	if r < 0x80 {
		return false
	}
	switch width.LookupRune(r).Kind() {
	case width.EastAsianWide, width.EastAsianFullwidth:
		return false
	}
	return true
}

// writeColumns writes text as HTML, with the characters that should be
// in two columns kept so.
func writeColumns(sb *strings.Builder, t string) {
	start := 0
	for i, r := range t {
		if !isNarrowWide(r) {
			continue
		}
		sb.WriteString(html.EscapeString(t[start:i]))
		sb.WriteString(wideSpan)
		sb.WriteRune(r)
		sb.WriteString("</span>")
		start = i + len(string(r))
	}
	sb.WriteString(html.EscapeString(t[start:]))
}

// joinSplitBig5 moves the escape sequences in the middle of Big5
// characters (for two colors in one character) to after them, so the
// characters can be decoded; such a character takes the first color.
func joinSplitBig5(b []byte) []byte {
	if !strings.Contains(string(b), "\x1b") {
		return b
	}
	out := make([]byte, 0, len(b))
	for i := 0; i < len(b); {
		c := b[i]
		if c < 0x81 || c == 0xff {
			out = append(out, c)
			i++
			continue
		}
		// a lead byte: skip the escape sequences after it
		j := i + 1
		for j < len(b) && b[j] == 0x1b {
			k := j + 1
			if k < len(b) && b[k] == '[' {
				k++
				for k < len(b) && (b[k] < 0x40 || b[k] > 0x7e) {
					k++
				}
			}
			j = k + 1
		}
		if j > i+1 && j < len(b) && (b[j] >= 0x40 && b[j] != 0x7f) {
			out = append(out, c, b[j])
			out = append(out, b[i+1:j]...)
			i = j + 1
			continue
		}
		if i+1 < len(b) {
			out = append(out, c, b[i+1])
			i += 2
			continue
		}
		out = append(out, c)
		i++
	}
	return out
}
