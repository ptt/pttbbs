#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include "bbs.h"

// Mock terminal output capture
static char outbuf[131072];
static int outlen = 0;

int ochar(int c) {
    if (outlen < (int)sizeof(outbuf) - 1)
        outbuf[outlen++] = (char)c;
    return c;
}

void oflush(void) {
    outbuf[outlen] = '\0';
}

void obegin_frame(void) {}
void oend_frame(void) {}
int vkey_is_typeahead(void) { return 0; }
int t_lines = 24, t_columns = 80;
ConvertMode convert_mode = CONV_UTF8;
userec_t pwcuser;

static void reset_outbuf(void) {
    outlen = 0;
    memset(outbuf, 0, sizeof(outbuf));
}

static void test_basic_hyperlink(void) {
    printf("Testing basic hyperlink...\n");
    initscr();
    reset_outbuf();

    start_url("https://term.ptt.cc");
    outs("PTT");
    end_url();
    outs(" BBS");

    assert(get_url_at(0, 0) != NULL && strcmp(get_url_at(0, 0), "https://term.ptt.cc") == 0);
    assert(get_url_at(0, 1) != NULL && strcmp(get_url_at(0, 1), "https://term.ptt.cc") == 0);
    assert(get_url_at(0, 2) != NULL && strcmp(get_url_at(0, 2), "https://term.ptt.cc") == 0);
    assert(get_url_at(0, 3) == NULL);
    assert(get_url_at(0, 4) == NULL);

    reset_outbuf();
    doupdate();

    // Verify OSC 8 open and close in output
    assert(strstr(outbuf, "\x1b]8;;https://term.ptt.cc\x1b\\PTT\x1b]8;;\x1b\\ BBS") != NULL);
    printf("  -> PASS\n");
}

static void test_color_in_hyperlink(void) {
    printf("Testing color changes inside hyperlink...\n");
    clear();
    reset_outbuf();

    start_url("https://example.com");
    outs("\033[1;33mYellow \033[32mGreen\033[m");
    end_url();
    outs(" Normal");

    // All characters of "Yellow Green" must have the URL
    for (int x = 0; x < 12; x++) {
        const char *u = get_url_at(0, x);
        assert(u != NULL && strcmp(u, "https://example.com") == 0);
    }
    assert(get_url_at(0, 12) == NULL); // " Normal"

    reset_outbuf();
    doupdate();

    // Check that link is maintained across ANSI color changes
    assert(strstr(outbuf, "\x1b]8;;https://example.com\x1b\\") != NULL);
    assert(strstr(outbuf, "Yellow") != NULL);
    assert(strstr(outbuf, "Green") != NULL);
    assert(strstr(outbuf, "\x1b]8;;\x1b\\") != NULL);
    printf("  -> PASS\n");
}

static void test_adjacent_different_urls(void) {
    printf("Testing adjacent different URLs...\n");
    clear();
    reset_outbuf();

    start_url("https://url1.com");
    outs("AAA");
    end_url();

    start_url("https://url2.com");
    outs("BBB");
    end_url();

    assert(get_url_at(0, 0) != NULL && strcmp(get_url_at(0, 0), "https://url1.com") == 0);
    assert(get_url_at(0, 1) != NULL && strcmp(get_url_at(0, 1), "https://url1.com") == 0);
    assert(get_url_at(0, 2) != NULL && strcmp(get_url_at(0, 2), "https://url1.com") == 0);

    assert(get_url_at(0, 3) != NULL && strcmp(get_url_at(0, 3), "https://url2.com") == 0);
    assert(get_url_at(0, 4) != NULL && strcmp(get_url_at(0, 4), "https://url2.com") == 0);
    assert(get_url_at(0, 5) != NULL && strcmp(get_url_at(0, 5), "https://url2.com") == 0);

    reset_outbuf();
    doupdate();

    assert(strstr(outbuf, "\x1b]8;;https://url1.com\x1b\\AAA") != NULL);
    assert(strstr(outbuf, "\x1b]8;;https://url2.com\x1b\\BBB\x1b]8;;\x1b\\") != NULL);
    printf("  -> PASS\n");
}

static void test_clear_and_movement(void) {
    printf("Testing clear and cursor movements...\n");
    clear();
    move(2, 5);
    start_url("https://move.test");
    outs("LinkAt2_5");
    end_url();

    assert(get_url_at(2, 5) != NULL && strcmp(get_url_at(2, 5), "https://move.test") == 0);
    assert(get_url_at(0, 0) == NULL);

    reset_outbuf();
    doupdate();
    assert(strstr(outbuf, "\x1b]8;;https://move.test\x1b\\LinkAt2_5\x1b]8;;\x1b\\") != NULL);

    // Now clear screen
    clear();
    assert(get_url_at(2, 5) == NULL);
    printf("  -> PASS\n");
}

static void test_stream_osc8(void) {
    printf("Testing stream OSC 8 input via outs()...\n");
    clear();
    outs("\x1b]8;;https://stream-osc8.com\x1b\\StreamText\x1b]8;;\x1b\\ Tail");

    assert(get_url_at(0, 0) != NULL && strcmp(get_url_at(0, 0), "https://stream-osc8.com") == 0);
    assert(get_url_at(0, 9) != NULL && strcmp(get_url_at(0, 9), "https://stream-osc8.com") == 0);
    assert(get_url_at(0, 10) == NULL); // " Tail"

    reset_outbuf();
    doupdate();
    assert(strstr(outbuf, "\x1b]8;;https://stream-osc8.com\x1b\\StreamText\x1b]8;;\x1b\\ Tail") != NULL);
    printf("  -> PASS\n");
}

static void test_scr_dump_and_restore(void) {
    printf("Testing scr_dump and scr_restore with hyperlinks...\n");
    clear();
    move(1, 0);
    start_url("https://dump-restore.test");
    outs("SavedLink");
    end_url();

    screen_backup_t psb;
    scr_dump(&psb);

    // Modify screen
    clear();
    move(1, 0);
    outs("Overwritten");
    assert(get_url_at(1, 0) == NULL);

    // Restore screen
    reset_outbuf();
    scr_restore(&psb);

    assert(get_url_at(1, 0) != NULL && strcmp(get_url_at(1, 0), "https://dump-restore.test") == 0);
    assert(strstr(outbuf, "\x1b]8;;https://dump-restore.test\x1b\\SavedLink\x1b]8;;\x1b\\") != NULL);
    printf("  -> PASS\n");
}

static void test_url_pool_eviction_and_invalidation(void) {
    printf("Testing URL pool ring-buffer eviction and safe invalidation (MAX_URLS=64)...\n");
    clear();

    // 1. Place URL 0 at (0, 0)
    move(0, 0);
    start_url("https://initial-link.org");
    outs("FirstLink");
    end_url();
    assert(get_url_at(0, 0) != NULL && strcmp(get_url_at(0, 0), "https://initial-link.org") == 0);

    // 2. Output 70 different URLs on other lines to exceed MAX_URLS (64)
    char urlbuf[128];
    for (int i = 1; i <= 70; i++) {
        snprintf(urlbuf, sizeof(urlbuf), "https://stream-url-%d.org", i);
        move(1, 0);
        start_url(urlbuf);
        outs("Streaming");
        end_url();
    }

    // 3. Most recent link (URL 70) should be valid at (1, 0)
    assert(get_url_at(1, 0) != NULL && strcmp(get_url_at(1, 0), "https://stream-url-70.org") == 0);

    // 4. Initial link at (0, 0) was evicted by the ring buffer.
    // It MUST be invalidated safely (returning NULL), and NEVER misidentified / aliased to a new URL!
    assert(get_url_at(0, 0) == NULL);

    // 5. Verify doupdate() outputs FirstLink as regular text without any wrong hyperlink
    reset_outbuf();
    doupdate();
    assert(strstr(outbuf, "\x1b]8;;https://initial-link.org") == NULL);
    assert(strstr(outbuf, "FirstLink") != NULL);

    printf("  -> PASS\n");
}

static void test_multibyte_cjk(void) {
    printf("Testing CJK multibyte text with hyperlinks...\n");
    clear();
    start_url("https://cjk.test/tw");
    outs("§å½ð½ð¹ê·~§{");
    end_url();

    int end_col = 0;
    getyx(NULL, &end_col);
    assert(end_col > 0);
    for (int x = 0; x < end_col; x++) {
        const char *u = get_url_at(0, x);
        assert(u != NULL && strcmp(u, "https://cjk.test/tw") == 0);
    }
    assert(get_url_at(0, end_col) == NULL);

    reset_outbuf();
    doupdate();
    assert(strstr(outbuf, "\x1b]8;;https://cjk.test/tw\x1b\\") != NULL);
    assert(strstr(outbuf, "\x1b]8;;\x1b\\") != NULL);
    printf("  -> PASS\n");
}

static void test_url_matching(void) {
    printf("Testing URL matching rules...\n");
    int ulen = 0;

    // Valid URLs
    const char *u1 = "https://google.com";
    assert(match_url(u1, strlen(u1), &ulen));
    assert(ulen == 18);

    const char *u2 = "http://ptt.cc/bbs/index.html";
    assert(match_url(u2, strlen(u2), &ulen));
    assert(ulen == (int)strlen(u2));

    // Trailing punctuation trimming
    const char *u3 = "https://ptt.cc.";
    assert(match_url(u3, strlen(u3), &ulen));
    assert(ulen == 14);

    const char *u4 = "https://ptt.cc, next word";
    assert(match_url(u4, strlen(u4), &ulen));
    assert(ulen == 14);

    const char *u5 = "https://ptt.cc;";
    assert(match_url(u5, strlen(u5), &ulen));
    assert(ulen == 14);

    // Query parameters
    const char *u6 = "https://ptt.cc/search?q=foo+bar&cat=1";
    assert(match_url(u6, strlen(u6), &ulen));
    assert(ulen == (int)strlen(u6));

    // Parentheses handling
    const char *u7 = "https://en.wikipedia.org/wiki/Foo_(bar)";
    assert(match_url(u7, strlen(u7), &ulen));
    assert(ulen == (int)strlen(u7));

    const char *u8 = "https://en.wikipedia.org/wiki/Foo_(bar))";
    assert(match_url(u8, strlen(u8), &ulen));
    assert(ulen == (int)strlen(u7));

    // URL broken at delimiters (+, =, /, &)
    const char *u9 = "https://ptt.cc/path?q=123+";
    assert(match_url(u9, strlen(u9), &ulen));
    assert(ulen == (int)strlen(u9));

    // Invalid URLs
    assert(!match_url("http://", 7, &ulen));
    assert(!match_url("https://", 8, &ulen));
    assert(!match_url("ftp://ptt.cc", 12, &ulen));
    assert(!match_url("javascript:alert(1)", 19, &ulen));
    assert(!match_url("not a url", 9, &ulen));

    // URL continuation
    const char *c1 = "param=123&more=456";
    assert(match_url_continuation(c1, strlen(c1)) == (int)strlen(c1));

    const char *c2 = "param=123. Next sentence";
    assert(match_url_continuation(c2, strlen(c2)) == 9);

    // Test url_tracker_init with single-line URL
    url_tracker_t trk;
    const char *s_single = "https://ptt.cc for info";
    int s_ulen = 0;
    assert(match_url(s_single, strlen(s_single), &s_ulen));
    assert(url_tracker_init(&trk, s_single, s_ulen, s_single + strlen(s_single), 0));
    assert(trk.in_url == true);
    assert(trk.cont_next_line == false);
    assert(strcmp(trk.url, "https://ptt.cc") == 0);
    assert(trk.cur_line_bytes_left == s_ulen);

    // Test url_tracker_init with multi-line broken URL (wrapped at column 79)
    const char *s_multi = "https://ptt.cc/bbs/Gossiping/M.1234567890.A.123.html?query=something_very_long+\nbar&cat=1\n";
    int m_ulen = 0;
    assert(match_url(s_multi, strlen(s_multi), &m_ulen));
    assert(m_ulen == 79);
    assert(url_tracker_init(&trk, s_multi, m_ulen, s_multi + strlen(s_multi), 0));
    assert(trk.in_url == true);
    assert(trk.cont_next_line == true);
    assert(strcmp(trk.url, "https://ptt.cc/bbs/Gossiping/M.1234567890.A.123.html?query=something_very_long+bar&cat=1") == 0);
    assert(trk.cur_line_bytes_left == m_ulen);

    // Next line transition
    const char *line2_ptr = strchr(s_multi, '\n') + 1;
    assert(url_tracker_next_line(&trk, line2_ptr, s_multi + strlen(s_multi)));
    assert(trk.in_url == true);
    assert(trk.cont_next_line == true); // touches \n
    assert(trk.cur_line_bytes_left == (int)strlen("bar&cat=1"));
    assert(strcmp(trk.url, "https://ptt.cc/bbs/Gossiping/M.1234567890.A.123.html?query=something_very_long+bar&cat=1") == 0);

    // Test url_tracker_init_from_prev_line
    url_tracker_t trk2;
    assert(url_tracker_init_from_prev_line(&trk2, line2_ptr, s_multi, s_multi + strlen(s_multi)));
    assert(trk2.in_url == true);
    assert(trk2.cur_line_bytes_left == (int)strlen("bar&cat=1"));
    assert(strcmp(trk2.url, "https://ptt.cc/bbs/Gossiping/M.1234567890.A.123.html?query=something_very_long+bar&cat=1") == 0);

    // Test early broken URL (< 74 cols, e.g. 29 cols) should NOT continue
    const char *s_early_break = "https://ptt.cc/search?q=foo+\nbar&cat=1\n";
    int e_ulen = 0;
    assert(match_url(s_early_break, strlen(s_early_break), &e_ulen));
    assert(url_tracker_init(&trk, s_early_break, e_ulen, s_early_break + strlen(s_early_break), 0));
    assert(trk.in_url == true);
    assert(trk.cont_next_line == false);
    assert(strcmp(trk.url, "https://ptt.cc/search?q=foo+") == 0);

    // Test long line (> 80 cols) should NOT continue across \n
    const char *s_long_line = "https://ptt.cc/bbs/Gossiping/M.1234567890.A.123.html?query=something_very_long_exceeding_eighty_columns\nNext line\n";
    int l_ulen = 0;
    assert(match_url(s_long_line, strlen(s_long_line), &l_ulen));
    assert(l_ulen > 80);
    assert(url_tracker_init(&trk, s_long_line, l_ulen, s_long_line + strlen(s_long_line), 0));
    assert(trk.in_url == true);
    assert(trk.cont_next_line == false);

    // Test quoted URL reaching boundary (col 2 + len 74 = 76 cols) DOES continue
    const char *s_quote_cont = ": https://ptt.cc/bbs/Gossiping/M.1234567890.A.123.html?query=something_long+\n: bar&cat=1\n";
    int qc_ulen = 0;
    assert(match_url(s_quote_cont + 2, strlen(s_quote_cont + 2), &qc_ulen));
    assert(qc_ulen == 74);
    assert(url_tracker_init(&trk, s_quote_cont + 2, qc_ulen, s_quote_cont + strlen(s_quote_cont), 2));
    assert(trk.in_url == true);
    assert(trk.cont_next_line == true);
    assert(strcmp(trk.url, "https://ptt.cc/bbs/Gossiping/M.1234567890.A.123.html?query=something_long+bar&cat=1") == 0);

    // Test two consecutive URL lines should NOT be merged
    const char *s_two_urls = "https://ptt.cc/\nhttps://google.com/\n";
    int ulen1 = 0;
    assert(match_url(s_two_urls, strlen(s_two_urls), &ulen1));
    assert(url_tracker_init(&trk, s_two_urls, ulen1, s_two_urls + strlen(s_two_urls), 0));
    assert(trk.in_url == true);
    assert(trk.cont_next_line == false);
    assert(strcmp(trk.url, "https://ptt.cc/") == 0);
    assert(trk.cur_line_bytes_left == ulen1);

    // Test two consecutive 76-char URL lines (reaching wrap boundary) should NOT be merged
    const char *s_two_long_urls =
        "https://example.com/test1/very_long_url_reaching_seventy_six_chars_exactly__\n"
        "https://example.com/test2/very_long_url_reaching_seventy_six_chars_exactly__\n";
    int ulen_l1 = 0;
    assert(match_url(s_two_long_urls, strlen(s_two_long_urls), &ulen_l1));
    assert(ulen_l1 == 76);
    assert(url_tracker_init(&trk, s_two_long_urls, ulen_l1, s_two_long_urls + strlen(s_two_long_urls), 0));
    assert(trk.in_url == true);
    assert(trk.cont_next_line == false);
    assert(strcmp(trk.url, "https://example.com/test1/very_long_url_reaching_seventy_six_chars_exactly__") == 0);

    // Test multi-line Markdown double URL tracking (wrapped across 4 lines)
    const char *md_text =
        "[https://www.apple.com/tw/shop/buy-iphone/iphone-18-pro/6.3-%E5%90%8B%E9%A1%AF\n"
        "%E7%A4%BA%E5%99%A8-256gb-%E9%BB%91%E8%89%B2](https://www.apple.com/tw/shop/buy\n"
        "-iphone/iphone-18-pro/6.3-%E5%90%8B%E9%A1%AF%E7%A4%BA%E5%99%A8-256gb-%E9%BB%91\n"
        "%E8%89%B2)\n";

    // URL 1 should stitch across line 1 and line 2
    int md_u1_len = 0;
    assert(match_url(md_text + 1, strlen(md_text + 1), &md_u1_len));
    assert(url_tracker_init(&trk, md_text + 1, md_u1_len, md_text + strlen(md_text), 1));
    assert(trk.in_url == true);
    assert(trk.cont_next_line == true);
    assert(strcmp(trk.url, "https://www.apple.com/tw/shop/buy-iphone/iphone-18-pro/6.3-%E5%90%8B%E9%A1%AF%E7%A4%BA%E5%99%A8-256gb-%E9%BB%91%E8%89%B2") == 0);

    // Transition to line 2 for URL 1
    const char *md_l2 = strchr(md_text, '\n') + 1;
    assert(url_tracker_next_line(&trk, md_l2, md_text + strlen(md_text)));
    assert(trk.cont_next_line == false); // ends at ']' on line 2!

    // On line 2, URL 2 starts after "](", at col 44
    const char *md_l2_u2 = strstr(md_l2, "https://");
    assert(md_l2_u2 != NULL);
    int md_u2_len = 0;
    assert(match_url(md_l2_u2, strlen(md_l2_u2), &md_u2_len));
    url_tracker_t trk_u2;
    int md_u2_col = (int)(md_l2_u2 - md_l2);
    assert(url_tracker_init(&trk_u2, md_l2_u2, md_u2_len, md_text + strlen(md_text), md_u2_col));
    assert(trk_u2.in_url == true);
    assert(trk_u2.cont_next_line == true);
    assert(strcmp(trk_u2.url, "https://www.apple.com/tw/shop/buy-iphone/iphone-18-pro/6.3-%E5%90%8B%E9%A1%AF%E7%A4%BA%E5%99%A8-256gb-%E9%BB%91%E8%89%B2") == 0);

    // Test quote line with new URL should also NOT be merged
    const char *s_quote_urls = "https://ptt.cc/search?q=123&\n: https://google.com/\n";
    int ulen_q = 0;
    assert(match_url(s_quote_urls, strlen(s_quote_urls), &ulen_q));
    assert(url_tracker_init(&trk, s_quote_urls, ulen_q, s_quote_urls + strlen(s_quote_urls), 0));
    assert(trk.in_url == true);
    assert(trk.cont_next_line == false);
    assert(strcmp(trk.url, "https://ptt.cc/search?q=123&") == 0);

    printf("  -> PASS\n");
}

static void test_pmore_url_modes(void) {
    printf("Testing pmore URL modes (single line, soft-wrap, and multi-line joining)...\n");

    // 1. Intact single-line URL reading mode simulation:
    // Intact single-line URLs do NOT emit OSC 8; terminal client regex handles them natively.
    initscr();
    reset_outbuf();
    const char *line1 = "Check https://ptt.cc for info.";
    const char *p = line1;
    while (*p) {
        outc(*p++);
    }
    reset_outbuf();
    doupdate();
    oflush();
    // Verify no OSC 8 escape sequences are emitted for intact single-line URLs
    assert(strstr(outbuf, "\x1b]8;;") == NULL);
    assert(strstr(outbuf, "https://ptt.cc") != NULL);

    // 2. Soft-wrap (æŠ˜è¡Œ) of a 120-char URL across 2 rows (row width 78):
    clear();
    reset_outbuf();
    const char *long_url = "https://example.com/very/long/path/query?param1=value1&param2=value2&param3=value3&param4=value4&param5=value5&extra=abcdefgh";
    int long_len = strlen(long_url);
    int row_max = 78;

    // Row 0 gets first 78 chars
    move(0, 0);
    start_url(long_url);
    for (int i = 0; i < row_max; i++)
        outc(long_url[i]);
    end_url();

    // Row 1 gets remaining chars
    move(1, 0);
    start_url(long_url);
    for (int i = row_max; i < long_len; i++)
        outc(long_url[i]);
    end_url();

    // Verify Row 0 has full URL attached to every cell
    for (int x = 0; x < row_max; x++) {
        const char *u = get_url_at(0, x);
        assert(u != NULL && strcmp(u, long_url) == 0);
    }
    // Verify Row 1 has full URL attached to every cell
    for (int x = 0; x < long_len - row_max; x++) {
        const char *u = get_url_at(1, x);
        assert(u != NULL && strcmp(u, long_url) == 0);
    }
    assert(get_url_at(1, long_len - row_max) == NULL);

    reset_outbuf();
    doupdate();
    // Both lines must contain OSC 8 with the full URL
    assert(strstr(outbuf, "\x1b]8;;https://example.com/very/long/path/query?param1=value1") != NULL);

    // 2b. Truncate mode (æˆªè¡Œ) of a 120-char URL on Row 0 (not wrapped to Row 1):
    clear();
    reset_outbuf();
    const char *trunc_url = "https://trunc.example.com/very/long/path/query?param1=value1&param2=value2&param3=value3&param4=value4&param5=value5";
    move(0, 0);
    start_url(trunc_url);
    for (int i = 0; i < row_max; i++)
        outc(trunc_url[i]);
    end_url();

    // Verify Row 0 has full URL attached to every cell
    for (int x = 0; x < row_max; x++) {
        const char *u = get_url_at(0, x);
        assert(u != NULL && strcmp(u, trunc_url) == 0);
    }
    // Verify Row 1 has NO URL (it was truncated, not wrapped)
    assert(get_url_at(1, 0) == NULL);

    reset_outbuf();
    doupdate();
    oflush();
    // Outbuf still contains OSC 8 with the full URL for Row 0
    assert(strstr(outbuf, "\x1b]8;;https://trunc.example.com/very/long/path/query?param1=value1") != NULL);

    // 3. Multi-line hard break (æ–·è¡Œ) joining simulation:
    clear();
    reset_outbuf();
    const char *part1 = "https://ptt.cc/search?q=query_part1+";
    const char *part2 = "part2_rest_of_query&cat=news";
    char joined_url[256];
    snprintf(joined_url, sizeof(joined_url), "%s%s", part1, part2);

    // Line 1:
    move(0, 0);
    start_url(joined_url);
    for (int i = 0; part1[i]; i++)
        outc(part1[i]);
    end_url();

    // Line 2:
    move(1, 0);
    start_url(joined_url);
    for (int i = 0; part2[i]; i++)
        outc(part2[i]);
    end_url();

    // Verify every cell of Line 1 and Line 2 has joined_url
    for (int x = 0; x < (int)strlen(part1); x++) {
        const char *u = get_url_at(0, x);
        assert(u != NULL && strcmp(u, joined_url) == 0);
    }
    for (int x = 0; x < (int)strlen(part2); x++) {
        const char *u = get_url_at(1, x);
        assert(u != NULL && strcmp(u, joined_url) == 0);
    }

    // 4. Raw / Show control code mode (MFDISP_RAW_NOANSI) simulation:
    clear();
    reset_outbuf();
    outs("\033[4;36m");
    for (int i = 0; line1[i]; i++)
        outc(line1[i]);
    outs("\033[m");
    doupdate();
    // In raw mode, no hyperlinks attached to cells, syntax highlight visible
    assert(get_url_at(0, 6) == NULL);
    assert(strstr(outbuf, "https://ptt.cc") != NULL);

    // 5. Two distinct URLs on the same line (e.g. Markdown link with URL as label):
    clear();
    reset_outbuf();
    const char *url_label = "https://example.com/url1";
    const char *url_dest  = "https://example.com/url2";
    // [url1](url2)
    move(0, 0);
    outc('[');
    start_url(url_label);
    for (int i = 0; url_label[i]; i++)
        outc(url_label[i]);
    end_url();
    outc(']');
    outc('(' );
    start_url(url_dest);
    for (int i = 0; url_dest[i]; i++)
        outc(url_dest[i]);
    end_url();
    outc(')');

    // Verify cell at url1 has url_label
    const char *u_at_label = get_url_at(0, 1);
    assert(u_at_label != NULL && strcmp(u_at_label, url_label) == 0);

    // Verify cell at url2 has url_dest
    int dest_start_col = 1 + strlen(url_label) + 2; // '[' + label + ""](""
    const char *u_at_dest = get_url_at(0, dest_start_col);
    assert(u_at_dest != NULL && strcmp(u_at_dest, url_dest) == 0);

    // Verify separator ']' and '(' have no URL
    assert(get_url_at(0, 1 + strlen(url_label)) == NULL);
    assert(get_url_at(0, 1 + strlen(url_label) + 1) == NULL);

    printf("  -> PASS\n");
}

int main(void) {
    printf("Running comprehensive pfterm hyperlink tests...\n");
    test_basic_hyperlink();
    test_color_in_hyperlink();
    test_adjacent_different_urls();
    test_clear_and_movement();
    test_stream_osc8();
    test_scr_dump_and_restore();
    test_url_pool_eviction_and_invalidation();
    test_multibyte_cjk();
    test_url_matching();
    test_pmore_url_modes();
    printf("\nAll 10 test suites PASSED successfully!\n");
    return 0;
}
