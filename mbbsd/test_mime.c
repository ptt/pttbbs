/*
 * Tests for str_decode_M3() in common/sys/string.c, which decodes RFC 2047
 * mail headers ("=?UTF-8?B?...?=") into Big5. bbsmail runs it on the
 * Subject of incoming mail, so its input can come from anyone.
 *
 * Run with: make test_mime
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cmsys.h"

#define GUARD 16

static int failures;

static void report(const char *name, int ok, const char *got)
{
    printf("%-40s %s\n", name, ok ? "ok" : "FAIL");
    if (!ok && got) {
        const unsigned char *p;
        printf("    got: \"");
        for (p = (const unsigned char *)got; *p; p++) {
            if (*p >= 0x20 && *p < 0x7f)
                putchar(*p);
            else
                printf("\\x%02x", *p);
        }
        printf("\"\n");
    }
    if (!ok)
        failures++;
}

/*
 * Decode `in` in a heap buffer of exactly `size` bytes followed by guard
 * bytes that must stay untouched. Returns the result (caller frees), or
 * NULL if anything was written past the buffer.
 */
static char *decode(const char *in, size_t size)
{
    char *b = malloc(size + GUARD);
    size_t n = strlen(in), i;

    memset(b, 'G', size + GUARD);
    if (size) {
        if (n > size - 1)
            n = size - 1;
        memcpy(b, in, n);
        b[n] = '\0';
    }
    str_decode_M3(b, size);
    for (i = 0; i < GUARD; i++) {
        if (b[size + i] != 'G') {
            free(b);
            return NULL;
        }
    }
    return b;
}

static void expect(const char *name, const char *in, size_t size,
                   const char *want)
{
    char *got = decode(in, size);
    report(name, got && strcmp(got, want) == 0, got);
    free(got);
}

static void test_decoding(void)
{
    expect("plain text unchanged", "hello world\n", 512, "hello world\n");
    expect("big5 QP", "=?big5?Q?=A4=A4?=", 512, "\xa4\xa4");
    expect("utf-8 base64 to big5", "=?UTF-8?B?5Lit5paH?=", 512,
           "\xa4\xa4\xa4\xe5");
    expect("adjacent words joined", "=?big5?Q?A?= =?big5?Q?B?=", 512, "AB");
    expect("text around a word", "Re: =?utf-8?Q?a?= x", 512, "Re: a x");
}

static void test_charset_names(void)
{
    expect("alias UTF8", "=?UTF8?Q?hi?=", 512, "hi");
    expect("alias utf_8", "=?utf_8?Q?=E4=B8=AD?=", 512, "\xa4\xa4");
    expect("alias Latin-1", "=?Latin-1?Q?abc?=", 512, "abc");
    expect("iso-8859-15", "=?iso-8859-15?Q?abc?=", 512, "abc");
    expect("RFC 2231 language tag", "=?utf-8*en?Q?=E4=B8=AD?=", 512,
           "\xa4\xa4");
    /* charsets we don't hand to iconv are decoded but not converted */
    expect("unknown charset kept as bytes", "=?x-unknown?Q?hello?=", 512,
           "hello");
    expect("unknown charset, 8-bit bytes", "=?x-unknown?Q?=A4=A4?=", 512,
           "\xa4\xa4");
    expect("iso-8859-17 not converted", "=?iso-8859-17?Q?a?=", 512, "a");
    expect("empty charset", "=??Q?a?=", 512, "a");
}

static void test_whitespace(void)
{
    expect("space before unknown charset word",
           "=?utf-8?Q?a?= =?koi8-r?Q?b?=", 512, "ab");
    expect("space kept before '='", "=?utf-8?Q?a?= =b", 512, "a =b");
    expect("spaces kept before bad word", "=?utf-8?Q?a?=  =?bad", 512,
           "a  =?bad");
    expect("space kept before text", "=?utf-8?Q?a?= plain", 512, "a plain");
}

static void test_control_bytes(void)
{
    expect("decoded NUL", "=?utf-8?Q?a=00b?=", 512, "a b");
    expect("decoded ESC and CRLF", "=?utf-8?Q?=1B[1m=0D=0Ax?=", 512,
           " [1m  x");
    expect("decoded DEL", "=?utf-8?B?fw==?=", 512, " ");
}

static void test_sizes(void)
{
    /* 45 x U+00A7 in latin1 grows from 1 to 2 bytes each in big5 */
    const char *grow = "=?iso-8859-1?B?"
        "p6enp6enp6enp6enp6enp6enp6enp6enp6enp6enp6enp6enp6enp6enp6en?=";
    char want[128] = "";
    char *got;
    int i;

    for (i = 0; i < 45; i++)
        strcat(want, "\xa1\xb1");
    expect("growth fits in 512", grow, 512, want);

    /* in a buffer sized to the 77-byte input, 38 whole characters fit */
    want[38 * 2] = '\0';
    expect("growth cut to whole characters", grow, strlen(grow) + 1, want);

    expect("plain text cut to size - 1", "abcdefghij", 5, "abcd");
    expect("no half big5 char at cut", "\xa4\xa4\xa4\xe5", 4, "\xa4\xa4");
    expect("size 1 gives empty string", "=?big5?Q?A?=", 1, "");

    got = decode("abc", 0);
    report("size 0 writes nothing", got != NULL, NULL);
    free(got);
}

static void test_str_iconv_no_room(void)
{
    char src[] = "abc";
    char d = 'Z';
    size_t r = str_iconv("UTF-8", "BIG5", src, 3, &d, 0);

    report("str_iconv with dstlen 0", r == 0 && d == 'Z', NULL);
}

/*
 * Regression tests for the two reachable stack buffer overflows that the
 * hardening fixed. Both start from the same fact: a decoded word can be
 * longer than its encoded input (Latin-1 "=A7" and its base64 form each
 * decode to the two Big5 bytes 0xA1 0xB1), so the write pointer can be
 * driven to the far end of the 512-byte work buffer, where the old code
 * stopped checking. The analysis is by construction, not fuzzing; these
 * cases drive the fixed code right up to the bound and check, via the
 * guard bytes in decode(), that nothing is written past the buffer.
 */

/*
 * Finding 1 -- unbounded copy of non-encoded text (str_decode_M3).
 *
 * The tail copy "while (src != tmp) *dst++ = *src++;" had no bound. The
 * outer loop only checks the write pointer at the top of each iteration,
 * so after a grown Latin-1 word leaves it near the end of the buffer, a
 * following malformed "=?..." word (one that never completes as "?X?")
 * falls through to that copy and, in the old code, runs off the buffer.
 * The fix bounds the copy by cap - 1.
 */
static void test_overflow_raw_copy(void)
{
    char in[600], body[341];
    char *got;
    int i, p;

    /* "p6en" x 85 is base64 for 255 bytes of 0xA7; Latin-1 -> Big5 doubles
     * that to 510 bytes, leaving the write pointer one short of buf[512]. */
    for (i = 0; i < 85; i++)
        memcpy(body + i * 4, "p6en", 4);
    body[340] = '\0';
    p = snprintf(in, sizeof in, "=?iso-8859-1?B?%s?= =?", body);
    memset(in + p, 'z', 148);		/* a "=?" word with no closing "?" */
    in[p + 148] = '\0';

    got = decode(in, 512);
    report("overflow: trailing raw copy bounded",
           got != NULL && strlen(got) <= 511, got);
    free(got);
}

/*
 * Finding 2 -- length counter wrap in str_iconv.
 *
 * glibc iconv() returns EILSEQ with outbytesleft == 0 when the output
 * fills exactly as the next input byte is invalid (checked directly
 * against glibc). The old EILSEQ branch then ran "*dst = *src; dst++;
 * dstlen--" with dstlen already 0, wrapping the size_t to SIZE_MAX, so
 * the next iconv() call wrote the remaining source with no output bound.
 * The fix stops the loop when dstlen == 0.
 *
 * Here dstlen 2 leaves one writable byte after the reserved NUL: iconv()
 * writes the 'A', fills the byte, then hits the invalid 0xff. The trailing
 * "ABCD..." is the source that the old code would have written past dst.
 */
static void test_overflow_iconv_wrap(void)
{
    char src[] = "A\xff" "ABCDEFGHIJKLMNOP";
    char dst[2 + GUARD];
    size_t r, i;
    int ok = 1;

    memset(dst, 'G', sizeof dst);
    r = str_iconv("UTF-8", "BIG5", src, sizeof(src) - 1, dst, 2);
    for (i = 0; i < GUARD; i++)
        if (dst[2 + i] != 'G')
            ok = 0;
    report("overflow: str_iconv length wrap bounded", ok && r <= 2, NULL);
}

/* True if the Big5 string ends on a lead byte with no trail (a split char). */
static int ends_on_dangling_lead(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    size_t n = strlen(s), i = 0;

    while (i < n) {
        if (big5_is_valid_lead(p[i]) && i + 1 < n && big5_is_valid_trail(p[i + 1]))
            i += 2;
        else if (big5_is_valid_lead(p[i]) && i + 1 == n)
            return 1;
        else
            i++;
    }
    return 0;
}

/*
 * The size limit must land on a Big5 character boundary. A Latin-1 word
 * grows the output (each 0xA7 -> two Big5 bytes) to push the write pointer
 * near the end of the buffer, then plain Big5 text ("\xa4\x40" repeated)
 * is copied; the cut must not leave half of one of those characters.
 */
static void test_trim_growth_boundary(void)
{
    char in[600], body[341];
    char *got;
    int i, p;

    for (i = 0; i < 84; i++)
        memcpy(body + i * 4, "p6en", 4);
    body[336] = '\0';				/* 252 bytes 0xA7 -> 504 Big5 */
    p = snprintf(in, sizeof in, "=?iso-8859-1?B?%s?=", body);
    for (i = 0; i < 70 && p < (int)sizeof in - 3; i++) {
        in[p++] = '\xa4';
        in[p++] = '\x40';
    }
    in[p] = '\0';

    got = decode(in, 512);
    report("trim: growth cut on a char boundary",
           got != NULL && !ends_on_dangling_lead(got), got);
    free(got);
}

/*
 * Regression for the review finding: a stray raw byte kept from an unknown
 * charset used to desync the old trim's Big5 scan and make it cut a *whole*
 * trailing character in half. Build output = [504 Big5][raw 0x80][real char
 * 0xA4 0xA4] landing at the size limit; the real char must stay intact.
 */
static void test_trim_stray_byte(void)
{
    char in[600], body[341];
    char *got;
    size_t n;
    int i, p;

    for (i = 0; i < 84; i++)
        memcpy(body + i * 4, "p6en", 4);
    memcpy(body + 336, "p6c=", 4);		/* 254 bytes 0xA7 -> 508 Big5 */
    body[340] = '\0';
    p = snprintf(in, sizeof in, "=?iso-8859-1?B?%s?= =?x-foo?B?gA==?= =?big5?Q?=A4=A4?=",
                 body);
    in[p] = '\0';

    got = decode(in, 512);
    n = got ? strlen(got) : 0;
    report("trim: stray byte keeps real char whole",
           got != NULL && n >= 2 &&
           (unsigned char)got[n - 2] == 0xA4 && (unsigned char)got[n - 1] == 0xA4,
           got);
    free(got);
}

/* Random headers in random buffer sizes: output must always fit. */
static void test_random(void)
{
    static const char *const cs[] = {
        "big5", "utf-8", "UTF8", "gbk", "iso-8859-1", "latin1", "koi8-r",
        "x-unknown", "utf-8*en", "cp950", "",
    };
    unsigned long rng = 88172645463325252UL;
    char in[600];
    int it, bad = 0;

#define RND() (rng ^= rng << 13, rng ^= rng >> 7, rng ^= rng << 17, \
               (unsigned)rng)
    for (it = 0; it < 20000 && !bad; it++) {
        size_t len = 0, size;
        int parts = 1 + RND() % 10, p, j;
        char *got;

        for (p = 0; p < parts && len < 500; p++) {
            if (RND() % 2) {
                char enc = "BQbq"[RND() % 4];
                int pl = RND() % 60;

                len += snprintf(in + len, sizeof(in) - len, "=?%s?%c?",
                                cs[RND() % (sizeof(cs) / sizeof(cs[0]))], enc);
                for (j = 0; j < pl && len < 590; j++)
                    in[len++] = (enc == 'B' || enc == 'b')
                        ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                          "0123456789+/="[RND() % 65]
                        : (char)(0x21 + RND() % 0x5e);
                if (len < 590) {
                    in[len++] = '?';
                    in[len++] = '=';
                }
            } else {
                int pl = 1 + RND() % 40;

                for (j = 0; j < pl && len < 590; j++)
                    in[len++] = (char)(0x20 + RND() % 0xe0);
            }
            if (RND() % 2 && len < 590)
                in[len++] = ' ';
        }
        if (len > 500)
            len = 500;
        in[len] = '\0';

        size = (RND() % 2) ? len + 1 : 1 + RND() % 512;
        got = decode(in, size);
        if (!got || strlen(got) >= size)
            bad = 1;
        free(got);
    }
    report("20000 random headers fit their buffer", !bad, NULL);
}

int main(void)
{
    test_decoding();
    test_charset_names();
    test_whitespace();
    test_control_bytes();
    test_sizes();
    test_str_iconv_no_room();
    test_overflow_raw_copy();
    test_overflow_iconv_wrap();
    test_trim_growth_boundary();
    test_trim_stray_byte();
    test_random();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
