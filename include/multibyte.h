/*
 * multibyte.h - Multibyte stream decoder abstraction for Big5 and UTF-8.
 *
 * Copyright (c) 2026 Hung-Te Lin <hungte@gmail.com>
 * All rights reserved.
 * Distributed under BSD license (GPL compatible).
 */

#ifndef MULTIBYTE_H
#define MULTIBYTE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#if !defined(MB_IS_BIG5) && !defined(MB_IS_UTF8)
#  define MB_IS_UTF8 0
#endif

#if defined(MB_IS_UTF8) && !defined(MB_IS_BIG5)
#  define MB_IS_BIG5 (!(MB_IS_UTF8))
#elif defined(MB_IS_BIG5) && !defined(MB_IS_UTF8)
#  define MB_IS_UTF8 (!(MB_IS_BIG5))
#endif

#if (MB_IS_BIG5 && MB_IS_UTF8) || (!MB_IS_BIG5 && !MB_IS_UTF8)
#  error "Exactly one of MB_IS_BIG5 or MB_IS_UTF8 must be 1"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * utf8_ctx: UTF-8 stream decoder
 * ------------------------------------------------------------------------- */
typedef struct {
    uint8_t buf[5];
    uint8_t need;
    uint8_t got;
    uint8_t len;
    int     ucs;
} utf8_ctx;

static inline void
utf8_init(utf8_ctx *ctx)
{
    ctx->buf[0] = 0;
    ctx->need = 0;
    ctx->got = 0;
    ctx->len = 0;
    ctx->ucs = -1;
}
#define utf8_reset(ctx) utf8_init(ctx)

static inline void
utf8_error(utf8_ctx *ctx)
{
    ctx->buf[0] = '?';
    ctx->buf[1] = 0;
    ctx->need = 0;
    ctx->got = 0;
    ctx->len = 1;
    ctx->ucs = '?';
}

static inline int
utf8_pending(const utf8_ctx *ctx)
{
    return (ctx && ctx->need > ctx->got) ? (ctx->need - ctx->got) : 0;
}

static inline int
utf8_is_ready(const utf8_ctx *ctx)
{
    return ctx && ctx->need > 0 && ctx->got == ctx->need;
}

static inline int
utf8_get_ucs(utf8_ctx *ctx)
{
    if (!utf8_is_ready(ctx))
        return -1;
    int ucs = ctx->ucs;
    ctx->need = 0;
    ctx->got = 0;
    ctx->ucs = -1;
    return ucs;
}

int   utf8_add_byte(utf8_ctx *ctx, unsigned char byte);
int   utf8_from_ucs(utf8_ctx *ctx, int ucs);
int   utf8_to_mb(const utf8_ctx *ctx, char *mb);
char *utf8_to_big5(const char *utf8, char *big5, size_t max_len);
char *big5_to_utf8(const char *big5, char *utf8, size_t max_len);
char *utf8_to_big5_n(const char *utf8, size_t src_len, char *big5, size_t max_len);
char *big5_to_utf8_n(const char *big5, size_t src_len, char *utf8, size_t max_len);

/* -------------------------------------------------------------------------
 * big5_ctx: Big5 stream decoder
 * ------------------------------------------------------------------------- */
typedef struct {
    uint8_t buf[3];
    uint8_t need;
    uint8_t got;
    uint8_t len;
    int     ch;
} big5_ctx;

static inline void
big5_init(big5_ctx *ctx)
{
    ctx->buf[0] = 0;
    ctx->need = 0;
    ctx->got = 0;
    ctx->len = 0;
    ctx->ch = -1;
}
#define big5_reset(ctx) big5_init(ctx)

static inline void
big5_error(big5_ctx *ctx)
{
    ctx->buf[0] = '?';
    ctx->buf[1] = 0;
    ctx->need = 0;
    ctx->got = 0;
    ctx->len = 1;
    ctx->ch = '?';
}

static inline int
big5_pending(const big5_ctx *ctx)
{
    return (ctx && ctx->need > ctx->got) ? (ctx->need - ctx->got) : 0;
}

static inline int
big5_is_ready(const big5_ctx *ctx)
{
    return ctx && ctx->need > 0 && ctx->got == ctx->need;
}

static inline int
big5_get_char(big5_ctx *ctx)
{
    if (!big5_is_ready(ctx))
        return -1;
    int ch = ctx->ch;
    ctx->need = 0;
    ctx->got = 0;
    ctx->ch = -1;
    return ch;
}

int   big5_add_byte(big5_ctx *ctx, unsigned char byte);
int   big5_from_char(big5_ctx *ctx, int ch);
int   big5_to_mb(const big5_ctx *ctx, char *mb);

/* -------------------------------------------------------------------------
 * mb_ctx: Generic multibyte context mapping based on MB_IS_UTF8
 * ------------------------------------------------------------------------- */
#if MB_IS_UTF8
typedef utf8_ctx mb_ctx;
#define mb_init(ctx)            utf8_init(ctx)
#define mb_reset(ctx)           utf8_reset(ctx)
#define mb_error(ctx)           utf8_error(ctx)
#define mb_pending(ctx)         utf8_pending(ctx)
#define mb_is_ready(ctx)        utf8_is_ready(ctx)
#define mb_get_char(ctx)        utf8_get_ucs(ctx)
#define mb_add_byte(ctx, b)     utf8_add_byte(ctx, b)
#define mb_from_char(ctx, ch)   utf8_from_ucs(ctx, ch)
#define mb_to_str(ctx, mb)      utf8_to_mb(ctx, mb)
#else
typedef big5_ctx mb_ctx;
#define mb_init(ctx)            big5_init(ctx)
#define mb_reset(ctx)           big5_reset(ctx)
#define mb_error(ctx)           big5_error(ctx)
#define mb_pending(ctx)         big5_pending(ctx)
#define mb_is_ready(ctx)        big5_is_ready(ctx)
#define mb_get_char(ctx)        big5_get_char(ctx)
#define mb_add_byte(ctx, b)     big5_add_byte(ctx, b)
#define mb_from_char(ctx, ch)   big5_from_char(ctx, ch)
#define mb_to_str(ctx, mb)      big5_to_mb(ctx, mb)
#endif

int ucs_width(int ucs);

static inline int
mb_char_width(int ch)
{
    if (ch < 0)
        return 0;
    if (MB_IS_UTF8)
        return ucs_width(ch);
    return (ch >= 0x0100) ? 2 : (ch >= 0x20 && ch != 0x7F ? 1 : 0);
}

static inline int
utf8_is_valid_trail(int c)
{
    return ((unsigned char)c & 0xC0) == 0x80;
}

static inline int
big5_is_valid_trail(int c)
{
    unsigned char b = (unsigned char)c;
    return (b >= 0x40 && b != 0xFF);
}

static inline int
mb_is_valid_trail(int c)
{
    if (MB_IS_UTF8)
        return utf8_is_valid_trail(c);
    return big5_is_valid_trail(c);
}

/* Big5 tables */
extern const uint16_t b2u_table[];
extern const uint16_t u2b_table[];
extern const uint8_t  b2u_ambiguous_width[];

#ifdef __cplusplus
}
#endif

#endif /* MULTIBYTE_H */
