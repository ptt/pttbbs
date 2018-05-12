#include "cmpttdb/pttdb_comment_reply.h"
#include "cmpttdb/pttdb_comment_reply_private.h"

Err
create_comment_reply_from_fd(UUID main_id, UUID comment_id, char *poster, char *ip, int len, int fd_content, UUID comment_reply_id, time64_t create_milli_timestamp)
{
    Err error_code = S_OK;

    if (!create_milli_timestamp) {
        error_code = get_milli_timestamp(&create_milli_timestamp);
        if (error_code) return error_code;
    }

    error_code = gen_uuid_with_db(MONGO_COMMENT_REPLY, comment_reply_id, create_milli_timestamp);
    if (error_code) return error_code;

    // db-comment-reply-block
    int n_total_line = 0;
    int n_block = 0;
    if (!error_code) {
        error_code = split_contents_from_fd(fd_content, len, comment_id, comment_reply_id, MONGO_COMMENT_REPLY_BLOCK, &n_total_line, &n_block);
    }

    if(!error_code) {
        error_code = _create_comment_reply_core(main_id, comment_id, poster, ip, len, comment_reply_id, create_milli_timestamp, n_total_line, n_block);
    }

    return error_code;
}

Err
create_comment_reply(UUID main_id, UUID comment_id, char *poster, char *ip, int len, char *content, UUID comment_reply_id, time64_t create_milli_timestamp)
{
    Err error_code = S_OK;

    if (!create_milli_timestamp) {
        error_code = get_milli_timestamp(&create_milli_timestamp);
        if (error_code) return error_code;
    }

    error_code = gen_uuid_with_db(MONGO_COMMENT_REPLY, comment_reply_id, create_milli_timestamp);
    if (error_code) return error_code;

    // db-comment-reply-block
    int n_total_line = 0;
    int n_block = 0;
    if (!error_code) {
        error_code = split_contents(content, len, comment_id, comment_reply_id, MONGO_COMMENT_REPLY_BLOCK, &n_total_line, &n_block);
    }

    if(!error_code) {
        error_code = _create_comment_reply_core(main_id, comment_id, poster, ip, len, comment_reply_id, create_milli_timestamp, n_total_line, n_block);
    }

    return error_code;
}

Err
create_comment_reply_from_content_block_infos(UUID main_id, UUID comment_id, char *poster, char *ip, UUID orig_comment_reply_id, int n_orig_comment_reply_block, ContentBlockInfo *content_blocks, UUID comment_reply_id, time64_t create_milli_timestamp)
{
    Err error_code = S_OK;

    if (!create_milli_timestamp) {
        error_code = get_milli_timestamp(&create_milli_timestamp);
        if (error_code) return error_code;
    }

    error_code = gen_uuid_with_db(MONGO_COMMENT_REPLY, comment_reply_id, create_milli_timestamp);
    if (error_code) return error_code;    

    int len = 0;
    int n_total_line = 0;
    int n_block = 0;
    error_code = construct_contents_from_content_block_infos(
        main_id,
        PTTDB_CONTENT_TYPE_COMMENT_REPLY,
        comment_id,
        orig_comment_reply_id,
        MONGO_COMMENT_REPLY_BLOCK,
        n_orig_comment_reply_block,
        content_blocks,
        create_milli_timestamp,
        comment_reply_id,
        &n_total_line,
        &n_block,
        &len);

    if(!error_code) {
        error_code = _create_comment_reply_core(main_id, comment_id, poster, ip, len, comment_reply_id, create_milli_timestamp, n_total_line, n_block);
    };

    return error_code;
}


Err
_create_comment_reply_core(UUID main_id, UUID comment_id, char *poster, char *ip, int len, UUID comment_reply_id, time64_t create_milli_timestamp, int n_total_line, int n_block)
{
    Err error_code = S_OK;

    CommentReply comment_reply = {};

    memcpy(comment_reply.the_id, comment_reply_id, sizeof(UUID));
    memcpy(comment_reply.comment_id, comment_id, sizeof(UUID));
    memcpy(comment_reply.main_id, main_id, sizeof(UUID));
    comment_reply.status = LIVE_STATUS_ALIVE;
    strcpy(comment_reply.status_updater, poster);
    strcpy(comment_reply.status_update_ip, ip);

    strcpy(comment_reply.poster, poster);
    strcpy(comment_reply.ip, ip);
    comment_reply.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment_reply.updater, poster);
    strcpy(comment_reply.update_ip, ip);
    comment_reply.update_milli_timestamp = create_milli_timestamp;

    comment_reply.n_total_line = n_total_line;
    comment_reply.n_block = n_block;
    comment_reply.len_total = len;

    // use associate to associate content directly

    ContentBlock content_block = {};
    error_code = init_content_block_buf_block(&content_block);

    if(!error_code) {
        error_code = read_content_block(comment_reply.the_id, 0, MONGO_COMMENT_REPLY_BLOCK, &content_block);
    }

    if(!error_code) {
        error_code = associate_comment_reply(&comment_reply, content_block.buf_block, content_block.len_block);
    }

    if(!error_code) {
        comment_reply.len = content_block.len_block;
        comment_reply.n_line = content_block.n_line;
    }

    // db-comment-reply
    bson_t *comment_reply_id_bson = NULL;
    bson_t *comment_reply_bson = NULL;

    if (!error_code) {
        error_code = serialize_comment_reply_bson(&comment_reply, &comment_reply_bson);
    }

    if (!error_code) {
        error_code = serialize_uuid_bson(comment_reply_id, &comment_reply_id_bson);
    }

    if (!error_code) {
        error_code = db_update_one(MONGO_COMMENT_REPLY, comment_reply_id_bson, comment_reply_bson, true);
    }

    // db-comment
    if (!error_code) {
        error_code = update_comment_reply_to_comment(comment_id, comment_reply_id, comment_reply.n_line, comment_reply.n_block, comment_reply.n_total_line);
    }

    // free
    bson_safe_destroy(&comment_reply_bson);
    bson_safe_destroy(&comment_reply_id_bson);
    dissociate_comment_reply(&comment_reply);
    destroy_content_block(&content_block);

    return error_code;
}

Err
_construct_comment_reply_blocks(CommentReply *comment_reply, char *content, int len, ContentBlock **content_blocks, int *n_content_block)
{
    Err error_code = S_OK;
    int tmp_n_content_block = len / MAX_BUF_SIZE;
    if(tmp_n_content_block * MAX_BUF_SIZE != len) tmp_n_content_block++;

    *n_content_block = tmp_n_content_block;
    *content_blocks = malloc(sizeof(ContentBlock) * tmp_n_content_block);
    ContentBlock *p_content_block = *content_blocks;
    bzero(p_content_block, sizeof(ContentBlock) * tmp_n_content_block);

    char *p_content = content;
    int len_block = 0;    
    for(int i = 0; i < tmp_n_content_block; i++, p_content_block++, p_content += len_block, len -= len_block) {
        len_block = len < MAX_BUF_SIZE ? len : MAX_BUF_SIZE;

        error_code = pttdb_count_lines(p_content, len_block, &p_content_block->n_line);
        if(error_code) break;

        memcpy(p_content_block->the_id, comment_reply->the_id, UUIDLEN);
        memcpy(p_content_block->ref_id, comment_reply->comment_id, UUIDLEN);
        p_content_block->block_id = i;
        p_content_block->len_block = len_block;
        p_content_block->max_buf_len = len_block;
        p_content_block->buf_block = p_content;

    }

    return error_code;
}

Err
get_comment_reply_info_by_main(UUID main_id, int *n_comment_reply, int *n_line, int *total_len)
{
    Err error_code = S_OK;
    bson_t *pipeline = BCON_NEW(
                           "pipeline", "[",
                           "{",
                           "$match", "{",
                           "main_id", BCON_BINARY(main_id, UUIDLEN),
                           "status", BCON_INT32((int)LIVE_STATUS_ALIVE),
                           "}",
                           "}",
                           "{",
                           "$group", "{",
                           "_id", BCON_NULL,
                           "count", "{",
                           "$sum", BCON_INT32(1),
                           "}",
                           "n_line", "{",
                           "$sum", "$n_line",
                           "}",
                           "len", "{",
                           "$sum", "$len",
                           "}",
                           "}",
                           "}",
                           "]"
                       );

    if (pipeline == NULL) error_code = S_ERR;

    bson_t *result = NULL;

    int n_result = 0;
    if (!error_code) {
        error_code = db_aggregate(MONGO_COMMENT_REPLY, pipeline, 1, &result, &n_result);
    }

    if (!error_code) {
        error_code = _get_comment_reply_info_by_main_deal_with_result(result, n_result, n_comment_reply, n_line, total_len);
    }

    bson_safe_destroy(&result);
    bson_safe_destroy(&pipeline);

    return error_code;
}

Err
read_comment_reply(UUID comment_reply_id, CommentReply *comment_reply)
{
    Err error_code = S_OK;
    bson_t *key = BCON_NEW(
                      "the_id", BCON_BINARY(comment_reply_id, UUIDLEN)
                  );

    bson_t *db_result = NULL;
    error_code = db_find_one(MONGO_COMMENT_REPLY, key, NULL, &db_result);

    if (!error_code) {
        error_code = deserialize_comment_reply_bson(db_result, comment_reply);
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
delete_comment_reply(UUID comment_reply_id, char *updater, char *ip) {
    Err error_code = S_OK;

    bson_t *key = BCON_NEW(
                      "the_id", BCON_BINARY(comment_reply_id, UUIDLEN)
                  );

    bson_t *val = BCON_NEW(
                      "status_updater", BCON_BINARY((unsigned char *)updater, IDLEN),
                      "status", BCON_INT32((int)LIVE_STATUS_DELETED),
                      "status_update_ip", BCON_BINARY((unsigned char *)ip, IPV4LEN)
                  );

    error_code = db_update_one(MONGO_COMMENT_REPLY, key, val, true);

    bson_t *fields = BCON_NEW(
                         "_id", BCON_BOOL(false),
                         "comment_id", BCON_BOOL(true)
                     );
    bson_t *b_comment_reply = NULL;

    if (!error_code) {
        error_code = db_find_one(MONGO_COMMENT_REPLY, key, fields, &b_comment_reply);
    }

    int len = 0;
    UUID comment_id = {};
    if (!error_code) {
        error_code = bson_get_value_bin(b_comment_reply, "comment_id", UUIDLEN, (char *)comment_id, &len);
    }

    if (!error_code) {
        error_code = remove_comment_reply_from_comment(comment_id, comment_reply_id);
    }

    bson_safe_destroy(&key);
    bson_safe_destroy(&val);
    bson_safe_destroy(&fields);
    bson_safe_destroy(&b_comment_reply);
    return error_code;
}


Err
init_comment_reply_buf(CommentReply *comment_reply)
{
    if (comment_reply->buf != NULL) return S_OK;

    comment_reply->buf = malloc(MAX_BUF_SIZE);
    if (comment_reply->buf == NULL) return S_ERR;

    comment_reply->max_buf_len = MAX_BUF_SIZE;
    bzero(comment_reply->buf, MAX_BUF_SIZE);
    comment_reply->len = 0;
    comment_reply->n_line = 0;

    return S_OK;
}

Err
destroy_comment_reply(CommentReply *comment_reply)
{
    if (comment_reply->buf == NULL) return S_OK;

    free(comment_reply->buf);
    comment_reply->buf = NULL;
    comment_reply->max_buf_len = 0;
    comment_reply->len = 0;
    comment_reply->n_line = 0;

    return S_OK;
}

Err
associate_comment_reply(CommentReply *comment_reply, char *buf, int max_buf_len)
{
    if (comment_reply->buf != NULL) return S_ERR;

    comment_reply->buf = buf;
    comment_reply->max_buf_len = max_buf_len;
    comment_reply->len = 0;
    comment_reply->n_line = 0;

    return S_OK;
}

Err
dissociate_comment_reply(CommentReply *comment_reply)
{
    if (comment_reply->buf == NULL) return S_OK;

    comment_reply->buf = NULL;
    comment_reply->max_buf_len = 0;
    comment_reply->len = 0;
    comment_reply->n_line = 0;

    return S_OK;
}

Err
read_comment_replys_by_query_to_bsons(bson_t *query, bson_t *fields, int max_n_comment_replys, bson_t **b_comment_replys, int *n_comment_reply)
{
    Err error_code = db_find(MONGO_COMMENT_REPLY, query, fields, NULL, max_n_comment_replys, n_comment_reply, b_comment_replys);

    return error_code;
}

Err
_get_comment_reply_info_by_main_deal_with_result(bson_t *result, int n_result, int *n_comment_reply, int *n_line, int *total_len)
{
    if (!n_result) {
        *n_comment_reply = 0;
        *n_line = 0;
        *total_len = 0;
        return S_OK;
    }

    Err error_code = S_OK;

    error_code = bson_get_value_int32(result, "count", n_comment_reply);

    if (!error_code) {
        error_code = bson_get_value_int32(result, "n_line", n_line);
    }

    if (!error_code) {
        error_code = bson_get_value_int32(result, "len", total_len);
    }

    return error_code;
}

/**
 * @brief [brief description]
 * @details [long description]
 *
 * @param comment [description]
 * @param comment_bson [description]
 *
 * @return [description]
 */
Err
serialize_comment_reply_bson(CommentReply *comment_reply, bson_t **comment_reply_bson)
{
    *comment_reply_bson = BCON_NEW(
                              "version", BCON_INT32(comment_reply->version),
                              "the_id", BCON_BINARY(comment_reply->the_id, UUIDLEN),
                              "comment_id", BCON_BINARY(comment_reply->comment_id, UUIDLEN),
                              "main_id", BCON_BINARY(comment_reply->main_id, UUIDLEN),
                              "status", BCON_INT32(comment_reply->status),
                              "status_updater", BCON_BINARY((unsigned char *)comment_reply->status_updater, IDLEN),
                              "status_update_ip", BCON_BINARY((unsigned char *)comment_reply->status_update_ip, IPV4LEN),
                              "poster", BCON_BINARY((unsigned char *)comment_reply->poster, IDLEN),
                              "ip", BCON_BINARY((unsigned char *)comment_reply->ip, IPV4LEN),
                              "create_milli_timestamp", BCON_INT64(comment_reply->create_milli_timestamp),
                              "updater", BCON_BINARY((unsigned char *)comment_reply->updater, IDLEN),
                              "update_ip", BCON_BINARY((unsigned char *)comment_reply->update_ip, IPV4LEN),
                              "update_milli_timestamp", BCON_INT64(comment_reply->update_milli_timestamp),
                              "n_block", BCON_INT32(comment_reply->n_block),
                              "len_total", BCON_INT32(comment_reply->len_total),
                              "n_total_line", BCON_INT32(comment_reply->n_total_line),
                              "len", BCON_INT32(comment_reply->len),
                              "n_line", BCON_INT32(comment_reply->n_line),
                              "buf", BCON_BINARY((unsigned char *)comment_reply->buf, comment_reply->len)
                          );

    return S_OK;
}

/**
 * @brief [brief description]
 * @details [long description]
 *
 * @param comment_bson [description]
 * @param comment [description]
 *
 * @return [description]
 */
Err
deserialize_comment_reply_bson(bson_t *comment_reply_bson, CommentReply *comment_reply)
{
    Err error_code;

    int len;
    error_code = bson_get_value_int32(comment_reply_bson, "version", (int *)&comment_reply->version);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_reply_bson, "the_id", UUIDLEN, (char *)comment_reply->the_id, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_reply_bson, "comment_id", UUIDLEN, (char *)comment_reply->comment_id, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_reply_bson, "main_id", UUIDLEN, (char *)comment_reply->main_id, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(comment_reply_bson, "status", (int *)&comment_reply->status);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_reply_bson, "status_updater", IDLEN, comment_reply->status_updater, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_reply_bson, "status_update_ip", IPV4LEN, comment_reply->status_update_ip, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_reply_bson, "poster", IDLEN, comment_reply->poster, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_reply_bson, "ip", IPV4LEN, comment_reply->ip, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_int64(comment_reply_bson, "create_milli_timestamp", (long *)&comment_reply->create_milli_timestamp);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_reply_bson, "updater", IDLEN, comment_reply->updater, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_reply_bson, "update_ip", IPV4LEN, comment_reply->update_ip, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_int64(comment_reply_bson, "update_milli_timestamp", (long *)&comment_reply->update_milli_timestamp);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(comment_reply_bson, "n_block", &comment_reply->n_block);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(comment_reply_bson, "len_total", &comment_reply->len_total);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(comment_reply_bson, "n_total_line", &comment_reply->n_total_line);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(comment_reply_bson, "len", &comment_reply->len);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(comment_reply_bson, "n_line", &comment_reply->n_line);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_reply_bson, "buf", comment_reply->max_buf_len, comment_reply->buf, &len);
    if (error_code) return error_code;

    return S_OK;
}

/**
 * @brief [brief description]
 * @details [long description]
 *
 * @param comment_bson [description]
 * @param comment [description]
 *
 * @return [description]
 */
Err
deserialize_comment_reply_bson_with_buf(bson_t *comment_reply_bson, CommentReply *comment_reply)
{
    Err error_code = S_OK;
    if (comment_reply->buf) return S_ERR;

    int len = 0;
    error_code = bson_get_value_int32(comment_reply_bson, "len", &len);
    if (error_code) return error_code;

    comment_reply->buf = malloc(len);
    comment_reply->max_buf_len = len;
    comment_reply->len = 0;
    comment_reply->n_line = 0;

    return deserialize_comment_reply_bson(comment_reply_bson, comment_reply);
}
