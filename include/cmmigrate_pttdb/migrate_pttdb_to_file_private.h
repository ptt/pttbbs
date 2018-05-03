/* $Id$ */
#ifndef MIGRATE_PTTDB_TO_FILE_PRIVATE_H
#define MIGRATE_PTTDB_TO_FILE_PRIVATE_H

#include "ptterr.h"
#include "cmpttdb.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

#define COMMENT_BUF_SIZE 80
#define N_MIGRATE_MAIN_CONTENT_TO_FILE_BLOCK 5
#define N_MIGRATE_COMMENT_COMMENT_REPLY_TO_FILE_BLOCK 256
#define MAX_MIGRATE_COMMENT_COMMENT_REPLY_BUF_SIZE 8192 * 5

Err _migrate_main_content_to_file(MainHeader *main_header, FILE *fp);
Err _migrate_main_content_to_file_core(UUID content_id, FILE *fp, int start_block_id, int next_block_id);
Err _migrate_comment_comment_reply_by_main_to_file(UUID main_id, FILE *fp);
Err _migrate_comment_comment_reply_by_main_to_file_core(bson_t **b_comments, int n_comment, FILE *fp);

#ifdef __cplusplus
}
#endif

#endif /* MIGRATE_PTTDB_TO_FILE_PRIVATE_H */
