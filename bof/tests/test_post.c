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
    RUN(test_tombstone_titles);
    cleanup();
    return bof_test_report();
}
