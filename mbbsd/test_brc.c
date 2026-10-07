#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "bbs.h"

#ifdef BRC_MAXSIZE
#undef BRC_MAXSIZE
#endif
#ifdef BRC_MAXNUM
#undef BRC_MAXNUM
#endif

// Include brc.c directly to test internal static functions
#include "brc.c"

// Dummy definitions for symbols needed by brc.c
static boardheader_t dummy_bcache[MAX_BOARD];
boardheader_t *bcache = dummy_bcache;
const char *currboard = "";
int currbid = 0;
unsigned int currbrdattr = 0;
userec_t pwcuser;
time4_t now = 1700000000;
time4_t login_start_time = 1700000000;
SHM_t mock_shm;
SHM_t *SHM = &mock_shm;

int b_lines = 24;
void mvprints(int y, int x, const char *fmt, ...) { (void)y; (void)x; (void)fmt; }
void refresh(void) {}

int getbnum(const char *bname) {
    (void)bname;
    return 1;
}

void setuserfile(char *buf, const char *fname) {
    snprintf(buf, STRLEN, "/tmp/test_%s", fname);
}

static void setup_test(void) {
    brc_release();
    brc_initialized = 1;
    brc_currbid = 0;
    brc_num = 0;
    brc_expire_time = now - 365 * DAY_SECONDS;
    pwcuser.userlevel = PERM_BASIC;

    memset(&mock_shm, 0, sizeof(mock_shm));
    SHM = &mock_shm;
}

static void test_basic_unread(void) {
    printf("Testing basic unread record storage and retrieval...\n");
    setup_test();

    brc_rec r1[3];
    for (int i = 0; i < 3; i++) {
        r1[i].create = now - 100 - i * 50;
        r1[i].modified = now - 100 - i * 50;
    }
    brc_insert_record(10, 3, r1);

    int bnum = 0;
    const brc_rec *rec = brc_find_record(10, &bnum);
    assert(rec != NULL);
    assert(bnum == 3);
    for (int i = 0; i < 3; i++) {
        assert(rec[i].create == r1[i].create);
    }
    printf("  -> PASS\n");
}

static void test_move_to_front(void) {
    printf("Testing move-to-front for existing boards (MRU/LRU board order)...\n");
    setup_test();

    brc_rec r[3];
    for (int i = 0; i < 3; i++) {
        r[i].create = now - 100 - i * 10;
        r[i].modified = now - 100 - i * 10;
    }

    // Insert board 10, then board 20.
    // In head-insert, board 20 is at the beginning (offset 0), followed by board 10.
    brc_insert_record(10, 2, r);
    brc_insert_record(20, 2, r);

    // Verify initial layout: board 20 at offset 0, board 10 at offset > 0
    brcbid_t first_bid = brc_read_bid(brc_buf);
    assert(first_bid == 20);

    // Now update board 10 with 3 records (growing the record)
    brc_insert_record(10, 3, r);

    // Move-to-front: board 10 MUST move to offset 0 (head)!
    first_bid = brc_read_bid(brc_buf);
    assert(first_bid == 10);

    int bnum = 0;
    const brc_rec *rec = brc_find_record(10, &bnum);
    assert(rec != NULL && bnum == 3);

    // Board 20 is now behind board 10
    rec = brc_find_record(20, &bnum);
    assert(rec != NULL && bnum == 2);

    // Now shrink board 10 back to 1 record (board 10 is already at head)
    brc_insert_record(10, 1, r);
    first_bid = brc_read_bid(brc_buf);
    assert(first_bid == 10);
    rec = brc_find_record(10, &bnum);
    assert(rec != NULL && bnum == 1);

    // Now update board 20 (behind board 10): it must move back to offset 0!
    brc_insert_record(20, 3, r);
    first_bid = brc_read_bid(brc_buf);
    assert(first_bid == 20);
    rec = brc_find_record(20, &bnum);
    assert(rec != NULL && bnum == 3);
    rec = brc_find_record(10, &bnum);
    assert(rec != NULL && bnum == 1);

    printf("  -> PASS\n");
}

static void test_on_demand_tail_compact(void) {
    printf("Testing on-demand tail-only compaction (gentle compaction)...\n");
    setup_test();

    brc_rec r[5];
    for (int i = 0; i < 5; i++) {
        r[i].create = now - 100 - i * 10;
        r[i].modified = now - 100 - i * 10;
    }

    // Insert board 10, then 20, then 30.
    // Order in brc_buf: Board 30 (head), Board 20 (middle), Board 10 (tail).
    brc_insert_record(10, 5, r);
    brc_insert_record(20, 5, r);
    brc_insert_record(30, 5, r);

    int bnum = 0;
    assert(brc_find_record(10, &bnum) && bnum == 5);
    assert(brc_find_record(20, &bnum) && bnum == 5);
    assert(brc_find_record(30, &bnum) && bnum == 5);

    // Ask to free just 10 bytes over limit
    int needed = (BRC_MAXSIZE - brc_size) + 10;
    brc_compact(needed, 0);

    // ONLY the tail-most board (Board 10) should be compacted to num = 1!
    assert(brc_find_record(10, &bnum) && bnum == 1);
    // Board 20 and Board 30 must REMAIN FULL (num == 5)! Not compacted!
    assert(brc_find_record(20, &bnum) && bnum == 5);
    assert(brc_find_record(30, &bnum) && bnum == 5);

    // If more space is needed, then Board 20 gets compacted
    needed = (BRC_MAXSIZE - brc_size) + 10;
    brc_compact(needed, 0);

    assert(brc_find_record(10, &bnum) && bnum == 1);
    assert(brc_find_record(20, &bnum) && bnum == 1);
    assert(brc_find_record(30, &bnum) && bnum == 5); // Most recent Board 30 still num == 5!

    // Now re-access Board 10 (which was at the tail with num = 1) with 5 records!
    // Move-to-front moves Board 10 to the HEAD (offset 0)!
    brc_insert_record(10, 5, r);
    assert(brc_read_bid(brc_buf) == 10);
    assert(brc_find_record(10, &bnum) && bnum == 5);

    // Buffer order is now: Board 10 (head), Board 30 (middle), Board 20 (tail).
    // If compaction triggers again, Board 30 (now LRU among num > 1) MUST be compacted,
    // and Board 10 (most recently used) MUST be protected!
    needed = (BRC_MAXSIZE - brc_size) + 10;
    brc_compact(needed, 0);

    assert(brc_find_record(30, &bnum) && bnum == 1); // Board 30 compacted!
    assert(brc_find_record(10, &bnum) && bnum == 5); // Board 10 STILL intact at 5!

    printf("  -> PASS\n");
}

static void test_preserve_latest_read_timestamp_and_unread_semantics(void) {
    printf("Testing latest read timestamp preservation and unread semantics...\n");
    setup_test();

    time4_t t3 = now - 100;
    time4_t t2 = now - 300;
    time4_t t1 = now - 500;

    brc_rec r[3];
    r[0].create = t3; r[0].modified = t3;
    r[1].create = t2; r[1].modified = t2;
    r[2].create = t1; r[2].modified = t1;

    brc_insert_record(101, 3, r);

    // Compact board 101 to num = 1
    int needed = (BRC_MAXSIZE - brc_size) + 10;
    brc_compact(needed, 0);

    int bnum = 0;
    const brc_rec *rec = brc_find_record(101, &bnum);
    assert(rec != NULL && bnum == 1);
    assert(rec[0].create == t3); // Kept the latest timestamp!

    // Verify unread logic:
    // Any post on or before t3 is considered READ (0)
    assert(brc_unread_time(101, t3, 0) == 0);
    assert(brc_unread_time(101, t2, 0) == 0);
    assert(brc_unread_time(101, t1, 0) == 0);
    assert(brc_unread_time(101, t1 - 100, 0) == 0);

    // Any new post after t3 is considered UNREAD (1)
    time4_t t_new = t3 + 50;
    assert(brc_unread_time(101, t_new, 0) == 1);

    printf("  -> PASS\n");
}

static void test_recovery_after_compact(void) {
    printf("Testing recovery after compaction (num expands on new post)...\n");
    setup_test();

    time4_t t_base = now - 200;
    brc_rec r[2] = { { t_base, t_base }, { t_base - 100, t_base - 100 } };
    brc_insert_record(105, 2, r);

    // Compact to 1 (board 105 has 2 records, can free 8 bytes. Ask for 5 bytes).
    int needed = (BRC_MAXSIZE - brc_size) + 5;
    brc_compact(needed, 0);

    int bnum = 0;
    assert(brc_find_record(105, &bnum) && bnum == 1);

    // User enters board 105
    brc_currbid = 105;
    currbid = 105;
    currboard = "test105";
    brc_read_record(105, &brc_num, brc_list);
    assert(brc_num == 1 && brc_list[0].create == t_base);

    // User reads an older post (t_base - 50) -> ignored by brc_addlist (already read)
    char fn_old[32];
    snprintf(fn_old, sizeof(fn_old), "M.%d.A.001", (int)(t_base - 50));
    brc_addlist(fn_old, t_base - 50);
    assert(brc_num == 1);

    // User reads a newer post (now - 10) -> added and num expands to 2!
    time4_t t_new = now - 10;
    char fn_new[32];
    snprintf(fn_new, sizeof(fn_new), "M.%d.A.002", (int)t_new);
    brc_addlist(fn_new, t_new);
    assert(brc_num == 2);
    assert(brc_list[0].create == t_new);
    assert(brc_list[1].create == t_base);

    // Update back to buffer
    brc_update();
    const brc_rec *rec = brc_find_record(105, &bnum);
    assert(rec && bnum == 2);
    assert(rec[0].create == t_new && rec[1].create == t_base);

    printf("  -> PASS\n");
}

static void test_head_insert_for_new_board(void) {
    printf("Testing head insert for newly accessed boards...\n");
    setup_test();

    brc_rec r[1] = { { now - 10, now - 10 } };
    brc_insert_record(10, 1, r);
    assert(brc_read_bid(brc_buf) == 10);

    brc_insert_record(20, 1, r);
    assert(brc_read_bid(brc_buf) == 20);

    brc_insert_record(30, 1, r);
    assert(brc_read_bid(brc_buf) == 30);

    printf("  -> PASS\n");
}

static void test_delete_record(void) {
    printf("Testing record deletion (num == 0)...\n");
    setup_test();

    brc_rec r[2] = { { now - 10, now - 10 }, { now - 20, now - 20 } };
    brc_insert_record(10, 2, r);
    brc_insert_record(20, 2, r);
    brc_insert_record(30, 2, r);

    int bnum = 0;
    assert(brc_find_record(20, &bnum) != NULL);

    // Delete board 20
    brc_insert_record(20, 0, NULL);
    assert(brc_find_record(20, &bnum) == NULL);

    // Board 10 and 30 must remain intact
    assert(brc_find_record(10, &bnum) != NULL && bnum == 2);
    assert(brc_find_record(30, &bnum) != NULL && bnum == 2);

    printf("  -> PASS\n");
}

static void test_buffer_overflow_with_compact(void) {
    printf("Testing full buffer overflow with on-demand compaction...\n");
    setup_test();

    brc_rec r[10];
    for (int i = 0; i < 10; i++) {
        r[i].create = now - 100 - i * 10;
        r[i].modified = now - 100 - i * 10;
    }

    // Insert 800 boards with 10 records each (each board takes 6 + 10*8 = 86 bytes)
    // Total raw data: 800 * 86 = 68,800 bytes > BRC_MAXSIZE (65,536)
    for (int b = 1; b <= 800; b++) {
        brc_insert_record(b, 10, r);
        assert(brc_size <= BRC_MAXSIZE);
    }

    assert(brc_size <= BRC_MAXSIZE);

    // The most recently inserted board (800) must be at the head and fully intact with 10 records
    int bnum = 0;
    const brc_rec *rec = brc_find_record(800, &bnum);
    assert(rec != NULL && bnum == 10);

    // Earlier boards should have been compacted to num = 1 rather than being dropped!
    // With 600 boards, compacted size is 600 * 14 = 8400 bytes, which easily fits in 49152!
    // So Board 1 should STILL exist with num = 1!
    rec = brc_find_record(1, &bnum);
    assert(rec != NULL);
    assert(bnum == 1); // Compacted to 1, not dropped!

    printf("  -> PASS\n");
}

static void test_brcstore_filename(void) {
    printf("Testing brcstore filename hashing and generation...\n");
    char path[PATHLEN];
    strcpy(pwcuser.userid, "SYSOP");
    get_brc_alt_filename(path);
    unsigned hash = StringHash("SYSOP") & 0xffff;
    char expected[PATHLEN];
    snprintf(expected, sizeof(expected), "%s/%02x/%02x/brc.SYSOP",
             BRCSTORE_DIR, (hash >> 8) & 0xff, hash & 0xff);
    assert(strcmp(path, expected) == 0);
    assert(strncmp(path, BRCSTORE_DIR "/", strlen(BRCSTORE_DIR) + 1) == 0);
    printf("  -> PASS (path: %s)\n", path);
}

int main(void) {
    printf("Running comprehensive BRC on-demand compacting tests...\n");
    test_basic_unread();
    test_move_to_front();
    test_head_insert_for_new_board();
    test_delete_record();
    test_on_demand_tail_compact();
    test_preserve_latest_read_timestamp_and_unread_semantics();
    test_recovery_after_compact();
    test_buffer_overflow_with_compact();
    test_brcstore_filename();
    printf("\nAll BRC on-demand compacting tests PASSED successfully!\n");
    return 0;
}
