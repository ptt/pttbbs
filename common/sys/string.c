#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <sys/random.h>

#include "fnv_hash.h"
#include "ansi.h"
#include "cmsys.h"

#define CHAR_LOWER(c)  ((c >= 'A' && c <= 'Z') ? c|32 : c)
/* ----------------------------------------------------- */
/* 字串轉換檢查函數                                      */
/* ----------------------------------------------------- */
/**
 * 將字串 s 轉為小寫存回 t
 * @param t allocated char array
 * @param s
 */
void
str_lower(char *t, const char *s)
{
    register unsigned char ch;

    do {
	ch = *s++;
	*t++ = CHAR_LOWER(ch);
    } while (ch);
}

int
str_starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*prefix++ != *str++)
            return 0;
    }
    return 1;
}

int
str_case_starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (tolower(*prefix++) != tolower(*str++))
            return 0;
    }
    return 1;
}

int
str_ends_with(const char *str, const char *suffix) {
    if (!str || !suffix)
        return 0;
    size_t len_str = strlen(str);
    size_t len_suffix = strlen(suffix);
    if (len_str < len_suffix)
        return 0;
    return strcmp(str + len_str - len_suffix, suffix) == 0;
}

int
str_case_ends_with(const char *str, const char *suffix) {
    if (!str || !suffix)
        return 0;
    size_t len_str = strlen(str);
    size_t len_suffix = strlen(suffix);
    if (len_str < len_suffix)
        return 0;
    return strcasecmp(str + len_str - len_suffix, suffix) == 0;
}

const char *
path_basename(const char *path) {
    if (!path)
        return "";
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

/**
 * 移除字串 buf 後端多餘的空白。
 * @param buf
 */
void
trim(char *buf)
{				/* remove trailing space */
    char           *p = buf;

    while (*p)
	p++;
    while (--p >= buf) {
	if (*p == ' ')
	    *p = '\0';
	else
	    break;
    }
}

/**
 * 移除 src 的 '\n' 並改成 '\0'
 * @param src
 */
void chomp(char *src)
{
    while(*src){
	if (*src == '\n')
	    *src = 0;
	else
	    src++;
    }
}

/* ----------------------------------------------------- */
/* ANSI 處理函數                                         */
/* ----------------------------------------------------- */
int
strip_blank(char *cbuf, const char *buf)
{
    for (; *buf; buf++)
	if (*buf != ' ')
	    *cbuf++ = *buf;
    *cbuf = 0;
    return 0;
}

int
reduce_blank(char *cbuf, const char *buf) {
    char *obuf = cbuf;
    int need_remove;
    for (need_remove = 1; *buf; buf++) {
        if (isascii(*buf) && isblank(*buf)) {
            if (need_remove)
                continue;
            need_remove = 1;
        } else {
            need_remove = 0;
        }
        *cbuf++ = *buf;
    }
    // in the end, remove trailing spaces.
    if (cbuf > obuf && *(cbuf-1) == ' ')
        cbuf --;
    *cbuf = 0;
    return 0;
}

/*
 * Scans an ECMA-48 (also known as ANSI) control sequence, started by the
 * Escape character (*src == ESC_CHR).
 * Returns the pointer to the last byte of the escape sequence (e.g. the final char),
 * or returns src if it is a truncated/lone ESC at the end of the string.
 * Also sets *is_color to 1 if it is an SGR color sequence (ESC [ ... m).
 * Sets *is_safe_cmd to 1 if it is a safe command in NO_RELOAD mode.
 */
static const char *
scan_control_sequence(const char *src, int *is_color, int *is_safe_cmd)
{
    if (is_color)
        *is_color = 0;
    if (is_safe_cmd)
        *is_safe_cmd = 0;

    const char *p = src + 1;
    if (*p == '\0')
        return src;

    if (*p == '[') {
        // CSI: ESC [ [P...P] [I...I] F
        p++;
        int has_private = (*p >= 0x3C && *p <= 0x3F); // '<', '=', '>', '?'
        while (*p >= 0x30 && *p <= 0x3F)
            p++;
        while (*p >= 0x20 && *p <= 0x2F)
            p++;
        if (*p >= 0x40 && *p <= 0x7E) {
            // Valid final character reached
            if (!has_private) {
                if (*p == 'm') {
                    if (is_color)
                        *is_color = 1;
                    if (is_safe_cmd)
                        *is_safe_cmd = 1;
                } else if (is_safe_cmd) {
                    const uint64_t safe_mask =
                        (1ULL << ('A' - 0x40)) | (1ULL << ('B' - 0x40)) |
                        (1ULL << ('C' - 0x40)) | (1ULL << ('D' - 0x40)) |
                        (1ULL << ('H' - 0x40)) | (1ULL << ('I' - 0x40)) |
                        (1ULL << ('J' - 0x40)) | (1ULL << ('K' - 0x40)) |
                        (1ULL << ('f' - 0x40)) | (1ULL << ('h' - 0x40)) |
                        (1ULL << ('l' - 0x40)) | (1ULL << ('m' - 0x40)) |
                        (1ULL << ('s' - 0x40)) | (1ULL << ('u' - 0x40));
                    if ((safe_mask >> (*p - 0x40)) & 1)
                        *is_safe_cmd = 1;
                }
            }
            return p;
        }
        // Incomplete / malformed CSI: consume up to p - 1 if advanced
        return (p > src + 2) ? (p - 1) : (src + 1);
    }

    if (*p == ']') {
        // OSC: ESC ] ... (BEL | ESC \)
        p++;
        while (*p && *p != '\x07' && !(*p == ESC_CHR && *(p + 1) == '\\'))
            p++;
        if (*p == '\x07')
            return p;
        if (*p == ESC_CHR && *(p + 1) == '\\')
            return p + 1;
        return (*p) ? p : (p - 1);
    }

    // Standard 3-byte escape sequences: ESC [()*+-./#] <char>
    if (*p == '(' || *p == ')' || *p == '*' || *p == '+' ||
        *p == '-' || *p == '.' || *p == '/' || *p == '#') {
        if (*(p + 1) != '\0')
            return p + 1;
        return p;
    }

    // 2-byte escape sequences (e.g. ESC M, ESC E, ESC =, ESC >)
    return p;
}

/*
 * Fast skipper for ECMA-48 (also known as ANSI) control sequences, started by
 * the Escape character (*src == ESC_CHR).
 * Returns pointer to the first character AFTER the control sequence.
 */
const char *
skip_control_sequence(const char *src)
{
    const char *p = src + 1;
    unsigned char c = *p;
    if (c == '\0')
        return p;

    if (c == '[') {
        // CSI: ESC [ [0x20..0x3F]* [0x40..0x7E]
        p++;
        while ((unsigned char)(*p - 0x20) < 0x20)
            p++;
        if ((unsigned char)(*p - 0x40) <= 0x3E)
            p++;
        return p;
    }

    if (c == ']') {
        // OSC: ESC ] ... (BEL | ESC \)
        p++;
        while (*p && *p != '\x07' && !(*p == ESC_CHR && *(p + 1) == '\\'))
            p++;
        if (*p == '\x07')
            return p + 1;
        if (*p == ESC_CHR && *(p + 1) == '\\')
            return p + 2;
        return p;
    }

    // 3-byte sequences: ESC [()*+-./#] <char>
    if (c == '(' || c == ')' || c == '*' || c == '+' ||
        c == '-' || c == '.' || c == '/' || c == '#')
        return (p[1] != '\0') ? (p + 2) : (p + 1);

    // 2-byte sequence: ESC <char>
    return p + 1;
}

/**
 * Strip ECMA-48 (also known as ANSI) control sequences, started by the
 * Escape character, from src according to mode.
 * @param dst
 * @param src (if NULL then only return length)
 * @param mode enum {STRIP_ALL = 0, ONLY_COLOR, NO_RELOAD};
 *             STRIP_ALL:  strip all
 *             ONLY_COLOR: keep only color commands (ESC[*m)
 *             NO_RELOAD:  keep safe commands (cursor move + color)
 * @return stripped length
 */
int
strip_control_sequence(char *dst, const char *src)
{
    int count = 0;

    while (*src) {
        const char *p = strchrnul(src, ESC_CHR);
        int chunk = p - src;
        if (dst && chunk > 0) {
            memmove(dst, src, chunk);
            dst += chunk;
        }
        count += chunk;
        if (*p == '\0')
            break;
        src = skip_control_sequence(p);
    }
    if (dst)
        *dst = '\0';
    return count;
}

int
strip_control_sequence_ex(char *dst, const char *src, enum STRIP_FLAG mode)
{
    if (mode == STRIP_ALL)
        return strip_control_sequence(dst, src);

    int count = 0;
    for (; *src; ++src) {
        if (*src != ESC_CHR) {
            if (dst)
                *dst++ = *src;
            ++count;
        } else {
            int is_color = 0, is_safe_cmd = 0;
            const char *end = scan_control_sequence(src, &is_color, &is_safe_cmd);

            if ((mode == NO_RELOAD && is_safe_cmd) ||
                (mode == ONLY_COLOR && is_color)) {
                int len = end - src + 1;
                if (dst) {
                    memmove(dst, src, len);
                    dst += len;
                }
                count += len;
            }

            src = end;
            if (*src == '\0')
                break;
        }
    }
    if (dst)
        *dst = '\0';
    return count;
}

/**
 * Query the byte offset of the nth terminal display column in stream s
 * (skipping ECMA-48 / ANSI control sequences started by the Escape character).
 * If the stream width is less than count, return missing columns in negative value.
 */
int
stream_col_offset(int count, const char *s)
{
    const char *os = s;

    while (count > 0 && *s) {
        if (*s == ESC_CHR) {
            s = skip_control_sequence(s);
            continue;
        }
        if (MB_IS_BIG5) {
            const char *p = strchrnul(s, ESC_CHR);
            int chunk = p - s;
            if (chunk >= count)
                return (s + count) - os;
            count -= chunk;
            s = p;
        } else {
            int w = mb_width(s);
            if (w > count)
                return s - os;
            count -= w;
            s += mb_bytes(s);
        }
    }
    return (count > 0) ? -count : (s - os);
}

int
mb_bytes(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    if (!p || !p[0])
        return 0;
    if (MB_IS_UTF8) {
        utf8_ctx ctx;
        utf8_init(&ctx);
        for (int i = 0; p[i]; i++) {
            if (utf8_add_byte(&ctx, p[i]))
                return ctx.len;
            if (!utf8_pending(&ctx))
                break;
        }
        return 1;
    } else {
        return (IS_DBCSLEAD(p[0]) && (unsigned char)p[1] >= 0x40) ? 2 : 1;
    }
}

int
ucs_width(int ucs)
{
    int w = mk_wcwidth_cjk(ucs);
    return (w < 0) ? 0 : w;
}

int
mb_width(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    if (!p || !p[0])
        return 0;
    if (MB_IS_UTF8) {
        if (p[0] < 0x80)
            return (p[0] >= 0x20 && p[0] != 0x7F) ? 1 : 0;
        if ((p[1] & 0xC0) == 0x80) {
            // Fast path: CJK Unified Ideographs (U+4000..U+9FFF), Hangul, and PUA
            if ((p[0] >= 0xE4 && p[0] <= 0xE9) ||
                p[0] == 0xEB || p[0] == 0xEC || p[0] == 0xEE)
                return 2;
            // Fast path: Hiragana, Katakana, Bopomofo, CJK Ext-A (U+3040..U+3FFF)
            if (p[0] == 0xE3 && p[1] != 0x80 && p[1] != 0x82)
                return 2;
            // Fast path: PUA, CJK Compat Ideographs (U+F000..U+FAFF) & Fullwidth ASCII (U+FF00..U+FF3F)
            if (p[0] == 0xEF && (p[1] <= 0xAB || p[1] == 0xBC))
                return 2;
        }
        utf8_ctx ctx;
        utf8_init(&ctx);
        for (int i = 0; p[i]; i++) {
            if (utf8_add_byte(&ctx, p[i]))
                return ucs_width(utf8_get_ucs(&ctx));
            if (!utf8_pending(&ctx))
                break;
        }
        return 1;
    } else {
        return mb_bytes(s);
    }
}

int
mb_from_vkey(int key, char *buf)
{
    if (key <= 0 || ((key & 0xF800) == 0xD800)) {
        buf[0] = '\0';
        return 0;
    }
    if (VKEY_IS_MB) {
        if (key <= 0xFF) {
            buf[0] = (char)key;
            buf[1] = '\0';
            return 1;
        }
        buf[0] = '\0';
        return 0;
    }
    if (MB_IS_UTF8) {
        utf8_ctx ctx;
        if (!utf8_from_ucs(&ctx, key)) {
            buf[0] = '\0';
            return 0;
        }
        return utf8_to_mb(&ctx, buf);
    }
    if (key < 0x80) {
        buf[0] = (char)key;
        buf[1] = '\0';
        return 1;
    }
    if (key >= 0x8140 && key <= 0xFEFE) {
        buf[0] = (char)((key >> 8) & 0xFF);
        buf[1] = (char)(key & 0xFF);
        buf[2] = '\0';
        return 2;
    }
    buf[0] = '\0';
    return 0;
}

int
stream_width(const char *s)
{
    if (!s || !*s)
        return 0;

    int width = 0;
    while (*s) {
        if (*s == ESC_CHR) {
            s = skip_control_sequence(s);
            continue;
        }
        if (MB_IS_BIG5) {
            const char *p = strchrnul(s, ESC_CHR);
            width += p - s;
            s = p;
        } else {
            width += mb_width(s);
            s += mb_bytes(s);
        }
    }
    return width;
}

/* ----------------------------------------------------- */
/* DBCS 處理函數                                         */
/* ----------------------------------------------------- */

void
strip_nonebig5(unsigned char *str, int maxlen)
{
  int i;
  int len=0;
  if (MB_IS_UTF8) {
    utf8_ctx ctx;
    utf8_init(&ctx);
    for (i = 0; i < maxlen && str[i]; i++) {
      if (32 <= str[i] && str[i] < 128) {
        utf8_reset(&ctx);
        str[len++] = str[i];
      } else if (str[i] >= 0x80) {
        if (utf8_add_byte(&ctx, str[i])) {
          memcpy(str + len, ctx.buf, ctx.len);
          len += ctx.len;
          utf8_reset(&ctx);
        }
      } else {
        utf8_reset(&ctx);
      }
    }
    if (len < maxlen)
      str[len] = '\0';
    return;
  }
  for(i=0;i<maxlen && str[i];i++) {
    if(32<=str[i] && str[i]<128)
      str[len++]=str[i];
    else if(str[i]&0x80) {
      if(i+1<maxlen)
	if((0x40<=str[i+1] && str[i+1]<=0x7e) ||
	   (0xa1<=str[i+1] && str[i+1]<=0xfe)) {
	  str[len++]=str[i];
	  str[len++]=str[i+1];
	  i++;
	}
    }
  }
  if(len<maxlen)
    str[len]='\0';
}

/**
 * mbs_remove_intr_escape(buf, len): 去除 DBCS 一字雙色字。
 * (deprecated)
 */
int mbs_remove_intr_escape(unsigned char *buf, int *len)
{
    int l = len ? *len : (int)strlen((const char *)buf);
    if (!memchr(buf, ESC_CHR, l))
        return 0;

    int oldl = l;
    int isInDBCS = 0;

    for (int i = 0; i < l; i++) {
        if (buf[i] == ESC_CHR) {
            const char *next = skip_control_sequence((const char *)(buf + i));
            int inext = (int)((const unsigned char *)next - buf);
            if (inext > l)
                inext = l;
            if (isInDBCS && inext < l) {
                int sz = inext - i;
                memmove(buf + i, buf + inext, l - inext);
                l -= sz;
                i--; // for the ++ in loop
            } else {
                i = inext - 1;
            }
        } else if (isInDBCS) {
            isInDBCS = 0;
        } else if (IS_DBCSLEAD(buf[i])) {
            isInDBCS = 1;
        }
    }

    if (len)
        *len = l;
    return (oldl != l) ? 1 : 0;
}

/**
 * big5_next_status(c, prev_status): 取得 c 的 DBCS 狀態
 */
static int
big5_next_status(char c, int prev_status)
{
    if (prev_status == MB_LEADING)
        return MB_TRAILING;
    if ((unsigned char)c >= 0x80)
        return MB_LEADING;
    return MB_ASCII;
}

int
mbs_status(const char *s, int pos)
{
    if (MB_IS_UTF8) {
        unsigned char c = (unsigned char)s[pos];
        if (c < 0x80)
            return MB_ASCII;
        if ((c & 0xC0) == 0x80)
            return MB_TRAILING;
        return MB_LEADING;
    }

    int sts = MB_ASCII;
    char c;

    while (pos-- >= 0) {
        c = *s++;
        sts = big5_next_status(c, sts);
        if (c == 0)
            break;
    }
    return sts;
}

void
mbs_safe_trim(char *s)
{
    int len = strlen(s);
    if (len < 1)
        return;
    if (MB_IS_UTF8) {
        int i = len - 1;
        while (i >= 0 && mbs_status(s, i) == MB_TRAILING)
            i--;
        if (i >= 0 && mbs_status(s, i) == MB_LEADING) {
            utf8_ctx ctx;
            utf8_init(&ctx);
            for (int j = i; j < len; j++)
                utf8_add_byte(&ctx, (unsigned char)s[j]);
            if (!utf8_is_ready(&ctx))
                s[i] = '\0';
        }
    } else {
        if (mbs_status(s, len - 1) == MB_LEADING)
            s[len - 1] = '\0';
    }
}

const char *
mbs_nth(const char *s, int nth)
{
    int w;
    while (nth-- > 0 && (w = mb_bytes(s)) > 0)
        s += w;
    return (*s) ? s : NULL;
}

char *
mbs_strstr(const char *pool, const char *ptr)
{
    if (MB_IS_UTF8)
        return (char *)strstr(pool, ptr);

    if (!*ptr)
        return (char *)pool;

    const char *scan = pool;
    const char *match;
    while ((match = strstr(scan, ptr)) != NULL) {
        while (scan < match) {
            if (IS_DBCSLEAD(*scan) && scan[1])
                scan += 2;
            else
                scan += 1;
        }
        if (scan == match)
            return (char *)match;
    }
    return NULL;
}

char *
mbs_strcasestr(const char *pool, const char *ptr)
{
    if (MB_IS_UTF8)
        return (char *)strcasestr(pool, ptr);

    int i = 0, i2 = 0, found = 0,
        szpool = strlen(pool),
        szptr  = strlen(ptr);

    for (i = 0; i <= szpool - szptr; i++)
    {
        found = 1;

        for (i2 = 0; i2 < szptr; i2++)
        {
            if (IS_DBCSLEAD(pool[i + i2]))
            {
                if (ptr[i2]   != pool[i + i2] ||
                    ptr[i2 + 1] != pool[i + i2 + 1])
                {
                    found = 0;
                    break;
                }
                i2++;
            } else {
                if (IS_DBCSLEAD(ptr[i2]) ||
                    tolower(ptr[i2]) != tolower(pool[i + i2]))
                {
                    found = 0;
                    break;
                }
            }
        }

        if (found)
            return (char *)pool + i;

        if (IS_DBCSLEAD(pool[i]))
            i++;
    }
    return NULL;
}

int
mbs_strncasecmp(const char *s1, const char *s2, size_t len)
{
    if (MB_IS_UTF8)
        return strncasecmp(s1, s2, len);

    int r = strncasecmp(s1, s2, len);
    int sts1 = MB_ASCII, sts2 = MB_ASCII;
    if (r != 0)
        return r;

    while (len-- > 0) {
        char c1 = *s1++, c2 = *s2++;
        sts1 = big5_next_status(c1, sts1);
        sts2 = big5_next_status(c2, sts2);
        if (sts1 != MB_ASCII && c1 != c2)
            return (unsigned char)c1 - (unsigned char)c2;
    }
    return 0;
}

unsigned
mbs_strcasehash(const char *s)
{
    if (MB_IS_UTF8)
        return fnv1a_32_strcase(s, FNV1_32_INIT);
    return fnv1a_32_dbcs_strcase(s, FNV1_32_INIT);
}

/* ----------------------------------------------------- */
/* 字串檢查函數：英文、數字、檔名、E-mail address        */
/* ----------------------------------------------------- */

int
invalid_pname(const char *str)
{
    const char           *p1, *p2, *p3;

    p1 = str;
    while (*p1) {
	if (!(p2 = strchr(p1, '/')))
	    p2 = str + strlen(str);
	if (p1 + 1 > p2 || p1 + strspn(p1, ".") == p2) /* 不允許用 / 開頭, 或是 // 之間只有 . */
	    return 1;
	for (p3 = p1; p3 < p2; p3++)
	    if (!isalnum(*p3) && !strchr("@[]-._", *p3)) /* 只允許 alnum 或這些符號 */
		return 1;
	p1 = p2 + (*p2 ? 1 : 0);
    }
    return 0;
}

/*
 * return	1	if /^[0-9]+$/
 * 		0	else, 含空字串
 */
int is_number(const char *p)
{
    if (*p == '\0')
	return 0;

    for(; *p; p++) {
	if (*p < '0' || '9' < *p)
	    return 0;
    }
    return 1;
}

unsigned
StringHash(const char *s)
{
    return fnv1a_32_strcase(s, FNV1_32_INIT);
}

/* qp_encode() modified from mutt-1.5.7/rfc2047.c q_encoder() */
const char MimeSpecials[] = "@.,;:<>[]\\\"()?/= \t";
char * qp_encode (char *s, size_t slen, const char *d, const char *tocode)
{
    char hex[] = "0123456789ABCDEF";
    char *s0 = s;

    memcpy(s, "=?", 2), s += 2;
    memcpy(s, tocode, strlen (tocode)), s += strlen (tocode);
    memcpy(s, "?Q?", 3), s += 3;
    assert(s - s0 + 3 < (int)slen);

    while (*d != '\0' && (s - s0 + 6 < (int)slen))
    {
	unsigned char c = *d++;
	if (c == ' ')
	    *s++ = '_';
	else if (c >= 0x7f || c < 0x20 || c == '_' ||  strchr (MimeSpecials, c))
	{
	    *s++ = '=';
	    *s++ = hex[(c & 0xf0) >> 4];
	    *s++ = hex[c & 0x0f];
	}
	else
	    *s++ = c;
    }
    memcpy (s, "?=", 2), s += 2;
    *s='\0';
    return s0;
}

// following code is moved from innbbsd/str_decode.c
/*-------------------------------------------------------*/
/* lib/str_decode.c    ( NTHU CS MapleBBS Ver 3.00 )    */
/*-------------------------------------------------------*/
/* target : included C for QP/BASE64 decoding           */
/* create : 95/03/29                                    */
/* update : 97/03/29                                    */
/*-------------------------------------------------------*/
#include <errno.h>
#include <iconv.h>


/* ----------------------------------------------------- */
/* QP code : "0123456789ABCDEF"                                 */
/* ----------------------------------------------------- */

static int
qp_code(int x)
{
    if (x >= '0' && x <= '9')
	return x - '0';
    if (x >= 'a' && x <= 'f')
	return x - 'a' + 10;
    if (x >= 'A' && x <= 'F')
	return x - 'A' + 10;
    return -1;
}


/* ------------------------------------------------------------------ */
/* BASE64 :							      */
/* "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/" */
/* ------------------------------------------------------------------ */

static int
base64_code(int x)
{
    if (x >= 'A' && x <= 'Z')
	return x - 'A';
    if (x >= 'a' && x <= 'z')
	return x - 'a' + 26;
    if (x >= '0' && x <= '9')
	return x - '0' + 52;
    if (x == '+')
	return 62;
    if (x == '/')
	return 63;
    return -1;
}


/* ----------------------------------------------------- */
/* judge & decode QP / BASE64				 */
/* ----------------------------------------------------- */

static inline int
isreturn(unsigned char c)
{
    return c == '\r' || c == '\n';
}

static inline
int
mmdecode(unsigned char *src, unsigned char encode, unsigned char *dst)
{
    /* Thor.980901: src和dst可相同, 但src 一定有?或\0結束 */
    /* Thor.980901: 注意, decode出的結果不會自己加上 \0 */
    unsigned char  *t = dst;
    int             pattern = 0, bits = 0;
    encode |= 0x20;		/* Thor: to lower */
    switch (encode) {
    case 'q':			/* Thor: quoted-printable */
	while (*src && *src != '?') {	/* Thor: delimiter *//* Thor.980901:
					 * 0 算是 delimiter */
	    if (*src == '=') {
		int             x = *++src, y = x ? *++src : 0;
		if (isreturn(x))
		    continue;
		if ((x = qp_code(x)) < 0 || (y = qp_code(y)) < 0)
		    return -1;
		*t++ = (x << 4) + y, src++;
	    } else if (*src == '_')
		*t++ = ' ', src++;
#if 0
	    else if (!*src)	/* Thor: no delimiter is not successful */
		return -1;
#endif
	    else		/* Thor: *src != '=' '_' */
		*t++ = *src++;
	}
	return t - dst;
    case 'b':			/* Thor: base 64 */
	while (*src && *src != '?') {	/* Thor: delimiter */
	    /*
	     * Thor.980901: 0也算 *//* Thor: pattern & bits are cleared
	     * outside
	     */
	    int             x;
#if 0
	    if (!*src)
		return -1;	/* Thor: no delimiter is not successful */
#endif
	    x = base64_code(*src++);
	    if (x < 0)
		continue;	/* Thor: ignore everything not in the
				 * base64,=,.. */
	    pattern = (pattern << 6) | x;
	    bits += 6;		/* Thor: 1 code gains 6 bits */
	    if (bits >= 8) {	/* Thor: enough to form a byte */
		bits -= 8;
		*t++ = (pattern >> bits) & 0xff;
	    }
	}
	return t - dst;
    }
    return -1;
}

size_t
str_iconv(
	  const char *fromcode,	/* charset of source string */
	  const char *tocode,	/* charset of destination string */
	  char *src,		/* source string */
	  size_t srclen,	/* source string length */
	  char *dst,		/* destination string */
	  size_t dstlen)
{				/* destination string length */
    /*
     * 這個函式會將一個字串 (src) 從 charset=fromcode 轉成 charset=tocode,
     * srclen 是 src 的長度, dst 是輸出的buffer, dstlen 則指定了 dst 的大小,
     * 最後會補 '\0', 所以要留一個byte給'\0'. 如果遇到 src 中有非字集的字,
     * 或是 src 中有未完整的 byte, 都會砍掉.
     */
    iconv_t         iconv_descriptor;
    size_t          iconv_ret, dstlen_old;

    dstlen--;			/* keep space for '\0' */

    dstlen_old = dstlen;

    /* Open a descriptor for iconv */
    iconv_descriptor = iconv_open(tocode, fromcode);

    if (iconv_descriptor == ((iconv_t) (-1))) {	/* if open fail */
	strncpy(dst, src, dstlen);
	return dstlen;
    }
    /* Start translation */
    while (srclen > 0 && dstlen > 0) {
	iconv_ret = iconv(iconv_descriptor, &src, &srclen,
			  &dst, &dstlen);
	if (iconv_ret != 0) {
	    switch (errno) {
		/* invalid multibyte happened */
	    case EILSEQ:
		/* forward that byte */
		*dst = *src;
		src++;
		srclen--;
		dst++;
		dstlen--;
		break;
		/* incomplete multibyte happened */
	    case EINVAL:
		/* forward that byte (maybe wrong) */
		*dst = *src;
		src++;
		srclen--;
		dst++;
		dstlen--;
		break;
		/* dst no rooms */
	    case E2BIG:
		/* break out the while loop */
		srclen = 0;
		break;
	    }
	}
    }
    *dst = '\0';
    /* close descriptor of iconv */
    iconv_close(iconv_descriptor);

    return (dstlen_old - dstlen);
}


/**
 * inplace decode mime header string (rfc2047) to big5 encoding
 *
 * @param str	[in,out] string, output size is limited to 512. Assume output size is shorter than input size.
 *
 * TODO rewrite, don't hardcode 512
 */
void
str_decode_M3(char *str)
{
    int             adj;
    int             i;
    unsigned char  *src, *dst;
    unsigned char   buf[512];
    unsigned char   charset[512], dst1[512];


    src = (unsigned char*)str;
    dst = buf;
    adj = 0;

    while (*src && (dst - buf) < (int)sizeof(buf) - 1) {
	if (*src != '=') {	/* Thor: not coded */
	    unsigned char  *tmp = src;
	    while (adj && *tmp && isspace(*tmp))
		tmp++;
	    if (adj && *tmp == '=') {	/* Thor: jump over space */
		adj = 0;
		src = tmp;
	    } else
		*dst++ = *src++;
	    /* continue; *//* Thor: take out */
	} else {		/* Thor: *src == '=' */
	    unsigned char  *tmp = src + 1;
	    if (*tmp == '?') {	/* Thor: =? coded */
		/* "=?%s?Q?" for QP, "=?%s?B?" for BASE64 */
		charset[0] = '\0';
		tmp++;
		i = 0;
		while (*tmp && *tmp != '?') {
		    if (i + 1 < (int)sizeof(charset)) {
			charset[i] = *tmp;
			charset[i + 1] = '\0';
			i++;
		    }
		    tmp++;
		}
		if (*tmp && tmp[1] && tmp[2] == '?') {	/* Thor: *tmp == '?' */
		    int             i = mmdecode(tmp + 3, tmp[1], dst1);
		    if (i >= 0)
			i = str_iconv((char*)charset, "big5", (char*)dst1, i, (char*)dst,
				      sizeof(buf) - ((int)(dst - buf)));
		    if (i >= 0) {
			tmp += 3;	/* Thor: decode's src */
#if 0
			while (*tmp++ != '?');	/* Thor: no ? end, mmdecode
						 * -1 */
#endif
			while (*tmp && *tmp++ != '?');	/* Thor: no ? end,
							 * mmdecode -1 */
			/* Thor.980901: 0 也算 decode 結束 */
			if (*tmp == '=')
			    tmp++;
			src = tmp;	/* Thor: decode over */
			dst += i;
			adj = 1;/* Thor: adjcent */
		    }
		}
	    }
	    while (src != tmp)	/* Thor: not coded */
		*dst++ = *src++;
	}
    }
    *dst = 0;
    assert(strlen(str) >= strlen((char*)buf));
    strcpy(str, (char*)buf);
}

/**
 * Obtain random bytes from /dev/urandom.
 */
void
must_getrandom(void *buf, size_t len)
{
    while (1) {
	ssize_t r = getrandom(buf, len, 0);
	if (r == (ssize_t)len)
	    return;
	if (r < 0 && errno == EINTR)
	    continue;
	assert(!"getrandom failed");
	exit(1);
    }
}


/**
 * Generate a random ascii text code.
 */
void
random_text_code(char *buf, size_t len)
{
    // prevent ambigious characters: oOlI
    static const char chars[] = "qwertyuipasdfghjkzxcvbnmoQWERTYUPASDFGHJKLZXCVBNM";
    static const size_t charslen = sizeof(chars) - 1;

    must_getrandom(buf, sizeof(*buf) * len);

    // read rand values as unsigned
    const unsigned char *rnd = (const unsigned char *)buf;
    for (size_t i = 0; i < len; i++)
	buf[i] = chars[rnd[i] % charslen];
    buf[len] = '\0';
}
