/*
 * Tests of changing communities (boards) through bof, kept in the files
 * of a BBS: removing, renaming, merging and rebuilding boards.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 */

#include "bbs.h"
#include "bof_test.h"

int bof_test_failures;

static void
make_board(const char *name)
{
    char path[PATHLEN];

    setbpath(path, name);
    mkdir(path, 0755);
    setapath(path, name);
    mkdir(path, 0755);
}

static void
make_post(const char *board, const char *filename, const char *content)
{
    fileheader_t fh;
    char path[PATHLEN];
    FILE *fp;

    memset(&fh, 0, sizeof(fh));
    strlcpy(fh.filename, filename, sizeof(fh.filename));
    strlcpy(fh.owner, "alice", sizeof(fh.owner));
    strlcpy(fh.title, "hello", sizeof(fh.title));
    setbfile(path, board, filename);
    fp = fopen(path, "w");
    fprintf(fp, "%s\n", content);
    fclose(fp);
    setbfile(path, board, FN_DIR);
    append_record(path, &fh, sizeof(fh));
}

static int
board_exists(const char *name)
{
    char path[PATHLEN];

    setbpath(path, name);
    return dashd(path);
}

static const bof_actor_t sysop = { .name = "SYSOP" };

// The header of slot bid in .BRD.
static boardheader_t in_brd;

static const boardheader_t *
slot(int bid)
{
    memset(&in_brd, 0, sizeof(in_brd));
    get_record(FN_BOARD, &in_brd, sizeof(in_brd), bid);
    return &in_brd;
}

static void
test_board_headers(void)
{
    boardheader_t bh = { 0 };
    bof_create_community_t cr = { .bbs_header = &bh };
    bof_create_community_response_t res;
    bof_edit_community_t ed = { .bbs_id = 1, .bbs_header = &bh };

    // A new board, after the last slot: in .BRD and the board cache.
    strlcpy(bh.brdname, "Alpha", sizeof(bh.brdname));
    strlcpy(bh.desc, "first", sizeof(bh.desc));
    bh.moderators[0].unum = 7;
    EXPECT_OK(bof_create_community(&sysop, &cr, &res));
    EXPECT_INT(res.bbs_id, 1);
    EXPECT_STR(slot(1)->desc, "first");
    EXPECT_STR(getbcache(1)->brdname, "Alpha");
    EXPECT_INT(SHM->Bnumber, 1);

    // Edited: both, and its moderators.
    strlcpy(bh.desc, "changed", sizeof(bh.desc));
    bh.moderators[0].unum = 8;
    EXPECT_OK(bof_edit_community(&sysop, &ed));
    EXPECT_STR(slot(1)->desc, "changed");
    EXPECT_STR(getbcache(1)->desc, "changed");
    EXPECT_INT(SHM->deprecated_BMcache[0][0], 8);

    // Emptied, as when it is deleted (a note may stay).
    memset(&bh, 0, sizeof(bh));
    strlcpy(bh.desc, "Alpha deleted", sizeof(bh.desc));
    EXPECT_OK(bof_edit_community(&sysop, &ed));
    EXPECT_STR(slot(1)->brdname, "");

    // Not a slot, nor a board name.
    ed.bbs_id = 0;
    EXPECT_INT(bof_edit_community(&sysop, &ed), BOF_ERR_INVALID_BODY_FIELD);
    ed.bbs_id = 1;
    strlcpy(bh.brdname, "../x", sizeof(bh.brdname));
    EXPECT_INT(bof_edit_community(&sysop, &ed), BOF_ERR_INVALID_BODY_FIELD);
    EXPECT_INT(bof_create_community(&sysop, &cr, &res),
               BOF_ERR_INVALID_BODY_FIELD);

    // An older board whose name the rules no longer allow (a digit first)
    // keeps it when edited, but may not be renamed to another such name.
    memset(&bh, 0, sizeof(bh));
    strlcpy(bh.brdname, "Beta", sizeof(bh.brdname));
    EXPECT_OK(bof_create_community(&sysop, &cr, &res));
    strlcpy(bh.brdname, "98Old", sizeof(bh.brdname));
    substitute_record(FN_BOARD, &bh, sizeof(bh), res.bbs_id);
    reset_board(res.bbs_id);
    ed.bbs_id = res.bbs_id;
    bh.brdattr |= BRD_COMMENT_READONLY;
    EXPECT_OK(bof_edit_community(&sysop, &ed));
    EXPECT_INT(slot(res.bbs_id)->brdattr & BRD_COMMENT_READONLY,
               BRD_COMMENT_READONLY);
    strlcpy(bh.brdname, "99New", sizeof(bh.brdname));
    EXPECT_INT(bof_edit_community(&sysop, &ed), BOF_ERR_INVALID_BODY_FIELD);
}

static void
test_remove_moderator(void)
{
    boardheader_t bh = { 0 };
    bof_create_community_t cr = { .bbs_header = &bh };
    bof_bbs_remove_moderator_t req = { .bbs_usernum = 7, .name = "gone" };
    int count = -1;

    strlcpy(bh.brdname, "Mods", sizeof(bh.brdname));
    bh.moderators[0].unum = 7;
    bh.moderators[1].unum = 8;
    bh.moderators[2].unum = 7;
    EXPECT_OK(bof_create_community(&sysop, &cr, NULL));
    EXPECT_OK(bof_bbs_remove_moderator(&sysop, &req, &count));
    EXPECT_INT(count, 1);
    // The others move up, in .BRD and the board cache.
    EXPECT_INT(getbcache(num_boards())->moderators[0].unum, 8);
    EXPECT_INT(getbcache(num_boards())->moderators[1].unum, 0);
    EXPECT_INT(slot(num_boards())->moderators[0].unum, 8);
    EXPECT_OK(bof_bbs_remove_moderator(&sysop, &req, &count));
    EXPECT_INT(count, 0);
}

static void
test_remove(void)
{
    bof_remove_community_t req = { .community = "Gone", .removed = 1 };
    bof_remove_community_response_t res = { .bbs_posts_kept = 1 };

    make_board("Gone");
    make_post("Gone", "M.1.A.001", "bye");

    // Lemmy's removed: false cannot be done.
    req.removed = 0;
    EXPECT_INT(bof_remove_community(&sysop, &req, &res),
               BOF_ERR_INVALID_BODY_FIELD);
    EXPECT(board_exists("Gone"));

    req.removed = 1;
    EXPECT_OK(bof_remove_community(&sysop, &req, &res));
    EXPECT(!board_exists("Gone"));
    // The files were the posts.
    EXPECT_INT(res.bbs_posts_kept, 0);
}

static void
test_rename(void)
{
    bof_bbs_rename_community_t req = { .community = "Old", .new_name = "New" };
    char path[PATHLEN];

    make_board("Old");
    make_post("Old", "M.1.A.001", "kept");
    EXPECT_OK(bof_bbs_rename_community(&sysop, &req));
    EXPECT(!board_exists("Old"));
    setbfile(path, "New", "M.1.A.001");
    EXPECT(dashf(path));
    setapath(path, "New");
    EXPECT(dashd(path));

    // Gone, or taken.
    EXPECT_INT(bof_bbs_rename_community(&sysop, &req),
               BOF_ERR_COULDNT_FIND_COMMUNITY);
    make_board("Other");
    req.community = "New";
    req.new_name = "Other";
    EXPECT_INT(bof_bbs_rename_community(&sysop, &req),
               BOF_ERR_COULDNT_UPDATE_COMMUNITY);
    EXPECT(board_exists("New"));
    // Not a board name.
    req.new_name = "../x";
    EXPECT_INT(bof_bbs_rename_community(&sysop, &req),
               BOF_ERR_INVALID_BODY_FIELD);
}

static void
test_merge(void)
{
    bof_bbs_merge_communities_t req = { .from = "From", .to = "Into" };
    bof_bbs_merge_communities_response_t res;
    fileheader_t recs[8];
    char path[PATHLEN];
    int n;

    make_board("From");
    make_board("Into");
    make_post("Into", "M.100.A.001", "into 1");
    make_post("Into", "M.300.A.003", "into 3");
    make_post("From", "M.200.A.002", "from 2");
    make_post("From", "M.300.A.003", "from 3");

    req.to = "From";
    EXPECT_INT(bof_bbs_merge_communities(&sysop, &req, &res),
               BOF_ERR_INVALID_BODY_FIELD);
    req.to = "Into";
    EXPECT_OK(bof_bbs_merge_communities(&sysop, &req, &res));
    EXPECT_INT(res.moved, 1);
    EXPECT_INT(res.collisions, 1);

    // In the order of time, once each; the file the board had is kept.
    setbfile(path, "Into", FN_DIR);
    n = get_records(path, recs, sizeof(fileheader_t), 1, 8);
    EXPECT_INT(n, 3);
    EXPECT_STR(recs[0].filename, "M.100.A.001");
    EXPECT_STR(recs[1].filename, "M.200.A.002");
    EXPECT_STR(recs[2].filename, "M.300.A.003");
    setbfile(path, "Into", "M.200.A.002");
    EXPECT(dashf(path));
}

static void
test_rebuild_and_checks(void)
{
    bof_bbs_rebuild_community_t req = { .community = "NoSuch" };
    bof_bbs_rebuild_community_response_t res;
    bof_person_t user = { .name = "bob" };
    bof_actor_t bob = { .name = "bob", .bbs_person = &user };

    EXPECT_INT(bof_bbs_rebuild_community(&sysop, &req, &res),
               BOF_ERR_COULDNT_FIND_COMMUNITY);
    // Only admins and board admins.
    make_board("Rebuilt");
    req.community = "Rebuilt";
    EXPECT_INT(bof_bbs_rebuild_community(&bob, &req, &res),
               BOF_ERR_NOT_AN_ADMIN);
    user.bbs_perms = PERM_BOARDS;
    EXPECT_OK(bof_bbs_rebuild_community(&bob, &req, &res));
    EXPECT_INT(res.in_background, 1);
}

int
main(void)
{
    char dir[] = "/tmp/bof_test_XXXXXX", cmd[PATHLEN];
    const char *letters = "FGINOR";

    if (!mkdtemp(dir) || chdir(dir) != 0) {
        perror("setup");
        return 1;
    }
    mkdir("boards", 0755);
    mkdir("man", 0755);
    mkdir("man/boards", 0755);
    mkdir("tmp", 0755);
    for (const char *c = letters; *c; c++) {
        snprintf(cmd, sizeof(cmd), "boards/%c", *c);
        mkdir(cmd, 0755);
        snprintf(cmd, sizeof(cmd), "man/boards/%c", *c);
        mkdir(cmd, 0755);
    }

    // A board cache, as the BBS keeps it in shared memory.
    SHM = calloc(1, sizeof(SHM_t));
    bcache = SHM->bcache;

    RUN(test_board_headers);
    RUN(test_remove_moderator);
    RUN(test_remove);
    RUN(test_rename);
    RUN(test_merge);
    RUN(test_rebuild_and_checks);

    free(SHM);
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    if (system(cmd) != 0)
        fprintf(stderr, "could not remove %s\n", dir);
    return bof_test_report();
}
