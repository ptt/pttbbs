#include "pttdb.h"
#include "pttdb_internal.h"

/**
 * @brief [brief description]
 * @details [long description]
 *
 * @param buf [description]
 * @param bytes [description]
 * @param ref_id [description]
 * @param conten_id [description]
 * @param MongoDBId [description]
 * @param n_line [description]
 * @param n_block [description]
 */
Err
split_contents(char *buf, int bytes, UUID ref_id, UUID content_id, enum MongoDBId mongo_db_id, int *n_line, int *n_block) {
    Err error_code = S_OK;
    int bytes_in_line = 0;
    char line[MAX_BUF_SIZE] = {};

    ContentBlock content_block = {};

    // init
    (*n_block) = 0;
    error_code = init_content_block_with_buf_block(&content_block, ref_id, content_id, *n_block);
    (*n_block)++;

    if (!error_code) {
        error_code = _split_contents_core(buf, bytes, ref_id, content_id, mongo_db_id, n_line, n_block, line, &bytes_in_line, &content_block);
    }

    if (!error_code) {
        error_code = _split_contents_deal_with_last_line_block(bytes_in_line, line, ref_id, content_id, mongo_db_id, &content_block, n_line, n_block);
    }

    destroy_content_block(&content_block);

    return error_code;
}

Err
split_contents_from_fd(int fd_content, int len, UUID ref_id, UUID content_id, enum MongoDBId mongo_db_id, int *n_line, int *n_block) {
    Err error_code = S_OK;
    char buf[MAX_BUF_SIZE] = {};
    char line[MAX_BUF_SIZE] = {};
    int bytes = 0;
    int buf_size = 0;
    int bytes_in_line = 0;
    ContentBlock content_block = {};

    // init
    *n_block = 0;
    error_code = init_content_block_with_buf_block(&content_block, ref_id, content_id, *n_block);
    (*n_block)++;

    if (!error_code) {
        while ((buf_size = len < MAX_BUF_SIZE ? len : MAX_BUF_SIZE) && (bytes = read(fd_content, buf, buf_size)) > 0) {
            error_code = _split_contents_core(buf, bytes, ref_id, content_id, mongo_db_id, n_line, n_block, line, &bytes_in_line, &content_block);
            if (error_code) break;

            len -= bytes;
        }
    }

    if (!error_code) {
        error_code = _split_contents_deal_with_last_line_block(bytes_in_line, line, ref_id, content_id, mongo_db_id, &content_block, n_line, n_block);
    }

    destroy_content_block(&content_block);

    return error_code;
}

Err
delete_content(UUID content_id, enum MongoDBId mongo_db_id)
{
    Err error_code = S_OK;
    bson_t *key = BCON_NEW("the_id", BCON_BINARY(content_id, UUIDLEN));
    if (key == NULL) error_code = S_ERR_INIT;

    if (!error_code) {
        error_code = db_remove(mongo_db_id, key);
    }

    bson_safe_destroy(&key);

    return error_code;
}

Err
reset_content_block(ContentBlock *content_block, UUID ref_id, UUID content_id, int block_id)
{
    reset_content_block_buf_block(content_block);

    memcpy(content_block->the_id, content_id, sizeof(UUID));
    memcpy(content_block->ref_id, ref_id, sizeof(UUID));

    content_block->block_id = block_id;

    return S_OK;
}

Err
reset_content_block_buf_block(ContentBlock *content_block)
{
    if (!content_block->len_block) return S_OK;

    bzero(content_block->buf_block, content_block->len_block);
    content_block->len_block = 0;
    content_block->n_line = 0;

    return S_OK;
}

/**
 * @brief [brief description]
 * @details initialize new content-block with block-id as current n_block.
 *
 * @param content_block [description]
 * @param ref_id [description]
 * @param content_id [description]
 * @param n_block [description]
 */
Err
init_content_block_with_buf_block(ContentBlock *content_block, UUID ref_id, UUID content_id, int block_id)
{
    Err error_code = init_content_block_buf_block(content_block);

    if (error_code) return error_code;

    error_code = reset_content_block(content_block, ref_id, content_id, block_id);
    if (error_code) return error_code;

    return S_OK;
}

Err
init_content_block_buf_block(ContentBlock *content_block)
{
    if (content_block->buf_block != NULL) return S_OK;

    content_block->buf_block = malloc(MAX_BUF_SIZE);
    if (content_block->buf_block == NULL) return S_ERR;

    content_block->max_buf_len = MAX_BUF_SIZE;
    bzero(content_block->buf_block, MAX_BUF_SIZE);
    content_block->len_block = 0;

    return S_OK;
}

Err
destroy_content_block(ContentBlock *content_block)
{
    if (content_block->buf_block == NULL) return S_OK;

    free(content_block->buf_block);
    content_block->buf_block = NULL;
    content_block->max_buf_len = 0;
    content_block->len_block = 0;

    return S_OK;
}

Err
associate_content_block(ContentBlock *content_block, char *buf_block, int max_buf_len)
{
    if (content_block->buf_block != NULL) return S_ERR;

    content_block->buf_block = buf_block;
    content_block->max_buf_len = max_buf_len;
    content_block->len_block = 0;

    return S_OK;
}

Err
dissociate_content_block(ContentBlock *content_block)
{
    if (content_block->buf_block == NULL) return S_OK;

    content_block->buf_block = NULL;
    content_block->max_buf_len = 0;
    content_block->len_block = 0;

    return S_OK;
}

Err
save_content_block(ContentBlock *content_block, enum MongoDBId mongo_db_id)
{
    Err error_code = S_OK;
    if (content_block->buf_block == NULL) return S_ERR;

    bson_t *content_block_bson = NULL;
    bson_t *content_block_id_bson = NULL;

    char tmp_ref_id[UUIDLEN + 1] = {};
    char tmp_the_id[UUIDLEN + 1] = {};
    memcpy(tmp_ref_id, content_block->ref_id, UUIDLEN);
    memcpy(tmp_the_id, content_block->the_id, UUIDLEN);

    error_code = _serialize_content_block_bson(content_block, &content_block_bson);

    if (!error_code) {
        error_code = _serialize_content_uuid_bson(content_block->the_id, content_block->block_id, &content_block_id_bson);
    }

    if (!error_code) {
        error_code = db_update_one(mongo_db_id, content_block_id_bson, content_block_bson, true);
    }

    bson_safe_destroy(&content_block_bson);
    bson_safe_destroy(&content_block_id_bson);

    return error_code;
}

Err
read_content_block(UUID content_id, int block_id, enum MongoDBId mongo_db_id, ContentBlock *content_block)
{
    Err error_code = S_OK;

    if (content_block->buf_block == NULL) return S_ERR;

    bson_t *key = BCON_NEW(
                      "the_id", BCON_BINARY(content_id, UUIDLEN),
                      "block_id", BCON_INT32(block_id)
                  );
    if (key == NULL) error_code = S_ERR;

    bson_t *db_result = NULL;

    if (!error_code) {
        error_code = db_find_one(mongo_db_id, key, NULL, &db_result);
    }

    if (!error_code) {
        error_code = _deserialize_content_block_bson(db_result, content_block);
    }

    bson_safe_destroy(&db_result);
    bson_safe_destroy(&key);

    return error_code;
}

Err
read_content_blocks(UUID content_id, int max_n_block, int block_id, enum MongoDBId mongo_db_id, ContentBlock *content_blocks, int *n_block, int *len)
{
    Err error_code = S_OK;

    bson_t *key = BCON_NEW(
                      "the_id", BCON_BINARY(content_id, UUIDLEN)
                  );
    if (key == NULL) error_code = S_ERR;

    if(!error_code) {
        error_code = _read_content_blocks_core(key, max_n_block, block_id, mongo_db_id, content_blocks, n_block, len);
    }

    bson_safe_destroy(&key);

    return error_code;
}

Err
read_content_blocks_by_ref(UUID ref_id, int max_n_block, int block_id, enum MongoDBId mongo_db_id, ContentBlock *content_blocks, int *n_block, int *len)
{
    Err error_code = S_OK;

    bson_t *key = BCON_NEW(
                      "ref_id", BCON_BINARY(ref_id, UUIDLEN)
                  );
    if (key == NULL) error_code = S_ERR;

    if(!error_code) {
        error_code = _read_content_blocks_core(key, max_n_block, block_id, mongo_db_id, content_blocks, n_block, len);
    }

    bson_safe_destroy(&key);

    return error_code;
}

Err
_read_content_blocks_core(bson_t *key, int max_n_block, int block_id, enum MongoDBId mongo_db_id, ContentBlock *content_blocks, int *n_block, int *len)
{
    Err error_code = S_OK;
    // init db_results
    bson_t **db_results = malloc(sizeof(bson_t *) * max_n_block);
    if (db_results == NULL) return S_ERR_INIT;
    bzero(db_results, sizeof(bson_t *) * max_n_block);

    if (!error_code) {
        error_code = _read_content_blocks_get_db_results(db_results, key, max_n_block, block_id, mongo_db_id, n_block);
    }

    // db_results to content-blocks
    int tmp_n_block = *n_block;
    bson_t **p_db_results = db_results;
    ContentBlock *p_content_blocks = content_blocks;

    int tmp_len = 0;
    if (!error_code) {
        for (int i = 0; i < tmp_n_block; i++) {
            error_code = _deserialize_content_block_bson(*p_db_results, p_content_blocks);

            tmp_len += p_content_blocks->len_block;
            p_db_results++;
            p_content_blocks++;

            if (error_code) {
                *n_block = i;
                break;
            }
        }
    }

    *len = tmp_len;

    // free
    p_db_results = db_results;
    for (int i = 0; i < tmp_n_block; i++) {
        bson_safe_destroy(p_db_results);
        p_db_results++;
    }
    free(db_results);

    return error_code;
}

/**
 * @brief Cconten
 * @details content blocks does not init with buf. All the content blocks shares buf
 *
 * @param content_id [description]
 * @param max_n_blocks [description]
 * @param block_id [description]
 * @param MongoDBId [description]
 * @param content_blocks [description]
 * @param n_blocks [description]
 */
Err
dynamic_read_content_blocks(UUID content_id, int max_n_block, int block_id, enum MongoDBId mongo_db_id, char *buf, int max_buf_size, ContentBlock *content_blocks, int *n_block, int *len)
{
    Err error_code = S_OK;

    bson_t *key = BCON_NEW(
                      "the_id", BCON_BINARY(content_id, UUIDLEN)
                  );
    if(key == NULL) error_code = S_ERR;

    if(!error_code){
        error_code = _dynamic_read_content_blocks_core(key, max_n_block, block_id, mongo_db_id, buf, max_buf_size, content_blocks, n_block, len);
    }

    bson_safe_destroy(&key);

    return error_code;
}

Err
dynamic_read_content_blocks_by_ref(UUID ref_id, int max_n_block, int block_id, enum MongoDBId mongo_db_id, char *buf, int max_buf_size, ContentBlock *content_blocks, int *n_block, int *len)
{
    Err error_code = S_OK;

    bson_t *key = BCON_NEW(
                      "ref_id", BCON_BINARY(ref_id, UUIDLEN)
                  );
    if(key == NULL) error_code = S_ERR;

    if(!error_code) {
        error_code = _dynamic_read_content_blocks_core(key, max_n_block, block_id, mongo_db_id, buf, max_buf_size, content_blocks, n_block, len);
    }

    bson_safe_destroy(&key);

    return error_code;    
}

Err
_dynamic_read_content_blocks_core(bson_t *key, int max_n_block, int block_id, enum MongoDBId mongo_db_id, char *buf, int max_buf_size, ContentBlock *content_blocks, int *n_block, int *len)
{
    Err error_code = S_OK;
    // init db_results
    bson_t **db_results = malloc(sizeof(bson_t *) * max_n_block);
    if (db_results == NULL) return S_ERR_INIT;
    bzero(db_results, sizeof(bson_t *) * max_n_block);

    error_code = _read_content_blocks_get_db_results(db_results, key, max_n_block, block_id, mongo_db_id, n_block);
    fprintf(stderr, "_dynamic_read_content_blocks_core: after _read_content_blocks_get_db_results: error_code: %d\n", error_code);

    // db_results to content-blocks
    int tmp_n_block = *n_block;
    bson_t **p_db_results = db_results;
    ContentBlock *p_content_blocks = content_blocks;
    char *p_buf = buf;
    int tmp_len_block = 0;
    int tmp_len = 0;

    if (!error_code) {
        for (int i = 0; i < tmp_n_block; i++) {
            if (max_buf_size < 0) {
                error_code = S_ERR_BUFFER_LEN;
                break;
            }

            p_content_blocks->buf_block = p_buf;
            p_content_blocks->max_buf_len = max_buf_size;

            error_code = _deserialize_content_block_bson(*p_db_results, p_content_blocks);

            tmp_len_block = p_content_blocks->len_block;
            p_content_blocks->max_buf_len = tmp_len_block;
            max_buf_size -= tmp_len_block;
            p_buf += tmp_len_block;
            tmp_len += tmp_len_block;

            p_db_results++;
            p_content_blocks++;

            if (error_code) {
                *n_block = i;
                break;
            }
        }
    }

    *len = tmp_len;

    // free
    p_db_results = db_results;
    for (int i = 0; i < tmp_n_block; i++) {
        bson_safe_destroy(p_db_results);
        p_db_results++;
    }
    free(db_results);

    return error_code;
}

Err
_read_content_blocks_get_db_results(bson_t **db_results, bson_t *key, int max_n_block, int block_id, enum MongoDBId mongo_db_id, int *n_block)
{
    Err error_code = S_OK;

    bson_t *b_array_block_ids = bson_new();
    if (b_array_block_ids == NULL) error_code = S_ERR_INIT;

    if (!error_code) {
        error_code = _form_b_array_block_ids(block_id, max_n_block, b_array_block_ids);
    }

    bool status = true;
    if (!error_code) {
        status = bson_append_document(key, "block_id", -1, b_array_block_ids);
        if (!status) error_code = S_ERR;
    }

    if (!error_code) {
        error_code = db_find(mongo_db_id, key, NULL, NULL, max_n_block, n_block, db_results);
    }

    int tmp_n_block = *n_block;

    if (!error_code) {
        error_code = _sort_by_block_id(db_results, tmp_n_block);
    }
    if (!error_code) {
        error_code = _ensure_block_ids(db_results, block_id, tmp_n_block);
    }

    bson_safe_destroy(&b_array_block_ids);

    return error_code;
}

Err
_form_b_array_block_ids(int block_id, int max_n_block, bson_t *b)
{
    Err error_code = S_OK;

    bson_t child;
    char buf[16];
    const char *key;
    size_t keylen;

    bool status;

    BSON_APPEND_ARRAY_BEGIN(b, "$in", &child);
    for (int i = 0; i < max_n_block; i++) {
        keylen = bson_uint32_to_string(i, &key, buf, sizeof(buf));
        status = bson_append_int32(&child, key, (int)keylen, block_id + i);
        if (!status) {
            error_code = S_ERR_INIT;
            break;
        }
    }
    bson_append_array_end(b, &child);

    return error_code;
}

Err
_sort_by_block_id(bson_t **db_results, int n_block)
{
    qsort(db_results, n_block, sizeof(bson_t *), _cmp_sort_by_block_id);

    return S_OK;
}

int
_cmp_sort_by_block_id(const void *a, const void *b)
{
    bson_t **tmp_tmp_a = (bson_t **)a;
    bson_t *tmp_a = *tmp_tmp_a;
    bson_t **tmp_tmp_b = (bson_t **)b;
    bson_t *tmp_b = *tmp_tmp_b;
    int block_id_a = 0;
    int block_id_b = 0;

    Err error_code = S_OK;

    error_code = bson_get_value_int32(tmp_a, "block_id", &block_id_a);
    if (error_code) block_id_a = -1;

    error_code = bson_get_value_int32(tmp_b, "block_id", &block_id_b);
    if (error_code) block_id_b = -1;

    return block_id_a - block_id_b;
}

Err
_ensure_block_ids(bson_t **db_results, int start_block_id, int n_block)
{
    Err error_code = S_OK;
    int db_block_id;
    bson_t **p_db_results = db_results;
    for (int i = 0; i < n_block; i++, p_db_results++) {
        error_code = bson_get_value_int32(*p_db_results, "block_id", &db_block_id);

        if (error_code) db_block_id = -1;

        if (db_block_id != i + start_block_id) return S_ERR;

    }

    return S_OK;
}

/**
 * @brief core for split contents.
 * @details core for split contents. No need to deal with last line block if used by split_contents_from_fd
 *          (There are multiple input-bufs in split_contents_from_fd and split_contents_from_fd will take care of last line-block)
 *
 * @param buf [description]
 * @param bytes [description]
 * @param ref_id [description]
 * @param content_id [description]
 * @param MongoDBId [description]
 * @param n_line [description]
 * @param n_block [description]
 * @param is_deal_wtih_last_line_block [description]
 */
Err
_split_contents_core(char *buf, int bytes, UUID ref_id, UUID content_id, enum MongoDBId mongo_db_id, int *n_line, int *n_block, char *line, int *bytes_in_line, ContentBlock *content_block)
{
    Err error_code;

    int bytes_in_new_line = 0;
    for (int offset_buf = 0; offset_buf < bytes; offset_buf += bytes_in_new_line) {
        error_code = get_line_from_buf(buf, offset_buf, bytes, line, *bytes_in_line, &bytes_in_new_line);
        *bytes_in_line += bytes_in_new_line;
        // unable to get more lines from buf
        if (error_code) break;

        // Main-op
        error_code = _split_contents_core_one_line(line, *bytes_in_line, ref_id, content_id, mongo_db_id, content_block, n_line, n_block);
        if (error_code) return error_code;

        // reset line
        bzero(line, sizeof(char) * *bytes_in_line);
        *bytes_in_line = 0;
    }

    return S_OK;
}

Err
_split_contents_core_one_line(char *line, int bytes_in_line, UUID ref_id, UUID content_id, enum MongoDBId mongo_db_id, ContentBlock *content_block, int *n_line, int *n_block)
{
    Err error_code;

    // check for max-lines in block-buf
    // check for max-buf in block-buf
    if (content_block->n_line >= MAX_BUF_LINES ||
            content_block->len_block + bytes_in_line > MAX_BUF_BLOCK) {

        error_code = save_content_block(content_block, mongo_db_id);
        if (error_code) return error_code;

        error_code = reset_content_block(content_block, ref_id, content_id, *n_block);
        (*n_block)++;
        if (error_code) return error_code;
    }

    memcpy(content_block->buf_block + content_block->len_block, line, bytes_in_line);
    content_block->len_block += bytes_in_line;

    // 1 more line
    if (line[bytes_in_line - 2] == '\r' && line[bytes_in_line - 1] == '\n') {
        (*n_line)++;
        content_block->n_line++;
    }

    return S_OK;
}

Err
_split_contents_deal_with_last_line_block(int bytes_in_line, char *line, UUID ref_id, UUID content_id, enum MongoDBId mongo_db_id, ContentBlock *content_block, int *n_line, int *n_block)
{
    Err error_code = S_OK;

    // last line
    if (bytes_in_line) {
        // Main-op
        error_code = _split_contents_core_one_line(line, bytes_in_line, ref_id, content_id, mongo_db_id, content_block, n_line, n_block);
        if (error_code) return error_code;
    }

    // last block
    if (content_block->len_block) {
        error_code = save_content_block(content_block, mongo_db_id);
        if (error_code) return error_code;
    }

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
_serialize_content_block_bson(ContentBlock *content_block, bson_t **content_block_bson)
{
    *content_block_bson = BCON_NEW(
                              "the_id", BCON_BINARY(content_block->the_id, UUIDLEN),
                              "ref_id", BCON_BINARY(content_block->ref_id, UUIDLEN),
                              "block_id", BCON_INT32(content_block->block_id),
                              "len_block", BCON_INT32(content_block->len_block),
                              "n_line", BCON_INT32(content_block->n_line),
                              "buf_block", BCON_BINARY((unsigned char *)content_block->buf_block, content_block->len_block)
                          );

    return S_OK;
}

/**
 * @brief Deserialize bson to content-block
 * @details Deserialize bson to content-block (receive only len_block, not dealing with '\0' in the end)
 *
 * @param main_content_block_bson [description]
 * @param main_content_block [description]
 *
 * @return [description]
 */
Err
_deserialize_content_block_bson(bson_t *content_block_bson, ContentBlock *content_block)
{
    Err error_code;

    if (content_block->buf_block == NULL) return S_ERR;

    int len;
    error_code = bson_get_value_bin(content_block_bson, "the_id", UUIDLEN, (char *)content_block->the_id, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(content_block_bson, "ref_id", UUIDLEN, (char *)content_block->ref_id, &len);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(content_block_bson, "block_id", &content_block->block_id);
    if (error_code) return error_code;

    error_code = bson_get_value_int32(content_block_bson, "len_block", &content_block->len_block);
    if (error_code) return error_code;

    if (content_block->max_buf_len < content_block->len_block) {
        content_block->len_block = 0;
        return S_ERR_BUFFER_LEN;
    }

    error_code = bson_get_value_int32(content_block_bson, "n_line", &content_block->n_line);
    if (error_code) return error_code;

    error_code = bson_get_value_bin(content_block_bson, "buf_block", content_block->max_buf_len, content_block->buf_block, &len);
    if (error_code) return error_code;


    return S_OK;
}
