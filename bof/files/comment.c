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
    snprintf(buf, szbuf,
             "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33)
             ": %-*s" ANSI_RESET "%s\n",
             attrs[type], kinds[type], author, width, content, tail);
}

int
bof_bbs_comment_width(const char *author_shown, int show_from)
{
    int width = 78 - 3 /* mark */ - 6 /* date */ - 1 - 6 /* time */;

    if (show_from)
        width -= 15;
    width -= strlen(author_shown ? author_shown : "");
    width--;    // the space after the colon
    return width > 0 ? width : 0;
}

void
bof_bbs_format_comment(char *buf, size_t sz, int vote,
                       const char *author_shown, const char *text,
                       const char *from, int32_t when)
{
    char said[STRLEN], tail[STRLEN];
    time4_t t = when;
    int width = bof_bbs_comment_width(author_shown, from != NULL);
    int i, n = 0;

    if (width >= (int)sizeof(said))
        width = sizeof(said) - 1;
    // One line, cut to the room without splitting a double-byte character.
    for (i = 0; text && text[i] && n < width; i++) {
        unsigned char c = text[i];
        if (c & 0x80) {
            if (!text[i + 1] || n + 2 > width)
                break;
            said[n++] = text[i++];
            said[n++] = text[i];
            continue;
        }
        said[n++] = c < ' ' || c == 0x7f ? ' ' : c;
    }
    said[n] = 0;
    if (from)
        snprintf(tail, sizeof(tail), "%15s %s", from, Cdate_mdHM(&t));
    else
        snprintf(tail, sizeof(tail), " %s", Cdate_mdHM(&t));
    bof_files_format_comment(buf, sz, vote > 0 ? RECTYPE_GOOD :
                             vote < 0 ? RECTYPE_BAD : RECTYPE_ARROW,
                             author_shown ? author_shown : "", width, said,
                             tail);
}
