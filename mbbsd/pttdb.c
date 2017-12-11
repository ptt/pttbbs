#include "bbs.h"
#include "ptterr.h"
#include "pttdb.h"
#include "pttdb_internal.h"

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
init_mongo_global() {
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
free_mongo_global() {
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
init_mongo_collections() {
    MONGO_CLIENT = mongoc_client_pool_try_pop(MONGO_CLIENT_POOL);
    if (MONGO_CLIENT == NULL) {
        return S_ERR;
    }

    MONGO_COLLECTIONS = malloc(sizeof(mongoc_collection_t *) * N_MONGO_COLLECTIONS);
    MONGO_COLLECTIONS[MONGO_MAIN] = mongoc_client_get_collection(MONGO_CLIENT, MONGO_POST_DBNAME, MONGO_MAIN_NAME);
    MONGO_COLLECTIONS[MONGO_MAIN_CONTENT] = mongoc_client_get_collection(MONGO_CLIENT, MONGO_POST_DBNAME, MONGO_MAIN_CONTENT_NAME);

    MONGO_COLLECTIONS[MONGO_TEST] = mongoc_client_get_collection(MONGO_CLIENT, MONGO_TEST_DBNAME, MONGO_TEST_NAME);

    return S_OK;
}

/**
 * @brief Free collections (per thread)
 *        XXX Need to check what happened if we use fork.
 * @details collections, client.
 */
Err
free_mongo_collections() {
    for (int i = 0; i < N_MONGO_COLLECTIONS; i++) {
        mongoc_collection_destroy(MONGO_COLLECTIONS[i]);
    }
    free(MONGO_COLLECTIONS);

    mongoc_client_pool_push(MONGO_CLIENT_POOL, MONGO_CLIENT);

    return S_OK;
}

Err
db_set_if_not_exists(int collection, bson_t *key) {
    bool status;

    bson_t set_val;
    bson_t opts;
    bson_t reply;

    bson_error_t error;
    Err error_code;
    bool is_upsert = true;
    int n_upserted;

    // set_val
    bson_init(&set_val);
    status = bson_append_document(&set_val, "$setOnInsert", -1, key);
    if (!status) {
        bson_destroy(&set_val);
        return S_ERR;
    }

    // opts
    bson_init(&opts);

    status = bson_append_bool(&opts, "upsert", -1, is_upsert);
    if (!status) {
        bson_destroy(&set_val);
        bson_destroy(&opts);
        return S_ERR;
    }

    // reply
    bson_init(&reply);
    status = mongoc_collection_update_one(MONGO_COLLECTIONS[collection], key, &set_val, &opts, &reply, &error);
    if (!status) {
        bson_destroy(&set_val);
        bson_destroy(&opts);
        bson_destroy(&reply);
        return S_ERR;
    }

    error_code = _bson_exists(&reply, "upsertedId");
    if (error_code) {
        bson_destroy(&set_val);
        bson_destroy(&opts);
        bson_destroy(&reply);
        return S_ERR_ALREADY_EXISTS;
    }

    bson_destroy(&set_val);
    bson_destroy(&opts);
    bson_destroy(&reply);

    return S_OK;
}

Err
db_update_one(int collection, bson_t *key, bson_t *val, bool is_upsert) {
    bool status;

    bson_t set_val;
    bson_t opts;
    bson_t reply;

    bson_error_t error;

    // set_val
    bson_init(&set_val);
    status = bson_append_document(&set_val, "$set", -1, val);
    if (!status) {
        bson_destroy(&set_val);
        return S_ERR;
    }

    // opts
    bson_init(&opts);
    status = bson_append_bool(&opts, "upsert", -1, &is_upsert);
    if (!status) {
        bson_destroy(&set_val);
        bson_destroy(&opts);
        return S_ERR;
    }

    // reply
    bson_init(&reply);
    status = mongoc_collection_update_one(MONGO_COLLECTIONS[collection], key, &set_val, &opts, &reply, &error);
    if (!status) {
        bson_destroy(&set_val);
        bson_destroy(&opts);
        bson_destroy(&reply);
        return S_ERR;
    }

    bson_destroy(&set_val);
    bson_destroy(&opts);
    bson_destroy(&reply);

    return S_OK;
}

Err
_DB_FORCE_DROP_COLLECTION(int collection) {
    bool status;
    bson_error_t error;
    status = mongoc_collection_drop(MONGO_COLLECTIONS[collection], &error);
    if (!status) {
        return S_ERR;
    }

    return S_OK;
}

Err
_bson_exists(bson_t *b, char *name) {
    bool status;
    bson_iter_t iter;
    bson_iter_t it_val;

    status = bson_iter_init(&iter, b);
    if (!status) {
        return S_ERR;
    }

    status = bson_iter_find_descendant(&iter, name, &it_val);
    if (!status) {
        return S_ERR_NOT_EXISTS;
    }

    return S_OK;
}

Err
_bson_get_value_int32(bson_t *b, char *name, int *value) {
    bool status;
    bson_iter_t iter;
    bson_iter_t it_val;

    status = bson_iter_init(&iter, b);
    if (!status) {
        return S_ERR;
    }

    status = bson_iter_find_descendant(&iter, name, &it_val);
    if (!status) {
        return S_ERR;
    }

    status = BSON_ITER_HOLDS_INT32(&it_val);
    if (!status) {
        return S_ERR;
    }

    *value = bson_iter_int32(&it_val);

    return S_OK;
}

/**********
 * Milli-timestamp
 **********/

/**
 * @brief Get current time in milli-timestamp
 * @details Get current time in milli-timestamp
 *
 * @param milli_timestamp milli-timestamp (to-compute)
 * @return Err
 */
Err
get_milli_timestamp(time64_t *milli_timestamp) {
    struct timeval tv;
    struct timezone tz;

    int ret_code = gettimeofday(&tv, &tz);
    if (ret_code) return S_ERR;

    *milli_timestamp = ((time64_t)tv.tv_sec) * 1000L + ((time64_t)tv.tv_usec) / 1000L;

    return S_OK;
}

/**********
 * UUID
 **********/

/**
 * @brief Generate customized uuid (maybe we can use libuuid to save some random thing.)
 * @details Generate customized uuid (ASSUMING little endian in the system):
 *          1. byte[0-6]: random
 *          2. byte[7]: 0x60 as version
 *          3. byte[8-9]: random
 *          4: byte[10-15]: milli-timestamp
 *
 * @param uuid [description]
 */
Err
gen_uuid(UUID uuid) {
    Err error_code;

    time64_t milli_timestamp;
    time64_t *p_milli_timestamp;

    long int rand_num;
    long int *p_rand;
    int n_i;
    unsigned short *p_short_rand_num;

    unsigned short *p_short;
    unsigned int *p_int;
    unsigned char *p_char;
    _UUID _uuid;

    // last 8 chars as milli-timestamp, but only the last 6 chars will be used.
    error_code = get_milli_timestamp(&milli_timestamp);
    if (error_code) return error_code;

    p_char = &milli_timestamp;
    milli_timestamp <<= 16;
    p_char = &milli_timestamp;
    p_milli_timestamp = _uuid + 40;
    *p_milli_timestamp = milli_timestamp;

    rand_num = random();
    p_short_rand_num = &rand_num;
    p_short = _uuid + 40;
    *p_short = *p_short_rand_num;

    // first 40 chars as random, but 6th char is version (6 for now)
    p_rand = _uuid;
    for (int i = 0; i < (_UUIDLEN - 8) / sizeof(long int); i++) {
        rand_num = random();
        *p_rand = rand_num;
        p_rand++;
    }

    _uuid[6] &= 0x0f;
    _uuid[6] |= 0x60;

    b64_ntop(_uuid, _UUIDLEN, (char *)uuid, UUIDLEN);

    return S_OK;
}

/**
 * @brief Gen uuid and check with db.
 * @details Gen uuid and check with db for uniqueness.
 *
 * @param collection Collection
 * @param uuid uuid (to-compute).
 * @return Err
 */
Err
gen_uuid_with_db(int collection, UUID uuid) {
    Err error_code = S_OK;
    bson_t uuid_bson;

    for (int i = 0; i < N_GEN_UUID_WITH_DB; i++) {
        error_code = gen_uuid(uuid);
        if (error_code) {
            continue;
        }

        bson_init(&uuid_bson);
        error_code = _serialize_uuid_bson(uuid, MONGO_THE_ID, &uuid_bson);
        if (error_code) {
            bson_destroy(&uuid_bson);
            continue;
        }

        error_code = db_set_if_not_exists(collection, &uuid_bson);
        bson_destroy(&uuid_bson);

        if (!error_code) {
            return S_OK;
        }
    }

    return error_code;
}

/**
 * @brief Gen uuid with block-id as 0 and check with db.
 * @details Gen uuid and check with db for uniqueness.
 *
 * @param collection Collection
 * @param uuid uuid (to-compute).
 * @return Err
 */
Err
gen_content_uuid_with_db(int collection, UUID uuid) {
    Err error_code = S_OK;
    bson_t uuid_bson;

    for (int i = 0; i < N_GEN_UUID_WITH_DB; i++) {
        error_code = gen_uuid(uuid);
        if (error_code) {
            continue;
        }

        bson_init(&uuid_bson);
        error_code = _serialize_content_uuid_bson(uuid, MONGO_THE_ID, 0, &uuid_bson);
        if (error_code) {
            bson_destroy(&uuid_bson);
            continue;
        }

        error_code = db_set_if_not_exists(collection, &uuid_bson);
        bson_destroy(&uuid_bson);

        if (!error_code) {
            return S_OK;
        }
    }

    return error_code;
}

/**
 * @brief Serialize uuid to bson
 * @details Serialize uuid to bson, and name the uuid as the_id.
 *
 * @param uuid uuid
 * @param db_set_bson bson (to-compute)
 * @return Err
 */
Err
_serialize_uuid_bson(UUID uuid, const char *key, bson_t *uuid_bson) {
    bool bson_status = bson_append_utf8(uuid_bson, key, -1, uuid, UUIDLEN);
    if (!bson_status) return S_ERR;

    return S_OK;
}

/**
 * @brief Serialize uuid to bson
 * @details Serialize uuid to bson, and name the uuid as the_id.
 *
 * @param uuid uuid
 * @param db_set_bson bson (to-compute)
 * @return Err
 */
Err
_serialize_content_uuid_bson(UUID uuid, const char *key, int block_id, bson_t *uuid_bson) {
    bool bson_status = bson_append_utf8(uuid_bson, key, -1, uuid, UUIDLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int32(uuid_bson, MONGO_BLOCK_ID, -1, block_id);
    if (!bson_status) return S_ERR;

    return S_OK;
}

Err
uuid_to_milli_timestamp(UUID uuid, time64_t *milli_timestamp)
{
    _UUID _uuid;
    time64_t *p_uuid = _uuid + 40;
    b64_pton(uuid, _uuid, _UUIDLEN);

    *milli_timestamp = *p_uuid;
    *milli_timestamp >>= 16;

    return S_OK;
}

/**********
 * Post
 **********/

/**
 * @brief Get the lines of the post to be able to show the percentage.
 * @details Get the lines of the post to be able to show the percentage.
 *          XXX maybe we need to exclude n_line_comment_reply to simplify the indication of the line no.
 *          XXX Simplification: We count only the lines of main the numbers of comments.
 *                              It is with high probability that the users accept the modification.
 * @param main_id main_id for the post
 * @param n_line n_line (to-compute)
 * @return Err
 */
/*
Err
n_line_post(UUID main_id, int *n_line) {
    Err error_code = S_OK;
    int the_line_main = 0;
    int the_line_comments = 0;
    int the_line_comment_reply = 0;

    error_code = n_line_main(main_id, &the_line_main);
    if (error_code) return error_code;

    error_code = n_line_comments(main_id, &the_line_comments);
    if (error_code) return error_code;

    error_code = n_line_comment_reply_by_main(main_id, &the_line_comment_reply);
    if (error_code) return error_code;

    *n_line = the_line_main + the_line_comments + the_line_comment_reply;

    return S_OK;
}
*/

/**********
 * Main
 **********/

/**
 * @brief Create main from fd
 * @details Create main from fd
 *
 * @param aid aid
 * @param title title
 * @param poster poster
 * @param ip ip
 * @param origin origin
 * @param web_link web_link
 * @param len length of the content
 * @param fd_content fd
 * @param main_id (to-compute)
 * @return Err
 */
Err
create_main_from_fd(aidu_t aid, char *title, char *poster, unsigned char *ip, unsigned char *origin, unsigned char *web_link, int len, int fd_content, UUID main_id) {

    Err error_code = S_OK;
    int n_line;
    int n_block;

    UUID content_id;

    time64_t create_milli_timestamp;

    MainHeader main_header;
    bson_t main_id_bson;
    bson_t main_bson;

    error_code = get_milli_timestamp(&create_milli_timestamp);
    if (error_code) return error_code;

    error_code = gen_uuid_with_db(MONGO_MAIN, main_id);
    if (error_code) return error_code;

    error_code = gen_content_uuid_with_db(MONGO_MAIN_CONTENT, content_id);
    if (error_code) return error_code;

    // main_header
    strlcpy(main_header.the_id, main_id, sizeof(UUID));
    strlcpy(main_header.content_id, content_id, sizeof(UUID));
    main_header.aid = aid;
    main_header.status = LIVE_STATUS_ALIVE;
    strcpy(main_header.status_updater, poster);
    strcpy(main_header.status_update_ip, ip);
    strcpy(main_header.title, title);
    strcpy(main_header.poster, poster);
    strcpy(main_header.ip, ip);
    main_header.create_milli_timestamp = create_milli_timestamp;
    main_header.update_milli_timestamp = create_milli_timestamp;
    strcpy(main_header.origin, origin);
    strcpy(main_header.web_link, web_link);
    main_header.reset_karma = 0;

    // main_contents
    error_code = _split_main_contents(fd_content, len, main_id, content_id, &n_line, &n_block);
    if (error_code) {
        return error_code;
    }

    // db-main
    main_header.len_total = len;
    main_header.n_total_line = n_line;
    main_header.n_total_block = n_block;

    bson_init(&main_bson);
    error_code = _serialize_main_bson(&main_header, &main_bson);
    if (error_code) {
        bson_destroy(&main_bson);
        return error_code;
    }

    bson_init(&main_id_bson);
    error_code = _serialize_uuid_bson(main_id, MONGO_THE_ID, &main_id_bson);
    if (error_code) {
        bson_destroy(&main_bson);
        bson_destroy(&main_id_bson);
        return error_code;
    }

    error_code = db_update_one(MONGO_MAIN, &main_id_bson, &main_bson, true);
    bson_destroy(&main_bson);
    bson_destroy(&main_id_bson);
    if (error_code) {
        return error_code;
    }

    return S_OK;
}

/**
 * @brief Split main-content from fd to main-content-blocks.
 * @details Split main-content from fd to main-content-blocks:
 *          1. init
 *          2. read from buffer.
 *          2.1.    get one line from buffer.
 *          2.2.    do split-main-content-core from the line.
 *          3. deal with last-line.
 *
 * @param fd_content fd
 * @param len len to read from fd_content.
 * @param main_id main_id
 * @param content_id content_id
 * @param n_line n_line (to-compute)
 * @param n_block n_block (to-compute)
 * @return Err
 */
Err
_split_main_contents(int fd_content, int len, UUID main_id, UUID content_id, int *n_line, int *n_block) {
    Err error_code;
    char buf[MAX_BUF_SIZE];
    char line[MAX_BUF_SIZE];

    int bytes = 0;
    int buf_size = 0;
    int bytes_in_line = 0;
    int bytes_in_new_line = 0;
    MainContent main_content_block;

    // init
    bzero(line, sizeof(line));
    bzero(&main_content_block, sizeof(MainContent));

    error_code = _split_main_contents_init_main_content(&main_content_block, main_id, content_id, *n_block);
    if (error_code) {
        return error_code;
    }

    // while-loop
    while ((buf_size = len < MAX_BUF_SIZE ? len : MAX_BUF_SIZE) && (bytes = read(fd_content, buf, buf_size)) > 0) {
        for (int offset_buf = 0; offset_buf < bytes; offset_buf += bytes_in_new_line) {
            error_code = get_line_from_buf(buf, offset_buf, bytes, line, bytes_in_line, &bytes_in_new_line);
            bytes_in_line += bytes_in_new_line;
            if (error_code) {
                break;
            }

            // Main-op
            error_code = _split_main_contents_core(line, bytes_in_line, main_id, content_id, &main_content_block, n_line, n_block);
            if (error_code) {
                return error_code;
            }

            // reset line
            bzero(line, sizeof(char) * bytes_in_line);
            bytes_in_line = 0;
        }

        len -= bytes;
    }
    // last line
    if (bytes_in_line) {
        // Main-op
        error_code = _split_main_contents_core(line, bytes_in_line, main_id, content_id, &main_content_block, n_line, n_block);
        if (error_code) {
            return error_code;
        }
    }
    // last block
    if (main_content_block.n_line) {
        error_code = _split_main_contents_save_main_content_block(&main_content_block);
        if (error_code) {
            return error_code;
        }
    }

    return S_OK;
}

/**
 * @brief Core of _split_main_contents.
 * @details Core of _split_main_content.
 *          1. 1 more line.
 *          2. check whether the buffer exceeds limits.
 *          3. setup content->buffer, content->len, content->line.
 *
 * @param line line
 * @param bytes_in_line bytes_in_line
 * @param main_content_block main_content_block
 * @param n_line n_line
 * @param n_block n_block
 * @return Err
 */
Err
_split_main_contents_core(char *line, int bytes_in_line, UUID main_id, UUID content_id, MainContent *main_content_block, int *n_line, int *n_block) {
    Err error_code;

    //1 more line
    *n_line++;

    // check for max-lines in block-buf.
    if (main_content_block->n_line >= MAX_BUF_LINES) {
        error_code = _split_main_contents_save_main_content_block(main_content_block);
        if (error_code) {
            return error_code;
        }

        *n_block++;
        error_code = _split_main_contents_init_main_content(main_content_block, main_id, content_id, *n_block);
        if (error_code) {
            return error_code;
        }
    }
    // XXX should never happen.
    else if (main_content_block->len_block + bytes_in_line >= MAX_BUF_BLOCK) {
        error_code = _split_main_contents_save_main_content_block(main_content_block);
        if (error_code) {
            return error_code;
        }

        *n_block++;
        error_code = _split_main_contents_init_main_content(main_content_block, main_id, content_id, *n_block);
        if (error_code) {
            return error_code;
        }
    }

    strlcpy(main_content_block->buf_block, line, bytes_in_line);
    main_content_block->len_block += bytes_in_line;
    main_content_block->n_line++;

    return S_OK;
}

/**
 * @brief Initialize main-content
 * @details Set main_id, content_id, and block_id. Others as 0
 *
 * @param main_content_block main-content-block
 * @param main_id main_id
 * @param content_id content_id
 * @param n_block block_id
 * @return Err
 */
Err
_split_main_contents_init_main_content(MainContent *main_content_block, UUID main_id, UUID content_id, int block_id) {
    if (main_content_block->len_block) {
        bzero(main_content_block->buf_block, main_content_block->len_block);
    }
    main_content_block->len_block = 0;
    main_content_block->n_line = 0;
    strlcpy(main_content_block->the_id, content_id, sizeof(UUID));
    strlcpy(main_content_block->main_id, main_id, sizeof(UUID));
    main_content_block->block_id = block_id;

    return S_OK;
}

/**
 * @brief Save main-content-block
 * @details Save main-content-block
 *
 * @param main_content_block main-content-block
 * @return Err
 */
Err
_split_main_contents_save_main_content_block(MainContent *main_content_block) {
    Err error_code;
    bson_t main_content_block_bson;
    bson_t main_content_block_uuid_bson;

    bson_init(&main_content_block_bson);

    error_code = _serialize_main_content_block_bson(main_content_block, &main_content_block_bson);
    if (error_code) {
        bson_destroy(&main_content_block_bson);
        return error_code;
    }

    bson_init(&main_content_block_uuid_bson);
    error_code = _serialize_content_uuid_bson(main_content_block->the_id, MONGO_THE_ID, main_content_block->block_id, &main_content_block_uuid_bson);
    if (error_code) {
        bson_destroy(&main_content_block_bson);
        bson_destroy(&main_content_block_uuid_bson);
        return error_code;
    }

    error_code = db_update_one(MONGO_MAIN_CONTENT, &main_content_block_uuid_bson, &main_content_block_bson, true);
    bson_destroy(&main_content_block_uuid_bson);
    bson_destroy(&main_content_block_bson);
    if (error_code) {
        return error_code;
    }

    return S_OK;
}

/**
 * @brief Serialize main-header to bson
 * @details Serialize main-header to bson
 *
 * @param main_header main-header
 * @param main_bson main_bson (to-compute)
 * @return Err
 */
Err
_serialize_main_bson(MainHeader *main_header, bson_t *main_bson) {
    bool bson_status;

    bson_status = bson_append_int32(main_bson, "version", -1, main_header->version);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_utf8(main_bson, "the_id", -1, main_header->the_id, UUIDLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_utf8(main_bson, "content_id", -1, main_header->content_id, UUIDLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int32(main_bson, "aid", -1, main_header->aid);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int32(main_bson, "status", -1, main_header->status);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_utf8(main_bson, "status_updater", -1, main_header->status_updater, IDLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_utf8(main_bson, "status_update_ip", -1, main_header->status_update_ip, IPV4LEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_binary(main_bson, "title", -1, BSON_SUBTYPE_BINARY, main_header->title, TTLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_utf8(main_bson, "poster", -1, main_header->poster, IDLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_utf8(main_bson, "ip", -1, main_header->ip, IPV4LEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int64(main_bson, "create_milli_timestamp", -1, main_header->create_milli_timestamp);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_utf8(main_bson, "updater", -1, main_header->updater, IDLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_utf8(main_bson, "update_ip", -1, main_header->update_ip, IPV4LEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int64(main_bson, "update_milli_timestamp", -1, main_header->update_milli_timestamp);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_utf8(main_bson, "origin", -1, main_header->origin, strlen(main_header->origin));
    if (!bson_status) return S_ERR;

    bson_status = bson_append_utf8(main_bson, "web_link", -1, main_header->web_link, strlen(main_header->web_link));
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int32(main_bson, "reset_karma", -1, main_header->reset_karma);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int32(main_bson, "n_total_line", -1, main_header->n_total_line);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int32(main_bson, "n_total_block", -1, main_header->n_total_block);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int32(main_bson, "len_total", -1, main_header->len_total);
    if (!bson_status) return S_ERR;

    return S_OK;
}

/**
 * @brief Serialize main-content-block to bson
 * @details Serialize main-content-block to bson
 *
 * @param main_content_block main-content-block
 * @param main_content_block_bson main_content_block_bson (to-compute)
 * @return Err
 */
Err
_serialize_main_content_block_bson(MainContent *main_content_block, bson_t *main_content_block_bson) {
    bool bson_status;

    bson_status = bson_append_utf8(main_content_block_bson, "the_id", -1, main_content_block->the_id, UUIDLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_utf8(main_content_block_bson, "main_id", -1, main_content_block->main_id, UUIDLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int32(main_content_block_bson, "block_id", -1, main_content_block->block_id);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int32(main_content_block_bson, "len_block", -1, main_content_block->len_block);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int32(main_content_block_bson, "n_line", -1, main_content_block->n_line);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_binary(main_content_block_bson, "buf_block", -1, BSON_SUBTYPE_BINARY, main_content_block->buf_block, main_content_block->len_block);
    if (!bson_status) return S_ERR;

    return S_OK;
}

/**********
 * Misc
 **********/

/**
 * @brief Try to get a line (ending with \r\n) from buffer.
 * @details [long description]
 *
 * @param p_buf Starting point of the buffer.
 * @param offset_buf Current offset of the p_buf in the whole buffer.
 * @param bytes Total bytes of the buffer.
 * @param p_line Starting point of the line.
 * @param offset_line Offset of the line.
 * @param bytes_in_new_line To be obtained bytes in new extracted line.
 * @return Error
 */
Err
get_line_from_buf(char *p_buf, int offset_buf, int bytes, char *p_line, int offset_line, int *bytes_in_new_line) {

    // check the end of buf
    if (offset_buf >= bytes) {
        *bytes_in_new_line = 0;

        return S_ERR;
    }

    // init p_buf offset
    p_buf += offset_buf;
    p_line += offset_line;

    // check bytes in line and in buf.
    if (offset_line && p_line[-1] == '\r' && p_buf[0] == '\n') {
        *p_line = '\n';
        *bytes_in_new_line = 1;

        return S_OK;
    }

    // check \r\n in buf.
    for (int i = offset_buf; i < bytes - 1; i++) {
        if (*p_buf == '\r' && *(p_buf + 1) == '\n') {
            *p_line = '\r';
            *(p_line + 1) = '\n';
            *bytes_in_new_line = i - offset_buf + 1 + 1;

            return S_OK;
        }

        *p_line++ = *p_buf++;
    }

    // last char
    *p_line++ = *p_buf++;
    *bytes_in_new_line = bytes - offset_buf;

    return S_ERR;
}
