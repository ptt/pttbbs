#include "pttdb.h"
#include "pttdb_internal.h"

enum Karma KARMA_BY_COMMENT_TYPE[] = {
    KARMA_GOOD,
    KARMA_BAD,
    KARMA_ARROW,
    0,
    0,                   // forward
    0,                   // other
};

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
create_comment(UUID main_id, char *poster, char *ip, int len, char *content, enum CommentType comment_type, UUID comment_id)
{
    Err error_code = S_OK;

    time64_t create_milli_timestamp;

    // use associate to associate content directly
    Comment comment = {};
    associate_comment(&comment, content, len);
    comment.len = len;

    error_code = get_milli_timestamp(&create_milli_timestamp);
    if (error_code) return error_code;

    error_code = gen_uuid_with_db(MONGO_COMMENT, comment_id);
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

    error_code = _serialize_comment_bson(&comment, &comment_bson);
    if (!error_code) {
        error_code = _serialize_uuid_bson(comment_id, &comment_id_bson);
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
        error_code = _deserialize_comment_bson(db_result, comment);
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
get_comment_info_by_main(UUID main_id, int *n_total_comments, int *total_len)
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
        error_code = _get_comment_info_by_main_deal_with_result(result, n_result, n_total_comments, total_len);
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
read_comments_by_main(UUID main_id, time64_t create_milli_timestamp, char *poster, enum ReadCommentsOpType op_type, int max_n_comments, enum MongoDBId mongo_db_id, Comment *comments, int *n_read_comments, int *len)
{

    Err error_code = S_OK;

    // init db-results
    bson_t **db_results = malloc(sizeof(bson_t *) * max_n_comments);
    if (db_results == NULL) return S_ERR_INIT;
    bzero(db_results, sizeof(bson_t *) * max_n_comments);

    error_code = _read_comments_get_db_results(db_results, main_id, create_milli_timestamp, poster, op_type, max_n_comments, mongo_db_id, n_read_comments);

    fprintf(stderr, "pttdb_comment.read_comments_by_main: after _read_comments_get_db_results: e: %d n_read_comments: %d\n", error_code, *n_read_comments);

    int tmp_n_read_comments = *n_read_comments;
    bson_t **p_db_results = db_results;
    Comment *p_comments = comments;

    int tmp_len = 0;
    if (!error_code) {
        for (int i = 0; i < tmp_n_read_comments; i++) {
            error_code = _deserialize_comment_bson(*p_db_results, p_comments);

            tmp_len += p_comments->len;
            p_db_results++;
            p_comments++;

            if (error_code) {
                *n_read_comments = i;
                break;
            }
        }
    }

    *len = tmp_len;

    // free
    p_db_results = db_results;
    for (int i = 0; i < tmp_n_read_comments; i++) {
        bson_safe_destroy(p_db_results);
        p_db_results++;
    }
    free(db_results);

    return error_code;
}

Err
_get_comment_info_by_main_deal_with_result(bson_t *result, int n_result, int *n_total_comments, int *total_len)
{
    if (!n_result) {
        *n_total_comments = 0;
        *total_len = 0;
        return S_OK;
    }

    Err error_code = S_OK;

    error_code = bson_get_value_int32(result, "count", n_total_comments);

    if (!error_code) {
        error_code = bson_get_value_int32(result, "len", total_len);
    }

    return error_code;
}

Err
_read_comments_get_db_results(bson_t **db_results, UUID main_id, time64_t create_milli_timestamp, char *poster, enum ReadCommentsOpType op_type, int max_n_comments, enum MongoDBId mongo_db_id, int *n_comments)
{
    Err error_code = S_OK;

    int n_comments_same_create_milli_timestamp = 0;

    bson_t **p_db_results = db_results;
    error_code = _read_comments_get_db_results_same_create_milli_timestamp(p_db_results, main_id, create_milli_timestamp, poster, op_type, max_n_comments, mongo_db_id, &n_comments_same_create_milli_timestamp);
    if(error_code == S_ERR_NOT_EXISTS) error_code = S_OK;

    if(!error_code) {
        p_db_results += n_comments_same_create_milli_timestamp;
        max_n_comments -= n_comments_same_create_milli_timestamp;
    }

    int n_comments_diff_create_milli_timestamp = 0;
    if(!error_code && max_n_comments > 0) {
        error_code = _read_comments_get_db_results_diff_create_milli_timestamp(p_db_results, main_id, create_milli_timestamp, op_type, max_n_comments, mongo_db_id, &n_comments_diff_create_milli_timestamp);
        if(error_code == S_ERR_NOT_EXISTS) error_code = S_OK;
    }

    *n_comments = n_comments_same_create_milli_timestamp + n_comments_diff_create_milli_timestamp;

    if(!error_code && (op_type == READ_COMMENTS_OP_TYPE_LT || op_type == READ_COMMENTS_OP_TYPE_LTE)) {
        error_code = _reverse_db_results(db_results, *n_comments);
    }

    return error_code;
}


Err
_read_comments_get_db_results_same_create_milli_timestamp(bson_t **db_results, UUID main_id, time64_t create_milli_timestamp, char *poster, enum ReadCommentsOpType op_type, int max_n_comments, enum MongoDBId mongo_db_id, int *n_comments)
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

    if (key == NULL) error_code = S_ERR;

    bson_t *sort = BCON_NEW(
        "poster", BCON_INT32(order)
    );
    
    if(sort == NULL) error_code = S_ERR;

    if(!error_code) {
        error_code = _read_comments_get_db_results_core(db_results, key, sort, op_type, max_n_comments, mongo_db_id, n_comments);
    }

    bson_safe_destroy(&key);
    bson_safe_destroy(&sort);

    return error_code;
}

Err
_read_comments_get_db_results_diff_create_milli_timestamp(bson_t **db_results, UUID main_id, time64_t create_milli_timestamp, enum ReadCommentsOpType op_type, int max_n_comments, enum MongoDBId mongo_db_id, int *n_comments)
{
    Err error_code = S_OK;
    int order = (op_type == READ_COMMENTS_OP_TYPE_GT || op_type == READ_COMMENTS_OP_TYPE_GTE) ? 1 : -1;
    if(op_type == READ_COMMENTS_OP_TYPE_GTE) {
        op_type = READ_COMMENTS_OP_TYPE_GT;
    }

    if(op_type == READ_COMMENTS_OP_TYPE_LTE) {
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
    
    if(sort == NULL) error_code = S_ERR;

    if(!error_code) {
        error_code = _read_comments_get_db_results_core(db_results, key, sort, op_type, max_n_comments, mongo_db_id, n_comments);
    }

    bson_safe_destroy(&key);
    bson_safe_destroy(&sort);

    return error_code;
}

Err
_read_comments_get_db_results_core(bson_t **db_results, bson_t *key, bson_t *sort, enum ReadCommentsOpType op_type, int max_n_comments, enum MongoDBId mongo_db_id, int *n_comments)
{
    Err error_code = db_find(mongo_db_id, key, NULL, sort, max_n_comments, n_comments, db_results);

    int tmp_n_comments = *n_comments;

    Err error_code_ensure_order = S_OK;
    if (!error_code) {
        error_code_ensure_order = _ensure_db_results_order(db_results, tmp_n_comments, op_type);
    }

    if (!error_code && error_code_ensure_order) {
        error_code = _sort_db_results_order(db_results, tmp_n_comments, op_type);
    }

    return error_code;
}

Err
_ensure_db_results_order(bson_t **db_results, int n_results, enum ReadCommentsOpType op_type)
{
    Err error_code = S_OK;
    bson_t **p_db_results = db_results;
    bson_t **p_next_db_results = db_results + 1;
    int cmp = 0;
    int (*_cmp)(const void *a, const void *b) = (op_type == READ_COMMENTS_OP_TYPE_LT || op_type == READ_COMMENTS_OP_TYPE_LTE) ? _cmp_descending : _cmp_ascending;

    for (int i = 0; i < n_results - 1; i++, p_db_results++, p_next_db_results++) {
        cmp = _cmp(p_db_results, p_next_db_results);
        if (cmp > 0) {
            error_code = S_ERR;
            break;
        }
    }

    return error_code;
}

Err
_sort_db_results_order(bson_t **db_results, int n_results, enum ReadCommentsOpType op_type)
{
    int (*_cmp)(const void *a, const void *b) = (op_type == READ_COMMENTS_OP_TYPE_LT || op_type == READ_COMMENTS_OP_TYPE_LTE) ? _cmp_descending : _cmp_ascending;

    qsort(db_results, n_results, sizeof(bson_t *), _cmp);

    return S_OK;
}

int
_cmp_ascending(const void *a, const void *b)
{
    bson_t **tmp_tmp_a = (bson_t **)a;
    bson_t *tmp_a = *tmp_tmp_a;
    bson_t **tmp_tmp_b = (bson_t **)b;
    bson_t *tmp_b = *tmp_tmp_b;

    time64_t create_milli_timestamp_a = 0;
    time64_t create_milli_timestamp_b = 0;

    char poster_a[IDLEN + 1] = {};
    char poster_b[IDLEN + 1] = {};

    Err error_code;
    error_code = bson_get_value_int64(tmp_a, "create_milli_timestamp", (long *)&create_milli_timestamp_a);
    if (error_code) create_milli_timestamp_a = -1;

    error_code = bson_get_value_int64(tmp_b, "create_milli_timestamp", (long *)&create_milli_timestamp_b);
    if (error_code) create_milli_timestamp_b = -1;

    if (create_milli_timestamp_a != create_milli_timestamp_b) {
        return create_milli_timestamp_a - create_milli_timestamp_b;
    }

    int len;
    error_code = bson_get_value_bin(tmp_a, "poster", IDLEN, poster_a, &len);
    if (error_code) bzero(poster_a, IDLEN + 1);

    error_code = bson_get_value_bin(tmp_b, "poster", IDLEN, poster_b, &len);
    if (error_code) bzero(poster_b, IDLEN + 1);

    return strncmp(poster_a, poster_b, IDLEN);
}

int
_cmp_descending(const void *a, const void *b)
{
    bson_t **tmp_tmp_a = (bson_t **)a;
    bson_t *tmp_a = *tmp_tmp_a;
    bson_t **tmp_tmp_b = (bson_t **)b;
    bson_t *tmp_b = *tmp_tmp_b;

    time64_t create_milli_timestamp_a = 0;
    time64_t create_milli_timestamp_b = 0;

    char poster_a[IDLEN + 1] = {};
    char poster_b[IDLEN + 1] = {};

    Err error_code;
    error_code = bson_get_value_int64(tmp_a, "create_milli_timestamp", (long *)&create_milli_timestamp_a);
    if (error_code) create_milli_timestamp_a = -1;

    error_code = bson_get_value_int64(tmp_b, "create_milli_timestamp", (long *)&create_milli_timestamp_b);
    if (error_code) create_milli_timestamp_b = -1;

    if (create_milli_timestamp_a != create_milli_timestamp_b) {
        return create_milli_timestamp_b - create_milli_timestamp_a;
    }

    int len;
    error_code = bson_get_value_bin(tmp_a, "poster", IDLEN, poster_a, &len);
    if (error_code) bzero(poster_a, IDLEN + 1);

    error_code = bson_get_value_bin(tmp_b, "poster", IDLEN, poster_b, &len);
    if (error_code) bzero(poster_b, IDLEN + 1);

    return strncmp(poster_b, poster_a, IDLEN);
}

Err
_reverse_db_results(bson_t **db_results, int n_results)
{
    int half_results = n_results / 2;
    bson_t **p_db_results = db_results;
    bson_t **p_end_db_results = db_results + n_results - 1;
    bson_t *temp;
    for(int i = 0; i < half_results; i++, p_db_results++, p_end_db_results--) {
        temp = *p_db_results;
        *p_db_results = *p_end_db_results;
        *p_end_db_results = temp;        
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
_serialize_comment_bson(Comment *comment, bson_t **comment_bson)
{
    *comment_bson = BCON_NEW(
                        "version", BCON_INT32(comment->version),
                        "the_id", BCON_BINARY(comment->the_id, UUIDLEN),
                        "main_id", BCON_BINARY(comment->main_id, UUIDLEN),
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
_deserialize_comment_bson(bson_t *comment_bson, Comment *comment)
{
    Err error_code;

    int len;
    error_code = bson_get_value_int32(comment_bson, "version", (int *)&comment->version);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_bson, "the_id", UUIDLEN, (char *)comment->the_id, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(comment_bson, "main_id", UUIDLEN, (char *)comment->main_id, &len);
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
 * @param comment [description]
 *
 * @return [description]
 */
Err
_deserialize_comment_bson_with_buf(bson_t *comment_bson, Comment *comment)
{
    Err error_code = S_OK;
    if (comment->buf) return S_ERR;

    int len = 0;
    error_code = bson_get_value_int32(comment_bson, "len", &len);
    if (error_code) return error_code;

    comment->buf = malloc(len);
    comment->max_buf_len = len;
    comment->len = 0;

    return _deserialize_comment_bson(comment_bson, comment);
}
