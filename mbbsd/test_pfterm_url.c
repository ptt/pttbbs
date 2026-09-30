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
    outbuf[0] = '\0';
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
    printf("\nAll 8 test suites PASSED successfully!\n");
    return 0;
}
