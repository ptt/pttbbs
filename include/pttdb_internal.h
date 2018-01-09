/* $ID$ */
#ifndef PTTDB_INTERNAL_H
#define PTTDB_INTERNAL_H

#include <mongoc.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/time.h>
#include <resolv.h>

/**********
 * UUID
 **********/
Err _serialize_uuid_bson(UUID uuid, const char *key, bson_t *uuid_bson);
Err _serialize_content_uuid_bson(UUID uuid, const char *key, int block_id, bson_t *uuid_bson);

/**********
 * Mongo
 **********/
Err db_set_if_not_exists(int collection, bson_t *key);
Err db_update_one(int collection, bson_t *key, bson_t *val, bool is_upsert);

Err _bson_exists(bson_t *b, char *name);
Err _bson_get_value_int32(bson_t *b, char *name, int *value);

// XXX NEVER USE UNLESS IN TEST
Err _DB_FORCE_DROP_COLLECTION(int collection);

/**********
 * Main
 **********/

Err _split_main_contents(int fd_content, int len, UUID main_id, UUID content_id, int *n_line, int *n_block);
Err _split_main_contents_core(char *line, int bytes_in_line, UUID main_id, UUID content_id, MainContent *main_content_block, int *n_line, int *n_block);
Err _split_main_contents_init_main_content(MainContent *main_content_block, UUID main_id, UUID content_id, int block_id);

Err _split_main_contents_save_main_content_block(MainContent *main_content_block);

Err _serialize_main_bson(MainHeader *main_header, bson_t *main_bson);

Err _serialize_main_content_block_bson(MainContent *main_content_block, bson_t *main_content_block_bson);

/**********
 * Misc
 **********/

Err get_line_from_buf(char *p_buf, int offset_buf, int bytes, char *p_line, int offset_line, int *bytes_in_new_line);


#ifdef __cplusplus
}
#endif

#endif /* PTTDB_INTERNAL_H */