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
create_main_from_fd(aidu_t aid, char *title, char *poster, char *ip, char *origin, char *web_link, int len, int fd_content, UUID main_id, UUID content_id)
{

    Err error_code = S_OK;
    int n_line = 0;
    int n_block = 0;

    time64_t create_milli_timestamp;

    MainHeader main_header;
    bson_t main_id_bson;
    bson_t main_bson;

    char debug_uuid[UUIDLEN + 1] = {};

    error_code = get_milli_timestamp(&create_milli_timestamp);
    if (error_code) return error_code;

    error_code = gen_uuid_with_db(MONGO_MAIN, main_id);
    if (error_code) return error_code;

    error_code = gen_content_uuid_with_db(MONGO_MAIN_CONTENT, content_id);
    if (error_code) return error_code;

    // main_header
    strncpy(debug_uuid, main_id, sizeof(UUID));
    strncpy(main_header.the_id, main_id, sizeof(UUID));
    strncpy(debug_uuid, main_header.the_id, sizeof(UUID));
    strncpy(main_header.content_id, content_id, sizeof(UUID));
    strncpy(main_header.update_content_id, content_id, sizeof(UUID));
    main_header.aid = aid;
    main_header.status = LIVE_STATUS_ALIVE;
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
_split_main_contents(int fd_content, int len, UUID main_id, UUID content_id, int *n_line, int *n_block)
{
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

    *n_block = 0;
    error_code = _split_main_contents_init_main_content(&main_content_block, main_id, content_id, n_block);
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
    if (main_content_block.len_block) {
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
_split_main_contents_core(char *line, int bytes_in_line, UUID main_id, UUID content_id, MainContent *main_content_block, int *n_line, int *n_block)
{
    Err error_code;

    // check for max-lines in block-buf.
    if (main_content_block->n_line > MAX_BUF_LINES) {
        error_code = _split_main_contents_save_main_content_block(main_content_block);
        if (error_code) {
            return error_code;
        }

        error_code = _split_main_contents_init_main_content(main_content_block, main_id, content_id, n_block);
        if (error_code) {
            return error_code;
        }
    }
    // XXX should never happen.
    else if (main_content_block->len_block + bytes_in_line > MAX_BUF_BLOCK) {
        error_code = _split_main_contents_save_main_content_block(main_content_block);
        if (error_code) {
            return error_code;
        }

        error_code = _split_main_contents_init_main_content(main_content_block, main_id, content_id, n_block);
        if (error_code) {
            return error_code;
        }
    }

    strncpy(main_content_block->buf_block + main_content_block->len_block, line, bytes_in_line);
    main_content_block->len_block += bytes_in_line;

    //1 more line
    if (line[bytes_in_line - 2] == '\r' && line[bytes_in_line - 1] == '\n') {
        (*n_line)++;
        main_content_block->n_line++;
    }


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
_split_main_contents_init_main_content(MainContent *main_content_block, UUID main_id, UUID content_id, int *block_id)
{
    if (main_content_block->len_block) {
        bzero(main_content_block->buf_block, main_content_block->len_block);
    }
    main_content_block->len_block = 0;
    main_content_block->n_line = 0;
    strncpy(main_content_block->the_id, content_id, sizeof(UUID));
    strncpy(main_content_block->main_id, main_id, sizeof(UUID));
    main_content_block->block_id = *block_id;

    (*block_id)++;

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
_split_main_contents_save_main_content_block(MainContent *main_content_block)
{
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
    Err error_code;
    bson_t key;
    bson_init(&key);
    bson_append_bin(&key, "the_id", -1, main_id, UUIDLEN);

    bson_t fields;
    bson_init(&fields);
    bson_append_bool(&fields, "_id", -1, false);
    bson_append_bool(&fields, "the_id", -1, true);
    bson_append_bool(&fields, "len_total", -1, true);

    bson_t db_result;
    bson_init(&db_result);
    error_code = db_find_one(MONGO_MAIN, &key, &fields, &db_result);
    if (error_code) {
        bson_destroy(&key);
        bson_destroy(&fields);
        bson_destroy(&db_result);
        return error_code;
    }

    error_code = bson_get_value_int32(&db_result, "len_total", len);
    if (error_code) {
        bson_destroy(&key);
        bson_destroy(&fields);
        bson_destroy(&db_result);
        return error_code;
    }
    bson_destroy(&key);
    bson_destroy(&fields);
    bson_destroy(&db_result);

    return S_OK;
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
    Err error_code;
    bson_t key;
    bson_init(&key);
    bson_append_int32(&key, "aid", -1, aid);

    bson_t fields;
    bson_init(&fields);
    bson_append_bool(&fields, "_id", -1, false);
    bson_append_bool(&fields, "aid", -1, true);
    bson_append_bool(&fields, "len_total", -1, true);

    bson_t db_result;
    bson_init(&db_result);
    error_code = db_find_one(MONGO_MAIN, &key, &fields, &db_result);
    if (error_code) {
        bson_destroy(&key);
        bson_destroy(&fields);
        bson_destroy(&db_result);
        return error_code;
    }

    error_code = bson_get_value_int32(&db_result, "len_total", len);
    if (error_code) {
        bson_destroy(&key);
        bson_destroy(&fields);
        bson_destroy(&db_result);
        return error_code;
    }
    bson_destroy(&key);
    bson_destroy(&fields);
    bson_destroy(&db_result);

    return S_OK;
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
    Err error_code;
    bson_t key;
    bson_init(&key);
    bson_append_bin(&key, "the_id", -1, main_id, UUIDLEN);

    bson_t fields;
    bson_init(&fields);
    bson_append_bool(&fields, "_id", -1, false);
    bson_append_bool(&fields, "the_id", -1, true);
    bson_append_bool(&fields, "n_total_line", -1, true);

    bson_t db_result;
    bson_init(&db_result);
    error_code = db_find_one(MONGO_MAIN, &key, &fields, &db_result);
    if (error_code) {
        bson_destroy(&key);
        bson_destroy(&fields);
        bson_destroy(&db_result);
        return error_code;
    }

    error_code = bson_get_value_int32(&db_result, "n_total_line", n_line);
    if (error_code) {
        bson_destroy(&key);
        bson_destroy(&fields);
        bson_destroy(&db_result);
        return error_code;
    }
    bson_destroy(&key);
    bson_destroy(&fields);
    bson_destroy(&db_result);

    return S_OK;
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
    Err error_code;
    bson_t key;
    bson_init(&key);
    bson_append_int64(&key, "aid", -1, aid);

    bson_t fields;
    bson_init(&fields);
    bson_append_bool(&fields, "_id", -1, false);
    bson_append_bool(&fields, "aid", -1, true);
    bson_append_bool(&fields, "n_total_line", -1, true);

    bson_t db_result;
    bson_init(&db_result);
    error_code = db_find_one(MONGO_MAIN, &key, &fields, &db_result);
    if (error_code) {
        bson_destroy(&key);
        bson_destroy(&fields);
        bson_destroy(&db_result);
        return error_code;
    }

    error_code = bson_get_value_int32(&db_result, "n_total_line", n_line);
    if (error_code) {
        bson_destroy(&key);
        bson_destroy(&fields);
        bson_destroy(&db_result);
        return error_code;
    }
    bson_destroy(&key);
    bson_destroy(&fields);
    bson_destroy(&db_result);

    return S_OK;
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
    Err error_code;
    bson_t key;
    bson_init(&key);
    bson_append_bin(&key, "the_id", -1, main_id, UUIDLEN);

    bson_t db_result;
    bson_init(&db_result);
    error_code = db_find_one(MONGO_MAIN, &key, NULL, &db_result);
    if (error_code) {
        bson_destroy(&key);
        bson_destroy(&db_result);
        return error_code;
    }

    error_code = _deserialize_main_bson(&db_result, main_header);
    if (error_code) {
        bson_destroy(&key);
        bson_destroy(&db_result);
        return error_code;
    }

    bson_destroy(&key);
    bson_destroy(&db_result);
    return S_OK;
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
    Err error_code;
    bson_t key;
    bson_t db_result;
    bool bson_status;

    bson_init(&key);

    bson_init(&db_result);

    bson_status = bson_append_int64(&key, "aid", -1, aid);
    if (!bson_status) {
        bson_destroy(&key);
        bson_destroy(&db_result);
        return S_ERR;
    }

    error_code = db_find_one(MONGO_MAIN, &key, NULL, &db_result);
    if (error_code) {
        bson_destroy(&key);
        bson_destroy(&db_result);
        return error_code;
    }

    error_code = _deserialize_main_bson(&db_result, main_header);
    if (error_code) {
        bson_destroy(&key);
        bson_destroy(&db_result);
        return error_code;
    }

    bson_destroy(&key);
    bson_destroy(&db_result);
    return S_OK;
}

/**
 * @brief [brief description]
 * @details [long description]
 *
 * @param main_content_id [description]
 * @param block_id [description]
 * @param max_n_main_content [description]
 * @param n_read_main_content [description]
 * @param main_content [description]
 * @return [description]
 */
Err
read_main_content(UUID main_content_id, int block_id, MainContent *main_content)
{
    Err error_code;
    bson_t key;
    bson_t db_result;
    bool bson_status;

    bson_init(&key);
    bson_init(&db_result);

    bson_status = bson_append_bin(&key, "the_id", -1, main_content_id, UUIDLEN);
    if (!bson_status) {
        bson_destroy(&key);
        bson_destroy(&db_result);
        return S_ERR;
    }

    bson_status = bson_append_int32(&key, "block_id", -1, block_id);
    if (!bson_status) {
        bson_destroy(&key);
        bson_destroy(&db_result);
        return S_ERR;
    }

    error_code = db_find_one(MONGO_MAIN_CONTENT, &key, NULL, &db_result);
    if (error_code) {
        bson_destroy(&key);
        bson_destroy(&db_result);
        return error_code;
    }

    error_code = _deserialize_main_content_block_bson(&db_result, main_content);
    if (error_code) {
        bson_destroy(&key);
        bson_destroy(&db_result);
        return error_code;
    }

    bson_destroy(&key);
    bson_destroy(&db_result);
    return S_OK;
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
    Err error_code;
    bool bson_status;

    bson_t key;
    bson_init(&key);

    bson_t val;
    bson_init(&val);

    bson_status = bson_append_bin(&key, "the_id", -1, main_id, UUIDLEN);
    if (!bson_status) {
        bson_destroy(&key);
        bson_destroy(&val);
        return S_ERR;
    }

    bson_status = bson_append_bin(&val, "status_updater", -1, updater, IDLEN);
    if (!bson_status) {
        bson_destroy(&key);
        bson_destroy(&val);
        return S_ERR;
    }

    bson_status = bson_append_int32(&val, "status", -1, LIVE_STATUS_DELETED);
    if (!bson_status) {
        bson_destroy(&key);
        bson_destroy(&val);
        return S_ERR;
    }

    bson_status = bson_append_bin(&val, "status_update_ip", -1, ip, IPV4LEN);
    if (!bson_status) {
        bson_destroy(&key);
        bson_destroy(&val);
        return S_ERR;
    }

    error_code = db_update_one(MONGO_MAIN, &key, &val, true);
    if (error_code) {
        bson_destroy(&key);
        bson_destroy(&val);
        return error_code;
    }

    bson_destroy(&key);
    bson_destroy(&val);

    return S_OK;
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
    Err error_code;
    bool bson_status;

    bson_t key;
    bson_init(&key);

    bson_t val;
    bson_init(&val);

    bson_status = bson_append_int64(&key, "aid", -1, aid);
    if (!bson_status) {
        bson_destroy(&key);
        bson_destroy(&val);
        return S_ERR;
    }

    bson_status = bson_append_bin(&val, "status_updater", -1, updater, IDLEN);
    if (!bson_status) {
        bson_destroy(&key);
        bson_destroy(&val);
        return S_ERR;
    }

    bson_status = bson_append_int32(&val, "status", -1, LIVE_STATUS_DELETED);
    if (!bson_status) {
        bson_destroy(&key);
        bson_destroy(&val);
        return S_ERR;
    }

    bson_status = bson_append_bin(&val, "status_update_ip", -1, ip, IPV4LEN);
    if (!bson_status) {
        bson_destroy(&key);
        bson_destroy(&val);
        return S_ERR;
    }

    error_code = db_update_one(MONGO_MAIN, &key, &val, true);
    if (error_code) {
        bson_destroy(&key);
        bson_destroy(&val);
        return error_code;
    }

    bson_destroy(&key);
    bson_destroy(&val);

    return S_OK;
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
    error_code = _split_main_contents(fd_content, len, main_id, content_id, &n_line, &n_block);
    if (error_code) {
        return error_code;
    }

    // db-main
    bson_t main_id_bson;
    bson_t main_bson;
    bson_init(&main_id_bson);
    bson_init(&main_bson);

    error_code = _serialize_uuid_bson(main_id, MONGO_THE_ID, &main_id_bson);
    if (error_code) {
        bson_destroy(&main_id_bson);
        bson_destroy(&main_bson);
        return error_code;
    }

    // update: content_id, update_content_id, updater, update_ip, update_milli_timestamp, n_total_line, n_total_block, len_total
    error_code = _serialize_update_main_bson(content_id, updater, update_ip, update_milli_timestamp, n_line, n_block, len, &main_bson);
    if (error_code) {
        bson_destroy(&main_id_bson);
        bson_destroy(&main_bson);
        return error_code;
    }

    error_code = db_update_one(MONGO_MAIN, &main_id_bson, &main_bson, false);
    if (error_code) {
        bson_destroy(&main_bson);
        bson_destroy(&main_id_bson);
        return error_code;
    }

    bson_destroy(&main_bson);
    bson_destroy(&main_id_bson);
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
_serialize_main_bson(MainHeader *main_header, bson_t *main_bson)
{
    bool bson_status;

    bson_status = bson_append_int32(main_bson, "version", -1, main_header->version);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_bin(main_bson, "the_id", -1, main_header->the_id, UUIDLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_bin(main_bson, "content_id", -1, main_header->content_id, UUIDLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_bin(main_bson, "update_content_id", -1, main_header->update_content_id, UUIDLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int64(main_bson, "aid", -1, main_header->aid);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int32(main_bson, "status", -1, main_header->status);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_bin(main_bson, "status_updater", -1, main_header->status_updater, IDLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_bin(main_bson, "status_update_ip", -1, main_header->status_update_ip, IPV4LEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_bin(main_bson, "title", -1, main_header->title, TTLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_bin(main_bson, "poster", -1, main_header->poster, IDLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_bin(main_bson, "ip", -1, main_header->ip, IPV4LEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int64(main_bson, "create_milli_timestamp", -1, main_header->create_milli_timestamp);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_bin(main_bson, "updater", -1, main_header->updater, IDLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_bin(main_bson, "update_ip", -1, main_header->update_ip, IPV4LEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int64(main_bson, "update_milli_timestamp", -1, main_header->update_milli_timestamp);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_bin(main_bson, "origin", -1, main_header->origin, strlen(main_header->origin));
    if (!bson_status) return S_ERR;

    bson_status = bson_append_bin(main_bson, "web_link", -1, main_header->web_link, strlen(main_header->web_link));
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
    Err error_code;
    error_code = bson_get_value_int32(main_bson, "version", &main_header->version);
    if (error_code) return error_code;

    int len;
    error_code = bson_get_value_bin(main_bson, "the_id", UUIDLEN, main_header->the_id, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "content_id", UUIDLEN, main_header->content_id, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "update_content_id", UUIDLEN, main_header->update_content_id, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_int64(main_bson, "aid", &main_header->aid);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(main_bson, "status", &main_header->status);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "status_updater", IDLEN, main_header->status_updater, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "status_update_ip", IPV4LEN, main_header->status_update_ip, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "title", TTLEN, main_header->title, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "poster", IDLEN, main_header->poster, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "ip", IPV4LEN, main_header->ip, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_int64(main_bson, "create_milli_timestamp", &main_header->create_milli_timestamp);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "updater", IDLEN, main_header->updater, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_bson, "update_ip", IPV4LEN, main_header->update_ip, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_int64(main_bson, "update_milli_timestamp", &main_header->update_milli_timestamp);
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
_serialize_update_main_bson(UUID content_id, char *updater, char *update_ip, time64_t update_milli_timestamp, int n_total_line, int n_total_block, int len_total, bson_t *main_bson)
{
    bool bson_status;
    bson_status = bson_append_bin(main_bson, "content_id", -1, content_id, UUIDLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_bin(main_bson, "updater", -1, updater, IDLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_bin(main_bson, "update_ip", -1, update_ip, IPV4LEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int64(main_bson, "update_milli_timestamp", -1, update_milli_timestamp);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int32(main_bson, "n_total_line", -1, n_total_line);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int32(main_bson, "n_total_block", -1, n_total_block);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int32(main_bson, "len_total", -1, len_total);
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
_serialize_main_content_block_bson(MainContent *main_content_block, bson_t *main_content_block_bson)
{
    bool bson_status;

    bson_status = bson_append_bin(main_content_block_bson, "the_id", -1, main_content_block->the_id, UUIDLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_bin(main_content_block_bson, "main_id", -1, main_content_block->main_id, UUIDLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int32(main_content_block_bson, "block_id", -1, main_content_block->block_id);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int32(main_content_block_bson, "len_block", -1, main_content_block->len_block);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int32(main_content_block_bson, "n_line", -1, main_content_block->n_line);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_bin(main_content_block_bson, "buf_block", -1, main_content_block->buf_block, main_content_block->len_block);
    if (!bson_status) return S_ERR;

    return S_OK;
}

/**
 * @brief Deserialize bson to main-content-block
 * @details [long description]
 *
 * @param main_content_block_bson [description]
 * @param main_content_block [description]
 *
 * @return [description]
 */
Err
_deserialize_main_content_block_bson(bson_t *main_content_block_bson, MainContent *main_content_block)
{
    Err error_code;

    int len;
    error_code = bson_get_value_bin(main_content_block_bson, "the_id", UUIDLEN, main_content_block->the_id, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_content_block_bson, "main_id", UUIDLEN, main_content_block->main_id, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(main_content_block_bson, "block_id", &main_content_block->block_id);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(main_content_block_bson, "len_block", &main_content_block->len_block);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(main_content_block_bson, "n_line", &main_content_block->n_line);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(main_content_block_bson, "buf_block", MAX_BUF_SIZE, main_content_block->buf_block, &len);
    if (error_code) return error_code;

    return S_OK;
}
