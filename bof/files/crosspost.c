/*
 * crosspost.c - bof (Birds Of Feather), the files of a BBS: the copies
 * of posts in other boards.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * New posts of open boards are copied (or noted, with CONFIG_LIVE_ALLPOST)
 * into boards that collect them (ALLPOST, ALLHIDPOST, NEWIDPOST, and
 * UnAnonymous for anonymous posts), with the same file name and the
 * origin board at the end of the title. Lemmy and Reddit show such
 * listings (r/all) without copies, so these are PTT's; a crosspost by a
 * user (Reddit's crosspost_parent) is a new post and not handled here.
 */

#include "../bof_int.h"

int
bof_bbs_is_aggregate(const char *community)
{
    return community &&
        (strcmp(community, BN_ALLPOST) == 0 ||
         strcmp(community, BN_ALLHIDPOST) == 0 ||
         strcmp(community, BN_NEWIDPOST) == 0 ||
         strcmp(community, BN_UNANONYMOUS) == 0);
}

int
bof_bbs_crosspost_origin(const fileheader_t *fh, char *board, size_t sz)
{
    const char *title, *bn;
    size_t tlen;
    int bnlen;

    if (!fh || !board || sz == 0)
        return 0;
    title = fh->title;
    tlen = strnlen(title, sizeof(fh->title));
    *board = '\0';
    if (tlen && title[tlen - 1] == ')') {
        // The format now: "title(BOARD)".
        if (!(bn = strrchr(title, '(')))
            return 0;
        bnlen = (int)strlen(++bn) - 1;
    } else {
        // The old format: "title .BOARD<2 bytes for board>", which may be
        // confused with board names.
        if (!(bn = strrchr(title, '.')))
            return 0;
        bnlen = (int)strlen(++bn) - 2;
    }
    if (bnlen <= 0 || bnlen > IDLEN)
        return 0;
    snprintf(board, sz, "%.*s", bnlen, bn);
    return *board != '\0';
}
