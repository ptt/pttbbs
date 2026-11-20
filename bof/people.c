/*
 * people.c - bof (Birds Of Feather): communities (boards) and persons
 * (accounts), as Lemmy shows them.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Boards come from the board cache of the BBS (the shared memory every
 * process of the BBS attaches), accounts from its password file; both are
 * site-wide, not the state of a session. Only what Lemmy shows and what the
 * rules of the BBS need is given: nothing private.
 */

#include "bof_int.h"
#include "bof_storage.h"

// The board cache (shared memory), defined by the programs of the BBS
// (mbbsd/var.c) and attached by them; bof does not take the rest of var.h.
extern SHM_t *SHM;
extern boardheader_t *bcache;

// Boards the system keeps read only, whatever their attributes.
static int
read_only_board(const char *name)
{
    return strcasecmp(name, BN_SECURITY) == 0 ||
           strcasecmp(name, BN_ALLPOST) == 0;
}

static uint64_t
local_user(const user_t *u)
{
    uint64_t v = 0;
    _Static_assert(sizeof(*u) == sizeof(v), "user_t");
    if (u->unum > 0)
        memcpy(&v, u, sizeof(v));
    return v;
}

// What the rules of reading (bof_bbs_can_read()) look at, into out
// (zeroed by the caller): no names, which the lists of boards do not
// need for it.
static void
fill_community_access(int bid, const boardheader_t *bh,
                      bof_community_t *out)
{
    out->nsfw = (bh->brdattr & BRD_OVER18) != 0;
    out->hidden = (bh->brdattr & BRD_HIDE) != 0;
    out->bbs_id = bid;
    out->bbs_attr = bh->brdattr;
    out->bbs_level = bh->level;
    _Static_assert(MAX_BMs <= BOF_MODERATORS_MAX, "BOF_MODERATORS_MAX");
    for (int i = 0; i < MAX_BMs; i++)
        if (bh->moderators[i].unum > 0)
            out->moderators[out->nmoderators++] =
                local_user(&bh->moderators[i]);
}

static void
fill_community(int bid, const boardheader_t *bh, bof_community_t *out)
{
    memset(out, 0, sizeof(*out));
    strlcpy(out->name, bh->brdname, sizeof(out->name));
    strlcpy(out->title, bh->desc, sizeof(out->title));
    fill_community_access(bid, bh, out);
    out->bbs_read_only = read_only_board(bh->brdname);
    out->bbs_post_limit_logins = bh->post_limit_logins;
    out->bbs_post_limit_badpost = bh->post_limit_badpost;
}

bof_error_t
bof_get_community(const char *name, bof_community_t *out)
{
    const boardheader_t *bh;
    int bid;

    if (!name || !*name || !out)
        return BOF_ERR_INVALID_BODY_FIELD;
    if (!SHM || !bcache || !(bid = getbnum(name)) ||
        !(bh = getbcache(bid))->brdname[0])
        return BOF_ERR_COULDNT_FIND_COMMUNITY;
    fill_community(bid, bh, out);
    bof_bbs_community_id(out->name, &out->id);
    return BOF_OK;
}

// Whether account p is among the moderators of bh, as
// bof_bbs_is_moderator() (which checks the account first).
static int
moderates(const bof_person_t *p, const boardheader_t *bh)
{
    for (int i = 0; i < MAX_BMs; i++) {
        uint64_t m = local_user(&bh->moderators[i]);
        if (m && (uint32_t)m == (uint32_t)p->bbs_local_user &&
            ((m >> 32) == 0 || m == p->bbs_local_user))
            return 1;
    }
    return 0;
}

// Group trees are shallow; a walk up a chain of groups gives up after this
// many, in case of a cycle (as mbbsd's GROUP_DEPTH_MAX).
#define BOF_GROUP_DEPTH_MAX 64

int
bof_bbs_group_roles(const bof_person_t *p, const bof_community_t *c)
{
    const boardheader_t *bh;
    int gid, roles = 0;

    // As mbbsd's group_roles_above(): none for those who may not be
    // moderators, or before the account is verified.
    if (!p || !c || !SHM || !bcache || c->bbs_id <= 0 ||
        c->bbs_id > MAX_BOARD || !p->bbs_local_user ||
        (p->bbs_perms & PERM_NOCITIZEN) || !(p->bbs_perms & PERM_BASIC) ||
        !(p->bbs_perms & PERM_LOGINOK))
        return 0;
    gid = getbcache(c->bbs_id)->gid;
    if (gid == c->bbs_id)       // a few old groups are in themselves
        return 0;
    // Up the chain, through groups that list their boards; 1 is the root.
    for (int depth = 0; depth < BOF_GROUP_DEPTH_MAX && 1 < gid &&
         gid <= MAX_BOARD; depth++) {
        bh = getbcache(gid);
        if (!GROUP_LISTS_BOARDS(bh))
            break;
        if (moderates(p, bh))
            roles |= bh->gid == 1 ? BOF_BBS_GROUP_OP | BOF_BBS_GROUP_LEAD :
                                    BOF_BBS_GROUP_OP;
        gid = bh->gid;
    }
    return roles;
}

bof_error_t
bof_bbs_get_community_at(int bbs_id, bof_community_t *out)
{
    const boardheader_t *bh;

    if (!out)
        return BOF_ERR_INVALID_BODY_FIELD;
    if (!SHM || !bcache || bbs_id <= 0 || bbs_id > MAX_BOARD ||
        !(bh = getbcache(bbs_id))->brdname[0])
        return BOF_ERR_COULDNT_FIND_COMMUNITY;
    fill_community(bbs_id, bh, out);
    return BOF_OK;
}

bof_error_t
bof_bbs_can_read_at(const bof_person_t *person, int bbs_id,
                    bof_bbs_rule_t *rule)
{
    const boardheader_t *bh;
    bof_community_t c = { 0 };

    if (!SHM || !bcache || bbs_id <= 0 || bbs_id > MAX_BOARD ||
        !(bh = getbcache(bbs_id))->brdname[0]) {
        if (rule)
            *rule = BOF_BBS_RULE_NO_COMMUNITY;
        return BOF_ERR_COULDNT_FIND_COMMUNITY;
    }
    fill_community_access(bbs_id, bh, &c);
    return bof_bbs_can_read(person, &c, rule);
}

bof_error_t
bof_list_communities(const bof_list_communities_t *req,
                     bof_list_communities_response_t *out)
{
    int skip, n;

    if (!req || !out || !out->communities || req->limit <= 0 ||
        req->page < 0)
        return BOF_ERR_INVALID_BODY_FIELD;
    out->count = 0;
    if (!SHM || !bcache)
        return BOF_OK;
    skip = req->page > 1 ? (req->page - 1) * req->limit : 0;
    n = SHM->Bnumber;
    for (int bid = 1; bid <= n && out->count < req->limit; bid++) {
        const boardheader_t *bh = getbcache(bid);
        // Not group headers nor empty places; public ones unless asked.
        if (!bh->brdname[0] || (bh->brdattr & BRD_GROUPBOARD))
            continue;
        if (!req->bbs_all && ((bh->brdattr & BRD_HIDE) ||
                              (bh->level && !(bh->brdattr & BRD_POSTMASK))))
            continue;
        if (skip > 0) {
            skip--;
            continue;
        }
        fill_community(bid, bh, &out->communities[out->count]);
        bof_bbs_community_id(bh->brdname,
                             &out->communities[out->count].id);
        out->count++;
    }
    return BOF_OK;
}

bof_error_t
bof_get_person(const char *name, bof_person_t *out)
{
    userec_t u;
    int unum;

    if (!name || !out || !is_validuserid(name))
        return BOF_ERR_INVALID_BODY_FIELD;
    if ((unum = passwd_load_user(name, &u)) <= 0)
        return BOF_ERR_COULDNT_FIND_PERSON;
    memset(out, 0, sizeof(*out));
    strlcpy(out->name, u.userid, sizeof(out->name));
    strlcpy(out->display_name, u.nickname, sizeof(out->display_name));
    out->published = u.firstlogin;
    out->admin = (u.userlevel & PERM_SYSOP) != 0;
    user_t acct = { .unum = unum, .firstlogin = u.firstlogin };
    out->bbs_local_user = local_user(&acct);
    out->bbs_perms = u.userlevel;
    out->bbs_numlogindays = u.numlogindays;
    out->bbs_numposts = u.numposts;
    out->bbs_badpost = u.badpost;
    out->bbs_over_18 = u.over_18;
    return BOF_OK;
}
