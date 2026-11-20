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

static char *
slurp(const char *path, char *buf, size_t sz)
{
    FILE *fp = fopen(path, "r");
    size_t n = 0;

    if (fp) {
        n = fread(buf, 1, sz - 1, fp);
        fclose(fp);
    }
    buf[n] = '\0';
    return buf;
}

static void
spit(const char *path, const char *text)
{
    FILE *fp = fopen(path, "w");
    fputs(text, fp);
    fclose(fp);
}

static void
test_edit_post_body(void)
{
    fileheader_t recs[32], updated;
    bof_actor_t me = { .name = "kate" };
    bof_post_response_t res = { .bbs_header = &updated };
    bof_post_version_t ver, ver2;
    char buf[4096];
    int n;

    make_post("M.1700000031.A.031", "kate", "body", 0);
    n = read_dir(recs, 32);
    bof_post_ref_t post = ref(recs, n);

    // GetPost gives the body and its version.
    EXPECT_OK(bof_bbs_get_post_body(&post, "edit.tmp", &ver));
    EXPECT_STR(slurp("edit.tmp", buf, sizeof(buf)),
               "the content of M.1700000031.A.031\n");
    EXPECT_INT(ver.size, strlen("the content of M.1700000031.A.031\n"));
    EXPECT(bof_files_version_matches(BDIR "/M.1700000031.A.031", &ver));

    // A comment comes while editing; it stays after the new body.
    spit("edit.tmp", "new body\n");
    bof_create_comment_t c = {
        .post = post, .content = "a comment\n", .bbs_lock = 1,
    };
    EXPECT_OK(bof_create_comment(&me, &c, NULL));
    EXPECT(bof_files_version_matches(BDIR "/M.1700000031.A.031", &ver));
    bof_edit_post_t edit = {
        .post = post, .name = "body, edited",
        .bbs_body_path = "edit.tmp", .bbs_body_version = &ver,
    };
    EXPECT_OK(bof_edit_post(&me, &edit, &res));
    EXPECT_INT(res.bbs_body_saved, 1);
    EXPECT(!exists("edit.tmp"));
    EXPECT_STR(slurp(BDIR "/M.1700000031.A.031", buf, sizeof(buf)),
               "new body\na comment\n");
    EXPECT_STR(updated.title, "body, edited");
    EXPECT(updated.modified != 0);
    // The old content is in the time capsule, with when it was made.
    EXPECT_INT(timecapsule_get_max_revision_number(
                   BDIR "/M.1700000031.A.031"), 1);

    // An edit from the old version fails now and keeps its file.
    spit("edit.tmp", "stale body\n");
    n = read_dir(recs, 32);
    post = ref(recs, n);
    edit.post = post;
    EXPECT_INT(bof_edit_post(&me, &edit, &res), BOF_ERR_COULDNT_UPDATE_POST);
    EXPECT_INT(res.bbs_body_saved, 0);
    EXPECT(exists("edit.tmp"));
    EXPECT_STR(slurp(BDIR "/M.1700000031.A.031", buf, sizeof(buf)),
               "new body\na comment\n");

    // From the new version it works.
    EXPECT_OK(bof_bbs_get_post_body(&post, "edit.tmp", &ver2));
    spit("edit.tmp", "third body\n");
    edit.bbs_body_version = &ver2;
    EXPECT_OK(bof_edit_post(&me, &edit, &res));
    EXPECT_STR(slurp(BDIR "/M.1700000031.A.031", buf, sizeof(buf)),
               "third body\n");
}

static void
test_mask_post(void)
{
    fileheader_t fh;
    bof_actor_t mod = { .name = "boss" };
    char buf[4096];

    memset(&fh, 0, sizeof(fh));
    strlcpy(fh.filename, "M.1700000041.A.041", sizeof(fh.filename));
    spit(BDIR "/M.1700000041.A.041",
         "fine line\nsome \033[1mba\033[md\033[m word\nanother fine\n");
    bof_bbs_mask_post_t req = {
        .post = { .community = BOARD, .bbs_dir = DIRPATH, .bbs_header = &fh },
        .pattern = "bad", .reason = "rude",
    };
    EXPECT_OK(bof_bbs_mask_post(&mod, &req, NULL));
    slurp(BDIR "/M.1700000041.A.041", buf, sizeof(buf));
    EXPECT(strncmp(buf, "fine line\n" STR_POST_MARK STR_MASKED_LINE
                   "\nanother fine\n", strlen("fine line\n" STR_POST_MARK
                   STR_MASKED_LINE "\nanother fine\n")) == 0);
    EXPECT(strstr(buf, "boss") != NULL && strstr(buf, "rude") != NULL);
    EXPECT(strstr(buf, "bad") == NULL);
    EXPECT_INT(timecapsule_get_max_revision_number(
                   BDIR "/M.1700000041.A.041"), 1);

    // Nothing left to mask; too short a pattern.
    EXPECT_INT(bof_bbs_mask_post(&mod, &req, NULL), BOF_ERR_NOT_FOUND);
    req.pattern = "b";
    EXPECT_INT(bof_bbs_mask_post(&mod, &req, NULL),
               BOF_ERR_INVALID_BODY_FIELD);
}

static void
test_crosspost(void)
{
    fileheader_t recs[64], copy;
    bof_actor_t sysop = { .name = "police" };
    bof_post_result_t res;
    char origin[IDLEN + 1];
    int n;

    EXPECT(bof_bbs_is_aggregate(BN_ALLPOST));
    EXPECT(!bof_bbs_is_aggregate(BOARD));

    memset(&copy, 0, sizeof(copy));
    strlcpy(copy.title, "[news] something(" BOARD ")", sizeof(copy.title));
    EXPECT(bof_bbs_crosspost_origin(&copy, origin, sizeof(origin)));
    EXPECT_STR(origin, BOARD);
    strlcpy(copy.title, "no board here", sizeof(copy.title));
    EXPECT(!bof_bbs_crosspost_origin(&copy, origin, sizeof(origin)));

    // The original of a copy is removed from its board.
    make_post("M.1700000051.A.051", "mallory", "spam", 0);
    n = read_dir(recs, 64);
    copy = recs[n - 1];
    bof_bbs_remove_crosspost_t req = {
        .community = BOARD, .bbs_dir = DIRPATH, .bbs_header = &copy,
        .reason = "cross-post",
    };
    EXPECT_OK(bof_bbs_remove_crosspost(&sysop, &req, &res));
    EXPECT_INT(read_dir(recs, 64), n - 1);
    EXPECT(!exists(BDIR "/M.1700000051.A.051"));
    EXPECT_INT(bof_bbs_remove_crosspost(&sysop, &req, &res),
               BOF_ERR_COULDNT_FIND_POST);

    // With a tombstone.
    make_post("M.1700000052.A.052", "mallory", "spam again", 0);
    n = read_dir(recs, 64);
    copy = recs[n - 1];
    req.bbs_keep_record = 1;
    EXPECT_OK(bof_bbs_remove_crosspost(&sysop, &req, &res));
    EXPECT_INT(res.bbs_record_kept, 1);
    EXPECT_INT(read_dir(recs, 64), n);
    EXPECT(!exists(BDIR "/M.1700000052.A.052"));

    // Only the file of a copy.
    make_post("M.1700000053.A.053", "mallory", "copy", 0);
    n = read_dir(recs, 64);
    copy = recs[n - 1];
    bof_bbs_remove_crosspost_t only = {
        .community = BOARD, .bbs_dir = DIRPATH, .bbs_header = &copy,
        .bbs_content_only = 1,
    };
    EXPECT_OK(bof_bbs_remove_crosspost(&sysop, &only, &res));
    EXPECT_INT(read_dir(recs, 64), n);
    EXPECT(!exists(BDIR "/M.1700000053.A.053"));
}

static int
select_owner(const fileheader_t *fh, int index GCC_UNUSED, void *ctx)
{
    return strcmp(fh->owner, (const char *)ctx) == 0;
}

static void
test_remove_posts(void)
{
    fileheader_t recs[64];
    bof_actor_t mod = { .name = "boss" };
    int n, count;

    make_post("M.1700000061.A.061", "oscar", "one", 0);
    make_post("M.1700000062.A.062", "oscar", "two", FILE_MARKED);
    make_post("M.1700000063.A.063", "oscar", "three", 0);
    n = read_dir(recs, 64);
    bof_remove_posts_t req = {
        .community = BOARD, .bbs_dir = DIRPATH,
        .bbs_selected = select_owner, .bbs_ctx = "oscar",
        .bbs_keep_modes = FILE_MARKED | FILE_BOTTOM | FILE_DIGEST,
        .reason = "bulk",
    };
    EXPECT_OK(bof_remove_posts(&mod, &req, &count));
    EXPECT_INT(count, 2);
    EXPECT_INT(read_dir(recs, 64), n - 2);
    EXPECT(!exists(BDIR "/M.1700000061.A.061"));
    EXPECT(exists(BDIR "/M.1700000062.A.062"));
    EXPECT(!exists(BDIR "/M.1700000063.A.063"));
    // Archived in the time capsule.
    EXPECT_INT(timecapsule_get_max_archive_number(DIRPATH,
                                                  sizeof(fileheader_t)) > 0,
               1);
}

static void
test_modlog_with_actions(void)
{
    fileheader_t recs[64], updated;
    bof_actor_t mod = { .name = "boss", .from = "here", .bbs_role = "BM" };
    bof_post_response_t res = { .bbs_header = &updated };
    bof_modlog_t log;
    char buf[8192];
    int n;

    make_post("M.1700000071.A.071", "pat", "to lock", 0);
    n = read_dir(recs, 64);
    bof_modlog_add_post(bof_modlog_init(&log, "lock"), &recs[n - 1]);
    log.reason_audit_only = 1;
    bof_lock_post_t lock = {
        .post = ref(recs, n), .locked = 1, .reason = "flame",
        .bbs_modlog = &log,
    };
    EXPECT_OK(bof_lock_post(&mod, &lock, &res));
    slurp(BDIR "/" FN_MODLOG, buf, sizeof(buf));
    EXPECT(strstr(buf, " BM lock\n") != NULL);
    EXPECT(strstr(buf, "flame") == NULL);
    slurp(BDIR "/" FN_MODLOG_AUDIT, buf, sizeof(buf));
    EXPECT(strstr(buf, "flame") != NULL);
    EXPECT(strstr(buf, "boss (here)") != NULL);

    // Not recorded when it fails.
    lock.post.bbs_index = n + 10;
    bof_modlog_init(&log, "never");
    EXPECT(bof_lock_post(&mod, &lock, &res) != BOF_OK);
    EXPECT(strstr(slurp(BDIR "/" FN_MODLOG, buf, sizeof(buf)), "never") ==
           NULL);

    // Resetting the score notes it in the post too.
    make_post("M.1700000072.A.072", "pat", "scored", 0);
    n = read_dir(recs, 64);
    bof_modlog_init(&log, "reset");
    bof_bbs_reset_post_score_t reset = {
        .post = ref(recs, n), .reason = "votes bought", .bbs_modlog = &log,
    };
    EXPECT_OK(bof_bbs_reset_post_score(&mod, &reset, &res));
    EXPECT(strstr(slurp(BDIR "/M.1700000072.A.072", buf, sizeof(buf)),
                  "boss") != NULL);
    EXPECT(strstr(slurp(BDIR "/" FN_MODLOG, buf, sizeof(buf)),
                  " BM reset: votes bought\n") != NULL);
}

static void
test_get_posts(void)
{
    fileheader_t recs[64], got[8];
    int n;

    n = read_dir(recs, 64);
    // The last two of the list, then post 1 featured.
    int32_t featured[] = { 1 };
    bof_get_posts_t req = {
        .community = BOARD, .bbs_dir = DIRPATH, .bbs_start = n - 1,
        .limit = 8, .bbs_featured = featured, .bbs_nfeatured = 1,
    };
    bof_get_posts_response_t res = { .posts = got };
    EXPECT_OK(bof_get_posts(&req, &res));
    EXPECT_INT(res.bbs_total, n);
    EXPECT_INT(res.count, 3);
    EXPECT_STR(got[0].filename, recs[n - 2].filename);
    EXPECT_STR(got[1].filename, recs[n - 1].filename);
    EXPECT_STR(got[2].filename, recs[0].filename);
    EXPECT(got[2].filemode & FILE_BOTTOM);

    // Only the featured part; a short page of the list.
    req.bbs_start = n + 1;
    EXPECT_OK(bof_get_posts(&req, &res));
    EXPECT_INT(res.count, 1);
    req.bbs_start = 1;
    req.limit = 2;
    EXPECT_OK(bof_get_posts(&req, &res));
    EXPECT_INT(res.count, 2);
    EXPECT_STR(got[1].filename, recs[1].filename);

    // GetPost.
    fileheader_t one;
    int fd = -1;
    bof_get_post_t get = {
        .community = BOARD, .bbs_dir = DIRPATH, .bbs_index = 2, .bbs_fd = &fd,
    };
    bof_get_post_response_t gres = { .bbs_header = &one };
    EXPECT_OK(bof_get_post(&get, &gres));
    EXPECT_STR(one.filename, recs[1].filename);
    EXPECT(strstr(gres.bbs_path, recs[1].filename) != NULL);
    get.bbs_index = n + 1;
    EXPECT_INT(bof_get_post(&get, &gres), BOF_ERR_COULDNT_FIND_POST);
    if (fd >= 0)
        close(fd);
}

static void
test_edit_comment(void)
{
    fileheader_t recs[64], updated;
    bof_actor_t me = { .name = "ivan" }, mod = { .name = "boss" };
    bof_comment_t cs[8];
    bof_comment_response_t cres = { .bbs_header = &updated };
    char l1[ANSILINELEN], l2[ANSILINELEN], l3[ANSILINELEN], buf[4096];
    int n;

    make_post("M.1700000081.A.081", "kate", "comments", 0);
    n = read_dir(recs, 64);
    bof_files_format_comment(l1, sizeof(l1), RECTYPE_GOOD, "ivan", 20,
                             "first", " 01/01 00:00");
    bof_files_format_comment(l2, sizeof(l2), RECTYPE_ARROW, "judy", 20,
                             "second", " 01/01 00:01");
    bof_files_format_comment(l3, sizeof(l3), RECTYPE_GOOD, "ivan", 20,
                             "third", " 01/01 00:02");
    snprintf(buf, sizeof(buf), "body\n%s%s%s", l1, l2, l3);
    spit(BDIR "/M.1700000081.A.081", buf);

    // GetComments, all and by one.
    bof_get_comments_t get = { .post = ref(recs, n), .limit = 8 };
    bof_get_comments_response_t gres = { .comments = cs };
    EXPECT_OK(bof_get_comments(&get, &gres));
    EXPECT_INT(gres.count, 3);
    EXPECT_INT(cs[1].bbs_floor, 2);
    EXPECT_STR(cs[1].creator, "judy");
    EXPECT_STR(cs[1].content, "second");
    EXPECT_STR(cs[1].bbs_tail, " 01/01 00:01");
    EXPECT_INT(cs[1].bbs_width, 20);
    get.bbs_creator = "IVAN";
    EXPECT_OK(bof_get_comments(&get, &gres));
    EXPECT_INT(gres.count, 2);
    EXPECT_INT(cs[1].bbs_floor, 3);

    // A removed comment keeps its floor.
    bof_remove_comment_t rm = {
        .post = ref(recs, n), .bbs_vote = 0, .bbs_author = "judy",
        .bbs_content = "second", .reason = "spam",
    };
    EXPECT_OK(bof_remove_comment(&mod, &rm, NULL));
    get.bbs_creator = NULL;
    EXPECT_OK(bof_get_comments(&get, &gres));
    EXPECT_INT(gres.count, 3);
    EXPECT_INT(cs[1].removed, 1);
    EXPECT_STR(cs[1].creator, "judy");
    EXPECT_INT(cs[2].bbs_floor, 3);

    // EditComment.
    bof_edit_comment_t edit = {
        .post = ref(recs, n), .bbs_floor = 3, .bbs_creator = "ivan",
        .bbs_old_content = "third", .content = "third, edited",
    };
    EXPECT_OK(bof_edit_comment(&me, &edit, &cres));
    EXPECT(updated.modified != 0);
    EXPECT_OK(bof_get_comments(&get, &gres));
    EXPECT_STR(cs[2].content, "third, edited");
    EXPECT_STR(cs[2].bbs_tail, "~01/01 00:02");
    EXPECT_INT(cs[2].bbs_edited, 1);
    EXPECT_INT(cs[0].bbs_edited, 0);
    EXPECT_STR(cs[0].content, "first");
    // From what it said before; not by its creator; someone else's (and
    // removed); a missing one; bad input; too long for its room.
    EXPECT_INT(bof_edit_comment(&me, &edit, &cres),
               BOF_ERR_COULDNT_UPDATE_COMMENT);
    edit.bbs_old_content = "third, edited";
    edit.bbs_creator = "judy";
    EXPECT_INT(bof_edit_comment(&me, &edit, &cres),
               BOF_ERR_NO_COMMENT_EDIT_ALLOWED);
    EXPECT_INT(bof_edit_comment(&mod, &edit, &cres),
               BOF_ERR_NO_COMMENT_EDIT_ALLOWED);
    edit.bbs_creator = "ivan";
    edit.bbs_floor = 2;
    edit.bbs_old_content = "second";
    edit.content = "second, edited";
    EXPECT_INT(bof_edit_comment(&me, &edit, &cres),
               BOF_ERR_COULDNT_UPDATE_COMMENT);
    edit.bbs_floor = 9;
    EXPECT_INT(bof_edit_comment(&me, &edit, &cres),
               BOF_ERR_COULDNT_FIND_COMMENT);
    edit.bbs_floor = 3;
    edit.bbs_old_content = "third, edited";
    edit.content = "two\nlines";
    EXPECT_INT(bof_edit_comment(&me, &edit, &cres),
               BOF_ERR_INVALID_BODY_FIELD);
    edit.content = "a\ttab";
    EXPECT_INT(bof_edit_comment(&me, &edit, &cres),
               BOF_ERR_INVALID_BODY_FIELD);
    edit.content = "far too long to fit in the room of the old one";
    EXPECT_INT(bof_edit_comment(&me, &edit, &cres),
               BOF_ERR_INVALID_BODY_FIELD);
    // Edited again: still one mark.
    edit.content = "third, again";
    EXPECT_OK(bof_edit_comment(&me, &edit, &cres));
    EXPECT_OK(bof_get_comments(&get, &gres));
    EXPECT_STR(cs[2].content, "third, again");
    EXPECT_STR(cs[2].bbs_tail, "~01/01 00:02");

    // DeleteComment is not for posts in files.
    bof_delete_comment_t del = {
        .post = ref(recs, n), .bbs_floor = 1, .bbs_creator = "ivan",
        .bbs_content = "first", .deleted = 1,
    };
    EXPECT_INT(bof_delete_comment(&me, &del, NULL),
               BOF_ERR_BBS_STORAGE_NOT_SUPPORTED);
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
    RUN(test_edit_post_body);
    RUN(test_mask_post);
    RUN(test_crosspost);
    RUN(test_remove_posts);
    RUN(test_modlog_with_actions);
    RUN(test_get_posts);
    RUN(test_edit_comment);
    RUN(test_tombstone_titles);
    cleanup();
    return bof_test_report();
}
