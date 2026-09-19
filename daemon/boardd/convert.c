#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <event2/buffer.h>

#include "cmbbs.h"
#include "convert.h"

#define ESC_STR "\x1b"
#ifndef WORKAROUND_CJKWIDTH
#define WORKAROUND_CJKWIDTH (0)
#endif

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

// Make ANSI control code with SGR 66 prefix for dual-color characters
//   fg, bg, bright are the original color code (eg. 30, 42, 1)
//   provide -1 means no change
static void
make_sgr66_ansi_ctrl(char *buf, int size, int fg, int bg, int bright)
{
    strncpy(buf, "\033[66", size);
    size -= move_string_end(&buf);
    if (bright >= 0) {
	snprintf(buf, size, ";%d", bright);
	size -= move_string_end(&buf);
    }
    if (fg >= 0) {
	snprintf(buf, size, ";%d", fg);
	size -= move_string_end(&buf);
    }
    if (bg >= 0) {
	snprintf(buf, size, ";%d", bg);
	size -= move_string_end(&buf);
    }
    snprintf(buf, size, "m");
}

static int
evbuffer_add_sgr66_escape_code(struct evbuffer *destination, int fg, int bg, int bright)
{
    char ansicode[24];
    make_sgr66_ansi_ctrl(ansicode, sizeof(ansicode), fg, bg, bright);
    return evbuffer_add(destination, ansicode, strlen(ansicode));
}

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
		while (1) {
		    int v = (int) strtol((char *)p, (char **)&p, 10);
		    if (*p != 'm' && *p != ';')
			break;

		    if (v == 0) {
			bright = 0;
			fg = -1;
			bg = -1;
		    }
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

	    // n may be changed, check again
	    if (n < 2)
		break;

	    // Output SGR 66 control code before the UTF-8 character
	    if (fg >= 0 || bg >= 0 || bright >= 0) {
		int dlen = evbuffer_add_sgr66_escape_code(destination, fg, bg, bright);
		if (dlen < 0)
		    break;
		out += dlen;
	    }

	    uint8_t utf8[4];
            int b5c = c[0] << 8 | c[1];
            bool need_jump = false;

            if (WORKAROUND_CJKWIDTH && b2u_ambiguous_width[b5c]) {
                need_jump = true;
                // Keep cursor position
                const char *DECSC = ESC_STR "7";
                if (evbuffer_add(destination, DECSC, strlen(DECSC)))
                    break;
            }

	    int len = ucs2utf(b2u_table[b5c], utf8);
	    utf8[len] = 0;

	    if (evbuffer_add(destination, utf8, len) < 0)
		break;

            if (need_jump) {
                // Print 1 space (to clear the remaining byte),
                // restore cursor and the move to 2 bytes right.
                const char *SPACE_DECRC_CUF2 = " " ESC_STR "8" ESC_STR "[2C";
                if (evbuffer_add(destination, SPACE_DECRC_CUF2, strlen(SPACE_DECRC_CUF2)))
                    break;
            }

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
