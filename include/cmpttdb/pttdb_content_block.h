/* $Id$ */
#ifndef PTTDB_CONTENT_BLOCK_H
#define PTTDB_CONTENT_BLOCK_H

#include "ptterr.h"
#include "cmpttdb/pttdb_const.h"
#include "cmpttdb/pttdb_uuid.h"
#include "cmpttdb/pttdb_util.h"
#include "cmpttdb/util_db.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_BUF_LINES 256

typedef struct ContentBlock {
    UUID the_id;                                     // content-id
    UUID ref_id;                                     // corresponding ref-id

    int block_id;                                    // corresponding block-id

    int len_block;                                   // size of this block.
    int n_line;                                      // n-line of this block.

    int max_buf_len;                                 // max buf len (not in db)
    char *buf_block;                                 // buf
} ContentBlock;

Err split_contents(char *buf, int bytes, UUID ref_id, UUID content_id, enum MongoDBId mongo_db_id, int *n_line, int *n_block);
Err split_contents_from_fd(int fd_content, int len, UUID ref_id, UUID content_id, enum MongoDBId mongo_db_id, int *n_line, int *n_block);

Err delete_content(UUID content_id, enum MongoDBId mongo_db_id);

Err reset_content_block(ContentBlock *content_block, UUID ref_id, UUID content_id, int block_id);
Err reset_content_block_buf_block(ContentBlock *content_block);

Err init_content_block_with_buf_block(ContentBlock *content_block, UUID ref_id, UUID content_id, int block_id);
Err init_content_block_buf_block(ContentBlock *content_block);
Err destroy_content_block(ContentBlock *content_block);

Err associate_content_block(ContentBlock *content_block, char *buf_block, int max_buf_len);
Err dissociate_content_block(ContentBlock *content_block);

Err save_content_block(ContentBlock *content_block, enum MongoDBId mongo_db_id);
Err read_content_block(UUID content_id, int block_id, enum MongoDBId mongo_db_id, ContentBlock *content_block);
Err read_content_blocks(UUID content_id, int max_n_block, int block_id, enum MongoDBId mongo_db_id, ContentBlock *content_blocks, int *n_block, int *len);
Err read_content_blocks_by_ref(UUID ref_id, int max_n_block, int block_id, enum MongoDBId mongo_db_id, ContentBlock *content_blocks, int *n_block, int *len);
Err dynamic_read_content_blocks(UUID content_id, int max_n_block, int block_id, enum MongoDBId mongo_db_id, char *buf, int max_buf_size, ContentBlock *content_blocks, int *n_block, int *len);
Err dynamic_read_content_blocks_by_ref(UUID ref_id, int max_n_block, int block_id, enum MongoDBId mongo_db_id, char *buf, int max_buf_size, ContentBlock *content_blocks, int *n_block, int *len);

Err read_content_blocks_to_bsons(UUID content_id, bson_t *fields, int max_n_content_block, enum MongoDBId mongo_db_id, bson_t **b_content_blocks, int *n_content_block);

Err serialize_content_block_bson(ContentBlock *content_block, bson_t **content_block_bson);
Err deserialize_content_block_bson(bson_t *content_block_bson, ContentBlock *content_block);

Err sort_b_content_blocks_by_block_id(bson_t **b_content_blocks, int n_block);
Err ensure_b_content_blocks_block_ids(bson_t **b_content_blocks, int start_block_id, int n_block);

#ifdef __cplusplus
}
#endif

#endif /* PTTDB_CONTENT_BLOCK_H */
