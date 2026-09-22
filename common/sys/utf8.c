#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "cmsys.h"

int
utf8_add_byte(utf8_ctx *ctx, unsigned char byte)
{
    if (!ctx)
        return 0;

    if (utf8_is_ready(ctx))
        utf8_reset(ctx);

    if (ctx->need > 0) {
        if ((byte & 0xC0) != 0x80 || ctx->need < 2 || ctx->got == 0 || ctx->got >= ctx->need) {
            utf8_error(ctx);
            return 0;
        }
        ctx->buf[ctx->got++] = byte;
        ctx->ucs = (ctx->ucs << 6) | (byte & 0x3F);
        if (ctx->got == ctx->need) {
            if ((ctx->need == 2 && ctx->ucs < 0x80) ||
                (ctx->need == 3 && (ctx->ucs < 0x800 || (ctx->ucs >= 0xD800 && ctx->ucs <= 0xDFFF))) ||
                (ctx->need == 4 && (ctx->ucs < 0x10000 || ctx->ucs > 0x10FFFF))) {
                utf8_error(ctx);
                return 0;
            }
            ctx->buf[ctx->got] = 0;
            ctx->len = ctx->got;
            return 1;
        }
        return 0;
    }

    if (byte < 0x80) {
        ctx->buf[0] = byte;
        ctx->buf[1] = 0;
        ctx->ucs = byte;
        ctx->need = 1;
        ctx->got = 1;
        ctx->len = 1;
        return 1;
    }

    if (byte < 0xC2 || byte > 0xF4) {
        utf8_error(ctx);
        return 0;
    }

    ctx->buf[0] = byte;
    ctx->got = 1;
    ctx->len = 0;
    if ((byte & 0xE0) == 0xC0) {
        ctx->need = 2;
        ctx->ucs = byte & 0x1F;
    } else if ((byte & 0xF0) == 0xE0) {
        ctx->need = 3;
        ctx->ucs = byte & 0x0F;
    } else {
        ctx->need = 4;
        ctx->ucs = byte & 0x07;
    }
    return 0;
}

int
utf8_from_ucs(utf8_ctx *ctx, int ucs)
{
    if (!ctx)
        return 0;
    if (ucs < 0 || ucs > 0x10FFFF || (ucs >= 0xD800 && ucs <= 0xDFFF)) {
        utf8_reset(ctx);
        return 0;
    }
    ctx->ucs = ucs;
    if (ucs < 0x80) {
        ctx->buf[0] = (uint8_t)ucs;
        ctx->buf[1] = 0;
        ctx->need = ctx->got = ctx->len = 1;
    } else if (ucs < 0x800) {
        ctx->buf[0] = (uint8_t)(0xC0 | (ucs >> 6));
        ctx->buf[1] = (uint8_t)(0x80 | (ucs & 0x3F));
        ctx->buf[2] = 0;
        ctx->need = ctx->got = ctx->len = 2;
    } else if (ucs < 0x10000) {
        ctx->buf[0] = (uint8_t)(0xE0 | (ucs >> 12));
        ctx->buf[1] = (uint8_t)(0x80 | ((ucs >> 6) & 0x3F));
        ctx->buf[2] = (uint8_t)(0x80 | (ucs & 0x3F));
        ctx->buf[3] = 0;
        ctx->need = ctx->got = ctx->len = 3;
    } else {
        ctx->buf[0] = (uint8_t)(0xF0 | (ucs >> 18));
        ctx->buf[1] = (uint8_t)(0x80 | ((ucs >> 12) & 0x3F));
        ctx->buf[2] = (uint8_t)(0x80 | ((ucs >> 6) & 0x3F));
        ctx->buf[3] = (uint8_t)(0x80 | (ucs & 0x3F));
        ctx->buf[4] = 0;
        ctx->need = ctx->got = ctx->len = 4;
    }
    return ctx->len;
}

int
utf8_to_mb(const utf8_ctx *ctx, char *mb)
{
    if (!ctx || ctx->len == 0) {
        if (mb)
            mb[0] = '\0';
        return 0;
    }
    if (mb) {
        memcpy(mb, ctx->buf, ctx->len);
        mb[ctx->len] = '\0';
    }
    return ctx->len;
}

extern const uint16_t u2b_table[];

char *utf8_to_big5_n(const char *utf8, size_t src_len, char *big5, size_t max_len) {
    if (!utf8 || !big5 || max_len == 0) {
        return big5;
    }
    utf8_ctx ctx;
    utf8_init(&ctx);
    const uint8_t *p = (const uint8_t *)utf8;
    const uint8_t *end = (src_len == (size_t)-1) ? (const uint8_t *)-1 : (p + src_len);
    size_t out_idx = 0;
    while (p < end && *p && out_idx < (max_len - 1)) {
        if (!utf8_add_byte(&ctx, *p++)) {
            if (!utf8_pending(&ctx))
                big5[out_idx++] = '?';
            continue;
        }
        int ucs = utf8_get_ucs(&ctx);
        uint16_t b5 = (ucs >= 0 && ucs < 0x10000) ? u2b_table[ucs] : 0;
        if (b5 == 0) {
            b5 = '?';
        }
        if (b5 > 0xFF) {
            if (out_idx + 2 >= max_len) {
                break;
            }
            big5[out_idx++] = (char)(b5 >> 8);
            big5[out_idx++] = (char)(b5 & 0xFF);
        } else {
            big5[out_idx++] = (char)b5;
        }
    }
    big5[out_idx] = '\0';
    return big5;
}

char *utf8_to_big5(const char *utf8, char *big5, size_t max_len) {
    return utf8_to_big5_n(utf8, (size_t)-1, big5, max_len);
}

extern const uint16_t b2u_table[];

char *big5_to_utf8_n(const char *big5, size_t src_len, char *utf8, size_t max_len) {
    if (!big5 || !utf8 || max_len == 0) {
        return utf8;
    }
    char tmp[big5 == utf8 ? max_len : 1];
    if (big5 == utf8) {
        if (src_len == (size_t)-1 || src_len >= max_len) {
            strlcpy(tmp, big5, max_len);
        } else {
            memcpy(tmp, big5, src_len);
            tmp[src_len] = '\0';
        }
        big5 = tmp;
    }
    utf8_ctx ctx;
    const uint8_t *p = (const uint8_t *)big5;
    const uint8_t *end = (src_len == (size_t)-1) ? (const uint8_t *)-1 : (p + src_len);
    size_t out_idx = 0;
    while (p < end && *p && out_idx < (max_len - 1)) {
        uint16_t b5 = *p;
        uint16_t ucs2 = 0;
        if (b5 & 0x80) {
            if (p + 1 >= end || !p[1]) {
                break;
            }
            if ((unsigned char)p[1] >= 0x40) {
                uint16_t b5_full = ((uint16_t)p[0] << 8) | (uint16_t)p[1];
                p += 2;
                ucs2 = b2u_table[b5_full];
            } else {
                p++;
                ucs2 = '?';
            }
        } else {
            p++;
            ucs2 = b5;
        }

        int utf_len = utf8_from_ucs(&ctx, ucs2);
        if (out_idx + (size_t)utf_len >= max_len) {
            break;
        }
        out_idx += utf8_to_mb(&ctx, utf8 + out_idx);
    }
    utf8[out_idx] = '\0';
    return utf8;
}

char *big5_to_utf8(const char *big5, char *utf8, size_t max_len) {
    return big5_to_utf8_n(big5, (size_t)-1, utf8, max_len);
}

#ifdef _TEST_MAIN_

const char * print_bits(uint8_t c) {
    static char bits[9] = {0};
    int i;
    for (i = 0; i < 8; i++)
        bits[i] = ((c >> (7-i)) & 0x01) ? '1' : '0';
    return bits;
}

void print_bytes(uint8_t *bytes, int len) {
    while (len-- > 0)
        printf("%02X ", *bytes++);
}

void wikipedia_test() {
    const int tests = 4;
    int ucs[] = {
        0x24, 0xA2, 0x20AC, 0x10348, 0
    };
    uint8_t utf[][5] = {
        {0x24, 0},
        {0xC2, 0xA2, 0},
        {0xE2, 0x82, 0xAC, 0},
        {0xF0, 0x90, 0x8D, 0x88, 0},
    };
    char t[5];
    int len, i;

    for (i = 0; i < tests; i++) {
        utf8_ctx ctx;
        utf8_init(&ctx);
        for (int j = 0; utf[i][j]; j++)
            utf8_add_byte(&ctx, utf[i][j]);
        int got = utf8_get_ucs(&ctx);
        if (got != ucs[i]) {
            printf("wikipedia_test utf8_get_ucs: failed in %04X (got %04X)\n", ucs[i], got);
        } else {
            printf("wikipedia_test utf8_get_ucs: passed %04X\n", ucs[i]);
        }
        utf8_from_ucs(&ctx, ucs[i]);
        len = utf8_to_mb(&ctx, t);
        if (strcmp(t, (const char *)utf[i]) != 0) {
            printf("wikipedia_test utf8_from_ucs: failed in %04X (got %d)\n", ucs[i], len);
            print_bytes(utf[i], 4);
            printf("\n");
            print_bytes((uint8_t *)t, len);
            printf("\n");
        } else {
            printf("wikipedia_test utf8_from_ucs: passed %04X\n", ucs[i]);
        }
    }
}

int main(int argc, char *argv[]) {
    wikipedia_test();
    return 0;
}
#endif
