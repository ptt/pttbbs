#include "cmpttdb/pttdb_comment.h"
#include "cmpttdb/pttdb_comment_private.h"

char *_read_comments_op_type[] = {
    "$lt",
    "$lte",
    "$gt",
    "$gte",
};

/**
 * @brief [brief description]
 * @details [long description]
 *
 * @param main_id [description]
 * @param poster [description]
 * @param ip [description]
 * @param len [description]
 * @param content [description]
 * @param comment_id [description]
 */
Err
create_comment(UUID main_id, char *poster, char *ip, int len, char *content, enum CommentType comment_type, UUID comment_id, time64_t create_milli_timestamp)
{
    Err error_code = S_OK;

    // use associate to associate content directly
    Comment comment = {};
    associate_comment(&comment, content, len);
    comment.len = len;

    if (!create_milli_timestamp) {
        error_code = get_milli_timestamp(&create_milli_timestamp);
        if (error_code) return error_code;
    }

    error_code = gen_uuid_with_db(MONGO_COMMENT, comment_id, create_milli_timestamp);
    if (error_code) return error_code;

    // comment
    memcpy(comment.the_id, comment_id, sizeof(UUID));
    memcpy(comment.main_id, main_id, sizeof(UUID));
    comment.status = LIVE_STATUS_ALIVE;
    strcpy(comment.status_updater, poster);
    strcpy(comment.status_update_ip, ip);

    comment.comment_type = comment_type;
    comment.karma = KARMA_BY_COMMENT_TYPE[comment_type];

    strcpy(comment.poster, poster);
    strcpy(comment.ip, ip);
    comment.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment.updater, poster);
    strcpy(comment.update_ip, ip);
    comment.update_milli_timestamp = create_milli_timestamp;

    // db-comment
    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    error_code = serialize_comment_bson(&comment, &comment_bson);
    if (!error_code) {
        error_code = serialize_uuid_bson(comment_id, &comment_id_bson);
    }

    if (!error_code) {
        error_code = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);
    }

    bson_safe_destroy(&comment_bson);
    bson_safe_destroy(&comment_id_bson);
    dissociate_comment(&comment);

    return error_code;
}

Err
read_comment(UUID comment_id, Comment *comment)
{
    Err error_code = S_OK;
    bson_t *key = BCON_NEW(
        "the_id", BCON_BINARY(comment_id, UUIDLEN)
        );

    bson_t *db_result = NULL;
    error_code = db_find_one(MONGO_COMMENT, key, NULL, &db_result);

    if (!error_code) {
        error_code = deserialize_comment_bson(db_result, comment);
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
delete_comment(UUID comment_id, char *updater, char *ip) {
    Err error_code = S_OK;

    bson_t *key = BCON_NEW(
        "the_id", BCON_BINARY(comment_id, UUIDLEN)
    );

    bson_t *val = BCON_NEW(
        "status_updater", BCON_BINARY((unsigned char *)updater, IDLEN),
        "status", BCON_INT32((int)LIVE_STATUS_DELETED),
        "status_update_ip", BCON_BINARY((unsigned char *)ip, IPV4LEN)
        );

    error_code = db_update_one(MONGO_COMMENT, key, val, true);

    bson_safe_destroy(&key);
    bson_safe_destroy(&val);

    return error_code;
}

Err
get_comment_info_by_main(UUID main_id, int *n_total_comment, int *total_len)
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
        error_code = db_aggregate(MONGO_COMMENT, pipeline, 1, &result, &n_result);
    }

    if (!error_code) {
        error_code = _get_comment_info_by_main_deal_with_result(result, n_result, n_total_comment, total_len);
    }

    bson_safe_destroy(&result);
    bson_safe_destroy(&pipeline);

    return error_code;
}

Err
get_comment_count_by_main(UUID main_id, int *count)
{
    Err error_code = S_OK;
    bson_t *key = BCON_NEW(
        "main_id", BCON_BINARY(main_id, UUIDLEN),
        "status", BCON_INT32((int)LIVE_STATUS_ALIVE)
        );

    error_code = db_count(MONGO_COMMENT, key, count);

    bson_safe_destroy(&key);

    return error_code;
}

Err
init_comment_buf(Comment *comment)
{
    if (comment->buf != NULL) return S_OK;

    comment->buf = malloc(MAX_BUF_COMMENT);
    if (comment->buf == NULL) return S_ERR;

    comment->max_buf_len = MAX_BUF_COMMENT;
    bzero(comment->buf, MAX_BUF_COMMENT);
    comment->len = 0;

    return S_OK;
}

Err
destroy_comment(Comment *comment)
{
    if (comment->buf == NULL) return S_OK;

    free(comment->buf);
    comment->buf = NULL;
    comment->max_buf_len = 0;
    comment->len = 0;

    return S_OK;
}

Err
associate_comment(Comment *comment, char *buf, int max_buf_len)
{
    if (comment->buf != NULL) return S_ERR;

    comment->buf = buf;
    comment->max_buf_len = max_buf_len;
    comment->len = 0;

    return S_OK;
}

Err
dissociate_comment(Comment *comment)
{
    if (comment->buf == NULL) return S_OK;

    comment->buf = NULL;
    comment->max_buf_len = 0;
    comment->len = 0;

    return S_OK;
}

Err
read_comments_by_main(UUID main_id, time64_t create_milli_timestamp, char *poster, enum ReadCommentsOpType op_type, int max_n_comment, enum MongoDBId mongo_db_id, Comment *comments, int *n_comment, int *len)
{

    Err error_code = S_OK;

    // init db-results
    bson_t **b_comments = malloc(sizeof(bson_t *) * max_n_comment);
    if (b_comments == NULL) return S_ERR_INIT;
    bzero(b_comments, sizeof(bson_t *) * max_n_comment);

    error_code = _read_comments_get_b_comments(b_comments, main_id, create_milli_timestamp, poster, op_type, max_n_comment, mongo_db_id, n_comment);

    int tmp_n_comment = *n_comment;
    bson_t **p_b_comments = b_comments;
    Comment *p_comments = comments;

    int tmp_len = 0;
    if (!error_code) {
        for (int i = 0; i < tmp_n_comment; i++) {
            error_code = deserialize_comment_bson(*p_b_comments, p_comments);

            tmp_len += p_comments->len;
            p_b_comments++;
            p_comments++;

            if (error_code) {
                *n_comment = i;
                break;
            }
        }
    }

    *len = tmp_len;

    // free
    safe_free_b_list(&b_comments, tmp_n_comment);

    return error_code;
}

Err
update_comment_reply_to_comment(UUID comment_id, UUID comment_reply_id, int n_comment_reply_line, int n_comment_reply_block, int n_comment_reply_total_line)
{
    Err error_code = S_OK;
    bson_t *key = BCON_NEW(
        "the_id", BCON_BINARY(comment_id, UUIDLEN)
        );

    bson_t *val = BCON_NEW(
        "comment_reply_id", BCON_BINARY(comment_reply_id, UUIDLEN),
        "n_comment_reply_line", BCON_INT32(n_comment_reply_line),
        "n_comment_reply_block", BCON_INT32(n_comment_reply_block),
        "n_comment_reply_total_line", BCON_INT32(n_comment_reply_total_line)
        );

    error_code = db_update_one(MONGO_COMMENT, key, val, false);

    bson_safe_destroy(&key);
    bson_safe_destroy(&val);

    return error_code;
}

Err
remove_comment_reply_from_comment(UUID comment_id, UUID comment_reply_id)
{
    Err error_code = S_OK;
    bson_t *key = BCON_NEW(
        "the_id", BCON_BINARY(comment_id, UUIDLEN),
        "comment_reply_id", BCON_BINARY(comment_reply_id, UUIDLEN)
        );

    UUID empty_id = {};
    bson_t *val = BCON_NEW(
        "comment_reply_id", BCON_BINARY(empty_id, UUIDLEN),
        "n_comment_reply_line", BCON_INT32(0),
        "n_comment_reply_block", BCON_INT32(0),
        "n_comment_reply_total_line", BCON_INT32(0)
        );

    error_code = db_update_one(MONGO_COMMENT, key, val, false);

    bson_safe_destroy(&key);
    bson_safe_destroy(&val);

    return error_code;
}


Err
get_newest_comment(UUID main_id, UUID comment_id, time64_t *create_milli_timestamp, char *poster, int *n_comment)
{
    Err error_code = S_OK;
    bson_t *key = BCON_NEW(
        "main_id", BCON_BINARY(main_id, UUIDLEN),
        "status", BCON_INT32((int)LIVE_STATUS_ALIVE),
        "create_milli_timestamp", "{",
            "$lt", BCON_INT64(MAX_CREATE_MILLI_TIMESTAMP),
        "}"
        );

    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "the_id", BCON_BOOL(true),
        "create_milli_timestamp", BCON_BOOL(true),
        "poster", BCON_BOOL(true)
        );

    bson_t *sort = BCON_NEW(
        "create_milli_timestamp", BCON_INT32(-1),
        "poster", BCON_INT32(-1)
        );

    bson_t *result = NULL;
    int len = 0;
    error_code = db_find(MONGO_COMMENT, key, fields, sort, 1, &len, &result);
    if (!error_code && !len) {
        error_code = S_ERR_NOT_EXISTS;
    }
    if (!error_code) {
        error_code = bson_get_value_bin(result, "the_id", UUIDLEN, (char *)comment_id, &len);
    }
    if (!error_code) {
        error_code = bson_get_value_int64(result, "create_milli_timestamp", (long *)create_milli_timestamp);
    }
    if (!error_code) {
        error_code = bson_get_value_bin(result, "poster", IDLEN, poster, &len);
    }

    int count_eq_create_milli_timestamp = 0;

    bson_t *count_query_eq = BCON_NEW(
        "main_id", BCON_BINARY(main_id, UUIDLEN),
        "status", BCON_INT32((int)LIVE_STATUS_ALIVE),
        "create_milli_timestamp", BCON_INT64(*create_milli_timestamp),
        "poster", "{",
            "$lte", BCON_BINARY((unsigned char *)poster, IDLEN),
        "}"
        );

    if (!error_code) {
        error_code = db_count(MONGO_COMMENT, count_query_eq, &count_eq_create_milli_timestamp);
    }

    int count_lt_create_milli_timestamp = 0;
    bson_t *count_query_lt = BCON_NEW(
        "main_id", BCON_BINARY(main_id, UUIDLEN),
        "status", BCON_INT32((int)LIVE_STATUS_ALIVE),
        "create_milli_timestamp", "{",
            "$lt", BCON_INT64(*create_milli_timestamp),
        "}"
        );

    if (!error_code) {
        error_code = db_count(MONGO_COMMENT, count_query_lt, &count_lt_create_milli_timestamp);
    }

    *n_comment = count_eq_create_milli_timestamp + count_lt_create_milli_timestamp;

    bson_safe_destroy(&key);
    bson_safe_destroy(&fields);
    bson_safe_destroy(&sort);
    bson_safe_destroy(&result);
    bson_safe_destroy(&count_query_lt);
    bson_safe_destroy(&count_query_eq);

    return error_code;
}

Err
read_comments_until_newest_to_bsons(UUID main_id, time64_t create_milli_timestamp, char *poster, bson_t *fields, int max_n_comment, bson_t **b_comments, int *n_comment)
{
    Err error_code = S_OK;

    bson_t *query_lt = BCON_NEW(
        "main_id", BCON_BINARY(main_id, UUIDLEN),
        "status", BCON_INT32((int)LIVE_STATUS_ALIVE),
        "create_milli_timestamp", "{",
            "$lt", BCON_INT64(create_milli_timestamp),
        "}"
        );

    bson_t **p_b_comments = b_comments;
    int n_comment_lt_create_milli_timestamp = 0;
    error_code = db_find(MONGO_COMMENT, query_lt, fields, NULL, max_n_comment, &n_comment_lt_create_milli_timestamp, p_b_comments);
    p_b_comments += n_comment_lt_create_milli_timestamp;
    max_n_comment -= n_comment_lt_create_milli_timestamp;

    bson_t *query_eq = BCON_NEW(
        "main_id", BCON_BINARY(main_id, UUIDLEN),
        "status", BCON_INT32((int)LIVE_STATUS_ALIVE),
        "create_milli_timestamp", BCON_INT64(create_milli_timestamp),
        "poster", "{",
            "$lte", BCON_BINARY((unsigned char *)poster, IDLEN),
        "}"
        );

    int n_comment_eq_create_milli_timestamp = 0;
    if (!error_code) {
        error_code = db_find(MONGO_COMMENT, query_eq, fields, NULL, max_n_comment, &n_comment_eq_create_milli_timestamp, p_b_comments);
    }

    *n_comment = n_comment_lt_create_milli_timestamp + n_comment_eq_create_milli_timestamp;

    // free
    bson_safe_destroy(&query_lt);
    bson_safe_destroy(&query_eq);

    return error_code;
}

Err
dynamic_read_b_comment_comment_reply_by_ids_to_buf(bson_t **b_comments, int n_comment, char *buf, int max_buf_size, int *n_read_comment, int *n_read_comment_reply, int *len_buf)
{
    Err error_code = S_OK;

    bson_t *q_b_comment_ids = NULL;
    bson_t *query_comment = NULL;
    bson_t *comment_fields = NULL;
    bson_t **b_comment_contents = NULL;
    DictBsonByUU dict_comment_content = {};
    int n_comment_content = 0;

    bson_t *q_b_comment_reply_ids = NULL;
    bson_t *query_comment_reply = NULL;
    bson_t *comment_reply_fields = NULL;
    bson_t **b_comment_replys = NULL;
    DictBsonByUU dict_comment_reply = {};
    int n_expected_comment_reply = 0;
    int n_expected_comment_reply_block = 0;
    int n_comment_reply = 0;

    // prepare query
    comment_fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "the_id", BCON_BOOL(true),
        "buf", BCON_BOOL(true),
        "len", BCON_BOOL(true)
        );

    if (!comment_fields) error_code = S_ERR;

    if (!error_code) {
        comment_reply_fields = BCON_NEW(
            "_id", BCON_BOOL(false),
            "the_id", BCON_BOOL(true),
            "comment_id", BCON_BOOL(true),
            "status", BCON_BOOL(true),
            "len", BCON_BOOL(true),
            "buf", BCON_BOOL(true)
            );

        if (!comment_reply_fields) error_code = S_ERR;
    }

    if (!error_code) {
        error_code = extract_b_comments_comment_id_to_bsons(b_comments, n_comment, "$in", &q_b_comment_ids);
    }

    if (!error_code) {
        query_comment = BCON_NEW(
            "the_id", BCON_DOCUMENT(q_b_comment_ids)
            );
    }


    if (!error_code) {
        b_comment_contents = malloc(sizeof(bson_t *) * n_comment);
        if (!b_comment_contents) error_code = S_ERR;
    }

    if (!error_code) {
        error_code = extract_b_comments_comment_reply_id_to_bsons(b_comments, n_comment, "$in", &q_b_comment_reply_ids, &n_expected_comment_reply, &n_expected_comment_reply_block);
    }

    if (!error_code) {
        query_comment_reply = BCON_NEW(
            "the_id", BCON_DOCUMENT(q_b_comment_reply_ids),
            "status", BCON_INT32(LIVE_STATUS_ALIVE)
            );

        if (!query_comment_reply) error_code = S_ERR;
    }

    if (!error_code) {
        b_comment_replys = malloc(sizeof(bson_t *) * n_expected_comment_reply);
        if (!b_comment_replys) error_code = S_ERR;
    }

    // query
    if (!error_code) {
        error_code = read_comments_by_query_to_bsons(query_comment, comment_fields, n_comment, b_comment_contents, &n_comment_content);
        if (n_comment != n_comment_content) error_code = S_ERR;
    }

    if (!error_code && n_expected_comment_reply) {
        error_code = read_comment_replys_by_query_to_bsons(query_comment_reply, comment_reply_fields, n_expected_comment_reply, b_comment_replys, &n_comment_reply);
        if (n_expected_comment_reply != n_comment_reply) error_code = S_ERR;
    }

    // to dict
    if (!error_code) {
        error_code = bsons_to_dict_bson_by_uu(b_comment_contents, n_comment_content, "the_id", &dict_comment_content);
    }

    if (!error_code) {
        error_code = bsons_to_dict_bson_by_uu(b_comment_replys, n_comment_reply, "comment_id", &dict_comment_reply);
    }

    // read to buf
    if (!error_code) {
        error_code = _dynamic_read_b_comment_comment_reply_by_ids_to_buf_core(b_comments, n_comment, &dict_comment_content, &dict_comment_reply, buf, max_buf_size, n_read_comment, n_read_comment_reply, len_buf);
    }

    // free
    safe_destroy_dict_bson_by_uu(&dict_comment_reply);
    safe_free_b_list(&b_comment_replys, n_comment_reply);
    bson_safe_destroy(&comment_reply_fields);
    bson_safe_destroy(&query_comment_reply);
    bson_safe_destroy(&q_b_comment_reply_ids);

    safe_destroy_dict_bson_by_uu(&dict_comment_content);
    safe_free_b_list(&b_comment_contents, n_comment_content);
    bson_safe_destroy(&comment_fields);
    bson_safe_destroy(&query_comment);
    bson_safe_destroy(&q_b_comment_ids);

    return error_code;
}

Err
dynamic_read_b_comment_comment_reply_by_ids_to_file(bson_t **b_comments, int n_comment, FILE *f, int *n_read_comment, int *n_read_comment_reply)
{
    Err error_code = S_OK;

    bson_t *q_b_comment_ids = NULL;
    bson_t *query_comment = NULL;
    bson_t *comment_fields = NULL;
    bson_t **b_comment_contents = NULL;
    DictBsonByUU dict_comment_content = {};
    int n_comment_content = 0;

    bson_t *q_b_comment_reply_ids = NULL;
    bson_t *query_comment_reply = NULL;
    bson_t *comment_reply_fields = NULL;
    bson_t **b_comment_replys = NULL;
    DictBsonByUU dict_comment_reply = {};
    int n_expected_comment_reply = 0;
    int n_expected_comment_reply_block = 0;
    int n_comment_reply = 0;

    // prepare query for comments
    comment_fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "the_id", BCON_BOOL(true),
        "buf", BCON_BOOL(true),
        "len", BCON_BOOL(true)
        );

    if (!comment_fields) error_code = S_ERR;

    if (!error_code) {
        error_code = extract_b_comments_comment_id_to_bsons(b_comments, n_comment, "$in", &q_b_comment_ids);
    }

    if (!error_code) {
        query_comment = BCON_NEW(
            "the_id", BCON_DOCUMENT(q_b_comment_ids)
            );
    }

    if (!error_code) {
        b_comment_contents = malloc(sizeof(bson_t *) * n_comment);
        if (!b_comment_contents) error_code = S_ERR;
    }

    // prepare query for comment-replys
    if (!error_code) {
        comment_reply_fields = BCON_NEW(
            "_id", BCON_BOOL(false),
            "the_id", BCON_BOOL(true),
            "comment_id", BCON_BOOL(true),
            "status", BCON_BOOL(true),
            "n_block", BCON_BOOL(true)
            );

        if (!comment_reply_fields) error_code = S_ERR;
    }

    if (!error_code) {
        error_code = extract_b_comments_comment_reply_id_to_bsons(b_comments, n_comment, "$in", &q_b_comment_reply_ids, &n_expected_comment_reply, &n_expected_comment_reply_block);
    }

    if (!error_code) {
        query_comment_reply = BCON_NEW(
            "the_id", BCON_DOCUMENT(q_b_comment_reply_ids),
            "status", BCON_INT32(LIVE_STATUS_ALIVE)
            );

        if (!query_comment_reply) error_code = S_ERR;
    }

    if (!error_code) {
        b_comment_replys = malloc(sizeof(bson_t *) * n_expected_comment_reply);
        if (!b_comment_replys) error_code = S_ERR;
    }

    // query
    if (!error_code) {
        error_code = read_comments_by_query_to_bsons(query_comment, comment_fields, n_comment, b_comment_contents, &n_comment_content);
        if (n_comment != n_comment_content) error_code = S_ERR;
    }

    if (!error_code && n_expected_comment_reply) {
        error_code = read_comment_replys_by_query_to_bsons(query_comment_reply, comment_reply_fields, n_expected_comment_reply, b_comment_replys, &n_comment_reply);
        if (n_expected_comment_reply != n_comment_reply) error_code = S_ERR;
    }

    // to dict
    if (!error_code) {
        error_code = bsons_to_dict_bson_by_uu(b_comment_contents, n_comment_content, "the_id", &dict_comment_content);
    }

    if (!error_code) {
        error_code = bsons_to_dict_bson_by_uu(b_comment_replys, n_comment_reply, "comment_id", &dict_comment_reply);
    }


    // read to buf
    if (!error_code) {
        error_code = _dynamic_read_b_comment_comment_reply_by_ids_to_file_core(b_comments, n_comment, &dict_comment_content, &dict_comment_reply, f, n_read_comment, n_read_comment_reply);
    }

    // free
    safe_destroy_dict_bson_by_uu(&dict_comment_reply);
    safe_free_b_list(&b_comment_replys, n_comment_reply);
    bson_safe_destroy(&comment_reply_fields);
    bson_safe_destroy(&query_comment_reply);
    bson_safe_destroy(&q_b_comment_reply_ids);

    safe_destroy_dict_bson_by_uu(&dict_comment_content);
    safe_free_b_list(&b_comment_contents, n_comment_content);
    bson_safe_destroy(&comment_fields);
    bson_safe_destroy(&query_comment);
    bson_safe_destroy(&q_b_comment_ids);

    return error_code;
}

Err
read_comments_by_query_to_bsons(bson_t *query, bson_t *fields, int max_n_comment, bson_t **b_comments, int *n_comment)
{
    Err error_code = db_find(MONGO_COMMENT, query, fields, NULL, max_n_comment, n_comment, b_comments);

    return error_code;
}


Err
extract_b_comments_comment_id_to_bsons(bson_t **b_comments, int n_comment, char *result_key, bson_t **b_comment_ids)
{
    bson_t child = {};
    char buf[16] = {};
    const char *array_key = NULL;
    size_t array_keylen = 0;

    Err error_code = S_OK;
    bson_t *tmp_b = bson_new();
    bson_t **p_b_comments = b_comments;

    UUID uuid = {};
    int len = 0;
    bool status = false;

    BSON_APPEND_ARRAY_BEGIN(tmp_b, result_key, &child);
    for (int i = 0; i < n_comment; i++, p_b_comments++) {
        error_code = bson_get_value_bin(*p_b_comments, "the_id", UUIDLEN, (char *)uuid, &len);
        if (error_code) break;

        array_keylen = bson_uint32_to_string(i, &array_key, buf, sizeof(buf));

        status = bson_append_bin(&child, array_key, (int)array_keylen, uuid, UUIDLEN);
        if (!status) {
            error_code = S_ERR;
            break;
        }
    }
    bson_append_array_end(tmp_b, &child);

    *b_comment_ids = tmp_b;

    return error_code;
}

Err
extract_b_comments_comment_reply_id_to_bsons(bson_t **b_comments, int n_comment, char *result_key, bson_t **b_comment_reply_ids, int *n_comment_reply, int *n_comment_reply_block)
{
    bson_t child;
    char buf[16];
    const char *array_key;
    size_t array_keylen;

    Err error_code = S_OK;
    *b_comment_reply_ids = bson_new();
    bson_t *tmp_b = *b_comment_reply_ids;
    bson_t **p_b_comments = b_comments;

    UUID uuid = {};
    UUID empty_id = {};
    int len = 0;

    int tmp_n_comment_reply = 0;
    int each_n_comment_reply_block = 0;
    int tmp_n_comment_reply_block = 0;
    bool status = true;

    BSON_APPEND_ARRAY_BEGIN(tmp_b, result_key, &child);
    for (int i = 0; i < n_comment; i++, p_b_comments++) {
        error_code = bson_get_value_bin(*p_b_comments, "comment_reply_id", UUIDLEN, (char *)uuid, &len);
        if (error_code) break;

        if (!strncmp((char *)uuid, (char *)empty_id, UUIDLEN)) continue;

        array_keylen = bson_uint32_to_string(tmp_n_comment_reply, &array_key, buf, sizeof(buf));
        status = bson_append_bin(&child, array_key, (int)array_keylen, uuid, UUIDLEN);
        if (!status) {
            error_code = S_ERR;
            break;
        }
        tmp_n_comment_reply++;

        error_code = bson_get_value_int32(*p_b_comments, "n_comment_reply_block", &each_n_comment_reply_block);
        if (error_code) continue;

        tmp_n_comment_reply_block += each_n_comment_reply_block;
    }
    bson_append_array_end(tmp_b, &child);

    *n_comment_reply = tmp_n_comment_reply;
    *n_comment_reply_block = tmp_n_comment_reply_block;

    return error_code;
}

Err
_dynamic_read_b_comment_comment_reply_by_ids_to_buf_core(bson_t **b_comments, int n_comment, DictBsonByUU *dict_comment_content, DictBsonByUU *dict_comment_reply, char *buf, int max_buf_size, int *n_read_comment, int *n_read_comment_reply, int *len_buf)
{
    Err error_code = S_OK;

    bson_t **p_b_comments = b_comments;
    char *p_buf = buf;
    UUID comment_id = {};
    UUID comment_reply_id = {};
    UUID empty_id = {};

    int len_comment = 0;
    int len_comment_reply = 0;
    int len_read_comment = 0;
    int len_read_comment_reply = 0;

    int tmp_n_comment_reply = 0;
    int len = 0;

    bson_t *b_comment_content = NULL;
    bson_t *b_comment_reply = NULL;
    Err error_code_b_comment_reply = S_OK;

    display_dict_bson_by_uu(dict_comment_content, "pttdb_comment._dynamic_read_b_comment_comment_reply_by_ids_to_buf_core: dict_comment_content: ");

    display_dict_bson_by_uu(dict_comment_reply, "pttdb_comment._dynamic_read_b_comment_comment_reply_by_ids_to_buf_core: dict_comment_reply: ");

    *n_read_comment = n_comment;
    for (int i = 0; i < n_comment; i++, p_b_comments++, len_comment = 0, len_comment_reply = 0, len_read_comment = 0, len_read_comment_reply = 0, b_comment_content = NULL, b_comment_reply = NULL) {
        // get comment_id
        error_code = bson_get_value_bin(*p_b_comments, "the_id", UUIDLEN, (char *)comment_id, &len);

        if (!error_code) {
            error_code = bson_get_value_bin(*p_b_comments, "comment_reply_id", UUIDLEN, (char *)comment_reply_id, &len);
        }

        // get comment
        if (!error_code) {
            error_code = get_bson_from_dict_bson_by_uu(dict_comment_content, comment_id, &b_comment_content);
        }

        // get comment-reply
        if (!error_code && strncmp((char *)comment_reply_id, (char *)empty_id, UUIDLEN)) {
            error_code_b_comment_reply = get_bson_from_dict_bson_by_uu(dict_comment_reply, comment_id, &b_comment_reply);
        }

        // get comment-len
        if (!error_code) {
            error_code = bson_get_value_int32(b_comment_content, "len", &len_comment);
        }

        // get comment-reply-len
        if (!error_code && b_comment_reply) {
            error_code = bson_get_value_int32(b_comment_reply, "len", &len_comment_reply);
        }

        if (!error_code && (len_comment + len_comment_reply > max_buf_size)) {
            error_code = S_ERR_BUFFER_LEN;
        }

        // get comment-buf
        if (!error_code) {
            error_code = bson_get_value_bin(b_comment_content, "buf", len_comment, p_buf, &len_read_comment);
        }

        if (!error_code) {
            p_buf += len_read_comment;
            max_buf_size -= len_read_comment;
        }

        // get comment-reply-buf
        if (!error_code && b_comment_reply) {
            error_code = bson_get_value_bin(b_comment_reply, "buf", len_comment_reply, p_buf, &len_read_comment_reply);
        }

        if (!error_code && b_comment_reply) {
            p_buf += len_read_comment_reply;
            max_buf_size -= len_read_comment_reply;
            tmp_n_comment_reply++;
        }

        if (error_code) {
            p_buf -= (len_read_comment + len_read_comment_reply);
            *n_read_comment = i;
            break;
        }
    }
    *n_read_comment_reply = tmp_n_comment_reply;
    *len_buf = p_buf - buf;

    return error_code;
}

Err
_dynamic_read_b_comment_comment_reply_by_ids_to_file_core(bson_t **b_comments, int n_comment, DictBsonByUU *dict_comment_content, DictBsonByUU *dict_comment_reply, FILE *f, int *n_read_comment, int *n_read_comment_reply)
{
    Err error_code = S_OK;

    bson_t **p_b_comments = b_comments;
    UUID comment_id = {};
    UUID comment_reply_id = {};
    UUID empty_id = {};

    int len_comment = 0;
    int len_comment_reply = 0;
    int len_read_comment = 0;
    int len_read_comment_reply = 0;

    int tmp_n_comment_reply = 0;
    int len = 0;

    char buf[MAX_BUF_SIZE];

    bson_t *b_comment_content = NULL;
    bson_t *b_comment_reply = NULL;
    Err error_code_b_comment_reply = S_OK;

    //display_dict_bson_by_uu(dict_comment_content, "pttdb_comment._dynamic_read_b_comment_comment_reply_by_ids_to_file_core: dict_comment_content: ");

    //display_dict_bson_by_uu(dict_comment_reply, "pttdb_comment._dynamic_read_b_comment_comment_reply_by_ids_to_file_core: dict_comment_reply: ");

    *n_read_comment = n_comment;
    for (int i = 0; i < n_comment; i++, p_b_comments++, len_comment = 0, len_comment_reply = 0, len_read_comment = 0, len_read_comment_reply = 0, b_comment_content = NULL, b_comment_reply = NULL) {
        // get comment_id
        error_code = bson_get_value_bin(*p_b_comments, "the_id", UUIDLEN, (char *)comment_id, &len);

        if (!error_code) {
            error_code = bson_get_value_bin(*p_b_comments, "comment_reply_id", UUIDLEN, (char *)comment_reply_id, &len);
        }

        // get comment
        if (!error_code) {
            error_code = get_bson_from_dict_bson_by_uu(dict_comment_content, comment_id, &b_comment_content);
        }

        if (!error_code) {
            error_code = bson_get_value_int32(b_comment_content, "len", &len_comment);
        }

        if (!error_code) {
            error_code = bson_get_value_bin(b_comment_content, "buf", len_comment, buf, &len_read_comment);
        }

        if (!error_code) {
            fwrite(buf, 1, len_read_comment, f);
        }

        // get comment-reply
        if (!error_code && strncmp((char *)comment_reply_id, (char *)empty_id, UUIDLEN)) {
            error_code_b_comment_reply = get_bson_from_dict_bson_by_uu(dict_comment_reply, comment_id, &b_comment_reply);
        }

        // get comment-reply-core
        if (!error_code && b_comment_reply) {
            error_code = _dynamic_read_b_comment_comment_reply_by_ids_to_file_comment_reply(b_comment_reply, f);
        }

        if (!error_code && b_comment_reply) {
            tmp_n_comment_reply++;
        }

        if (error_code) {
            *n_read_comment = i;
            break;
        }
    }
    *n_read_comment_reply = tmp_n_comment_reply;

    return error_code;
}

Err
_dynamic_read_b_comment_comment_reply_by_ids_to_file_comment_reply(bson_t *b_comment_reply, FILE *f)
{
    char buf[MAX_BUF_SIZE] = {};

    Err error_code = S_OK;

    int n_block = 0;
    error_code = bson_get_value_int32(b_comment_reply, "n_block", &n_block);
    if(error_code) return error_code;

    UUID comment_reply_id = {};
    int len = 0;
    error_code = bson_get_value_bin(b_comment_reply, "the_id", UUIDLEN, (char *)comment_reply_id, &len);
    if(error_code) return error_code;

    for(int i = 0; i < n_block; i++) {
        error_code = _dynamic_read_b_comment_comment_reply_by_ids_to_file_comment_reply_core(comment_reply_id, f, i, buf);
        if(error_code) break;
    }

    return error_code;
}

Err
_dynamic_read_b_comment_comment_reply_by_ids_to_file_comment_reply_core(UUID comment_reply_id, FILE *f, int idx, char *buf)
{
    Err error_code = S_OK;

    bson_t *key = BCON_NEW(
        "the_id", BCON_BINARY(comment_reply_id, UUIDLEN),
        "block_id", BCON_INT32(idx)
        );

    bson_t *b_comment_reply_block = NULL;    
    error_code = db_find_one(MONGO_COMMENT_REPLY_BLOCK, key, NULL, &b_comment_reply_block);

    int len = 0;

    if(!error_code) {
        error_code = bson_get_value_bin(b_comment_reply_block, "buf_block", MAX_BUF_SIZE, buf, &len);
    }

    if(!error_code){
        fwrite(buf, 1, len, f);
    }

    // free
    bson_safe_destroy(&key);
    bson_safe_destroy(&b_comment_reply_block);

    return error_code;
}


Err
sort_b_comments_by_comment_id(bson_t **b_comments, int n_comment)
{
    qsort(b_comments, n_comment, sizeof(bson_t *), _cmp_b_comments_by_comment_id);

    return S_OK;
}

int
_cmp_b_comments_by_comment_id(const void *a, const void *b)
{
    Err error_code = S_OK;
    bson_t **p_b_comment_a = (bson_t **)a;
    bson_t **p_b_comment_b = (bson_t **)b;
    bson_t *b_comment_a = *p_b_comment_a;
    bson_t *b_comment_b = *p_b_comment_b;

    UUID comment_id_a = {};
    UUID comment_id_b = {};
    int len = 0;

    error_code = bson_get_value_bin(b_comment_a, "the_id", UUIDLEN, (char *)comment_id_a, &len);
    if (error_code) comment_id_a[0] = 0;
    error_code = bson_get_value_bin(b_comment_b, "the_id", UUIDLEN, (char *)comment_id_b, &len);
    if (error_code) comment_id_b[0] = 0;

    return strncmp((char *)comment_id_a, (char *)comment_id_b, UUIDLEN);
}


Err
_get_comment_info_by_main_deal_with_result(bson_t *result, int n_result, int *n_total_comment, int *total_len)
{
    if (!n_result) {
        *n_total_comment = 0;
        *total_len = 0;
        return S_OK;
    }

    Err error_code = S_OK;

    error_code = bson_get_value_int32(result, "count", n_total_comment);

    if (!error_code) {
        error_code = bson_get_value_int32(result, "len", total_len);
    }

    return error_code;
}

Err
_read_comments_get_b_comments(bson_t **b_comments, UUID main_id, time64_t create_milli_timestamp, char *poster, enum ReadCommentsOpType op_type, int max_n_comment, enum MongoDBId mongo_db_id, int *n_comment)
{
    Err error_code = S_OK;

    int n_comment_same_create_milli_timestamp = 0;

    bson_t **p_b_comments = b_comments;
    error_code = _read_comments_get_b_comments_same_create_milli_timestamp(p_b_comments, main_id, create_milli_timestamp, poster, op_type, max_n_comment, mongo_db_id, &n_comment_same_create_milli_timestamp);
    if (error_code == S_ERR_NOT_EXISTS) error_code = S_OK;

    if (!error_code) {
        p_b_comments += n_comment_same_create_milli_timestamp;
        max_n_comment -= n_comment_same_create_milli_timestamp;
    }

    int n_comment_diff_create_milli_timestamp = 0;
    if (!error_code && max_n_comment > 0) {
        error_code = _read_comments_get_b_comments_diff_create_milli_timestamp(p_b_comments, main_id, create_milli_timestamp, op_type, max_n_comment, mongo_db_id, &n_comment_diff_create_milli_timestamp);
        if (error_code == S_ERR_NOT_EXISTS) error_code = S_OK;
    }

    *n_comment = n_comment_same_create_milli_timestamp + n_comment_diff_create_milli_timestamp;

    if (!error_code && (op_type == READ_COMMENTS_OP_TYPE_LT || op_type == READ_COMMENTS_OP_TYPE_LTE)) {
        error_code = _reverse_b_comments(b_comments, *n_comment);
    }

    return error_code;
}


Err
_read_comments_get_b_comments_same_create_milli_timestamp(bson_t **b_comments, UUID main_id, time64_t create_milli_timestamp, char *poster, enum ReadCommentsOpType op_type, int max_n_comment, enum MongoDBId mongo_db_id, int *n_comment)
{
    Err error_code = S_OK;
    int order = (op_type == READ_COMMENTS_OP_TYPE_GT || op_type == READ_COMMENTS_OP_TYPE_GTE) ? 1 : -1;

    // get same create_milli_timestamp but not same poster
    bson_t *key = BCON_NEW(
        "main_id", BCON_BINARY(main_id, UUIDLEN),
        "status", BCON_INT32(LIVE_STATUS_ALIVE),
        "create_milli_timestamp", BCON_INT64(create_milli_timestamp),
        "poster", "{",
            _read_comments_op_type[op_type], BCON_BINARY((unsigned char *)poster, IDLEN),
        "}"
        );

    if (key == NULL) error_code = S_ERR_INIT;

    bson_t *sort = BCON_NEW(
        "poster", BCON_INT32(order)
        );

    if (sort == NULL) error_code = S_ERR_INIT;

    if (!error_code) {
        error_code = _read_comments_get_b_comments_core(b_comments, key, sort, op_type, max_n_comment, mongo_db_id, n_comment);
    }

    bson_safe_destroy(&key);
    bson_safe_destroy(&sort);

    return error_code;
}

Err
_read_comments_get_b_comments_diff_create_milli_timestamp(bson_t **b_comments, UUID main_id, time64_t create_milli_timestamp, enum ReadCommentsOpType op_type, int max_n_comment, enum MongoDBId mongo_db_id, int *n_comment)
{
    Err error_code = S_OK;
    int order = (op_type == READ_COMMENTS_OP_TYPE_GT || op_type == READ_COMMENTS_OP_TYPE_GTE) ? 1 : -1;
    if (op_type == READ_COMMENTS_OP_TYPE_GTE) {
        op_type = READ_COMMENTS_OP_TYPE_GT;
    }

    if (op_type == READ_COMMENTS_OP_TYPE_LTE) {
        op_type = READ_COMMENTS_OP_TYPE_LT;
    }

    // get same create_milli_timestamp but not same poster
    bson_t *key = BCON_NEW(
        "main_id", BCON_BINARY(main_id, UUIDLEN),
        "status", BCON_INT32(LIVE_STATUS_ALIVE),
        "create_milli_timestamp", "{",
            _read_comments_op_type[op_type], BCON_INT64(create_milli_timestamp),
        "}"
        );

    if (key == NULL) error_code = S_ERR;

    bson_t *sort = BCON_NEW(
        "create_milli_timestamp", BCON_INT32(order),
        "poster", BCON_INT32(order)
        );

    if (sort == NULL) error_code = S_ERR;

    if (!error_code) {
        error_code = _read_comments_get_b_comments_core(b_comments, key, sort, op_type, max_n_comment, mongo_db_id, n_comment);
    }

    bson_safe_destroy(&key);
    bson_safe_destroy(&sort);

    return error_code;
}

Err
_read_comments_get_b_comments_core(bson_t **b_comments, bson_t *key, bson_t *sort, enum ReadCommentsOpType op_type, int max_n_comment, enum MongoDBId mongo_db_id, int *n_comment)
{
    Err error_code = db_find(mongo_db_id, key, NULL, sort, max_n_comment, n_comment, b_comments);

    enum ReadCommentsOrderType order_type = (op_type == READ_COMMENTS_OP_TYPE_GT || op_type == READ_COMMENTS_OP_TYPE_GTE) ? READ_COMMENTS_ORDER_TYPE_ASC : READ_COMMENTS_ORDER_TYPE_DESC;

    int tmp_n_comment = *n_comment;

    if (!error_code) {
        error_code = ensure_b_comments_order(b_comments, tmp_n_comment, order_type);
    }

    return error_code;
}

Err
ensure_b_comments_order(bson_t **b_comments, int n_comment, enum ReadCommentsOrderType order_type)
{
    bool is_good_b_comments_order = false;
    Err error_code = is_b_comments_order(b_comments, n_comment, order_type, &is_good_b_comments_order);
    if(error_code) return error_code;

    if(is_good_b_comments_order) return S_OK;

    error_code = sort_b_comments_order(b_comments, n_comment, order_type);
    if(error_code) return error_code;

    return S_OK;
}

Err
is_b_comments_order(bson_t **b_comments, int n_comment, enum ReadCommentsOrderType order_type, bool *is_good_b_comments_order)
{
    bson_t **p_b_comments = b_comments;
    bson_t **p_next_b_comments = b_comments + 1;
    int cmp = 0;
    int (*_cmp)(const void *a, const void *b) = order_type == READ_COMMENTS_ORDER_TYPE_ASC ? _cmp_b_comments_ascending : _cmp_b_comments_descending;

    bool tmp_is_good_b_comments_order = true;
    for (int i = 0; i < n_comment - 1; i++, p_b_comments++, p_next_b_comments++) {
        cmp = _cmp(p_b_comments, p_next_b_comments);
        if (cmp > 0) {
            tmp_is_good_b_comments_order = false;
            break;
        }
    }
    *is_good_b_comments_order = tmp_is_good_b_comments_order;

    return S_OK;
}

Err
sort_b_comments_order(bson_t **b_comments, int n_comment, enum ReadCommentsOrderType order_type)
{
    int (*_cmp)(const void *a, const void *b) = order_type == READ_COMMENTS_ORDER_TYPE_ASC ? _cmp_b_comments_ascending : _cmp_b_comments_descending;

    qsort(b_comments, n_comment, sizeof(bson_t *), _cmp);

    return S_OK;
}

int
_cmp_b_comments_ascending(const void *a, const void *b)
{
    bson_t **p_b_comment_a = (bson_t **)a;
    bson_t *b_comment_a = *p_b_comment_a;
    bson_t **p_b_comment_b = (bson_t **)b;
    bson_t *b_comment_b = *p_b_comment_b;

    time64_t create_milli_timestamp_a = 0;
    time64_t create_milli_timestamp_b = 0;

    char poster_a[IDLEN + 1] = {};
    char poster_b[IDLEN + 1] = {};

    Err error_code;
    error_code = bson_get_value_int64(b_comment_a, "create_milli_timestamp", (long *)&create_milli_timestamp_a);
    if (error_code) create_milli_timestamp_a = -1;

    error_code = bson_get_value_int64(b_comment_b, "create_milli_timestamp", (long *)&create_milli_timestamp_b);
    if (error_code) create_milli_timestamp_b = -1;

    if (create_milli_timestamp_a != create_milli_timestamp_b) {
        return create_milli_timestamp_a - create_milli_timestamp_b;
    }

    int len;
    error_code = bson_get_value_bin(b_comment_a, "poster", IDLEN, poster_a, &len);
    if (error_code) poster_a[0] = 0;

    error_code = bson_get_value_bin(b_comment_b, "poster", IDLEN, poster_b, &len);
    if (error_code) poster_b[0] = 0;

    return strncmp(poster_a, poster_b, IDLEN);
}

int
_cmp_b_comments_descending(const void *a, const void *b)
{
    bson_t **p_b_comment_a = (bson_t **)a;
    bson_t *b_comment_a = *p_b_comment_a;
    bson_t **p_b_comment_b = (bson_t **)b;
    bson_t *b_comment_b = *p_b_comment_b;

    time64_t create_milli_timestamp_a = 0;
    time64_t create_milli_timestamp_b = 0;

    char poster_a[IDLEN + 1] = {};
    char poster_b[IDLEN + 1] = {};

    Err error_code;
    error_code = bson_get_value_int64(b_comment_a, "create_milli_timestamp", (long *)&create_milli_timestamp_a);
    if (error_code) create_milli_timestamp_a = -1;

    error_code = bson_get_value_int64(b_comment_b, "create_milli_timestamp", (long *)&create_milli_timestamp_b);
    if (error_code) create_milli_timestamp_b = -1;

    if (create_milli_timestamp_a != create_milli_timestamp_b) {
        return create_milli_timestamp_b - create_milli_timestamp_a;
    }

    int len;
    error_code = bson_get_value_bin(b_comment_a, "poster", IDLEN, poster_a, &len);
    if (error_code) bzero(poster_a, IDLEN + 1);

    error_code = bson_get_value_bin(b_comment_b, "poster", IDLEN, poster_b, &len);
    if (error_code) bzero(poster_b, IDLEN + 1);

    return strncmp(poster_b, poster_a, IDLEN);
}

Err
_reverse_b_comments(bson_t **b_comments, int n_comment)
{
    int half_n_comment = (n_comment + 1) / 2; // 8 as 4, 7 as 4
    bson_t **p_b_comments = b_comments;
    bson_t **p_end_b_comments = b_comments + n_comment - 1;
    bson_t *tmp;
    for (int i = 0; i < half_n_comment; i++, p_b_comments++, p_end_b_comments--) {
        tmp = *p_b_comments;
        *p_b_comments = *p_end_b_comments;
        *p_end_b_comments = tmp;
    }

    return S_OK;
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
serialize_comment_bson(Comment *comment, bson_t **comment_bson)
{
    *comment_bson = BCON_NEW(
        "version", BCON_INT32(comment->version),
        "the_id", BCON_BINARY(comment->the_id, UUIDLEN),
        "main_id", BCON_BINARY(comment->main_id, UUIDLEN),
        "comment_reply_id", BCON_BINARY(comment->comment_reply_id, UUIDLEN),
        "n_comment_reply_line", BCON_INT32(comment->n_comment_reply_line),
        "n_comment_reply_block", BCON_INT32(comment->n_comment_reply_block),
        "n_comment_reply_total_line", BCON_INT32(comment->n_comment_reply_total_line),
        "status", BCON_INT32(comment->status),
        "status_updater", BCON_BINARY((unsigned char *)comment->status_updater, IDLEN),
        "status_update_ip", BCON_BINARY((unsigned char *)comment->status_update_ip, IPV4LEN),
        "comment_type", BCON_INT32(comment->comment_type),
        "karma", BCON_INT32(comment->karma),
        "poster", BCON_BINARY((unsigned char *)comment->poster, IDLEN),
        "ip", BCON_BINARY((unsigned char *)comment->ip, IPV4LEN),
        "create_milli_timestamp", BCON_INT64(comment->create_milli_timestamp),
        "updater", BCON_BINARY((unsigned char *)comment->updater, IDLEN),
        "update_ip", BCON_BINARY((unsigned char *)comment->update_ip, IPV4LEN),
        "update_milli_timestamp", BCON_INT64(comment->update_milli_timestamp),
        "len", BCON_INT32(comment->len),
        "buf", BCON_BINARY((unsigned char *)comment->buf, comment->len)
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
deserialize_comment_bson(bson_t *comment_bson, Comment *comment)
{
    Err error_code;

    int len;
    error_code = bson_get_value_int32(comment_bson, "version", (int *)&comment->version);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_bson, "the_id", UUIDLEN, (char *)comment->the_id, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_bson, "main_id", UUIDLEN, (char *)comment->main_id, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_bson, "comment_reply_id", UUIDLEN, (char *)comment->comment_reply_id, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(comment_bson, "n_comment_reply_line", &comment->n_comment_reply_line);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(comment_bson, "n_comment_reply_block", &comment->n_comment_reply_block);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(comment_bson, "n_comment_reply_total_line", &comment->n_comment_reply_total_line);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(comment_bson, "status", (int *)&comment->status);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(comment_bson, "comment_type", (int *)&comment->comment_type);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(comment_bson, "karma", (int *)&comment->karma);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_bson, "status_updater", IDLEN, comment->status_updater, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_bson, "status_update_ip", IPV4LEN, comment->status_update_ip, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_bson, "poster", IDLEN, comment->poster, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_bson, "ip", IPV4LEN, comment->ip, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_int64(comment_bson, "create_milli_timestamp", (long *)&comment->create_milli_timestamp);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_bson, "updater", IDLEN, comment->updater, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_bson, "update_ip", IPV4LEN, comment->update_ip, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_int64(comment_bson, "update_milli_timestamp", (long *)&comment->update_milli_timestamp);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(comment_bson, "len", &comment->len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_bson, "buf", comment->max_buf_len, comment->buf, &len);
    if (error_code) return error_code;

    return S_OK;
}

/**
 * @brief [brief description]
 * @details [long description]
 *
 * @param comment_bson [description]
 * @param comment ([WARNING] comment->buf must be NULL and comment->max_buf_len must be 0. Must do destroy_comment in the end)
 *
 * @return [description]
 */
Err
deserialize_comment_bson_with_buf(bson_t *comment_bson, Comment *comment)
{
    Err error_code = S_OK;
    if (comment->buf) return S_ERR;

    int len = 0;
    error_code = bson_get_value_int32(comment_bson, "len", &len);
    if (error_code) return error_code;

    comment->buf = malloc(len);
    comment->max_buf_len = len;
    comment->len = 0;

    return deserialize_comment_bson(comment_bson, comment);
}

Err
update_comment_from_comment_info(UUID main_id, CommentInfo *comment_info, char *updater, char *update_ip, time64_t update_milli_timestamp)
{
    Err error_code = S_OK;
    if(!update_milli_timestamp) {
        error_code = get_milli_timestamp(&update_milli_timestamp);
    }
    if(error_code) return error_code;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    char *tmp_buf = NULL;
    int n_tmp_buf = 0;;

    error_code = pttdb_file_get_data(main_id, PTTDB_CONTENT_TYPE_COMMENT, comment_info->comment_id, 0, 0, &tmp_buf, &n_tmp_buf);

    if(!error_code) {
        error_code = serialize_uuid_bson(comment_info->comment_id, &comment_id_bson);
    }

    if(!error_code) {
        comment_bson = BCON_NEW(
                "updater", BCON_BINARY((unsigned char *)updater, IDLEN),
                "update_ip", BCON_BINARY((unsigned char *)update_ip, IPV4LEN),
                "len", BCON_INT32(n_tmp_buf),
                "buf", BCON_BINARY((unsigned char *)tmp_buf, n_tmp_buf)
            );
    }

    if (!error_code) {
        error_code = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, false);
    }    

    // free
    safe_free((void **)&tmp_buf);
    bson_safe_destroy(&comment_bson);
    bson_safe_destroy(&comment_id_bson);
}