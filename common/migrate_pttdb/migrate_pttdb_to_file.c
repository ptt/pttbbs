#include "cmmigrate_pttdb/migrate_pttdb_to_file.h"
#include "cmmigrate_pttdb/migrate_pttdb_to_file_private.h"

/**
 * @brief [brief description]
 * @details adopted from edit.c line: 2015 (assert(*fpath) in write_file)
 *          many of the code are already integrated in vedit3 (in db)
 *
 *          XXX need to make sure whether to put the last empty line in the file.
 * 
 * @param main_id [description]
 * @param fpath [description]
 * @param saveheader [description]
 * @param title [description]
 * @param flags [description]
 */
Err
migrate_pttdb_to_file(UUID main_id, const char *fpath)
{
    Err error_code = S_OK;
    FILE *fp = NULL;

    if((fp = fopen(fpath, "w")) == NULL) {
        return S_ERR_ABORT_BBS;
    }

    MainHeader main_header = {};
    error_code = read_main_header(main_id, &main_header);

    if(!error_code) {
        error_code = _migrate_main_content_to_file(&main_header, fp);
    }

    if(!error_code) {
        error_code = _migrate_comment_comment_reply_by_main_to_file(main_header.the_id, fp);
    }

    // free
    fclose(fp);
    fp = NULL;

    return error_code;
}


Err
_migrate_main_content_to_file(MainHeader *main_header, FILE *fp)
{
    Err error_code = S_OK;
    int next_i = 0;
    int n_total_block = main_header->n_total_block;
    UUID content_id = {};
    memcpy(content_id, main_header->content_id, UUIDLEN);
    for(int i = 0; i < n_total_block; i += N_MIGRATE_MAIN_CONTENT_TO_FILE_BLOCK) {
        next_i = (i + N_MIGRATE_MAIN_CONTENT_TO_FILE_BLOCK) < n_total_block ? (i + N_MIGRATE_MAIN_CONTENT_TO_FILE_BLOCK) : n_total_block;
        error_code = _migrate_main_content_to_file_core(content_id, fp, i, next_i);
        if(error_code) break;
    }

    return error_code;
}

Err
_migrate_main_content_to_file_core(UUID content_id, FILE *fp, int start_block_id, int next_block_id)
{
    Err error_code = S_OK;
    char buf[MAX_BUF_SIZE * N_MIGRATE_MAIN_CONTENT_TO_FILE_BLOCK] = {};

    int n_block = 0;
    int len = 0;

    ContentBlock content_blocks[N_MIGRATE_COMMENT_COMMENT_REPLY_TO_FILE_BLOCK] = {};

    error_code = dynamic_read_content_blocks(content_id, next_block_id - start_block_id, start_block_id, MONGO_MAIN_CONTENT, buf, MAX_BUF_SIZE * N_MIGRATE_COMMENT_COMMENT_REPLY_TO_FILE_BLOCK, content_blocks, &n_block, &len);

    if(error_code) return error_code;

    int ret = 0;
    if(!error_code) {
        ret = fprintf(fp, "%s", buf);
        if(ret < 0) error_code = S_ERR;
    }

    return error_code;
}

Err
_migrate_comment_comment_reply_by_main_to_file(UUID main_id, FILE *fp)
{
    Err error_code = S_OK;
    UUID comment_id = {};
    time64_t create_milli_timestamp = 0;
    char poster[IDLEN + 1] = {};
    int n_expected_comment = 0;

    error_code = get_newest_comment(main_id, comment_id, &create_milli_timestamp, poster, &n_expected_comment);
    if(error_code == S_ERR_NOT_EXISTS) return S_OK;

    bson_t **b_comments = NULL;
    int n_comment = 0;
    if(!error_code) {
        b_comments = malloc(sizeof(bson_t *) * n_expected_comment);
        if(!b_comments) error_code = S_ERR_ABORT_BBS;
    }

    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "the_id", BCON_BOOL(true),
        "comment_reply_id", BCON_BOOL(true),
        "n_comment_reply_block", BCON_BOOL(true),
        "create_milli_timestamp", BCON_BOOL(true),
        "poster", BCON_BOOL(true)
        );
    if(!fields) error_code = S_ERR_ABORT_BBS;

    if(!error_code) {
        error_code = read_comments_until_newest_to_bsons(main_id, create_milli_timestamp, poster, fields, n_expected_comment, b_comments, &n_comment);
    }

    if(!error_code) {
        error_code = ensure_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC);
    }

    int next_i = 0;
    bson_t **p_b_comments = b_comments;
    if(!error_code) {
        for(int i = 0; i < n_comment; i += N_MIGRATE_COMMENT_COMMENT_REPLY_TO_FILE_BLOCK, p_b_comments += N_MIGRATE_COMMENT_COMMENT_REPLY_TO_FILE_BLOCK) {
            next_i = (i + N_MIGRATE_COMMENT_COMMENT_REPLY_TO_FILE_BLOCK) < n_comment ? (i + N_MIGRATE_COMMENT_COMMENT_REPLY_TO_FILE_BLOCK) : n_comment;
            error_code = _migrate_comment_comment_reply_by_main_to_file_core(p_b_comments, next_i - i, fp);
            if(error_code) break;
        }
    }

    // free
    bson_safe_destroy(&fields);
    safe_free_b_list(&b_comments, n_comment);

    return error_code;
}

Err
_migrate_comment_comment_reply_by_main_to_file_core(bson_t **b_comments, int n_comment, FILE *fp)
{
    Err error_code = S_OK;

    char buf[MAX_MIGRATE_COMMENT_COMMENT_REPLY_BUF_SIZE] = {};
    int n_read_comment = 0;
    int n_comment_reply = 0;
    int len = 0;

    bson_t **p_b_comments = b_comments;
    for(; n_comment > 0; p_b_comments += n_read_comment, n_comment -= n_read_comment, bzero(buf, len)) {
        error_code = dynamic_read_b_comment_comment_reply_by_ids_to_file(p_b_comments, n_comment, fp, &n_read_comment, &n_comment_reply);
        if(error_code) break;
    }

    return error_code;
}