/* $Id$ */
#ifndef PTTDB_COMMENT_PRIVATE_H
#define PTTDB_COMMENT_PRIVATE_H

#include "ptterr.h"
#include "cmpttdb/pttdb_const.h"
#include "cmpttdb/pttdb_uuid.h"
#include "cmpttdb/pttdb_dict_bson_by_uu.h"
#include "cmpttdb/pttdb_comment_reply.h"
#include "cmpttdb/pttdb_file.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "ansi.h"

Err _get_comment_info_by_main_deal_with_result(bson_t *result, int n_result, int *n_total_comment, int *total_len);
Err _read_comments_get_b_comments(bson_t **b_comments, UUID main_id, time64_t create_milli_timestamp, char *poster, enum ReadCommentsOpType op_type, int max_n_comment, enum MongoDBId mongo_db_id, int *n_comment);
Err _read_comments_get_b_comments_same_create_milli_timestamp(bson_t **b_comments, UUID main_id, time64_t create_milli_timestamp, char *poster, enum ReadCommentsOpType op_type, int max_n_comment, enum MongoDBId mongo_db_id, int *n_comment);
Err _read_comments_get_b_comments_diff_create_milli_timestamp(bson_t **b_comments, UUID main_id, time64_t create_milli_timestamp, enum ReadCommentsOpType op_type, int max_n_comment, enum MongoDBId mongo_db_id, int *n_comment);
Err _read_comments_get_b_comments_core(bson_t **b_comments, bson_t *key, bson_t *sort, enum ReadCommentsOpType op_type, int max_n_comment, enum MongoDBId mongo_db_id, int *n_comment);
int _cmp_b_comments_ascending(const void *a, const void *b);
int _cmp_b_comments_descending(const void *a, const void *b);
Err _reverse_b_comments(bson_t **b_comments, int n_comment);

// for file-info
int _cmp_b_comments_by_comment_id(const void *a, const void *b);

// 
Err _dynamic_read_b_comment_comment_reply_by_ids_to_buf_core(bson_t **b_comments, int n_comment, DictBsonByUU *dict_comment_content, DictBsonByUU *dict_comment_reply, char *buf, int max_buf_size, int *n_read_comment, int *n_read_comment_reply, int *len_buf);

Err _dynamic_read_b_comment_comment_reply_by_ids_to_file_core(bson_t **b_comments, int n_comment, DictBsonByUU *dict_comment_content, DictBsonByUU *dict_comment_reply, FILE *f, int *n_read_comment, int *n_read_comment_reply);

Err _dynamic_read_b_comment_comment_reply_by_ids_to_file_comment_reply(bson_t *b_comment_reply, FILE *f);

Err _dynamic_read_b_comment_comment_reply_by_ids_to_file_comment_reply_core(UUID comment_reply_id, FILE *f, int idx, char *buf);

#ifdef __cplusplus
}
#endif

#endif /* PTTDB_COMMENT_PRIVATE_H */
