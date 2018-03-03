#include "bbs.h"
#include "util_db.h"
#include "util_db_internal.h"

const char *DEFAULT_MONGO_DB[] = {
    "post",
    "test",
};

/**********
 * Globally Available
 **********/
mongoc_uri_t *MONGO_URI;
mongoc_client_pool_t *MONGO_CLIENT_POOL;

/**********
 * In-thread Available
 **********/
mongoc_client_t *MONGO_CLIENT;
mongoc_collection_t **MONGO_COLLECTIONS;

/**********
 * Mongo
 **********/

/**
 * @brief Init mongo global variables
 * @details mongo-init, mongo-uri, and mongo-client-pool
 */
Err
init_mongo_global()
{
    mongoc_init();

    MONGO_URI = mongoc_uri_new(MONGO_CLIENT_URL);
    if (!MONGO_URI) return S_ERR;

    MONGO_CLIENT_POOL = mongoc_client_pool_new(MONGO_URI);
    mongoc_client_pool_set_error_api(MONGO_CLIENT_POOL, MONGOC_ERROR_API_VERSION_2);
    return S_OK;
}

/**
 * @brief Free mongo global variables
 * @details mongo-client-pool, mongo-uri, and mongo-cleanup.
 */
Err
free_mongo_global()
{
    mongoc_client_pool_destroy(MONGO_CLIENT_POOL);
    mongoc_uri_destroy(MONGO_URI);
    mongoc_cleanup();
    return S_OK;
}

/**
 * @brief Init collections (per thread)
 * @details client, collections.
 */
Err
init_mongo_collections(const char *db_name[])
{
    if (db_name == NULL) {
        db_name = DEFAULT_MONGO_DB;
    }

    MONGO_CLIENT = mongoc_client_pool_try_pop(MONGO_CLIENT_POOL);
    if (MONGO_CLIENT == NULL) {
        return S_ERR;
    }

    MONGO_COLLECTIONS = malloc(sizeof(mongoc_collection_t *) * N_MONGO_COLLECTIONS);
    MONGO_COLLECTIONS[MONGO_MAIN] = mongoc_client_get_collection(MONGO_CLIENT, db_name[MONGO_POST_DBNAME], MONGO_MAIN_NAME);
    MONGO_COLLECTIONS[MONGO_MAIN_CONTENT] = mongoc_client_get_collection(MONGO_CLIENT, db_name[MONGO_POST_DBNAME], MONGO_MAIN_CONTENT_NAME);

    MONGO_COLLECTIONS[MONGO_TEST] = mongoc_client_get_collection(MONGO_CLIENT, db_name[MONGO_TEST_DBNAME], MONGO_TEST_NAME);

    return S_OK;
}

/**
 * @brief Free collections (per thread)
 *        XXX Need to check what happened if we use fork.
 * @details collections, client.
 */
Err
free_mongo_collections()
{
    for (int i = 0; i < N_MONGO_COLLECTIONS; i++) {
        mongoc_collection_destroy(MONGO_COLLECTIONS[i]);
    }
    free(MONGO_COLLECTIONS);

    mongoc_client_pool_push(MONGO_CLIENT_POOL, MONGO_CLIENT);

    return S_OK;
}

/**
 * @brief set if not exists
 * @details [long description]
 *
 * @param collection [description]
 * @param key [description]
 */
Err
db_set_if_not_exists(int collection, bson_t *key)
{
    bool status;

    bson_t *set_val;
    bson_t *opts;
    bson_t reply;

    bson_error_t error;
    Err error_code;
    bool is_upsert = true;
    int n_upserted;

    // set_val
    set_val = bson_new();
    status = bson_append_document(set_val, "$setOnInsert", -1, key);
    if (!status) {
        bson_destroy(set_val);
        return S_ERR;
    }

    // opts
    opts = bson_new();

    status = bson_append_bool(opts, "upsert", -1, is_upsert);
    if (!status) {
        bson_destroy(set_val);
        bson_destroy(opts);
        return S_ERR;
    }

    // reply
    status = mongoc_collection_update_one(MONGO_COLLECTIONS[collection], key, set_val, opts, &reply, &error);
    if (!status) {
        bson_destroy(set_val);
        bson_destroy(opts);
        bson_destroy(&reply);
        return S_ERR;
    }

    error_code = bson_exists(&reply, "upsertedId");
    if (error_code) {
        bson_destroy(set_val);
        bson_destroy(opts);
        bson_destroy(&reply);
        return S_ERR_ALREADY_EXISTS;
    }

    bson_destroy(set_val);
    bson_destroy(opts);
    bson_destroy(&reply);

    return S_OK;
}

/**
 * @brief update one key / val
 * @details [long description]
 *
 * @param collection [description]
 * @param key [description]
 * @param val [description]
 * @param is_upsert [description]
 */
Err
db_update_one(int collection, bson_t *key, bson_t *val, bool is_upsert)
{
    bool status;

    bson_t *set_val;
    bson_t *opts;

    bson_t reply;
    bson_error_t error;

    // set_val
    set_val = bson_new();
    status = bson_append_document(set_val, "$set", -1, val);
    if (!status) {
        bson_destroy(set_val);
        return S_ERR;
    }

    // opts
    opts = bson_new();
    status = bson_append_bool(opts, "upsert", -1, &is_upsert);
    if (!status) {
        bson_destroy(set_val);
        bson_destroy(opts);
        return S_ERR;
    }

    // reply
    status = mongoc_collection_update_one(MONGO_COLLECTIONS[collection], key, set_val, opts, &reply, &error);
    if (!status) {
        bson_destroy(set_val);
        bson_destroy(opts);
        bson_destroy(&reply);
        return S_ERR;
    }

    bson_destroy(set_val);
    bson_destroy(opts);
    bson_destroy(&reply);

    return S_OK;
}

/**
 * @brief find one result in db
 * @details [long description]
 *
 * @param collection [description]
 * @param key [description]
 * @param fields [description]
 * @param result result (MUST-NOT initialized and need to bson_destroy)
 */
Err
db_find_one(int collection, bson_t *key, bson_t *fields, bson_t *result)
{
    mongoc_cursor_t *cursor = mongoc_collection_find(MONGO_COLLECTIONS[collection], MONGOC_QUERY_NONE, 0, 1, 0, key, fields, NULL);

    bson_error_t error;
    bson_t *p_result;
    int len = 0;
    while (mongoc_cursor_next(cursor, &p_result)) {
        bson_copy_to(p_result, result);
        len++;
    }

    if (mongoc_cursor_error(cursor, &error)) {
        mongoc_cursor_destroy(cursor);
        return S_ERR;
    }

    if (len == 0) {
        mongoc_cursor_destroy(cursor);
        return S_ERR_NOT_EXISTS;
    }

    if (len > 1) {
        mongoc_cursor_destroy(cursor);
        return S_ERR_FOUND_MULTI;
    }

    mongoc_cursor_destroy(cursor);
    return S_OK;
}

/**
 * @brief [brief description]
 * @details [long description]
 *
 * @param collection [description]
 * @param key [description]
 * @param fields [description]
 * @param n_fields [description]
 * @param result [description]
 */
Err
db_find_one_with_fields(int collection, bson_t *key, char **fields, int n_fields, bson_t *result)
{
    Err error_code;
    bson_t b_fields;
    bool bson_status;
    bson_init(&b_fields);

    for (int i = 0; i < n_fields; i++) {
        bson_status = bson_append_bool(&b_fields, fields[i], -1, true);
        if (!bson_status) {
            bson_destroy(&b_fields);
            return S_ERR;
        }
    }
    bson_status = bson_append_bool(&b_fields, "_id", -1, false);
    if (!bson_status) {
        bson_destroy(&b_fields);
        return S_ERR;
    }

    error_code = db_find_one(collection, key, &b_fields, result);
    if (error_code) {
        bson_destroy(&b_fields);
        return error_code;
    }

    bson_destroy(&b_fields);
    return S_OK;
}


Err
_DB_FORCE_DROP_COLLECTION(int collection)
{
    bool status;
    bson_error_t error;
    status = mongoc_collection_drop(MONGO_COLLECTIONS[collection], &error);
    if (!status) {
        return S_ERR;
    }

    return S_OK;
}

/**
 * @brief exists the name in the bson-struct
 * @details [long description]
 *
 * @param b the bson-struct
 * @param name name
 */
Err
bson_exists(bson_t *b, char *name)
{
    bool status;
    bson_iter_t iter;

    status = bson_iter_init_find(&iter, b, name);
    if (!status) {
        return S_ERR;
    }

    return S_OK;
}

/**
 * @brief get int32 value in bson
 * @details [long description]
 *
 * @param b [description]
 * @param name [description]
 * @param value [description]
 */
Err
bson_get_value_int32(bson_t *b, char *name, int *value)
{
    bool status;
    bson_iter_t iter;

    status = bson_iter_init_find(&iter, b, name);
    if (!status) {
        return S_ERR;
    }

    status = BSON_ITER_HOLDS_INT32(&iter);
    if (!status) {
        return S_ERR;
    }

    *value = bson_iter_int32(&iter);

    return S_OK;
}

/**
 * @brief get int64 value in bson
 * @details [long description]
 *
 * @param b [description]
 * @param name [description]
 * @param value [description]
 */
Err
bson_get_value_int64(bson_t *b, char *name, long int *value)
{
    bool status;
    bson_iter_t iter;

    status = bson_iter_init_find(&iter, b, name);
    if (!status) {
        return S_ERR;
    }

    status = BSON_ITER_HOLDS_INT64(&iter);
    if (!status) {
        return S_ERR;
    }

    *value = bson_iter_int64(&iter);

    return S_OK;
}

/**
 * @brief find binary in the bson-struct
 * @details [long description]
 *
 * @param b [description]
 * @param name [description]
 * @param value [MUST-NOT initialized and need to free!]
 * @param len received length
 */
Err
bson_get_value_bin_with_init(bson_t *b, char *name, char **value, int *p_len)
{
    bool status;
    bson_subtype_t subtype;
    bson_iter_t iter;

    status = bson_iter_init_find(&iter, b, name);
    if (!status) {
        return S_ERR;
    }

    status = BSON_ITER_HOLDS_BINARY(&iter);
    if (!status) {
        return S_ERR;
    }

    char *p_value;
    int tmp_len;
    bson_iter_binary(&iter, &subtype, &tmp_len, &p_value);
    *value = malloc(tmp_len + 1);
    memcpy(*value, p_value, tmp_len);
    (*value)[tmp_len] = 0;
    *p_len = tmp_len;

    return S_OK;
}

/**
 * @brief get binary value without init
 * @details [long description]
 *
 * @param b [description]
 * @param name [description]
 * @param max_len max-length of the buffer
 * @param value [MUST INITIALIZED WITH max_len!]
 * @param len real received length
 */
Err
bson_get_value_bin(bson_t *b, char *name, int max_len, char *value, int *p_len)
{
    bool status;
    bson_subtype_t subtype;
    bson_iter_t iter;

    status = bson_iter_init_find(&iter, b, name);
    if (!status) {
        return S_ERR;
    }

    status = BSON_ITER_HOLDS_BINARY(&iter);
    if (!status) {
        return S_ERR;
    }

    char *p_value;
    Err error = S_OK;
    bson_iter_binary(&iter, &subtype, p_len, &p_value);
    int len = *p_len;
    if (len > max_len) {
        len = max_len;
        error = S_ERR_BUFFER_LEN;
    }
    memcpy(value, p_value, len);
    if (len < max_len) {
        value[len + 1] = 0;
    }

    return error;
}
