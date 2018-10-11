/* $Id$ */
#ifndef MIGRATE_FILE_TO_PTTDB_H
#define MIGRATE_FILE_TO_PTTDB_H

#include "ptterr.h"
#include "cmpttdb.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "pttstruct.h"

enum LegacyFileStatus {
    LEGACY_FILE_STATUS_MAIN_CONTENT,
    LEGACY_FILE_STATUS_COMMENT,
    LEGACY_FILE_STATUS_COMMENT_REPLY,
    LEGACY_FILE_STATUS_END,
    LEGACY_FILE_STATUS_ERROR
};

typedef struct LegacyCommentInfo {
    int comment_offset;

    time64_t comment_create_milli_timestamp;
    char comment_poster[IDLEN + 1];
    char comment_ip[IPV4LEN + 1];
    int comment_len;
    enum CommentType comment_type;

    time64_t comment_reply_create_milli_timestamp;
    char comment_reply_poster[IDLEN + 1];
    char comment_reply_ip[IPV4LEN + 1];
    int comment_reply_offset;
    int comment_reply_len;
} LegacyCommentInfo;

typedef struct LegacyFileInfo {
    aidu_t aid;
    char web_link[MAX_WEB_LINK_LEN + 1];

    char poster[IDLEN + 1];
    char board[IDLEN + 1];
    char title[TTLEN + 1];
    time64_t create_milli_timestamp;
    
    char origin[MAX_ORIGIN_LEN + 1];
    char ip[IPV4LEN + 1];
    int main_content_len;

    int n_comment_comment_reply;
    LegacyCommentInfo *comment_info;
} LegacyFileInfo;

Err migrate_get_file_info(const fileheader_t *fhdr, const boardheader_t *bp, char *poster, char *board, char *title, char *origin, aidu_t *aid, char *web_link, time64_t *create_milli_timestamp);

Err migrate_file_to_pttdb(const char *fpath, char *poster, char *board, char *title, char *origin, aidu_t aid, char *web_link, time64_t create_milli_timestamp, UUID main_id);

Err init_legacy_file_info_comment_comment_reply(LegacyFileInfo *legacy_file_info, int n_comment_comment_reply);

Err safe_destroy_legacy_file_info(LegacyFileInfo *legacy_file_info);

Err parse_legacy_file(const char *fpath, LegacyFileInfo *legacy_file_info);

#ifdef __cplusplus
}
#endif

#endif /* MIGRATE_FILE_TO_PTTDB_H */
