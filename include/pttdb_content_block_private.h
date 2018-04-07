/* $Id$ */
#ifndef PTTDB_CONTENT_BLOCK_PRIVATE_H
#define PTTDB_CONTENT_BLOCK_PRIVATE_H

#include "ptterr.h"
#include "pttdb_const.h"
#include "pttdb_uuid.h"

#ifdef __cplusplus
extern "C" {
#endif

Err _split_contents_core(char *buf, int bytes, UUID ref_id, UUID content_id, enum MongoDBId mongo_db_id, int *n_line, int *n_block, char *line, int line_size, int *bytes_in_line, ContentBlock *content_block);

Err _split_contents_core_one_line(char *line, int bytes_in_line, UUID ref_id, UUID content_id, enum MongoDBId mongo_db_id, ContentBlock *content_block, int *n_line, int *n_block);

Err _split_contents_deal_with_last_line_block(int bytes_in_line, char *line, UUID ref_id, UUID content_id, enum MongoDBId mongo_db_id, ContentBlock *content_block, int *n_line, int *n_block);

Err _read_content_blocks_core(bson_t *key, int max_n_block, int block_id, enum MongoDBId mongo_db_id, ContentBlock *content_blocks, int *n_block, int *len);
Err _dynamic_read_content_blocks_core(bson_t *key, int max_n_block, int block_id, enum MongoDBId mongo_db_id, char *buf, int max_buf_size, ContentBlock *content_blocks, int *n_block, int *len);
Err _read_content_blocks_get_b_content_blocks(bson_t **b_content_blocks, bson_t *key, int max_n_block, int block_id, enum MongoDBId mongo_db_id, int *n_block);

Err _form_content_block_b_array_block_ids(int block_id, int max_n_block, bson_t **b);
int _cmp_b_content_blocks_by_block_id(const void *a, const void *b);

#ifdef __cplusplus
}
#endif

#endif /* PTTDB_CONTENT_BLOCK_PRIVATE_H */
