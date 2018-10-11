/* $Id$ */
#ifndef PTTDB_COMMENT_REPLY_H
#define PTTDB_COMMENT_REPLY_H

#include <mongoc.h>
#include "ptterr.h"
#include "cmpttdb/pttdb_const.h"
#include "cmpttdb/pttdb_uuid.h"
#include "cmpttdb/pttdb_content_block.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "pttstruct.h"

typedef struct CommentReply {
    unsigned int version;                            // version

    UUID the_id;                                     // comment-reply-id

    UUID comment_id;                                 // corresponding comment-id
    UUID main_id;                                    // corresponding main-id

    enum LiveStatus status;                          // status
    char status_updater[IDLEN + 1];                  // last user updating status
    char status_update_ip[IPV4LEN + 1];              // last ip updating the status

    char poster[IDLEN + 1];                          // creater
    char ip[IPV4LEN + 1];                            // create-ip
    time64_t create_milli_timestamp;                 // create-time
    char updater[IDLEN + 1];                         // last updater
    char update_ip[IPV4LEN + 1];                     // last update-ip
    time64_t update_milli_timestamp;                 // last update-time

    int n_block;                                     // n-block for comment_reply_block
    int len_total;                                   // total-size.
    int n_total_line;                                // total-line.

    int max_buf_len;                                 // max_buf_len
    int len;                                         // size for buf.
    int n_line;                                      // n-line for buf.
    char *buf;
} CommentReply;

Err create_comment_reply_from_fd(UUID main_id, UUID comment_id, char *poster, char *ip, int len, int fd_content, UUID comment_reply_id, time64_t create_milli_timestamp);

Err create_comment_reply(UUID main_id, UUID comment_id, char *poster, char *ip, int len, char *content, UUID comment_reply_id, time64_t create_milli_timestamp);

Err create_comment_reply_from_content_block_infos(UUID main_id, UUID comment_id, char *poster, char *ip, UUID orig_comment_reply_id, int n_orig_comment_reply_block, ContentBlockInfo *content_blocks, UUID comment_reply_id, time64_t create_milli_timestamp);

Err read_comment_reply(UUID comment_reply_id, CommentReply *comment_reply);

Err delete_comment_reply(UUID comment_reply_id, char *updater, char *ip);

Err init_comment_reply_buf(CommentReply *comment_reply);
Err destroy_comment_reply(CommentReply *comment_reply);

Err get_comment_reply_info_by_main(UUID main_id, int *n_comment_reply, int *n_line, int *total_len);

Err associate_comment_reply(CommentReply *comment_reply, char *buf, int max_buf_len);
Err dissociate_comment_reply(CommentReply *comment_reply);

// for file_info
Err read_comment_replys_by_query_to_bsons(bson_t *query, bson_t *fields, int max_n_comment_reply, bson_t **b_comment_replys, int *n_comment_reply);

Err serialize_comment_reply_bson(CommentReply *comment_reply, bson_t **comment_reply_bson);
Err deserialize_comment_reply_bson(bson_t *comment_reply_bson, CommentReply *comment_reply);
Err deserialize_comment_reply_bson_with_buf(bson_t *comment_reply_bson, CommentReply *comment_reply);

#ifdef __cplusplus
}
#endif

#endif /* PTTDB_COMMENT_REPLY_H */
