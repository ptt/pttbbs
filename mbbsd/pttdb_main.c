#include "pttdb.h"
#include "pttdb_internal.h"

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
create_main_from_fd(aidu_t aid, char *board, char *title, char *poster, char *ip, char *origin, char *web_link, int len, int fd_content, UUID main_id, UUID content_id)
{

    Err error_code = S_OK;
    int n_line = 0;
    int n_block = 0;

    time64_t create_milli_timestamp;

    MainHeader main_header = {};

    error_code = get_milli_timestamp(&create_milli_timestamp);
    if (error_code) return error_code;

    error_code = gen_uuid_with_db(MONGO_MAIN, main_id);
    if (error_code) return error_code;

    error_code = gen_content_uuid_with_db(MONGO_MAIN_CONTENT, content_id);
    if (error_code) return error_code;

    // main_header
    memcpy(main_header.the_id, main_id, sizeof(UUID));
    memcpy(main_header.content_id, content_id, sizeof(UUID));
    memcpy(main_header.update_content_id, content_id, sizeof(UUID));
    main_header.aid = aid;
    main_header.status = LIVE_STATUS_ALIVE;
    strcpy(main_header.board, board);
    strcpy(main_header.status_updater, poster);
    strcpy(main_header.status_update_ip, ip);
    strcpy(main_header.title, title);
    strcpy(main_header.poster, poster);
    strcpy(main_header.ip, ip);
    strcpy(main_header.updater, poster);
    strcpy(main_header.update_ip, ip);
    main_header.create_milli_timestamp = create_milli_timestamp;
    main_header.update_milli_timestamp = create_milli_timestamp;
    strcpy(main_header.origin, origin);
    strcpy(main_header.web_link, web_link);
    main_header.reset_karma = 0;

    // main_contents
    error_code = split_contents_from_fd(fd_content, len, main_id, content_id, MONGO_MAIN_CONTENT, &n_line, &n_block);
    if (error_code) return error_code;

    // db-main
    main_header.len_total = len;
    main_header.n_total_line = n_line;
    main_header.n_total_block = n_block;

    bson_t *main_id_bson = NULL;
    bson_t *main_bson = NULL;
    if(!error_code) {
        error_code = _serialize_main_bson(&main_header, &main_bson);
    }

    char *str = bson_as_canonical_extended_json(main_bson, NULL);
    fprintf(stderr, "pttdb_main.create_main_from_fd: main_bson: %s\n", str);
    bson_free(str);

    if (!error_code) {
        error_code = _serialize_uuid_bson(main_id, &main_id_bson);
    }

    str = bson_as_canonical_extended_json(main_id_bson, NULL);
    fprintf(stderr, "pttdb_main.create_main_from_fd: main_id_bson: %s\n", str);
    bson_free(str);

    if(!error_code) {
        error_code = db_update_one(MONGO_MAIN, main_id_bson, main_bson, true);
    }

    bson_safe_destroy(&main_bson);
    bson_safe_destroy(&main_id_bson);

    return error_code;
}

/**
 * @brief Get total size of main from main_id
 * @details [long description]
 *
 * @param main_id main_id
 * @param len total-size of the main.
 *
 * @return Err
 */
Err
len_main(UUID main_id, int *len)
{
    Err error_code = S_OK;
    bson_t *key = BCON_NEW(
        "the_id", BCON_BINARY(main_id, UUIDLEN)
        );

    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "the_id", BCON_BOOL(true),
        "len_total", BCON_BOOL(true)
        );

    bson_t *db_result = NULL;

    error_code = db_find_one(MONGO_MAIN, key, fields, &db_result);

    if(!error_code) {
        error_code = bson_get_value_int32(db_result, "len_total", len);
    }

    bson_safe_destroy(&key);
    bson_safe_destroy(&fields);
    bson_safe_destroy(&db_result);

    return error_code;
}

/**
 * @brief Get total size of main from aid
 * @details
 *
 * @param aid aid
 * @return Err
 */
Err
len_main_by_aid(aidu_t aid, int *len)
{
    Err error_code = S_OK;
    bson_t *key = BCON_NEW(
        "aid", BCON_INT64(aid)
        );

    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "aid", BCON_BOOL(true),
        "len_total", BCON_BOOL(true)
        );

    bson_t *db_result = NULL;
    error_code = db_find_one(MONGO_MAIN, key, fields, &db_result);
    if(!error_code) {
        error_code = bson_get_value_int32(db_result, "len_total", len);
    }

    bson_safe_destroy(&key);
    bson_safe_destroy(&fields);
    bson_safe_destroy(&db_result);

    return error_code;
}

/**
 * @brief get total-lines of main from main_id
 * @details [long description]
 *
 * @param main_id main-id
 * @param n_line n-line
 *
 * @return Err
 */
Err
n_line_main(UUID main_id, int *n_line)
{
    Err error_code = S_OK;
    bson_t *key = BCON_NEW(
        "the_id", BCON_BINARY(main_id, UUIDLEN)
        );

    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "n_total_line", BCON_BOOL(true)
        );

    bson_t *db_result = NULL;
    error_code = db_find_one(MONGO_MAIN, key, fields, &db_result);

    if(!error_code) {
        error_code = bson_get_value_int32(db_result, "n_total_line", n_line);
    }

    bson_safe_destroy(&key);
    bson_safe_destroy(&fields);
    bson_safe_destroy(&db_result);

    return error_code;
}

/**
 * @brief get total-lines of main from aid
 * @details [long description]
 *
 * @param aid aid
 * @param n_line n-line
 *
 * @return Err
 */
Err
n_line_main_by_aid(aidu_t aid, int *n_line)
{
    Err error_code = S_OK;
    bson_t *key = BCON_NEW(
        "aid", BCON_INT64(aid)
        );

    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "n_total_line", BCON_BOOL(true)
        );

    bson_t *db_result = NULL;
    error_code = db_find_one(MONGO_MAIN, key, fields, &db_result);

    if(!error_code) {
        error_code = bson_get_value_int32(db_result, "n_total_line", n_line);
    }

    bson_safe_destroy(&key);
    bson_safe_destroy(&fields);
    bson_safe_destroy(&db_result);

    return error_code;
}

/**
 * @brief read main header
 * @details [long description]
 *
 * @param main_id main-id
 * @param main_header main-header
 *
 * @return Err
 */
Err
read_main_header(UUID main_id, MainHeader *main_header)
{
    Err error_code = S_OK;
    bson_t *key = BCON_NEW(
        "the_id", BCON_BINARY(main_id, UUIDLEN)
        );


    bson_t *db_result = NULL;
    error_code = db_find_one(MONGO_MAIN, key, NULL, &db_result);

    if(!error_code) {
        error_code = _deserialize_main_bson(db_result, main_header);
    }

    bson_safe_destroy(&key);
    bson_safe_destroy(&db_result);
    return error_code;
}

/**
 * @brief [brief description]
 * @details [long description]
 *
 * @param aid [description]
 * @param main [description]
 *
 * @return [description]
 */
Err
read_main_header_by_aid(aidu_t aid, MainHeader *main_header)
{
    Err error_code = S_OK;
    bson_t *key = BCON_NEW(
        "aid", BCON_INT64(aid)
        );


    bson_t *db_result = NULL;
    error_code = db_find_one(MONGO_MAIN, key, NULL, &db_result);

    if(!error_code) {
        error_code = _deserialize_main_bson(db_result, main_header);
    }

    bson_safe_destroy(&key);
    bson_safe_destroy(&db_result);
    return error_code;
}

/**
 * @brief [brief description]
 * @details [long description]
 *
 * @param main_id [description]
 * @param updater [description]
 * @param char [description]
 * @return [description]
 */
Err
delete_main(UUID main_id, char *updater, char *ip) {
    Err error_code = S_OK;

    bson_t *key = BCON_NEW(
        "the_id", BCON_BINARY(main_id, UUIDLEN)
        );

    bson_t *val = BCON_NEW(
        "status_updater", BCON_BINARY((unsigned char *)updater, IDLEN),
        "status", BCON_INT32((int)LIVE_STATUS_DELETED),
        "status_update_ip", BCON_BINARY((unsigned char *)ip, IPV4LEN)
        );

    error_code = db_update_one(MONGO_MAIN, key, val, true);

    bson_safe_destroy(&key);
    bson_safe_destroy(&val);

    return error_code;
}

/**
 * @brief [brief description]
 * @details [long description]
 *
 * @param main_id [description]
 * @param updater [description]
 * @param char [description]
 * @return [description]
 */
Err
delete_main_by_aid(aidu_t aid, char *updater, char *ip) {
    Err error_code = S_OK;

    bson_t *key = BCON_NEW(
        "aid", BCON_INT64(aid)
        );

    bson_t *val = BCON_NEW(
        "status_updater", BCON_BINARY((unsigned char *)updater, IDLEN),
        "status", BCON_INT32((int)LIVE_STATUS_DELETED),
        "status_update_ip", BCON_BINARY((unsigned char *)ip, IPV4LEN)
        );

    error_code = db_update_one(MONGO_MAIN, key, val, true);

    bson_safe_destroy(&key);
    bson_safe_destroy(&val);

    return error_code;
}

/**
 * @brief [brief description]
 * @details [long description]
 *
 * @param main_id [description]
 * @param title [description]
 * @param updater [description]
 * @param char [description]
 * @param len [description]
 * @param fd_content [description]
 * @return [description]
 */
Err
update_main_from_fd(UUID main_id, char *updater, char *update_ip, int len, int fd_content, UUID content_id)
{
    Err error_code = S_OK;
    int n_line;
    int n_block;

    time64_t update_milli_timestamp;

    error_code = get_milli_timestamp(&update_milli_timestamp);
    if (error_code) return error_code;

    error_code = gen_content_uuid_with_db(MONGO_MAIN_CONTENT, content_id);
    if (error_code) return error_code;

    // main-contents
    error_code = split_contents_from_fd(fd_content, len, main_id, content_id, MONGO_MAIN_CONTENT, &n_line, &n_block);
    if (error_code) return error_code;

    // db-main
    bson_t *main_id_bson = NULL;
    bson_t *main_bson = NULL;

    error_code = _serialize_uuid_bson(main_id, &main_id_bson);

    // update: content_id, update_content_id, updater, update_ip, update_milli_timestamp, n_total_line, n_total_block, len_total
    if(!error_code) {
        error_code = _serialize_update_main_bson(content_id, updater, update_ip, update_milli_timestamp, n_line, n_block, len, &main_bson);
    }

    if(!error_code) {
        error_code = db_update_one(MONGO_MAIN, main_id_bson, main_bson, false);
    }

    bson_safe_destroy(&main_bson);
    bson_safe_destroy(&main_id_bson);

    return error_code;
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
_serialize_main_bson(MainHeader *main_header, bson_t **main_bson)
{
    *main_bson = BCON_NEW(
        "version", BCON_INT32(main_header->version),
        "the_id", BCON_BINARY(main_header->the_id, UUIDLEN),
        "content_id", BCON_BINARY(main_header->content_id, UUIDLEN),
        "update_content_id", BCON_BINARY(main_header->update_content_id, UUIDLEN),
        "aid", BCON_INT64(main_header->aid),
        "status", BCON_INT32(main_header->status),
        "status_updater", BCON_BINARY((unsigned char *)main_header->status_updater, IDLEN),
        "status_update_ip", BCON_BINARY((unsigned char *)main_header->status_update_ip, IPV4LEN),
        "board", BCON_BINARY((unsigned char *)main_header->board, IDLEN),
        "title", BCON_BINARY((unsigned char *)main_header->title, TTLEN),
        "poster", BCON_BINARY((unsigned char *)main_header->poster, IDLEN),
        "ip", BCON_BINARY((unsigned char *)main_header->ip, IPV4LEN),
        "create_milli_timestamp", BCON_INT64(main_header->create_milli_timestamp),
        "updater", BCON_BINARY((unsigned char *)main_header->updater, IDLEN),
        "update_ip", BCON_BINARY((unsigned char *)main_header->update_ip,IPV4LEN),
        "update_milli_timestamp", BCON_INT64(main_header->update_milli_timestamp),
        "origin", BCON_BINARY((unsigned char *)main_header->origin, strlen(main_header->origin)),
        "web_link", BCON_BINARY((unsigned char *)main_header->web_link, strlen(main_header->web_link)),
        "reset_karma", BCON_INT32(main_header->reset_karma),
        "n_total_line", BCON_INT32(main_header->n_total_line),
        "n_total_block", BCON_INT32(main_header->n_total_block),
        "len_total", BCON_INT32(main_header->len_total)
        );
    if(*main_bson == NULL) return S_ERR;

    return S_OK;
}

/**
 * @brief [brief description]
 * @details [long description]
 *
 * @param main_bson [description]
 * @param main_header [description]
 *
 * @return Err
 */
Err
_deserialize_main_bson(bson_t *main_bson, MainHeader *main_header)
{    
    char *str = bson_as_canonical_extended_json(main_bson, NULL);
    fprintf(stderr, "pttdb_main._deserialze_main_bson: start: main_bson: %s\n", str);
    bson_free(str);

    Err error_code;
    error_code = bson_get_value_int32(main_bson, "version", (int *)&main_header->version);
    if (error_code) return error_code;

    int len;
    error_code = bson_get_value_bin(main_bson, "the_id", UUIDLEN, (char *)main_header->the_id, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "content_id", UUIDLEN, (char *)main_header->content_id, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "update_content_id", UUIDLEN, (char *)main_header->update_content_id, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_int64(main_bson, "aid", (long *)&main_header->aid);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(main_bson, "status", (int *)&main_header->status);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "status_updater", IDLEN, main_header->status_updater, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "status_update_ip", IPV4LEN, main_header->status_update_ip, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "board", IDLEN, main_header->board, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "title", TTLEN, main_header->title, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "poster", IDLEN, main_header->poster, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "ip", IPV4LEN, main_header->ip, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_int64(main_bson, "create_milli_timestamp", (long *)&main_header->create_milli_timestamp);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "updater", IDLEN, main_header->updater, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "update_ip", IPV4LEN, main_header->update_ip, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_int64(main_bson, "update_milli_timestamp", (long *)&main_header->update_milli_timestamp);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "origin", MAX_ORIGIN_LEN, main_header->origin, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "web_link", MAX_WEB_LINK_LEN, main_header->web_link, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(main_bson, "reset_karma", &main_header->reset_karma);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(main_bson, "n_total_line", &main_header->n_total_line);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(main_bson, "n_total_block", &main_header->n_total_block);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(main_bson, "len_total", &main_header->len_total);
    if (error_code) return error_code;

    return S_OK;
}

/**
 * @brief [brief description]
 * @details [long description]
 *
 * @param content_id [description]
 * @param updater [description]
 * @param p [description]
 * @param update_milli_timestamp [description]
 * @param n_total_line [description]
 * @param n_total_block [description]
 * @param len_total [description]
 * @param main_bson [description]
 */
Err
_serialize_update_main_bson(UUID content_id, char *updater, char *update_ip, time64_t update_milli_timestamp, int n_total_line, int n_total_block, int len_total, bson_t **main_bson)
{
    *main_bson = BCON_NEW(
        "content_id", BCON_BINARY(content_id, UUIDLEN),
        "updater", BCON_BINARY((unsigned char *)updater, IDLEN),
        "update_ip", BCON_BINARY((unsigned char *)update_ip, IPV4LEN),
        "update_milli_timestamp", BCON_INT64(update_milli_timestamp),
        "n_total_line", BCON_INT32(n_total_line),
        "n_total_block", BCON_INT32(n_total_block),
        "len_total", BCON_INT32(len_total)
        );
    if(*main_bson == NULL) return S_ERR;

    return S_OK;
}
