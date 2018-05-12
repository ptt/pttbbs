/* $Id$ */
#ifndef PTTDB_COMMENT_H
#define PTTDB_COMMENT_H

#include <mongoc.h>

#include "ptterr.h"
#include "cmpttdb/pttdb_const.h"
#include "cmpttdb/pttdb_uuid.h"
#include "cmpttdb/util_db.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "pttstruct.h"

#define MAX_BUF_COMMENT 256

enum CommentType {
    COMMENT_TYPE_GOOD,
    COMMENT_TYPE_BAD,
    COMMENT_TYPE_ARROW,
    COMMENT_TYPE_SIZE,

    COMMENT_TYPE_CROSS_POST,                        // hack for cross-post
    COMMENT_TYPE_RESET_KARMA,                       // hack for reset-karma

    COMMENT_TYPE_N_TYPE,
    COMMENT_TYPE_MAX     = COMMENT_TYPE_SIZE - 1,
    COMMENT_TYPE_DEFAULT = COMMENT_TYPE_GOOD,
};

enum Karma {
    KARMA_GOOD = 1,
    KARMA_BAD = -1,
    KARMA_ARROW = 0,
};

enum ReadCommentsOpType {
    READ_COMMENTS_OP_TYPE_LT,
    READ_COMMENTS_OP_TYPE_LTE,
    READ_COMMENTS_OP_TYPE_GT,
    READ_COMMENTS_OP_TYPE_GTE,
};

enum ReadCommentsOrderType {
    READ_COMMENTS_ORDER_TYPE_ASC,
    READ_COMMENTS_ORDER_TYPE_DESC,
};

extern const enum Karma KARMA_BY_COMMENT_TYPE[COMMENT_TYPE_N_TYPE];
extern const char *COMMENT_TYPE_ATTR[COMMENT_TYPE_N_TYPE];
extern const char *COMMENT_TYPE_ATTR_UTF8[COMMENT_TYPE_N_TYPE];
extern const char *COMMENT_TYPE_ATTR2[COMMENT_TYPE_N_TYPE];

typedef struct Comment {
    unsigned int version;                            // version

    UUID the_id;                                     // comment-id
    UUID main_id;                                    // corresponding main-id
    UUID comment_reply_id;                           // comment-reply-id
    int n_comment_reply_line;                        // n-comment-reply-line
    int n_comment_reply_block;                       // n-comment-reply-block
    int n_comment_reply_total_line;                  // n-comment-reply-total-line

    enum LiveStatus status;                          // status
    char status_updater[IDLEN + 1];                  // last user updaing the status
    char status_update_ip[IPV4LEN + 1];              // last ip updating the status

    enum CommentType comment_type;                   // comment-type.
    enum Karma karma;                                // karma

    char poster[IDLEN + 1];                          // creater
    char ip[IPV4LEN + 1];                            // create-ip
    time64_t create_milli_timestamp;                 // create-time
    char updater[IDLEN + 1];                         // last updater
    char update_ip[IPV4LEN + 1];                     // last update-ip
    time64_t update_milli_timestamp;                 // last update-time

    int max_buf_len;                                 // max buf len
    int len;                                         // size
    char *buf;                                       // buf    
} Comment;

Err create_comment(UUID main_id, char *poster, char *ip, int len, char *content, enum CommentType comment_type, UUID comment_id, time64_t create_milli_timestamp);

Err read_comment(UUID comment_id, Comment *comment);

Err delete_comment(UUID comment_id, char *updater, char *ip);

Err get_comment_info_by_main(UUID main_id, int *n_total_comment, int *total_len);
Err get_comment_count_by_main(UUID main_id, int *count);

Err init_comment_buf(Comment *comment);
Err destroy_comment(Comment *comment);

Err associate_comment(Comment *comment, char *buf, int max_buf_len);
Err dissociate_comment(Comment *comment);
Err read_comments_by_main(UUID main_id, time64_t create_milli_timestamp, char *poster, enum ReadCommentsOpType op_type, int max_n_comments, enum MongoDBId mongo_db_id, Comment *comments, int *n_read_comment, int *len);

Err update_comment_reply_to_comment(UUID comment_id, UUID comment_reply_id, int n_comment_reply_line, int n_comment_reply_block, int n_comment_reply_total_line);
Err remove_comment_reply_from_comment(UUID comment_id, UUID comment_reply_id);
Err extract_b_comments_comment_id_to_bsons(bson_t **b_comments, int n_comment, char *result_key, bson_t **b_comment_ids);
Err extract_b_comments_comment_reply_id_to_bsons(bson_t **b_comments, int n_comment, char *result_key, bson_t **b_comment_reply_ids, int *n_comment_reply, int *n_comment_reply_block);

Err get_newest_comment(UUID main_id, UUID comment_id, time64_t *create_milli_timestamp, char *poster, int *n_comment);
Err read_comments_until_newest_to_bsons(UUID main_id, time64_t create_milli_timestamp, char *poster, bson_t *fields, int max_n_comment, bson_t **b_comments, int *n_comment);
Err ensure_b_comments_order(bson_t **b_comments, int n_comment, enum ReadCommentsOrderType order_type);
Err is_b_comments_order(bson_t **b_comments, int n_comment, enum ReadCommentsOrderType order_type, bool *is_good_b_comments_order);
Err sort_b_comments_order(bson_t **b_comments, int n_comment, enum ReadCommentsOrderType order_type);
Err sort_b_comments_by_comment_id(bson_t **b_comments, int n_comment);

Err read_comments_by_query_to_bsons(bson_t *query, bson_t *fields, int max_n_comment, bson_t **b_comments, int *n_comment);

Err dynamic_read_b_comment_comment_reply_by_ids_to_buf(bson_t **b_comments, int n_comment, char *buf, int max_buf_size, int *n_read_comment, int *n_comment_reply, int *len_buf);

Err dynamic_read_b_comment_comment_reply_by_ids_to_file(bson_t **b_comments, int n_comment, FILE *f, int *n_read_comment, int *n_read_comment_reply);

Err serialize_comment_bson(Comment *comment, bson_t **comment_bson);
Err deserialize_comment_bson(bson_t *comment_bson, Comment *comment);

#ifdef __cplusplus
}
#endif

#endif /* PTTDB_COMMENT_H */
