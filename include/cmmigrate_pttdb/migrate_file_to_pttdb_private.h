/* $Id$ */
#ifndef MIGRATE_FILE_TO_PTTDB_PRIVATE_H
#define MIGRATE_FILE_TO_PTTDB_PRIVATE_H

#include "ptterr.h"
#include "ptt_const.h"
#include "cmpttdb.h"
#include "cmmigrate_pttdb/parse_legacy_file_comment_create_milli_timestamp_reset_karma.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include "proto.h"
#include "ansi.h"

#define MIGRATE_COMMENT_BUF_SIZE 80
#define N_MIGRATE_MAIN_CONTENT_TO_FILE_BLOCK 5
#define N_MIGRATE_COMMENT_COMMENT_REPLY_TO_FILE_BLOCK 256
#define MAX_MIGRATE_COMMENT_COMMENT_REPLY_BUF_SIZE 8192 * 5

#define LEGACY_ORIGIN_IP "◆ From:"
#define LEN_LEGACY_ORIGIN_IP 8

Err _parse_create_milli_timestamp_from_filename(char *filename, time64_t *create_milli_timestamp);

Err _parse_create_milli_timestamp_from_web_link(char *web_link, time64_t *create_milli_timestamp);

Err _parse_legacy_file_main_info(const char *fpath, int file_size, LegacyFileInfo *legacy_file_info);

Err _parse_legacy_file_trim_last_empty_line(const char *fpath, int *file_size);

Err _parse_legacy_file_main_info_core(char *buf, int bytes, char *line, int line_size, int *bytes_in_line, LegacyFileInfo *legacy_file_info, enum LegacyFileStatus *status);

Err _parse_legacy_file_main_info_core_one_line(char *line, int bytes_in_line, LegacyFileInfo *legacy_file_info, enum LegacyFileStatus *status);

Err _parse_legacy_file_main_info_core_one_line_main_content(char *line, int bytes_in_line, LegacyFileInfo *legacy_file_info, enum LegacyFileStatus *status);

Err _parse_legacy_file_main_info_last_line(int bytes_in_line, char *line, LegacyFileInfo *legacy_file_info, enum LegacyFileStatus *status);

Err _parse_legacy_file_comment_comment_reply(const char *fpath, int file_size, LegacyFileInfo *legacy_file_info);

Err _parse_legacy_file_n_comment_comment_reply(const char *fpath, int main_content_len, int file_size, int *n_comment_comment_reply);

Err _parse_legacy_file_n_comment_comment_reply_core(char *buf, int bytes, char *line, int line_size, int *bytes_in_line, int *n_comment_comment_reply);

Err _parse_legacy_file_n_comment_comment_reply_core_one_line(char *line, int bytes_in_line, int *n_comment_comment_reply);

Err _parse_legacy_file_n_comment_comment_reply_last_line(int bytes_in_line, char *line, int *n_comment_comment_reply);

Err _parse_legacy_file_comment_comment_reply_core(const char *fpath, int file_size, LegacyFileInfo *legacy_file_info);

Err _parse_legacy_file_comment_comment_reply_core_core(char *buf, int bytes, char *line, int line_size, int *bytes_in_line, LegacyFileInfo *legacy_file_info, int *comment_idx, time64_t *current_create_milli_timestamp, enum LegacyFileStatus *status, int *file_offset);

Err _parse_legacy_file_comment_comment_reply_core_one_line(char *line, int bytes_in_line, LegacyFileInfo *legacy_file_info, int *comment_idx, time64_t *current_create_milli_timestamp, enum LegacyFileStatus *status, int *file_offset);

Err _parse_legacy_file_comment_comment_reply_core_one_line_comment(char *line, int bytes_in_line, LegacyFileInfo *legacy_file_info, int *comment_idx, time64_t *current_create_milli_timestamp, enum LegacyFileStatus *status, int file_offset);

// parse create milli-timestamp
Err _parse_legacy_file_comment_create_milli_timestamp(char *line, int bytes_in_line, time64_t current_create_milli_timestamp, time64_t *create_milli_timestamp);

Err _parse_legacy_file_comment_create_milli_timestamp_good_bad_arrow_cross_post(char *line, int bytes_in_line, time64_t current_create_milli_timestamp, time64_t *create_milli_timestamp);

Err _parse_legacy_file_comment_create_milli_timestamp_get_datetime_from_line(char *line, int bytes_in_line, int *mm, int *dd, int *HH, int *MM);

// parse poster
Err _parse_legacy_file_comment_poster(char *line, int bytes_in_line, char *poster);

Err _parse_legacy_file_comment_poster_cross_post(char *line, int bytes_in_line, char *poster);

Err _parse_legacy_file_comment_poster_reset_karma(char *line, int bytes_in_line, char *poster);

Err _parse_legacy_file_comment_poster_good_bad_arrow(char *line, int bytes_in_line, char *poster);

// parse comment-type
Err _parse_legacy_file_comment_type(char *line, int bytes_in_line, enum CommentType *comment_type);

Err _parse_legacy_file_comment_comment_reply_core_one_line_comment_reply(char *line, int bytes_in_line, LegacyFileInfo *legacy_file_info, int *comment_idx, enum LegacyFileStatus *status, int file_offset);

Err _parse_legacy_file_comment_comment_reply_core_last_line(int bytes_in_line, char *line, LegacyFileInfo *legacy_file_info, int *comment_idx, time64_t *current_create_milli_timestamp, enum LegacyFileStatus *status, int *file_offset);

Err _parse_legacy_file_is_line_edit(char *line, int bytes_in_line, bool *is_valid);

Err _parse_legacy_file_is_comment_line(char *line, int bytes_in_line, bool *is_valid);

Err _parse_legacy_file_is_comment_line_good_bad_arrow(char *line, int bytes_in_line, bool *is_valid, enum CommentType comment_type);

Err _parse_legacy_file_is_comment_line_cross_post(char *line, int bytes_in_line, bool *is_valid);

Err _parse_legacy_file_is_comment_line_reset_karma(char *line, int bytes_in_line, bool *is_valid);

Err _parse_legacy_file_calc_comment_offset(LegacyFileInfo *legacy_file_info);

Err _parse_legacy_file_get_file_size(const char *fpath, int *file_size);

#ifdef __cplusplus
}
#endif

#endif /* MIGRATE_FILE_TO_PTTDB_H */
