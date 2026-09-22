#include "bbs.h"

int (*convert_write)(VBUF *v, char c) = vbuf_add;
int (*convert_read)(VBUF *v, const void *buf, size_t len) = vbuf_putblk;
ConvertMode convert_mode = CONV_NORMAL;

int
convert_write_utf8(VBUF *v, char c) {
    static union {
        char c[2];
        uint16_t u;
    } trail = { .u = 0, };

    // trail must be little endian.
    if (trail.c[1]) {
        utf8_ctx ctx;
        int len, i;
        uint16_t ucs;

        trail.c[0] = c;
        ucs = b2u_table[trail.u];

        utf8_init(&ctx);
        len = utf8_from_ucs(&ctx, ucs);

        // assert(len > 0 && len < 4);
        for (i = 0; i < len; i++)
            vbuf_add(v, (char)ctx.buf[i]);

        trail.c[1] = 0;
        return 1;
    }

    if (isascii(c)) {
        vbuf_add(v, c);
        return 1;
    }

    trail.c[1] = c;
    return 0;
}

int convert_read_utf8(VBUF *v, const void *buf, size_t len) {
    static utf8_ctx ctx;
    uint8_t c;
    int written = 0;

    while (len-- > 0) {
        c = *(uint8_t*)buf ++;
        if (isascii(c)) {
            utf8_reset(&ctx);
            vbuf_add(v, c);
            written++;
        } else if (utf8_add_byte(&ctx, c)) {
            int ucs = utf8_get_ucs(&ctx);
            uint16_t b5 = (ucs >= 0 && ucs < 0x10000) ? u2b_table[ucs] : 0;
            if (b5 == 0)
                b5 = '?';
            if (b5 > 0xFF) {
                vbuf_add(v, (char)(b5 >> 8));
                vbuf_add(v, (char)(b5 & 0xFF));
                written += 2;
            } else {
                vbuf_add(v, (char)b5);
                written++;
            }
        }
    }
    return written;
}

void set_converting_type(ConvertMode mode)
{
    switch(mode) {
        case CONV_NORMAL:
            convert_read = vbuf_putblk;
            convert_write = vbuf_add;
            break;

        case CONV_UTF8:
            convert_read = convert_read_utf8;
            convert_write = convert_write_utf8;
            break;
    }
    convert_mode = mode;
}

void init_convert()
{
    // nothing now.
}

