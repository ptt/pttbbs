/*
 * Tests of the posts of bof, kept in the files of a board.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 */

#include "bbs.h"
#include "bof_test.h"

int bof_test_failures;

#define BOARD "Test"
#define BDIR "boards/T/" BOARD
#define DIRPATH BDIR "/.DIR"

static void
make_post(const char *filename, const char *owner, const char *title,
          int filemode)
{
    fileheader_t fh;
    char path[PATHLEN];
    FILE *fp;

    memset(&fh, 0, sizeof(fh));
    strlcpy(fh.filename, filename, sizeof(fh.filename));
    strlcpy(fh.owner, owner, sizeof(fh.owner));
    strlcpy(fh.title, title, sizeof(fh.title));
    strlcpy(fh.date, " 1/01", sizeof(fh.date));
    fh.filemode = filemode;
    snprintf(path, sizeof(path), BDIR "/%s", filename);
    fp = fopen(path, "w");
    fprintf(fp, "the content of %s\n", filename);
    fclose(fp);
    fp = fopen(DIRPATH, "a");
    fwrite(&fh, sizeof(fh), 1, fp);
    fclose(fp);
}

static int
read_dir(fileheader_t *recs, int max)
{
    FILE *fp = fopen(DIRPATH, "r");
    int n;

    if (!fp)
        return 0;
    n = fread(recs, sizeof(fileheader_t), max, fp);
    fclose(fp);
    return n;
}

static int
exists(const char *path)
{
    return access(path, F_OK) == 0;
}

static char test_dir[] = "/tmp/bof_test_XXXXXX";

static void
cleanup(void)
{
    char cmd[PATHLEN];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", test_dir);
    if (system(cmd) != 0)
        fprintf(stderr, "could not remove %s\n", test_dir);
}

static void
setup(void)
{
    const char *dir = mkdtemp(test_dir);

    if (!dir || chdir(dir) != 0) {
        perror("setup");
        exit(1);
    }
    mkdir("boards", 0755);
    mkdir("boards/T", 0755);
    mkdir(BDIR, 0755);
    make_post("M.1700000001.A.001", "alice", "first", 0);
    make_post("M.1700000002.A.002", "bob", "second", 0);
    make_post("M.1700000003.A.003", "carol", "third", 0);
    make_post("M.1700000004.A.004", "dave", "anonymous", FILE_ANONYMOUS);
}

static bof_post_ref_t
ref(fileheader_t *recs, int index)
{
    bof_post_ref_t r = {
        .community = BOARD, .bbs_dir = DIRPATH, .bbs_index = index,
        .bbs_header = &recs[index - 1],
    };
    return r;
}

static void
test_delete_post(void)
{
    fileheader_t recs[8];
    bof_actor_t alice = { .name = "alice", .from = "127.0.0.1" };
    bof_post_result_t res;

    EXPECT_INT(read_dir(recs, 8), 4);
    // The creator deletes the first post: its record is dropped, and its
    // content archived with who deleted it.
    bof_delete_post_t req = { .post = ref(recs, 1), .deleted = 1 };
    EXPECT_OK(bof_delete_post(&alice, &req, &res));
    EXPECT_INT(res.bbs_content_result, DELETE_FILE_CONTENT_SUCCESS);
    EXPECT_INT(res.bbs_record_kept, 0);
    EXPECT(!exists(BDIR "/M.1700000001.A.001"));
    EXPECT(res.bbs_archived_path[0] && exists(res.bbs_archived_path));
    if (res.bbs_archived_path[0]) {
        char buf[512] = "";
        FILE *fp = fopen(res.bbs_archived_path, "r");
        size_t n = fp ? fread(buf, 1, sizeof(buf) - 1, fp) : 0;
        buf[n] = 0;
        if (fp)
            fclose(fp);
        EXPECT(strstr(buf, "the content of M.1700000001.A.001") != NULL);
        EXPECT(strstr(buf, "Deleted by: alice (127.0.0.1)") != NULL);
    }
    EXPECT_INT(read_dir(recs, 8), 3);
    EXPECT_STR(recs[0].filename, "M.1700000002.A.002");

    // A stale position: the record there is another post.
    fileheader_t stale = recs[1];
    bof_post_ref_t r = ref(recs, 1);
    r.bbs_header = &stale;
    bof_delete_post_t again = { .post = r, .deleted = 1 };
    EXPECT_INT(bof_delete_post(&alice, &again, &res),
               BOF_ERR_COULDNT_FIND_POST);
    bof_delete_post_t restore = { .post = ref(recs, 1), .deleted = 0 };
    EXPECT_INT(bof_delete_post(&alice, &restore, &res),
               BOF_ERR_INVALID_BODY_FIELD);
    EXPECT_INT(read_dir(recs, 8), 3);
}

static void
test_remove_post(void)
{
    fileheader_t recs[8];
    bof_actor_t mod = { .name = "sysop", .from = "127.0.0.1" };
    bof_post_result_t res;

    EXPECT_INT(read_dir(recs, 8), 3);
    // A moderator removes bob's post, keeping its record as a tombstone.
    bof_remove_post_t req = {
        .post = ref(recs, 1), .removed = 1, .reason = "spam",
        .bbs_keep_record = 1,
    };
    EXPECT_OK(bof_remove_post(&mod, &req, &res));
    EXPECT_INT(res.bbs_record_kept, 1);
    EXPECT_INT(read_dir(recs, 8), 3);
    EXPECT(strncmp(recs[0].filename, FN_SAFEDEL, strlen(FN_SAFEDEL)) == 0);
    EXPECT_STR(recs[0].owner, "-");
    EXPECT_STR(recs[0].title, STR_SAFEREMOVE_TITLE " <bob>");
    EXPECT(!exists(BDIR "/M.1700000002.A.002"));
    EXPECT(res.bbs_archived_path[0] && exists(res.bbs_archived_path));

    // With a title of the moderator's.
    bof_remove_post_t titled = {
        .post = ref(recs, 2), .removed = 1, .bbs_keep_record = 1,
        .bbs_title = "custom",
    };
    EXPECT_OK(bof_remove_post(&mod, &titled, &res));
    EXPECT_INT(read_dir(recs, 8), 3);
    EXPECT_STR(recs[1].title, "custom");

    // An anonymous post says it was removed, not deleted.
    bof_remove_post_t anon = {
        .post = ref(recs, 3), .removed = 1, .bbs_keep_record = 1,
    };
    EXPECT_OK(bof_remove_post(&mod, &anon, &res));
    EXPECT_INT(read_dir(recs, 8), 3);
    EXPECT_STR(recs[2].title, STR_SAFEREMOVE_TITLE);
}

static void
test_edit_post(void)
{
    fileheader_t recs[8], updated;
    bof_actor_t me = { .name = "sysop" };
    bof_post_response_t res = { .bbs_header = &updated };
    int32_t money = 0;

    EXPECT_INT(read_dir(recs, 8), 3);
    // The tombstones of the earlier tests are at 1..3: use the second
    // set of posts made here.
    make_post("M.1700000005.A.005", "frank", "to edit", 0);
    EXPECT_INT(read_dir(recs, 8), 4);
    bof_edit_post_t req = {
        .post = ref(recs, 4), .name = "edited", .bbs_modified = 1700000099,
        .bbs_owner = "grace", .bbs_money = &money,
    };
    EXPECT_OK(bof_edit_post(&me, &req, &res));
    EXPECT_STR(updated.title, "edited");
    EXPECT_STR(updated.owner, "grace");
    EXPECT_INT(updated.modified, 1700000099);
    EXPECT_INT(read_dir(recs, 8), 4);
    EXPECT_STR(recs[3].title, "edited");
    EXPECT_STR(recs[3].date, " 1/01");   // unchanged

    // Marks: set and clear filemode bits.
    bof_bbs_set_post_modes_t mark = {
        .post = ref(recs, 4), .enable = FILE_MARKED | FILE_SOLVED,
    };
    EXPECT_OK(bof_bbs_set_post_modes(&me, &mark, &res));
    EXPECT_INT(updated.filemode, FILE_MARKED | FILE_SOLVED);
    bof_bbs_set_post_modes_t unmark = {
        .post = ref(recs, 4), .disable = FILE_SOLVED,
    };
    EXPECT_OK(bof_bbs_set_post_modes(&me, &unmark, &res));
    EXPECT_INT(read_dir(recs, 8), 4);
    EXPECT_INT(recs[3].filemode, FILE_MARKED);
    bof_bbs_set_post_modes_t none = { .post = ref(recs, 4) };
    EXPECT_INT(bof_bbs_set_post_modes(&me, &none, &res),
               BOF_ERR_INVALID_BODY_FIELD);

    // A stale record.
    fileheader_t other = recs[0];
    bof_post_ref_t r = ref(recs, 4);
    r.bbs_header = &other;
    bof_edit_post_t stale = { .post = r, .name = "x" };
    EXPECT_INT(bof_edit_post(&me, &stale, &res), BOF_ERR_COULDNT_FIND_POST);

    // The score stays within MAX_DISPLAY_SCORE.
    EXPECT_INT(bof_files_modify_record(DIRPATH, 4, "M.1700000005.A.005", 0,
                                       NULL, NULL, NULL, MAX_DISPLAY_SCORE + 5,
                                       NULL, 0, 0, &updated), 0);
    EXPECT_INT(updated.score, MAX_DISPLAY_SCORE);
}

static void
test_lock_feature_score(void)
{
    fileheader_t recs[8], updated;
    bof_actor_t me = { .name = "sysop" };
    bof_post_response_t res = { .bbs_header = &updated };

    EXPECT_INT(read_dir(recs, 8), 4);
    // Lock: the record becomes L.<stamp> and the file X.<stamp>.
    bof_lock_post_t lock = { .post = ref(recs, 4), .locked = 1 };
    EXPECT_OK(bof_lock_post(&me, &lock, &res));
    EXPECT_STR(updated.filename, "L.1700000005.A.005");
    EXPECT(!exists(BDIR "/M.1700000005.A.005"));
    EXPECT(exists(BDIR "/X.1700000005.A.005"));
    EXPECT_INT(read_dir(recs, 8), 4);
    EXPECT_STR(recs[3].filename, "L.1700000005.A.005");
    // Locking again does nothing.
    lock.post = ref(recs, 4);
    EXPECT_OK(bof_lock_post(&me, &lock, &res));
    EXPECT_STR(updated.filename, "L.1700000005.A.005");
    // Unlock.
    bof_lock_post_t unlock = { .post = ref(recs, 4), .locked = 0 };
    EXPECT_OK(bof_lock_post(&me, &unlock, &res));
    EXPECT(exists(BDIR "/M.1700000005.A.005"));
    EXPECT(!exists(BDIR "/X.1700000005.A.005"));
    EXPECT_INT(read_dir(recs, 8), 4);
    EXPECT_STR(recs[3].filename, "M.1700000005.A.005");

    // Feature (pin): pinned and marked; unpinning keeps the mark.
    bof_feature_post_t pin = { .post = ref(recs, 4), .featured = 1 };
    EXPECT_OK(bof_feature_post(&me, &pin, &res));
    EXPECT(updated.filemode & FILE_BOTTOM);
    EXPECT(updated.filemode & FILE_MARKED);
    bof_feature_post_t unpin = { .post = ref(recs, 4), .featured = 0 };
    EXPECT_OK(bof_feature_post(&me, &unpin, &res));
    EXPECT(!(updated.filemode & FILE_BOTTOM));
    EXPECT(updated.filemode & FILE_MARKED);

    // The score goes back to 0.
    EXPECT_INT(read_dir(recs, 8), 4);
    EXPECT(recs[3].score != 0);   // MAX_DISPLAY_SCORE from test_edit_post
    bof_bbs_reset_post_score_t reset = { .post = ref(recs, 4) };
    EXPECT_OK(bof_bbs_reset_post_score(&me, &reset, &res));
    EXPECT_INT(updated.score, 0);
    EXPECT_INT(read_dir(recs, 8), 4);
    EXPECT_INT(recs[3].score, 0);
}

static int
select_titles_starting_with_p(const fileheader_t *fh, int index GCC_UNUSED,
                              void *ctx GCC_UNUSED)
{
    return fh->title[0] == 'p';
}

static void
count_removed(const fileheader_t *fh GCC_UNUSED, void *ctx)
{
    (*(int *)ctx)++;
}

static void
test_prune(void)
{
    fileheader_t recs[16];
    int removed = 0;

    EXPECT_INT(read_dir(recs, 16), 4);
    make_post("M.1700000006.A.006", "u", "prune one", 0);
    make_post("M.1700000007.A.007", "u", "prune kept", FILE_MARKED);
    make_post("M.1700000008.A.008", "u", "prune two", 0);
    make_post("M.1700000009.A.009", "u", "stays", 0);
    EXPECT_INT(read_dir(recs, 16), 8);

    // Tombstones in place: the marked one is kept.
    EXPECT_INT(bof_files_prune(DIRPATH, FILE_MARKED, 1,
                               select_titles_starting_with_p, count_removed,
                               &removed, "sysop"), 2);
    EXPECT_INT(removed, 2);
    EXPECT_INT(read_dir(recs, 16), 8);
    EXPECT(strncmp(recs[4].filename, FN_SAFEDEL, strlen(FN_SAFEDEL)) == 0);
    EXPECT_STR(recs[5].filename, "M.1700000007.A.007");
    EXPECT(strncmp(recs[6].filename, FN_SAFEDEL, strlen(FN_SAFEDEL)) == 0);

    // Dropping records: the list is rewritten, the files go.
    make_post("M.1700000010.A.010", "u", "prune three", 0);
    EXPECT_INT(bof_files_prune(DIRPATH, FILE_MARKED, 0,
                               select_titles_starting_with_p, NULL, NULL,
                               "sysop"), 1);
    EXPECT(!exists(BDIR "/M.1700000010.A.010"));
    EXPECT_INT(read_dir(recs, 16), 8);
    EXPECT_STR(recs[7].filename, "M.1700000009.A.009");
}

static void
test_create_post_and_comment(void)
{
    fileheader_t recs[16], fh, updated;
    bof_actor_t me = { .name = "henry" };
    bof_post_response_t pres = { .bbs_header = &updated };
    bof_comment_response_t cres = { .bbs_header = &updated };
    FILE *fp;
    int n;

    // CreatePost lists a file the editor wrote.
    n = read_dir(recs, 16);
    memset(&fh, 0, sizeof(fh));
    strlcpy(fh.filename, "M.1700000011.A.011", sizeof(fh.filename));
    strlcpy(fh.owner, "henry", sizeof(fh.owner));
    strlcpy(fh.title, "new post", sizeof(fh.title));
    fp = fopen(BDIR "/M.1700000011.A.011", "w");
    fputs("body\n", fp);
    fclose(fp);
    bof_create_post_t post = {
        .community = BOARD, .bbs_dir = DIRPATH, .bbs_header = &fh,
    };
    EXPECT_OK(bof_create_post(&me, &post, &pres));
    EXPECT_INT(read_dir(recs, 16), n + 1);
    EXPECT_STR(recs[n].title, "new post");

    // A push appends its line and votes up; a boo votes down.
    bof_create_comment_t push = {
        .post = ref(recs, n + 1), .content = "push line\n", .bbs_vote = 1,
        .bbs_lock = 1,
    };
    EXPECT_OK(bof_create_comment(&me, &push, &cres));
    EXPECT_INT(updated.score, 1);
    EXPECT(updated.modified != 0);
    EXPECT_INT(cres.bbs_file_size, strlen("body\npush line\n"));
    EXPECT_INT(read_dir(recs, 16), n + 1);
    EXPECT_INT(recs[n].score, 1);
    bof_create_comment_t boo = {
        .post = ref(recs, n + 1), .content = "boo line\n", .bbs_vote = -1,
    };
    EXPECT_OK(bof_create_comment(&me, &boo, &cres));
    EXPECT_INT(updated.score, 0);

    // A post without its file.
    unlink(BDIR "/M.1700000011.A.011");
    EXPECT_INT(read_dir(recs, 16), n + 1);
    bof_create_comment_t gone = { .post = ref(recs, n + 1), .content = "x\n" };
    EXPECT_INT(bof_create_comment(&me, &gone, &cres),
               BOF_ERR_COULDNT_FIND_POST);
}

static void
test_remove_comment(void)
{
    fileheader_t fh, kept;
    bof_actor_t mod = { .name = "boss" };
    bof_comment_response_t cres = { .bbs_header = &kept };
    char push[ANSILINELEN], boo[ANSILINELEN], buf[4096];
    FILE *fp;
    size_t n;

    memset(&fh, 0, sizeof(fh));
    strlcpy(fh.filename, "M.1700000021.A.021", sizeof(fh.filename));
    bof_files_format_comment(push, sizeof(push), RECTYPE_GOOD, "ivan", 20,
                             "nice", " 01/01 00:00");
    bof_files_format_comment(boo, sizeof(boo), RECTYPE_BAD, "judy", 20,
                             "bad", " 01/01 00:01");
    fp = fopen(BDIR "/M.1700000021.A.021", "w");
    fprintf(fp, "body\n%s%s", push, boo);
    fclose(fp);

    bof_remove_comment_t rm = {
        .post = { .community = BOARD, .bbs_dir = DIRPATH, .bbs_header = &fh },
        .bbs_vote = -1, .bbs_author = "judy", .bbs_content = "bad",
        .reason = "spam",
    };
    EXPECT_OK(bof_remove_comment(&mod, &rm, &cres));
    EXPECT_STR(kept.filename, fh.filename);
    fp = fopen(BDIR "/M.1700000021.A.021", "r");
    n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    EXPECT_INT(cres.bbs_file_size, n);
    EXPECT(strstr(buf, push) != NULL);
    EXPECT(strstr(buf, boo) == NULL);
    EXPECT(strstr(buf, "boss") != NULL && strstr(buf, "spam") != NULL);

    // It is gone now; the push is not a boo (comments of the old style all
    // look the same).
    EXPECT_INT(bof_remove_comment(&mod, &rm, NULL),
               BOF_ERR_COULDNT_FIND_COMMENT);
    rm.bbs_author = "ivan";
    rm.bbs_content = "nice";
    if (!IS_ENABLED(CONFIG_OLD_RECOMMEND))
        EXPECT_INT(bof_remove_comment(&mod, &rm, NULL),
                   BOF_ERR_COULDNT_FIND_COMMENT);
    rm.bbs_vote = 1;
    EXPECT_OK(bof_remove_comment(&mod, &rm, NULL));

    strlcpy(fh.filename, "M.1700000022.A.022", sizeof(fh.filename));
    EXPECT_INT(bof_remove_comment(&mod, &rm, NULL), BOF_ERR_COULDNT_FIND_POST);
}

static void
test_tombstone_titles(void)
{
    fileheader_t fh;

    memset(&fh, 0, sizeof(fh));
    strlcpy(fh.filename, "M.1700000009.A.009", sizeof(fh.filename));
    strlcpy(fh.owner, "erin", sizeof(fh.owner));
    bof_files_set_tombstone(&fh, NULL, "erin");
    EXPECT_STR(fh.title, STR_SAFEDEL_TITLE " [erin]");
    EXPECT_STR(fh.owner, "-");

    // The fields are rewritten whole: nothing is left of the old values after
    // the end of the new ones (what was after the end of the new value is
    // cleared).
    memset(&fh, 'x', sizeof(fh));
    fh.filemode = 0;
    strlcpy(fh.filename, "M.1700000009.A.009", sizeof(fh.filename));
    strlcpy(fh.owner, "erin", sizeof(fh.owner));
    bof_files_set_tombstone(&fh, NULL, "erin");
    EXPECT_STR(fh.title, STR_SAFEDEL_TITLE " [erin]");
    EXPECT_STR(fh.owner, "-");
    for (size_t k = strlen(fh.owner); k < sizeof(fh.owner); k++)
        EXPECT_INT(fh.owner[k], 0);
    for (size_t k = strlen(fh.filename); k < sizeof(fh.filename); k++)
        EXPECT_INT(fh.filename[k], 0);
    for (size_t k = strlen(fh.title); k < sizeof(fh.title); k++)
        EXPECT_INT(fh.title[k], 0);
}

int
main(void)
{
    setup();
    RUN(test_delete_post);
    RUN(test_remove_post);
    RUN(test_edit_post);
    RUN(test_lock_feature_score);
    RUN(test_prune);
    RUN(test_create_post_and_comment);
    RUN(test_remove_comment);
    RUN(test_tombstone_titles);
    cleanup();
    return bof_test_report();
}
