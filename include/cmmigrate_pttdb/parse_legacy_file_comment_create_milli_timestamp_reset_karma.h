/* $Id$ */
// XXX hack for strptime (require #define _XOPEN_SOURCE and conflict with <mongoc.h>)
#ifndef PARSE_LEGACY_FILE_COMMENT_CREATE_MILLI_TIMESTAMP_RESET_KARMA_H
#define PARSE_LEGACY_FILE_COMMENT_CREATE_MILLI_TIMESTAMP_RESET_KARMA_H

#include "ptterr.h"
#include "ptt_const.h"
#include "cmutil_time/util_timestamp.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>

Err _parse_legacy_file_comment_create_milli_timestamp_reset_karma(char *line, int bytes_in_line, time64_t current_create_milli_timestamp, time64_t *create_milli_timestamp);

#ifdef __cplusplus
}
#endif

#endif /* PARSE_LEGACY_FILE_COMMENT_CREATE_MILLI_TIMESTAMP_RESET_KARMA_H */
