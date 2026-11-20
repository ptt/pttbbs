/*
 * comment.c - bof (Birds Of Feather), the files of a BBS: the line of a
 * comment.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * A comment of the BBS is one line appended to the file of its post: its
 * mark (push, boo or arrow), the author, what was typed, and where from
 * and when.
 */

#include "../bof_int.h"

void
bof_files_format_comment(char *buf, size_t szbuf, int type, const char *author,
                         int width, const char *content, const char *tail)
{
    static const char *const kinds[] = {
        STR_COMMENT_PUSH, STR_COMMENT_BOO, STR_COMMENT_ARROW,
    };
    // Slightly different from the colors of the marks in the list.
    static const char *const attrs[] = {
        ANSI_COLOR(1;37), ANSI_COLOR(1;31), ANSI_COLOR(1;31),
    };
    _Static_assert(ARRAY_SIZE(kinds) == RECTYPE_SIZE,
                   "kinds must have one entry per comment type");
    _Static_assert(ARRAY_SIZE(attrs) == RECTYPE_SIZE,
                   "attrs must have one entry per comment type");

    if (type < 0 || type >= RECTYPE_SIZE)
        type = RECTYPE_ARROW;
    if (IS_ENABLED(CONFIG_OLD_RECOMMEND)) {
        snprintf(buf, szbuf,
                 ANSI_COLOR(1;31) STR_COMMENT_ARROW " " ANSI_COLOR(33) "%s"
                 ANSI_RESET ANSI_COLOR(33) ":%-*s" ANSI_RESET
                 STR_COMMENT_PUSH "%s\n",
                 author, width, content, tail);
        return;
    }
    snprintf(buf, szbuf,
             "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33)
             ": %-*s" ANSI_RESET "%s\n",
             attrs[type], kinds[type], author, width, content, tail);
}
