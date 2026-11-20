/*
 * Tests of the communities and persons of bof.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 */

#include "bbs.h"
#include "bof_test.h"

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

int
main(void)
{
    setup_boards();
    RUN(test_communities);
    RUN(test_persons);
    free(SHM);
    return bof_test_report();
}
