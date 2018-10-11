/* $Id$ */
#ifndef PTTDB_COMMENT_REPLY_PRIVATE_H
#define PTTDB_COMMENT_REPLY_PRIVATE_H

#include <mongoc.h>
#include "ptterr.h"
#include "cmpttdb/pttdb_const.h"
#include "cmpttdb/pttdb_util.h"
#include "cmpttdb/pttdb_comment_reply.h"
#include "cmpttdb/pttdb_comment.h"
#include "cmpttdb/pttdb_file_info.h"
#include "cmpttdb/util_db.h"

#ifdef __cplusplus
extern "C" {
#endif

Err _create_comment_reply_core(UUID main_id, UUID comment_id, char *poster, char *ip, int len, UUID comment_reply_id, time64_t create_milli_timestamp, int n_total_line, int n_block);

Err _get_comment_reply_info_by_main_deal_with_result(bson_t *result, int n_result, int *n_comment_reply, int *n_line, int *total_len);

Err _construct_comment_reply_blocks(CommentReply *comment_reply, char *content, int len, ContentBlock **content_blocks, int *n_content_block);


#ifdef __cplusplus
}
#endif

#endif /* PTTDB_COMMENT_REPLY_PRIVATE_H */
