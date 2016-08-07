#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <event2/buffer.h>

#include "cmbbs.h"
#include "convert.h"

static int
move_string_end(char **buf)
{
    int n = 0;
    while (**buf != '\0') {
	(*buf)++;
	n++;
    }
    return n;
}

// Make ANSI control code
//   fg, bg, bright are the original color code (eg. 30, 42, 1)
//   provide -1 means no change
//   all -1 means reset
static void
make_ansi_ctrl(char *buf, int size, int fg, int bg, int bright)
{
    int sep = 0;
    strncpy(buf, "\033[", size);
    size -= move_string_end(&buf);
    if (bright >= 0) {
	snprintf(buf, size, "%s%d", sep ? ";" : "", bright);
	size -= move_string_end(&buf);
	sep = 1;
    }
    if (fg >= 0) {
	snprintf(buf, size, "%s%d", sep ? ";" : "", fg);
	size -= move_string_end(&buf);
	sep = 1;
    }
    if (bg >= 0) {
	snprintf(buf, size, "%s%d", sep ? ";" : "", bg);
	size -= move_string_end(&buf);
	sep = 1;
    }
    snprintf(buf, size, "m");
}

static int
evbuffer_add_ansi_escape_code(struct evbuffer *destination, int fg, int bg, int bright)
{
    char ansicode[16];
    make_ansi_ctrl(ansicode, sizeof(ansicode), fg, bg, bright);
    return evbuffer_add_printf(destination, ansicode, strlen(ansicode));
}

#ifdef EXTENDED_INCHAR_ANSI
// Make extended ANSI control code
//   1 ==> 111, 0 ==> 110,
//   3x ==> 13x, 4y ==> 14y.
//   provide -1 means no change
//   all -1 means reset
static void
make_ext_ansi_ctrl(char *buf, int size, int fg, int bg, int bright)
{
    make_ansi_ctrl(buf, size,
                   fg >= 0 ? 100 + fg : fg,
                   bg >= 0 ? 100 + bg : bg,
                   bright >= 0 ? 110 + bright : bright);
}

static int
evbuffer_add_ext_ansi_escape_code(struct evbuffer *destination, int fg, int bg, int bright)
{
    char ansicode[24];
    make_ext_ansi_ctrl(ansicode, sizeof(ansicode), fg, bg, bright);
    return evbuffer_add_printf(destination, ansicode, strlen(ansicode));
}
#endif

// Converts given evbuffer contents to UTF-8 and returns the new buffer.
// The original buffer is freed. Returns NULL on error

struct evbuffer *
evbuffer_b2u(struct evbuffer *source)
{
    unsigned char c[16];
    int out = 0;

    if (evbuffer_get_length(source) == 0)
	return source;

    struct evbuffer *destination = evbuffer_new();

    // Peek at first byte
    while (evbuffer_copyout(source, c, 1) > 0) {
	if (isascii(c[0])) {
	    if (evbuffer_add(destination, c, 1) < 0)
		break;

	    // Remove byte from source buffer
	    evbuffer_drain(source, 1);
	    out++;
	} else {
	    // Big5
	    int todrain = 2;

	    // Handle in-character colors
	    int fg = -1, bg = -1, bright = -1;
	    int n = evbuffer_copyout(source, c, sizeof(c));
	    if (n < 2)
		break;
	    while (c[1] == '\033') {
		c[n - 1] = '\0';

		// At least have \033[m
		if (n < 4 || c[2] != '[')
		    break;

		unsigned char *p = c + 3;
		if (*p == 'm') {
		    // ANSI reset
		    fg = 7;
		    bg = 0;
		    bright = 0;
		}
		while (1) {
		    int v = (int) strtol((char *)p, (char **)&p, 10);
		    if (*p != 'm' && *p != ';')
			break;

		    if (v == 0)
			bright = 0;
		    else if (v == 1)
			bright = 1;
		    else if (v >= 30 && v <= 37)
			fg = v;
		    else if (v >= 40 && v <= 47)
			bg = v;

		    if (*p == 'm')
			break;
		    p++;
		}
		if (*p != 'm') {
		    // Skip malicious or unsupported codes
		    fg = bg = bright = -1;
		    break;
		} else {
		    evbuffer_drain(source, p - c + 1);
		    todrain = 1; // We keep a byte on buffer, so fix offset
		    n = evbuffer_copyout(source, c + 1, sizeof(c) - 1);
		    if (n < 1)
			break;
		    n++;
		}
	    }
#ifdef EXTENDED_INCHAR_ANSI
	    // Output control codes before the Big5 character
	    if (fg >= 0 || bg >= 0 || bright >= 0) {
                int dlen = evbuffer_add_ext_ansi_escape_code(destination, fg, bg, bright);
                if (dlen < 0)
                    break;
                out += dlen;
	    }
#endif

	    // n may be changed, check again
	    if (n < 2)
		break;

	    uint8_t utf8[4];
	    int len = ucs2utf(b2u_table[c[0] << 8 | c[1]], utf8);
	    utf8[len] = 0;

	    if (evbuffer_add(destination, utf8, len) < 0)
		break;

#ifndef EXTENDED_INCHAR_ANSI
            // Output in-char control codes to make state consistent
            if (fg >= 0 || bg >= 0 || bright >= 0) {
                int dlen = evbuffer_add_ansi_escape_code(destination, fg, bg, bright);
                if (dlen < 0)
                    break;
                out += dlen;
            }
#endif

	    // Remove DBCS character from source buffer
	    evbuffer_drain(source, todrain);
	    out += len;
	}
    }

    if (evbuffer_get_length(source) == 0 && out) {
	// Success
	evbuffer_free(source);
	return destination;
    }

    // Fail
    evbuffer_free(source);
    evbuffer_free(destination);
    return NULL;
}
