/* $Id$ */
#ifndef UTIL_DB_H
#define UTIL_DB_H

#include <mongoc.h>

#include "ptterr.h"
#include "pttdb_const.h"
#include "pttdb_util.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <resolv.h>

#include "config.h"

// Mongo the id
#define MONGO_THE_ID "the_id"
#define MONGO_BLOCK_ID "block_id"

enum
{
    MONGO_POST_DBNAME,
    MONGO_TEST_DBNAME,
};

enum MongoDBId {
    MONGO_MAIN,
    MONGO_MAIN_CONTENT,
    MONGO_COMMENT,
    MONGO_COMMENT_REPLY,
    MONGO_COMMENT_REPLY_BLOCK,

    MONGO_TEST,

    N_MONGO_COLLECTIONS,
};

// initialization / free
Err init_mongo_global();
Err free_mongo_global();
Err init_mongo_collections(const char *db_name[]);
Err free_mongo_collections();

// db-ops
Err db_set_if_not_exists(int collection, bson_t *key);
Err db_update_one(int collection, bson_t *key, bson_t *val, bool is_upsert);
Err db_remove(int collection, bson_t *key);
Err db_count(int collection, bson_t *key, int *count);

// XXX require bson_safe_destroy on results
Err db_find_one(int collection, bson_t *key, bson_t *fields, bson_t **result);
Err db_find(int collection, bson_t *key, bson_t *fields, bson_t *sort, int max_n_result, int *n_result, bson_t **results);
Err db_aggregate(int collection, bson_t *pipeline, int max_n_result, bson_t **results, int *n_result);

// bson-ops
#define BCON_BINARY(bin, length) BCON_BIN(BSON_SUBTYPE_BINARY, bin, length)
#define bson_append_bin(b, key, key_length, bin, length) bson_append_binary(b, key, key_length, BSON_SUBTYPE_BINARY, bin, length)

Err bson_exists(bson_t *b, char *name, bool *is_exist);
Err bson_get_value_int32(bson_t *b, char *name, int *value);
Err bson_get_value_int64(bson_t *b, char *name, long int *value);
Err bson_get_value_bin_not_initialized(bson_t *b, char *name, char **value, int *p_len);
Err bson_get_value_bin(bson_t *b, char *name, int max_len, char *value, int *p_len);
Err bson_get_descendant_value_int32(bson_t *b, char *name, int *value);
Err bson_get_descendant_value_bin(bson_t *b, char *name, int max_len, char *value, int *p_len);

Err bson_safe_destroy(bson_t **b);
Err safe_free_b_list(bson_t ***b, int n);

Err bsons_to_bson_dict_by_uu(bson_t **b, int n_b, char *key, bson_t **b_result);

#ifdef __cplusplus
}
#endif

#endif /* UTIL_DB_H */
