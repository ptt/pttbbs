#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "bbs.h"

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

static void test_hotboard_rank(void) {
    printf("Testing hotboard rank...\n");
    setup_test();

    // No hotboards yet
    assert(brc_get_hotboard_rank(10) == -1);

    // Setup 3 hotboards:
    // Rank 0: bid 10 (bidx 9)
    // Rank 1: bid 20 (bidx 19)
    // Rank 2: bid 30 (bidx 29)
    SHM->hotboards.num = 3;
    SHM->hotboards.bids[0] = 9;   // bid 10
    SHM->hotboards.bids[1] = 19;  // bid 20
    SHM->hotboards.bids[2] = 29;  // bid 30

    assert(brc_get_hotboard_rank(10) == 0);
    assert(brc_get_hotboard_rank(20) == 1);
    assert(brc_get_hotboard_rank(30) == 2);
    assert(brc_get_hotboard_rank(100) == -1);
    assert(brc_get_hotboard_rank(1) == -1);

    printf("  -> PASS\n");
}

static void test_basic_unread_and_compact(void) {
    printf("Testing basic unread logic before and after compact...\n");
    setup_test();

    time4_t t400 = now - 400;
    time4_t t300 = now - 300;
    time4_t t100 = now - 100;
    time4_t t50  = now - 50;
    time4_t t200 = now - 200;
    time4_t t350 = now - 350;
    time4_t t500 = now - 500;

    // Non-hotboard 101: records at t100 (newest), t300, t400 (oldest)
    brc_rec list[3];
    list[0].create = t100; list[0].modified = t100;
    list[1].create = t300; list[1].modified = t300;
    list[2].create = t400; list[2].modified = t400;

    brc_insert_record(101, 3, list);

    // Check unread logic:
    // t50 is newer than list[0] (t100) -> unread (1)
    assert(brc_unread_time(101, t50, t50) == 1);
    // t100 is read (0)
    assert(brc_unread_time(101, t100, t100) == 0);
    // t200 was skipped between t100 and t300 -> unread (1)
    assert(brc_unread_time(101, t200, t200) == 1);
    // t300 is read (0)
    assert(brc_unread_time(101, t300, t300) == 0);
    // t350 was skipped between t300 and t400 -> unread (1)
    assert(brc_unread_time(101, t350, t350) == 1);
    // t400 is read (0)
    assert(brc_unread_time(101, t400, t400) == 0);
    // t500 is older than oldest record (t400) -> read (0)
    assert(brc_unread_time(101, t500, t500) == 0);

    // Now compact non-hotboards
    brc_compact_non_hotboards(0);

    // Verify board 101 is compacted to num = 1
    int bnum = 0;
    const brc_rec *rec = brc_find_record(101, &bnum);
    assert(rec != NULL);
    assert(bnum == 1);
    assert(rec[0].create == t100);

    // Check unread logic on compacted board:
    // newer than t100 -> unread (1)
    assert(brc_unread_time(101, t50, t50) == 1);
    // t100 -> read (0)
    assert(brc_unread_time(101, t100, t100) == 0);
    // older than t100 -> all deemed read (0)
    assert(brc_unread_time(101, t200, t200) == 0);
    assert(brc_unread_time(101, t300, t300) == 0);
    assert(brc_unread_time(101, t350, t350) == 0);
    assert(brc_unread_time(101, t400, t400) == 0);
    assert(brc_unread_time(101, t500, t500) == 0);

    printf("  -> PASS\n");
}

static void test_compaction_order(void) {
    printf("Testing compaction priority: non-hotboard first, then hotboards low-to-high rank...\n");
    setup_test();

    SHM->hotboards.num = 3;
    SHM->hotboards.bids[0] = 9;   // bid 10 (Rank 0, hottest)
    SHM->hotboards.bids[1] = 19;  // bid 20 (Rank 1)
    SHM->hotboards.bids[2] = 29;  // bid 30 (Rank 2, lowest hotboard)

    brc_rec r5[5];
    for (int i = 0; i < 5; i++) {
        r5[i].create = now - 100 - i * 100;
        r5[i].modified = now - 100 - i * 100;
    }

    // Insert hotboards 10, 20, 30 and non-hotboards 101, 102
    brc_insert_record(10, 5, r5);
    brc_insert_record(20, 5, r5);
    brc_insert_record(30, 5, r5);
    brc_insert_record(101, 5, r5);
    brc_insert_record(102, 5, r5);

    int bnum = 0;
    assert(brc_find_record(10, &bnum) && bnum == 5);
    assert(brc_find_record(20, &bnum) && bnum == 5);
    assert(brc_find_record(30, &bnum) && bnum == 5);
    assert(brc_find_record(101, &bnum) && bnum == 5);
    assert(brc_find_record(102, &bnum) && bnum == 5);

    // Step 1 test: compact non-hotboards
    brc_compact_non_hotboards(0);

    // Non-hotboards 101 and 102 should be compacted to 1
    assert(brc_find_record(101, &bnum) && bnum == 1);
    assert(brc_find_record(102, &bnum) && bnum == 1);

    // Hotboards 10, 20, 30 must remain untouched (num == 5)!
    assert(brc_find_record(10, &bnum) && bnum == 5);
    assert(brc_find_record(20, &bnum) && bnum == 5);
    assert(brc_find_record(30, &bnum) && bnum == 5);

    // Step 2 test: compact hotboards by rank
    // Free enough bytes to force compacting 1 hotboard:
    // Board 30 (Rank 2, lowest hotboard) should be compacted first!
    int needed = (BRC_MAXSIZE - brc_size) + 10; // ask for 10 bytes over limit
    brc_compact_hotboards(needed, 0);

    // Board 30 should now be compacted to 1
    assert(brc_find_record(30, &bnum) && bnum == 1);
    // Board 20 and Board 10 should still be untouched!
    assert(brc_find_record(20, &bnum) && bnum == 5);
    assert(brc_find_record(10, &bnum) && bnum == 5);

    // Now request even more space to force compacting Rank 1 (Board 20):
    needed = (BRC_MAXSIZE - brc_size) + 10;
    brc_compact_hotboards(needed, 0);

    assert(brc_find_record(20, &bnum) && bnum == 1);
    assert(brc_find_record(10, &bnum) && bnum == 5); // Rank 0 still untouched!

    // Finally request space to compact Rank 0 (Board 10):
    needed = (BRC_MAXSIZE - brc_size) + 10;
    brc_compact_hotboards(needed, 0);

    assert(brc_find_record(10, &bnum) && bnum == 1);

    printf("  -> PASS\n");
}

static void test_drop_dead_records(void) {
    printf("Testing dropping of expired records...\n");
    setup_test();

    time4_t t_valid = brc_expire_time + 100;
    brc_rec r_valid[1];
    r_valid[0].create = t_valid;
    r_valid[0].modified = t_valid;
    brc_insert_record(201, 1, r_valid);

    int bnum = 0;
    assert(brc_find_record(201, &bnum) && bnum == 1);

    // Advance expire_time so that t_valid is expired
    brc_expire_time = t_valid + 50;

    // Compact should drop board 201 completely
    brc_compact_non_hotboards(0);
    assert(brc_find_record(201, &bnum) == NULL);
    assert(bnum == 0);

    printf("  -> PASS\n");
}

static void test_recovery_after_compact(void) {
    printf("Testing recovery after compaction (num increases on new post)...\n");
    setup_test();

    time4_t t500 = now - 500;
    time4_t t400 = now - 400;
    time4_t t100 = now - 100;

    brc_rec r[2];
    r[0].create = t400; r[0].modified = t400;
    r[1].create = t500; r[1].modified = t500;
    brc_insert_record(105, 2, r);

    // Compact to 1
    brc_compact_non_hotboards(0);
    int bnum = 0;
    assert(brc_find_record(105, &bnum) && bnum == 1);

    // User enters board 105
    brc_currbid = 105;
    currbid = 105;
    currboard = "test105";
    brc_read_record(105, &brc_num, brc_list);
    assert(brc_num == 1 && brc_list[0].create == t400);

    // User reads an older post (t500) -> ignored by brc_addlist
    char fn_old[32];
    snprintf(fn_old, sizeof(fn_old), "M.%d.A.001", (int)t500);
    brc_addlist(fn_old, t500);
    assert(brc_num == 1);

    // User reads a newer post (t100) -> added!
    char fn_new[32];
    snprintf(fn_new, sizeof(fn_new), "M.%d.A.002", (int)t100);
    brc_addlist(fn_new, t100);
    assert(brc_num == 2);
    assert(brc_list[0].create == t100);
    assert(brc_list[1].create == t400);

    // Update back to buffer
    brc_update();
    const brc_rec *rec = brc_find_record(105, &bnum);
    assert(rec && bnum == 2);
    assert(rec[0].create == t100 && rec[1].create == t400);

    printf("  -> PASS\n");
}

static void test_full_buffer_overflow(void) {
    printf("Testing full buffer automatic compaction on overflow...\n");
    setup_test();

    // 5 hotboards
    SHM->hotboards.num = 5;
    for (int i = 0; i < 5; i++)
        SHM->hotboards.bids[i] = i; // bids 1..5

    brc_rec r[10];
    for (int i = 0; i < 10; i++) {
        r[i].create = now - 100 - i * 10;
        r[i].modified = now - 100 - i * 10;
    }

    // Insert 600 boards with 10 records each (each board takes 6 + 10*8 = 86 bytes)
    // 600 * 86 = 51,600 bytes > BRC_MAXSIZE (49,152)
    for (int b = 1; b <= 600; b++) {
        brc_insert_record(b, 10, r);
        assert(brc_size <= BRC_MAXSIZE);
    }

    // Buffer must remain <= BRC_MAXSIZE
    assert(brc_size <= BRC_MAXSIZE);

    // Hotboards 1..5 should still be present
    for (int b = 1; b <= 5; b++) {
        int bnum = 0;
        assert(brc_find_record(b, &bnum) != NULL);
        assert(bnum >= 1);
    }

    // Verify recent boards are still present and have valid unread status
    int bnum = 0;
    assert(brc_find_record(600, &bnum) != NULL);
    assert(bnum == 10); // Most recently inserted board has full records!

    printf("  -> PASS\n");
}

int main(void) {
    printf("Running BRC compaction tests...\n");
    test_hotboard_rank();
    test_basic_unread_and_compact();
    test_compaction_order();
    test_drop_dead_records();
    test_recovery_after_compact();
    test_full_buffer_overflow();
    printf("\nAll BRC compaction tests PASSED successfully!\n");
    return 0;
}
