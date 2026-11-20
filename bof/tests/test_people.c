/*
 * Tests of the communities and persons of bof.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 */

#include "bbs.h"
#include "bof_test.h"
#include "bof_storage.h"

// A backend that only counts what it is asked to write.
static int writes;

static bof_error_t
count_edit_post(void *ctx GCC_UNUSED, const bof_actor_t *actor GCC_UNUSED,
                const bof_edit_post_t *req GCC_UNUSED,
                bof_post_response_t *out GCC_UNUSED)
{
    writes++;
    return BOF_OK;
}

static bof_error_t
count_lock_post(void *ctx GCC_UNUSED, const bof_actor_t *actor GCC_UNUSED,
                const bof_lock_post_t *req GCC_UNUSED,
                bof_post_response_t *out GCC_UNUSED)
{
    writes++;
    return BOF_OK;
}

// The record of the post as the backend keeps it: by "someone".
static bof_error_t
kept_find_post(void *ctx GCC_UNUSED, const bof_post_key_t *key,
               bof_found_post_t *out)
{
    memset(out->bbs_header, 0, sizeof(*out->bbs_header));
    strlcpy(out->bbs_header->filename, key->bbs_post_file,
            sizeof(out->bbs_header->filename));
    strlcpy(out->bbs_header->owner, "someone",
            sizeof(out->bbs_header->owner));
    setbfile(out->bbs_dir, key->community, FN_DIR);
    out->bbs_index = 1;
    return BOF_OK;
}

static const bof_storage_ops_t counting_ops = {
    .name = "counting",
    .edit_post = count_edit_post,
    .lock_post = count_lock_post,
    .find_post = kept_find_post,
};

int bof_test_failures;

// A board cache of two boards, as the BBS keeps it in shared memory.
static void
setup_boards(void)
{
    SHM = calloc(1, sizeof(SHM_t));
    if (!SHM) {
        perror("calloc");
        exit(1);
    }
    bcache = SHM->bcache;
    memset(SHM->hash_head, 0xff, sizeof(SHM->hash_head));

    strlcpy(bcache[0].brdname, "Alpha", sizeof(bcache[0].brdname));
    strlcpy(bcache[0].desc, "the first", sizeof(bcache[0].desc));
    bcache[0].brdattr = BRD_OVER18;
    bcache[0].post_limit_logins = 3;
    bcache[0].moderators[1].unum = 5;
    bcache[0].moderators[1].firstlogin = 1700000000;

    strlcpy(bcache[1].brdname, "Hidden", sizeof(bcache[1].brdname));
    bcache[1].brdattr = BRD_HIDE;

    SHM->Bnumber = 2;
    SHM->bsorted[0][0] = 0;
    SHM->bsorted[0][1] = 1;
}

static void
test_communities(void)
{
    bof_community_t c, list[4];

    EXPECT_OK(bof_get_community("alpha", &c));
    EXPECT_STR(c.name, "Alpha");
    EXPECT_STR(c.title, "the first");
    EXPECT_INT(c.nsfw, 1);
    EXPECT_INT(c.hidden, 0);
    EXPECT_INT(c.bbs_id, 1);
    EXPECT_INT(c.bbs_post_limit_logins, 3);
    EXPECT_INT(c.nmoderators, 1);
    EXPECT(c.moderators[0] == ((uint64_t)1700000000 << 32 | 5));
    EXPECT_INT(c.id, 0);        // no IDs in the files
    EXPECT_INT(bof_get_community("Nothing", &c),
               BOF_ERR_COULDNT_FIND_COMMUNITY);

    // Public ones only, unless all are asked for; by pages.
    bof_list_communities_t req = { .limit = 4 };
    bof_list_communities_response_t res = { .communities = list };
    EXPECT_OK(bof_list_communities(&req, &res));
    EXPECT_INT(res.count, 1);
    EXPECT_STR(list[0].name, "Alpha");
    req.bbs_all = 1;
    EXPECT_OK(bof_list_communities(&req, &res));
    EXPECT_INT(res.count, 2);
    EXPECT_INT(list[1].hidden, 1);
    req.limit = 1;
    req.page = 2;
    EXPECT_OK(bof_list_communities(&req, &res));
    EXPECT_INT(res.count, 1);
    EXPECT_STR(list[0].name, "Hidden");
}

static void
test_persons(void)
{
    bof_person_t p;

    EXPECT_INT(bof_get_person("no such!", &p), BOF_ERR_INVALID_BODY_FIELD);
    EXPECT_INT(bof_get_person("nobody", &p), BOF_ERR_COULDNT_FIND_PERSON);
}

// A person with the permissions given, of account slot 5.
static bof_person_t
person(const char *name, uint32_t perms, uint32_t firstlogin)
{
    bof_person_t p = { 0 };
    strlcpy(p.name, name, sizeof(p.name));
    p.bbs_perms = perms;
    p.bbs_local_user = (uint64_t)firstlogin << 32 | 5;
    p.bbs_numlogindays = 100;
    return p;
}

static void
test_rules(void)
{
    bof_community_t alpha, hidden;
    bof_bbs_rule_t rule;
    fileheader_t post;
    bof_person_t mod = person("mod", PERM_BASIC | PERM_LOGINOK | PERM_POST,
                              1700000000);
    bof_person_t other = person("other", PERM_BASIC | PERM_LOGINOK |
                                PERM_POST, 1600000000);
    bof_person_t guest = person("guest", PERM_BASIC, 1600000000);

    bof_get_community("Alpha", &alpha);
    bof_get_community("Hidden", &hidden);

    // Moderators: the same account.
    EXPECT_INT(bof_bbs_is_moderator(&mod, &alpha), 1);
    EXPECT_INT(bof_bbs_is_moderator(&other, &alpha), 0);
    EXPECT_INT(bof_bbs_is_moderator(NULL, &alpha), 0);

    // Reading: adults only; a hidden board that hides posting too.
    EXPECT_INT(bof_bbs_can_read(&other, &alpha, &rule),
               BOF_ERR_BBS_NOT_ALLOWED);
    EXPECT_INT(rule, BOF_BBS_RULE_ADULTS_ONLY);
    other.bbs_over_18 = 1;
    EXPECT_OK(bof_bbs_can_read(&other, &alpha, &rule));
    EXPECT_OK(bof_bbs_can_read(&mod, &alpha, NULL));
    EXPECT_INT(rule, BOF_BBS_RULE_NONE);
    // A hidden board that does not hide its posts: read, but told.
    EXPECT_OK(bof_bbs_can_read(&other, &hidden, &rule));
    EXPECT_INT(rule, BOF_BBS_RULE_HIDDEN);
    // The same by its place in the board cache, without the ID.
    bof_community_t at;
    EXPECT_OK(bof_bbs_get_community_at(2, &at));
    EXPECT_STR(at.name, "Hidden");
    EXPECT_INT(at.bbs_id, 2);
    EXPECT(at.id == 0);
    EXPECT_INT(bof_bbs_get_community_at(0, &at),
               BOF_ERR_COULDNT_FIND_COMMUNITY);
    EXPECT_INT(bof_bbs_get_community_at(MAX_BOARD + 1, &at),
               BOF_ERR_COULDNT_FIND_COMMUNITY);
    // The rules by the place, as the lists check every board.
    EXPECT_OK(bof_bbs_can_read_at(&other, 2, &rule));
    EXPECT_INT(rule, BOF_BBS_RULE_HIDDEN);
    EXPECT_OK(bof_bbs_can_read_at(&other, 1, &rule));
    EXPECT_INT(rule, BOF_BBS_RULE_NONE);
    EXPECT_INT(bof_bbs_can_read_at(&other, 0, &rule),
               BOF_ERR_COULDNT_FIND_COMMUNITY);
    EXPECT_INT(rule, BOF_BBS_RULE_NO_COMMUNITY);
    bcache[1].brdattr |= BRD_POSTMASK;
    bof_get_community("Hidden", &hidden);
    EXPECT_INT(bof_bbs_can_read(&other, &hidden, &rule),
               BOF_ERR_COULDNT_FIND_COMMUNITY);
    EXPECT_INT(rule, BOF_BBS_RULE_HIDDEN);
    EXPECT_INT(bof_bbs_can_read_at(&other, 2, &rule),
               BOF_ERR_COULDNT_FIND_COMMUNITY);

    // Posting: verified accounts; the limits of the board, which its
    // moderators pass.
    EXPECT_INT(bof_bbs_can_post(&guest, &alpha, &rule),
               BOF_ERR_BBS_NOT_ALLOWED);
    EXPECT_INT(rule, BOF_BBS_RULE_NOT_VERIFIED);
    EXPECT_OK(bof_bbs_can_post(&other, &alpha, &rule));
    other.bbs_numlogindays = 20;
    EXPECT_INT(bof_bbs_can_post(&other, &alpha, &rule),
               BOF_ERR_BBS_NOT_ALLOWED);
    EXPECT_INT(rule, BOF_BBS_RULE_TOO_FEW_LOGINS);
    mod.bbs_numlogindays = 20;
    EXPECT_OK(bof_bbs_can_post(&mod, &alpha, &rule));
    other.bbs_numlogindays = 100;

    // Commenting: not on locked, closed or deleted posts.
    memset(&post, 0, sizeof(post));
    strlcpy(post.filename, "M.1700000000.A.000", sizeof(post.filename));
    strlcpy(post.owner, "someone", sizeof(post.owner));
    EXPECT_OK(bof_bbs_can_comment(&other, &alpha, &post, &rule));
    post.filename[0] = 'L';
    EXPECT_INT(bof_bbs_can_comment(&other, &alpha, &post, &rule),
               BOF_ERR_LOCKED);
    post.filename[0] = 'M';
    post.filemode = FILE_MARKED | FILE_SOLVED;
    EXPECT_INT(bof_bbs_can_comment(&other, &alpha, &post, &rule),
               BOF_ERR_BBS_NOT_ALLOWED);
    EXPECT_INT(rule, BOF_BBS_RULE_NO_COMMENTS);
    post.filemode = 0;
    if (IS_ENABLED(CONFIG_SAFE_ARTICLE_DELETE)) {
        strlcpy(post.owner, "-", sizeof(post.owner));
        EXPECT_INT(bof_bbs_can_comment(&other, &alpha, &post, &rule),
                   BOF_ERR_COULDNT_FIND_POST);
        strlcpy(post.owner, "someone", sizeof(post.owner));
    }

    // The API checks an actor with a person, before any backend.
    bof_storage_reset();
    bof_storage_set(&counting_ops, NULL);
    writes = 0;
    bof_actor_t me = { .name = "other", .bbs_person = &other };
    bof_lock_post_t lock = {
        .post = { .community = "Alpha", .bbs_header = &post }, .locked = 1,
    };
    EXPECT_INT(bof_lock_post(&me, &lock, NULL), BOF_ERR_NOT_A_MODERATOR);
    bof_edit_post_t edit = { .post = lock.post, .name = "new title" };
    EXPECT_INT(bof_edit_post(&me, &edit, NULL),
               BOF_ERR_NO_POST_EDIT_ALLOWED);
    // A record made up by the caller does not count: the backend's does.
    fileheader_t forged = post;
    strlcpy(forged.owner, "other", sizeof(forged.owner));
    edit.post.bbs_header = &forged;
    EXPECT_INT(bof_edit_post(&me, &edit, NULL),
               BOF_ERR_NO_POST_EDIT_ALLOWED);
    // Nor a list other than the board's, nor names out of it.
    edit.post.bbs_dir = "home/s/someone/.DIR";
    EXPECT_INT(bof_edit_post(&me, &edit, NULL), BOF_ERR_NO_POST_EDIT_ALLOWED);
    me.bbs_person = NULL;
    EXPECT_INT(bof_edit_post(&me, &edit, NULL), BOF_ERR_INVALID_BODY_FIELD);
    edit.post.bbs_dir = "boards/A/Alpha/.DIR";
    strlcpy(forged.filename, ".DIR", sizeof(forged.filename));
    EXPECT_INT(bof_edit_post(&me, &edit, NULL), BOF_ERR_INVALID_BODY_FIELD);
    me.bbs_person = &other;
    EXPECT_INT(bof_edit_post(&me, &edit, NULL), BOF_ERR_INVALID_BODY_FIELD);
    EXPECT(!bof_bbs_valid_post_file("../x"));
    EXPECT(!bof_bbs_valid_post_file(".DIR"));
    EXPECT(bof_bbs_valid_post_file("M.1700000000.A.000"));
    bof_create_post_t create = { .community = "Alpha", .bbs_header = &post };
    me.bbs_person = &guest;
    EXPECT_INT(bof_create_post(&me, &create, NULL), BOF_ERR_BBS_NOT_ALLOWED);
    create.community = "Nothing";
    EXPECT_INT(bof_create_post(&me, &create, NULL),
               BOF_ERR_COULDNT_FIND_COMMUNITY);
    // Reports: of the board for its moderators, of the site for admins
    // and the police.
    bof_report_t reps[2];
    bof_list_reports_t lr = { .community = "Alpha", .limit = 2 };
    bof_list_reports_response_t lres = { .reports = reps };
    me.bbs_person = &other;
    EXPECT_INT(bof_list_reports(&me, &lr, &lres), BOF_ERR_NOT_A_MODERATOR);
    lr.community = NULL;
    EXPECT_INT(bof_list_reports(&me, &lr, &lres), BOF_ERR_NOT_AN_ADMIN);
    bof_person_t police = other;
    police.bbs_perms |= PERM_POLICE;
    me.bbs_person = &police;
    EXPECT(bof_list_reports(&me, &lr, &lres) != BOF_ERR_NOT_AN_ADMIN);
    me.bbs_person = &guest;

    // None of them got to the backend; the moderator does.
    EXPECT_INT(writes, 0);
    me.bbs_person = &mod;
    EXPECT_OK(bof_lock_post(&me, &lock, NULL));
    EXPECT_INT(writes, 1);
    bof_storage_reset();
}

// Groups: Top (bid 3, right under the root), Sub in it (bid 4), and the
// board Leaf in Sub (bid 5), without moderators of its own.
static void
test_group_roles(void)
{
    bof_community_t leaf;
    bof_person_t lead = person("lead", PERM_BASIC | PERM_LOGINOK, 1700000000);
    bof_person_t op = person("op", PERM_BASIC | PERM_LOGINOK, 1600000000);
    bof_person_t admin = person("admin", PERM_BASIC | PERM_LOGINOK |
                                PERM_BOARDS, 1500000000);

    op.bbs_local_user = (uint64_t)1600000000 << 32 | 6;
    admin.bbs_local_user = (uint64_t)1500000000 << 32 | 7;
    strlcpy(bcache[2].brdname, "Top", sizeof(bcache[2].brdname));
    bcache[2].brdattr = BRD_GROUPBOARD;
    bcache[2].gid = 1;
    bcache[2].moderators[0].unum = 5;
    bcache[2].moderators[0].firstlogin = 1700000000;
    strlcpy(bcache[3].brdname, "Sub", sizeof(bcache[3].brdname));
    bcache[3].brdattr = BRD_GROUPBOARD;
    bcache[3].gid = 3;
    bcache[3].moderators[0].unum = 6;
    strlcpy(bcache[4].brdname, "Leaf", sizeof(bcache[4].brdname));
    bcache[4].gid = 4;
    EXPECT_OK(bof_bbs_get_community_at(5, &leaf));

    // The lead of Top leads and operates; the operator of Sub operates.
    EXPECT_INT(bof_bbs_group_roles(&lead, &leaf),
               BOF_BBS_GROUP_OP | BOF_BBS_GROUP_LEAD);
    EXPECT_INT(bof_bbs_group_roles(&op, &leaf), BOF_BBS_GROUP_OP);
    EXPECT_INT(bof_bbs_group_roles(&admin, &leaf), 0);
    EXPECT_INT(bof_bbs_is_moderator(&op, &leaf), 0);
    // So they moderate Leaf while it has no moderators; board admins always.
    EXPECT_INT(bof_bbs_may_moderate(&op, &leaf), 1);
    EXPECT_INT(bof_bbs_may_moderate(&lead, &leaf), 1);
    EXPECT_INT(bof_bbs_may_moderate(&admin, &leaf), 1);
    bcache[4].moderators[0].unum = 8;
    EXPECT_OK(bof_bbs_get_community_at(5, &leaf));
    EXPECT_INT(bof_bbs_may_moderate(&op, &leaf), 0);
    EXPECT_INT(bof_bbs_may_moderate(&admin, &leaf), 1);
    // Not for those who may not be moderators.
    bcache[4].moderators[0].unum = 0;
    EXPECT_OK(bof_bbs_get_community_at(5, &leaf));
    op.bbs_perms |= PERM_NOCITIZEN;
    EXPECT_INT(bof_bbs_group_roles(&op, &leaf), 0);
    EXPECT_INT(bof_bbs_may_moderate(&op, &leaf), 0);
    // A group that does not list its boards ends the chain.
    bcache[3].brdattr |= BRD_SYMBOLIC;
    EXPECT_INT(bof_bbs_group_roles(&lead, &leaf), 0);
}

int
main(void)
{
    setup_boards();
    RUN(test_communities);
    RUN(test_persons);
    RUN(test_rules);
    RUN(test_group_roles);
    free(SHM);
    return bof_test_report();
}
