#include "cmpttdb/pttdb_comment.h"
#include "cmpttdb/pttdb_comment_private.h"

const enum Karma KARMA_BY_COMMENT_TYPE[] = {
    KARMA_GOOD,
    KARMA_BAD,
    KARMA_ARROW,
    0,
    0,                   // cross-post
    0                    // reset-karma
};

const char *COMMENT_TYPE_ATTR[] = {
    "±À",
    "¼N",
    "¡÷",
    "",
    "¡°",                 // cross-post
    "¡°"                  // reset-karma

};

const char *COMMENT_TYPE_ATTR2[] = {
    ANSI_COLOR(1;37),
    ANSI_COLOR(1;31),
    ANSI_COLOR(1;31),
    "",
    "",                  // cross-post
    ""                   // reset-karma
};
