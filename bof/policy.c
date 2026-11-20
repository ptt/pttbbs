/*
 * policy.c - bof (Birds Of Feather): the rules of the BBS, who may do
 * what, for any user interface.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * The rules mbbsd kept with the state of its session (HasBoardPerm,
 * postperm_msg, get_board_restriction_reason and those of comment()),
 * taken over as functions of a person and a community (people.c), so
 * that mbbsd and a Lemmy gateway apply the same ones; bof checks them
 * itself for an actor with a person (bof_actor_t.bbs_person). Lemmy checks
 * its rules in its API (bans, locked posts, moderators); PTT's own (login
 * days, levels, tickets) give bbs_not_allowed with the rule.
 */

#include "bof_int.h"

static int
has_perm(const bof_person_t *p, uint32_t perms)
{
    return p && (p->bbs_perms & perms) != 0;
}

static int
usernum_of(const bof_person_t *p)
{
    return p ? (int)(uint32_t)(p->bbs_local_user & 0xffffffffu) : 0;
}

static bof_error_t
deny(bof_bbs_rule_t *rule, bof_bbs_rule_t why, bof_error_t err)
{
    if (rule)
        *rule = why;
    return err;
}

static bof_error_t
allow(bof_bbs_rule_t *rule)
{
    if (rule)
        *rule = BOF_BBS_RULE_NONE;
    return BOF_OK;
}

int
bof_bbs_is_moderator(const bof_person_t *p, const bof_community_t *c)
{
    // Accounts that have not completed registration moderate nothing.
    if (!p || !c || !p->bbs_local_user || !has_perm(p, PERM_BASIC) ||
        !has_perm(p, PERM_LOGINOK))
        return 0;
    for (int i = 0; i < c->nmoderators; i++) {
        uint64_t m = c->moderators[i];
        // The same slot; the same account unless it was not recorded.
        if ((uint32_t)m == (uint32_t)p->bbs_local_user &&
            ((m >> 32) == 0 || m == p->bbs_local_user))
            return 1;
    }
    return 0;
}

int
bof_bbs_may_moderate(const bof_person_t *p, const bof_community_t *c)
{
    if (!p || !c)
        return 0;
    if (p->admin || has_perm(p, PERM_SYSOP))
        return 1;
    // As mbbsd gives the moderator mode (MODE_BM) on entering a board.
    if (has_perm(p, PERM_NOCITIZEN))
        return 0;
    if (has_perm(p, PERM_BOARDS) || bof_bbs_is_moderator(p, c))
        return 1;
    return c->nmoderators == 0 &&
        (bof_bbs_group_roles(p, c) & BOF_BBS_GROUP_OP) != 0;
}

int32_t
bof_bbs_muted_until(const bof_person_t *p, const bof_community_t *c)
{
    char path[PATHLEN];
    unsigned int expire = 0;
    FILE *fp;

    if (!p || !c || !is_validuserid(p->name))
        return 0;
    // muted/b_<board> in the home of the person: the time it ends, then
    // the reason. An expired one is no longer in effect.
    sethomefile(path, p->name, FN_MUTED "/b_");
    strlcat(path, c->name, sizeof(path));
    if (!(fp = fopen(path, "r")))
        return 0;
    if (fscanf(fp, "%u", &expire) != 1)
        expire = 0;
    fclose(fp);
    return time4_gt((time4_t)expire, time4(NULL)) ? (int32_t)expire : 0;
}

bof_error_t
bof_bbs_can_read(const bof_person_t *p, const bof_community_t *c,
                 bof_bbs_rule_t *rule)
{
    if (!c)
        return deny(rule, BOF_BBS_RULE_NO_COMMUNITY,
                    BOF_ERR_COULDNT_FIND_COMMUNITY);
    if (has_perm(p, PERM_SYSOP))
        return allow(rule);
    // The police may enter the boards of moderators.
    if ((c->bbs_level & PERM_BM) && has_perm(p, PERM_POLICE | PERM_POLICE_MAN))
        return allow(rule);
    if (bof_bbs_is_moderator(p, c))
        return allow(rule);
    // Hidden boards: for the friends of the board, unless it only hides
    // posting (as the BBS always did).
    if (c->bbs_attr & BRD_HIDE) {
        if (usernum_of(p) > 0 && is_hidden_board_friend(c->bbs_id,
                                                        usernum_of(p)))
            return allow(rule);
        if (c->bbs_attr & BRD_POSTMASK)
            return deny(rule, BOF_BBS_RULE_HIDDEN,
                        BOF_ERR_COULDNT_FIND_COMMUNITY);
        // Allowed, but told: the lists of the BBS treat it apart.
        return deny(rule, BOF_BBS_RULE_HIDDEN, BOF_OK);
    }
    if ((c->bbs_attr & BRD_OVER18) && !(p && p->bbs_over_18))
        return deny(rule, BOF_BBS_RULE_ADULTS_ONLY, BOF_ERR_BBS_NOT_ALLOWED);
    if (c->bbs_level && !(c->bbs_attr & BRD_POSTMASK) &&
        !has_perm(p, c->bbs_level))
        return deny(rule, BOF_BBS_RULE_LEVEL, BOF_ERR_BBS_NOT_ALLOWED);
    return allow(rule);
}

bof_error_t
bof_bbs_post_permission(const bof_person_t *p, const bof_community_t *c,
                        bof_bbs_rule_t *rule)
{
    uint32_t level;

    if (!c)
        return deny(rule, BOF_BBS_RULE_NO_COMMUNITY,
                    BOF_ERR_COULDNT_FIND_COMMUNITY);
    if (c->bbs_read_only)
        return deny(rule, BOF_BBS_RULE_READ_ONLY, BOF_ERR_BBS_NOT_ALLOWED);
    if (has_perm(p, PERM_SYSOP))
        return allow(rule);
    if (bof_bbs_muted_until(p, c))
        return deny(rule, BOF_BBS_RULE_MUTED, BOF_ERR_BANNED_FROM_COMMUNITY);
    if (strcasecmp(c->name, BN_SYSOP) == 0)
        return allow(rule);
    if (c->bbs_attr & BRD_NOREG_CAN_POST)
        return allow(rule);
    if (!has_perm(p, PERM_POST))
        return deny(rule, BOF_BBS_RULE_NOT_VERIFIED, BOF_ERR_BBS_NOT_ALLOWED);
    if (c->bbs_attr & BRD_HIDE)
        return allow(rule);
    if ((c->bbs_attr & BRD_RESTRICTEDPOST) &&
        !is_hidden_board_friend(c->bbs_id, usernum_of(p)))
        return deny(rule, BOF_BBS_RULE_RESTRICTED, BOF_ERR_BBS_NOT_ALLOWED);
    level = c->bbs_level;
    // With a ticket unpaid, only in the boards about tickets.
    if (has_perm(p, PERM_VIOLATELAW))
        return (level & PERM_VIOLATELAW) ? allow(rule) :
            deny(rule, BOF_BBS_RULE_UNPAID_TICKET, BOF_ERR_BBS_NOT_ALLOWED);
    if (!(level & ~PERM_POST) || has_perm(p, level & ~PERM_POST))
        return allow(rule);
    return deny(rule, BOF_BBS_RULE_LEVEL, BOF_ERR_BBS_NOT_ALLOWED);
}

bof_error_t
bof_bbs_post_restriction(const bof_person_t *p, const bof_community_t *c,
                         bof_bbs_rule_t *rule)
{
    if (!c)
        return deny(rule, BOF_BBS_RULE_NO_COMMUNITY,
                    BOF_ERR_COULDNT_FIND_COMMUNITY);
    if (has_perm(p, PERM_SYSOP) || bof_bbs_is_moderator(p, c))
        return allow(rule);
    if ((p ? p->bbs_numlogindays : 0) / 10 < c->bbs_post_limit_logins)
        return deny(rule, BOF_BBS_RULE_TOO_FEW_LOGINS,
                    BOF_ERR_BBS_NOT_ALLOWED);
    if (IS_ENABLED(CONFIG_ASSESS) &&
        (p ? p->bbs_badpost : 0) > 255 - c->bbs_post_limit_badpost)
        return deny(rule, BOF_BBS_RULE_TOO_MANY_BADPOSTS,
                    BOF_ERR_BBS_NOT_ALLOWED);
    return allow(rule);
}

bof_error_t
bof_bbs_can_post(const bof_person_t *p, const bof_community_t *c,
                 bof_bbs_rule_t *rule)
{
    bof_error_t err = bof_bbs_post_permission(p, c, rule);
    return err != BOF_OK ? err : bof_bbs_post_restriction(p, c, rule);
}

bof_error_t
bof_bbs_can_comment(const bof_person_t *p, const bof_community_t *c,
                    const fileheader_t *post, bof_bbs_rule_t *rule)
{
    bof_error_t err;

    if (!c)
        return deny(rule, BOF_BBS_RULE_NO_COMMUNITY,
                    BOF_ERR_COULDNT_FIND_COMMUNITY);
    if (!post)
        return deny(rule, BOF_BBS_RULE_DELETED, BOF_ERR_COULDNT_FIND_POST);
    // Closed: on the board, a locked post (Lemmy's locked), or a post
    // marked and solved.
    if (post->filename[0] == 'L')
        return deny(rule, BOF_BBS_RULE_NO_COMMENTS, BOF_ERR_LOCKED);
    if ((c->bbs_attr & BRD_NO_COMMENTS) ||
        ((post->filemode & FILE_MARKED) && (post->filemode & FILE_SOLVED)))
        return deny(rule, BOF_BBS_RULE_NO_COMMENTS, BOF_ERR_BBS_NOT_ALLOWED);
    if ((err = bof_bbs_post_permission(p, c, rule)) != BOF_OK)
        return err;
    if (*ONLY_OP_COMMENT_BOARD &&
        strcasecmp(c->name, ONLY_OP_COMMENT_BOARD) == 0 &&
        !bof_bbs_is_moderator(p, c) &&
        !has_perm(p, PERM_SYSOP | PERM_GROUP_LEAD | PERM_GROUP_OP))
        return deny(rule, BOF_BBS_RULE_ONLY_OPS_COMMENT,
                    BOF_ERR_BBS_NOT_ALLOWED);
    if (IS_ENABLED(CONFIG_SAFE_ARTICLE_DELETE) &&
        (post->filename[0] == '.' || post->owner[0] == '-'))
        return deny(rule, BOF_BBS_RULE_DELETED, BOF_ERR_COULDNT_FIND_POST);
    return bof_bbs_post_restriction(p, c, rule);
}
