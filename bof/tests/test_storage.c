/*
 * Tests of the storage virtualization of bof.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 */

#include "bbs.h"
#include "bof_storage.h"
#include "bof_test.h"

int bof_test_failures;

// A backend that records what it was asked, and fails when told.
typedef struct {
    const char *name;
    char calls[256];
    bof_error_t result;
    int modlogs;
} mock_t;

static void
called(void *ctx, const char *op)
{
    mock_t *m = (mock_t *)ctx;
    strlcat(m->calls, op, sizeof(m->calls));
    strlcat(m->calls, " ", sizeof(m->calls));
}

static bof_error_t
mock_delete_post(void *ctx, const bof_actor_t *actor GCC_UNUSED,
                 const bof_delete_post_t *req GCC_UNUSED,
                 bof_post_result_t *out)
{
    called(ctx, "delete_post");
    if (out)
        strlcpy(out->bbs_archived_path, ((mock_t *)ctx)->name,
                sizeof(out->bbs_archived_path));
    return ((mock_t *)ctx)->result;
}

static bof_error_t
mock_remove_post(void *ctx, const bof_actor_t *actor GCC_UNUSED,
                 const bof_remove_post_t *req GCC_UNUSED,
                 bof_post_result_t *out GCC_UNUSED)
{
    called(ctx, "remove_post");
    return ((mock_t *)ctx)->result;
}

static int
mock_write_modlog(void *ctx, const char *community GCC_UNUSED,
                  const bof_actor_t *actor GCC_UNUSED,
                  const char *reason GCC_UNUSED,
                  int reason_audit_only GCC_UNUSED,
                  const bof_modlog_t *m GCC_UNUSED)
{
    called(ctx, "write_modlog");
    ((mock_t *)ctx)->modlogs++;
    return 0;
}

static bof_error_t
mock_get_posts(void *ctx, const bof_get_posts_t *req GCC_UNUSED,
               bof_get_posts_response_t *out)
{
    called(ctx, "get_posts");
    out->count = 7;
    return BOF_OK;
}

static const bof_storage_ops_t full_ops = {
    .name = "full",
    .delete_post = mock_delete_post,
    .remove_post = mock_remove_post,
    .write_modlog = mock_write_modlog,
    .get_posts = mock_get_posts,
};

// Without remove_post (as the files backend is without delete_comment).
static const bof_storage_ops_t partial_ops = {
    .name = "partial",
    .delete_post = mock_delete_post,
    .write_modlog = mock_write_modlog,
    .get_posts = mock_get_posts,
};

static void
test_one_backend(void)
{
    mock_t a = { .name = "a" }, b = { .name = "b" };
    bof_actor_t me = { .name = "tester" };
    bof_post_result_t res;
    bof_delete_post_t req = { .post = { .community = "Test" }, .deleted = 1 };

    bof_storage_reset();
    bof_storage_set(&full_ops, &a);
    memset(&res, 0, sizeof(res));
    EXPECT_OK(bof_delete_post(&me, &req, &res));
    EXPECT_STR(a.calls, "delete_post ");
    EXPECT_STR(res.bbs_archived_path, "a");
    // Its error is the result.
    a.result = BOF_ERR_COULDNT_FIND_POST;
    EXPECT_INT(bof_delete_post(&me, &req, &res), BOF_ERR_COULDNT_FIND_POST);

    // Another one set replaces it: there is no fan-out to the one before.
    a.calls[0] = '\0';
    bof_storage_set(&full_ops, &b);
    EXPECT_OK(bof_delete_post(&me, &req, &res));
    EXPECT_STR(a.calls, "");
    EXPECT_STR(b.calls, "delete_post ");
    EXPECT_STR(res.bbs_archived_path, "b");
}

static void
test_reads(void)
{
    mock_t a = { .name = "a" };
    fileheader_t buf[1];
    bof_get_posts_t req = { .community = "Test", .bbs_start = 1, .limit = 1 };
    bof_get_posts_response_t res = { .posts = buf };

    bof_storage_reset();
    bof_storage_set(&full_ops, &a);
    EXPECT_OK(bof_get_posts(&req, &res));
    EXPECT_INT(res.count, 7);
    EXPECT_STR(a.calls, "get_posts ");
}

static void
test_not_supported(void)
{
    mock_t a = { .name = "a" }, b = { .name = "b" };
    bof_actor_t me = { .name = "tester" };
    bof_modlog_t log;
    fileheader_t fh = { .filename = "M.42.A.000" };
    bof_remove_post_t req = {
        .post = { .community = "Test", .bbs_header = &fh },
        .removed = 1,
    };

    bof_modlog_init(&log, "remove");
    req.bbs_modlog = &log;

    // Without it: bbs_storage_not_supported, and no modlog.
    bof_storage_reset();
    bof_storage_set(&partial_ops, &a);
    EXPECT_INT(bof_remove_post(&me, &req, NULL),
               BOF_ERR_BBS_STORAGE_NOT_SUPPORTED);
    EXPECT_STR(a.calls, "");

    // With it: done, then the modlog.
    bof_storage_set(&full_ops, &b);
    EXPECT_OK(bof_remove_post(&me, &req, NULL));
    EXPECT_STR(b.calls, "remove_post write_modlog ");
}

static void
test_files_by_default(void)
{
    bof_comment_response_t res;
    fileheader_t fh;
    bof_delete_comment_t req = {
        .post = {
            .community = "Test", .bbs_dir = "boards/T/Test/.DIR",
            .bbs_header = &fh,
        },
        .bbs_floor = 1, .bbs_creator = "someone", .deleted = 1,
    };

    // Nothing set: the files backend, which cannot delete comments.
    bof_storage_reset();
    memset(&fh, 0, sizeof(fh));
    strlcpy(fh.filename, "M.42.A.000", sizeof(fh.filename));
    EXPECT_INT(bof_delete_comment(NULL, &req, &res),
               BOF_ERR_BBS_STORAGE_NOT_SUPPORTED);
    EXPECT_STR(bof_storage_files.name, "files");
}

int
main(void)
{
    char dir[] = "/tmp/bof_test_XXXXXX", cmd[PATHLEN];

    // What backends could not keep in step goes to BOF_STORAGE_LOG, under
    // log/.
    if (!mkdtemp(dir) || chdir(dir) != 0) {
        perror("setup");
        return 1;
    }
    mkdir("log", 0755);
    RUN(test_one_backend);
    RUN(test_reads);
    RUN(test_not_supported);
    RUN(test_files_by_default);
    bof_storage_reset();
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    if (system(cmd) != 0)
        fprintf(stderr, "could not remove %s\n", dir);
    return bof_test_report();
}
