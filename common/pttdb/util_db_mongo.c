#include "cmpttdb/util_db.h"

const char *DEFAULT_MONGO_DB[] = {
    "post",      //MONGO_POST_DBNAME
    "test",      //MONGO_TEST_DBNAME
};

const char *MONGO_NAME[] = {
    "main",
    "main_content",
    "comment",
    "comment_reply",
    "comment_reply_block",

    "test"
};

const int MONGO_DBNAME[] = {
    MONGO_POST_DBNAME,
    MONGO_POST_DBNAME,
    MONGO_POST_DBNAME,
    MONGO_POST_DBNAME,
    MONGO_POST_DBNAME,

    MONGO_TEST_DBNAME
};

/**********
 * Globally Available
 **********/
mongoc_uri_t *MONGO_URI = NULL;
mongoc_client_pool_t *MONGO_CLIENT_POOL = NULL;

/**********
 * In-thread Available
 **********/
mongoc_client_t *MONGO_CLIENT = NULL;
mongoc_collection_t **MONGO_COLLECTIONS = NULL;

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
    if (MONGO_CLIENT_POOL) {
        mongoc_client_pool_destroy(MONGO_CLIENT_POOL);
        MONGO_CLIENT_POOL = NULL;
    }

    if (MONGO_URI) {
        mongoc_uri_destroy(MONGO_URI);
        MONGO_URI = NULL;
    }

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
    for (int i = 0; i < N_MONGO_COLLECTIONS; i++) {
        MONGO_COLLECTIONS[i] = mongoc_client_get_collection(MONGO_CLIENT, db_name[MONGO_DBNAME[i]], MONGO_NAME[i]);
    }

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
    // mongo-collections
    if (!MONGO_COLLECTIONS) return S_OK;

    for (int i = 0; i < N_MONGO_COLLECTIONS; i++) {
        mongoc_collection_destroy(MONGO_COLLECTIONS[i]);
    }
    free(MONGO_COLLECTIONS);
    MONGO_COLLECTIONS = NULL;

    // mongo-client
    if (!MONGO_CLIENT) return S_OK;

    mongoc_client_pool_push(MONGO_CLIENT_POOL, MONGO_CLIENT);
    MONGO_CLIENT = NULL;

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
    Err error_code = S_OK;

    bool status;

    bson_t *set_val = BCON_NEW("$setOnInsert", BCON_DOCUMENT(key));
    bson_t *opts = BCON_NEW("upsert", BCON_BOOL(true));

    bool is_upserted_id_exist;


    // reply
    if (!error_code) {
        bson_t reply;
        bson_error_t error;

        status = mongoc_collection_update_one(MONGO_COLLECTIONS[collection], key, set_val, opts, &reply, &error);
        if (!status) error_code = S_ERR;

        bson_exists(&reply, "upsertedId", &is_upserted_id_exist);

        bson_destroy(&reply);
    }

    if (!error_code) {
        if (!is_upserted_id_exist) error_code = S_ERR_ALREADY_EXISTS;
    }

    bson_destroy(set_val);
    bson_destroy(opts);

    return error_code;
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
    Err error_code = S_OK;
    bool status;

    bson_t *set_val = BCON_NEW("$set", BCON_DOCUMENT(val));
    bson_t *opts = BCON_NEW("upsert", BCON_BOOL(is_upsert));

    bson_t reply;
    bson_error_t error;

    status = mongoc_collection_update_one(MONGO_COLLECTIONS[collection], key, set_val, opts, &reply, &error);
    if (!status) error_code = S_ERR;
    bson_destroy(&reply);

    bson_destroy(set_val);
    bson_destroy(opts);

    return error_code;
}

/**
 * @brief find one result in db
 * @details
 *          Example:
 *                bson_t *key = BCON_NEW("a", BCON_BINARY("test", 4));
 *                bson_t *result = NULL;
 *                db_find_one(collection, key, NULL, &result);
 *
 *                if(result) {
 *                    bson_destroy(result);
 *                }
 *
 * @param collection [description]
 * @param key [description]
 * @param fields [description]
 * @param result result (MUST-NOT initialized and need to bson_destroy)
 */
Err
db_find_one(int collection, bson_t *key, bson_t *fields, bson_t **result)
{
    int n_result;
    return db_find(collection, key, fields, NULL, 1, &n_result, result);
}


Err
db_find(int collection, bson_t *key, bson_t *fields, bson_t *sort, int max_n_result, int *n_result, bson_t **results)
{
    Err error_code = S_OK;

    bool status;

    bson_t *opts = BCON_NEW("limit", BCON_INT64(max_n_result));
    if (fields) {
        status = bson_append_document(opts, "projection", -1, fields);
        if (!status) error_code = S_ERR;
    }
    if (sort) {
        status = bson_append_document(opts, "sort", -1, sort);
        if (!status) error_code = S_ERR;
    }

    int len = 0;
    if (!error_code) {
        mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(MONGO_COLLECTIONS[collection], key, opts, NULL);
        bson_error_t error;

        const bson_t *p_result;
        while (mongoc_cursor_next(cursor, &p_result)) {
            *results = bson_copy(p_result);
            results++;
            len++;
        }

        if (mongoc_cursor_error(cursor, &error)) {
            fprintf(stderr, "[ERROR] util_db_mongo.db_find: e: %s\n", error.message);
            error_code = S_ERR;
        }

        mongoc_cursor_destroy(cursor);
    }
    *n_result = len;

    if (!error_code) {
        if (*n_result == 0) error_code = S_ERR_NOT_EXISTS;
    }

    bson_destroy(opts);

    return error_code;
}

Err
db_remove(int collection, bson_t *key)
{
    Err error_code = S_OK;
    bson_error_t error;
    bool status;

    status = mongoc_collection_delete_many(MONGO_COLLECTIONS[collection], key, NULL, NULL, &error);
    if (!status) error_code = S_ERR;

    return error_code;
}

Err
db_aggregate(int collection, bson_t *pipeline, int max_n_results, bson_t **results, int *n_results)
{
    Err error_code = S_OK;
    const bson_t *doc = NULL;

    mongoc_cursor_t *cursor = mongoc_collection_aggregate(MONGO_COLLECTIONS[collection], MONGOC_QUERY_NONE, pipeline, NULL, NULL);

    int tmp_n_results = 0;
    bson_t **p_results = results;
    while (tmp_n_results < max_n_results) {
        if (!mongoc_cursor_next(cursor, &doc)) break;

        *p_results = bson_copy(doc);
        p_results++;
        tmp_n_results++;
    }

    *n_results = tmp_n_results;

    bson_error_t error;
    if (mongoc_cursor_error (cursor, &error)) {
        error_code = S_ERR;
    }

    mongoc_cursor_destroy(cursor);

    return error_code;
}

Err
db_count(int collection, bson_t *key, int *count)
{
    Err error_code = S_OK;
    bson_error_t error;

    int64_t tmp_count = mongoc_collection_count_with_opts(MONGO_COLLECTIONS[collection], MONGOC_QUERY_NONE, key, 0, 0, NULL, NULL, &error);

    if (tmp_count == -1) {
        error_code = S_ERR;
        tmp_count = 0;
    }

    *count = (int)tmp_count;

    return error_code;
}

/**
 * @brief exists the name in the bson-struct
 * @details [long description]
 *
 * @param b the bson-struct
 * @param name name
 */
Err
bson_exists(bson_t *b, char *name, bool *is_exist)
{
    bool status;
    bson_iter_t iter;

    status = bson_iter_init_find(&iter, b, name);
    if (!status) {
        *is_exist = false;
    }
    else  {
        *is_exist = true;
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
    if (!status) return S_ERR;

    status = BSON_ITER_HOLDS_INT32(&iter);
    if (!status) return S_ERR;

    *value = bson_iter_int32(&iter);

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
bson_get_descendant_value_int32(bson_t *b, char *name, int *value)
{
    bool status;
    bson_iter_t iter;
    bson_iter_t iter_value;

    status = bson_iter_init(&iter, b);
    if (!status) return S_ERR;

    status = bson_iter_find_descendant(&iter, name, &iter_value);
    if (!status) return S_ERR;

    status = BSON_ITER_HOLDS_INT32(&iter_value);
    if (!status) return S_ERR;

    *value = bson_iter_int32(&iter_value);

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
    if (!status) return S_ERR;

    status = BSON_ITER_HOLDS_INT64(&iter);
    if (!status) return S_ERR;

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
bson_get_value_bin_not_initialized(bson_t *b, char *name, char **value, int *p_len)
{
    bool status;
    bson_subtype_t subtype;
    bson_iter_t iter;

    status = bson_iter_init_find(&iter, b, name);
    if (!status) return S_ERR;

    status = BSON_ITER_HOLDS_BINARY(&iter);
    if (!status) return S_ERR;

    const char *p_value;
    int tmp_len;
    bson_iter_binary(&iter, &subtype, (unsigned int *)&tmp_len, (const unsigned char **)&p_value);

    *value = malloc(tmp_len + 1);
    if (!value) return S_ERR;

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
    Err error = S_OK;

    bool status;
    bson_subtype_t subtype;
    bson_iter_t iter;

    status = bson_iter_init_find(&iter, b, name);
    if (!status) return S_ERR;

    status = BSON_ITER_HOLDS_BINARY(&iter);
    if (!status) return S_ERR;

    char *p_value;
    bson_iter_binary(&iter, &subtype, (unsigned int *)p_len, (const unsigned char **)&p_value);

    int tmp_len = *p_len;
    if (tmp_len > max_len) {
        tmp_len = max_len;
        error = S_ERR_BUFFER_LEN;
    }

    memcpy(value, p_value, tmp_len);
    if (tmp_len < max_len) {
        value[tmp_len + 1] = 0;
    }

    return error;
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
bson_get_descendant_value_bin(bson_t *b, char *name, int max_len, char *value, int *p_len)
{
    Err error = S_OK;

    bool status;
    bson_subtype_t subtype;
    bson_iter_t iter;
    bson_iter_t iter_value;

    status = bson_iter_init(&iter, b);
    if (!status) return S_ERR;

    status = bson_iter_find_descendant(&iter, name, &iter_value);

    status = BSON_ITER_HOLDS_BINARY(&iter_value);
    if (!status) return S_ERR;

    char *p_value;
    bson_iter_binary(&iter_value, &subtype, (unsigned int *)p_len, (const unsigned char **)&p_value);

    int tmp_len = *p_len;
    if (tmp_len > max_len) {
        tmp_len = max_len;
        error = S_ERR_BUFFER_LEN;
    }

    memcpy(value, p_value, tmp_len);
    if (tmp_len < max_len) {
        value[tmp_len + 1] = 0;
    }

    return error;
}

Err
bson_safe_destroy(bson_t **b)
{
    if (*b == NULL) return S_OK;

    bson_destroy(*b);
    *b = NULL;

    return S_OK;
}

Err
safe_free_b_list(bson_t ***b, int n)
{
    bson_t **p_b = *b;
    for (int i = 0; i < n; i++, p_b++) {
        bson_safe_destroy(p_b);
    }
    safe_free((void **)b);

    return S_OK;
}
