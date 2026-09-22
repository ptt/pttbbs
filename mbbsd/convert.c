#include "bbs.h"


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

        trail.c[0] = c;
        len = utf8_from_ucs(&ctx, b2u_table[trail.u]);
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
    const uint8_t *p = (const uint8_t *)buf;
    int written = 0;

    while (len-- > 0) {
        uint8_t c = *p++;
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

int
convert_write_big5(VBUF *v, char c) {
    static utf8_ctx ctx;
    uint8_t uc = (uint8_t)c;

    if (isascii(uc)) {
        utf8_reset(&ctx);
        vbuf_add(v, c);
        return 1;
    }
    if (!utf8_add_byte(&ctx, uc))
        return 0;
    int ucs = utf8_get_ucs(&ctx);
    uint16_t b5 = (ucs >= 0 && ucs < 0x10000) ? u2b_table[ucs] : 0;
    if (b5 == 0)
        b5 = '?';
    if (b5 > 0xFF) {
        vbuf_add(v, (char)(b5 >> 8));
        vbuf_add(v, (char)(b5 & 0xFF));
    } else {
        vbuf_add(v, (char)b5);
    }
    return 1;
}

int
convert_read_big5(VBUF *v, const void *buf, size_t len) {
    static uint8_t lead = 0;
    utf8_ctx ctx;
    int written = 0;
    const uint8_t *p = (const uint8_t *)buf;

    while (len-- > 0) {
        uint8_t c = *p++;
        if (lead) {
            uint16_t ucs = b2u_table[((uint16_t)lead << 8) | c];
            int ulen = utf8_from_ucs(&ctx, ucs);
            for (int i = 0; i < ulen; i++)
                vbuf_add(v, (char)ctx.buf[i]);
            written += ulen;
            lead = 0;
            continue;
        }
        if (isascii(c)) {
            vbuf_add(v, (char)c);
            written++;
        } else {
            lead = c;
        }
    }
    return written;
}

#if MB_IS_UTF8
int (*convert_write)(VBUF *v, char c) = convert_write_big5;
int (*convert_read)(VBUF *v, const void *buf, size_t len) = convert_read_big5;
#else
int (*convert_write)(VBUF *v, char c) = vbuf_add;
int (*convert_read)(VBUF *v, const void *buf, size_t len) = vbuf_putblk;
#endif
ConvertMode convert_mode = CONV_BIG5;

void set_converting_type(ConvertMode mode)
{
    switch(mode) {
        case CONV_BIG5:
            if (MB_IS_UTF8) {
                convert_read = convert_read_big5;
                convert_write = convert_write_big5;
            } else {
                convert_read = vbuf_putblk;
                convert_write = vbuf_add;
            }
            break;

        case CONV_UTF8:
            if (MB_IS_UTF8) {
                convert_read = vbuf_putblk;
                convert_write = vbuf_add;
            } else {
                convert_read = convert_read_utf8;
                convert_write = convert_write_utf8;
            }
            break;
    }
    convert_mode = mode;
}

void init_convert()
{
    // nothing now.
}

