#include "cmpttui/pttui_resource_dict.h"
#include "cmpttui/pttui_resource_dict_private.h"

Err
init_pttui_resource_dict(UUID main_id, PttUIResourceDict *resource_dict){
    memcpy(resource_dict->main_id, main_id, UUIDLEN);

    return S_OK;
}

Err
pttui_resource_dict_get_main_from_db(PttQueue *queue, PttUIResourceDict *resource_dict)
{
    if(!queue->n_queue) return S_OK;

    PttUIBuffer *head_buffer = (PttUIBuffer *)queue->head->val.p;
    int min_block_id = head_buffer->block_offset;

    PttUIBuffer *tail_buffer = (PttUIBuffer *)queue->tail->val.p;
    int max_block_id = tail_buffer->block_offset;

    pttui_id_comment_id_dict_add_data(head_buffer->the_id, 0, &resource_dict->id_comment_id_dict);

    return _pttui_resource_dict_get_content_block_from_db_core(head_buffer->the_id, min_block_id, max_block_id, MONGO_MAIN_CONTENT, PTTDB_CONTENT_TYPE_MAIN, resource_dict);
}

Err
safe_destroy_pttui_resource_dict(PttUIResourceDict *resource_dict)
{
    _PttUIResourceDictLinkList *p = NULL;
    _PttUIResourceDictLinkList *tmp = NULL;

    for(int i = 0; i < N_PTTUI_RESOURCE_DICT_LINK_LIST; i++) {
        p = resource_dict->data[i];

        while(p) {
            tmp = p->next;
            if(p->buf) free(p->buf);
            free(p);
            p = tmp;
        }

        resource_dict->data[i] = NULL;
    }

    safe_destroy_pttui_id_comment_id_dict(&resource_dict->id_comment_id_dict);

    return S_OK;
}

Err
_pttui_resource_dict_content_block_db_to_dict(bson_t **b_content_blocks, int n_content_block, enum PttDBContentType content_type, PttUIResourceDict *resource_dict)
{
    Err error_code = S_OK;

    bson_t **p_content_block = b_content_blocks;
    int block_id = 0;
    int len_block = 0;
    char *buf_block = NULL;
    int len = 0;
    UUID the_id = {};
    for(int i = 0; i < n_content_block; i++, p_content_block++) {
        error_code = bson_get_value_bin(*p_content_block, "the_id", UUIDLEN, (char *)the_id, &len);
        if(error_code) break;

        error_code = bson_get_value_int32(*p_content_block, "block_id", &block_id);
        if(error_code) break;

        error_code = bson_get_value_int32(*p_content_block, "len_block", &len_block);
        if(error_code) break;

        error_code = bson_get_value_bin_not_initialized(*p_content_block, "buf_block", &buf_block, &len);
        if(error_code) break;

        error_code = _pttui_resource_dict_add_data(the_id, block_id, INVALID_FILE_OFFSET, len_block, buf_block, content_type, resource_dict);
        if(error_code) break;
    }

    return error_code;
}

Err
pttui_resource_dict_get_comment_from_db(PttQueue *queue, PttUIResourceDict *resource_dict)
{
    Err error_code = S_OK;
    if(!queue->n_queue) return S_OK;

    bson_t child = {};
    char buf[16] = {};
    const char *array_key = NULL;
    size_t array_keylen = 0;

    bson_t *q_array = bson_new();

    PttUIBuffer *p_buffer = NULL;
    BSON_APPEND_ARRAY_BEGIN(q_array, "$in", &child);    
    int i = 0;
    int status = 0;
    PttLinkList *p = queue->head;
    for(; p; p = p->next, i++) {
        p_buffer = (PttUIBuffer *)p->val.p;
        array_keylen = bson_uint32_to_string(i, &array_key, buf, sizeof(buf));
        status = bson_append_bin(&child, array_key, (int)array_keylen, p_buffer->the_id, UUIDLEN);
        if (!status) {
            error_code = S_ERR;
            break;
        }

        pttui_id_comment_id_dict_add_data(p_buffer->the_id, p_buffer->comment_offset, &resource_dict->id_comment_id_dict);
    }
    bson_append_array_end(q_array, &child);

    bson_t *q = BCON_NEW(
        "the_id", BCON_DOCUMENT(q_array)
        );

    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "the_id", BCON_BOOL(true),
        "len", BCON_BOOL(true),
        "buf", BCON_BOOL(true)
        );

    int max_n_comment = queue->n_queue;
    int n_comment = 0;

    bson_t **b_comments = NULL;
    if(max_n_comment) {
        b_comments = malloc(sizeof(bson_t *) * max_n_comment);
        bzero(b_comments, sizeof(bson_t *) * max_n_comment);
    }

    if(!error_code && max_n_comment) {
        error_code = read_comments_by_query_to_bsons(q, fields, max_n_comment, b_comments, &n_comment);
    }

    if(!error_code && n_comment) {
        error_code = _pttui_resource_dict_comment_db_to_dict(b_comments, n_comment, resource_dict);
    }

    //free
    bson_safe_destroy(&q);
    bson_safe_destroy(&fields);
    bson_safe_destroy(&q_array);
    safe_free_b_list(&b_comments, n_comment);

    return S_OK;
}

Err
_pttui_resource_dict_comment_db_to_dict(bson_t **b_comments, int n_comment, PttUIResourceDict *resource_dict)
{
    Err error_code = S_OK;

    bson_t **p_comment = b_comments;
    int block_id = 0;
    int len_block = 0;
    char *buf_block = NULL;
    int len = 0;
    UUID the_id = {};
    for(int i = 0; i < n_comment; i++, p_comment++) {
        error_code = bson_get_value_bin(*p_comment, "the_id", UUIDLEN, (char *)the_id, &len);
        if(error_code) break;

        error_code = bson_get_value_int32(*p_comment, "len", &len_block);
        if(error_code) break;

        error_code = bson_get_value_bin_not_initialized(*p_comment, "buf", &buf_block, &len);
        if(error_code) break;

        error_code = _pttui_resource_dict_add_data(the_id, block_id, INVALID_FILE_OFFSET, len_block, buf_block, PTTDB_CONTENT_TYPE_COMMENT, resource_dict);
        if(error_code) break;
    }

    return error_code;
}

Err
pttui_resource_dict_get_comment_reply_from_db(PttQueue *queue, PttUIResourceDict *resource_dict)
{
    Err error_code = S_OK;
    if(!queue->n_queue) return S_OK;

    bson_t child = {};
    char buf[16] = {};
    const char *array_key = NULL;
    size_t array_keylen = 0;

    PttUIBuffer *p_pre_buffer = NULL;
    PttUIBuffer *p_buffer = NULL;
    PttLinkList *p = NULL;

    bson_t *q_array = NULL;

    // init-head
    PttUIBuffer *p_head_buffer = (PttUIBuffer *)queue->head->val.p;
    UUID head_uuid = {};
    memcpy(head_uuid, p_head_buffer->the_id, UUIDLEN);
    pttui_id_comment_id_dict_add_data(p_head_buffer->the_id, p_head_buffer->comment_offset, &resource_dict->id_comment_id_dict);

    // init-tail
    PttUIBuffer *p_tail_buffer = (PttUIBuffer *)queue->tail->val.p;
    UUID tail_uuid = {};
    memcpy(tail_uuid, p_tail_buffer->the_id, UUIDLEN);
    pttui_id_comment_id_dict_add_data(p_head_buffer->the_id, p_head_buffer->comment_offset, &resource_dict->id_comment_id_dict);

    // head
    p_pre_buffer = p_head_buffer;
    for(p = queue->head; p; p = p->next) {
        p_buffer = (PttUIBuffer *)p->val.p;
        if(memcmp(head_uuid, p_buffer->the_id, UUIDLEN)) break;
        p_pre_buffer = p_buffer;

    }    

    int min_block_id = p_head_buffer->block_offset;
    int max_block_id = p_pre_buffer->block_offset;

    error_code = _pttui_resource_dict_get_content_block_from_db_core(head_uuid, min_block_id, max_block_id, MONGO_COMMENT_REPLY_BLOCK, PTTDB_CONTENT_TYPE_COMMENT_REPLY, resource_dict);

    if(error_code) return error_code;

    if(!p) return S_OK;

    // middle
    int max_n_middle = 0;
    q_array = bson_new();
    BSON_APPEND_ARRAY_BEGIN(q_array, "$in", &child);
    UUID pre_uuid = {};
    int i = 0;    
    int status = 0;
    for(; p; p = p->next, max_n_middle++, i++) {
        p_buffer = (PttUIBuffer *)p->val.p;
        if(!memcmp(p_buffer->the_id, tail_uuid, UUIDLEN)) break;

        if(!memcmp(p_buffer->the_id, pre_uuid, UUIDLEN)) continue;
        memcpy(pre_uuid, p_buffer->the_id, UUIDLEN);

        //_pttui_resource_dict_add_the_id_comment_id_map(p_buffer->the_id, p_buffer->comment_offset, resource_dict);

        array_keylen = bson_uint32_to_string(i, &array_key, buf, sizeof(buf));
        status = bson_append_bin(&child, array_key, (int)array_keylen, p_buffer->the_id, UUIDLEN);
        if (!status) {
            error_code = S_ERR;
            break;
        }        
    }
    bson_append_array_end(q_array, &child);

    bson_t *q_middle = NULL;
    if(max_n_middle) {
        q_middle = BCON_NEW(
            "the_id", BCON_DOCUMENT(q_array)
            );
        error_code = _pttui_resource_dict_get_content_block_from_db_core2(q_middle, max_n_middle, MONGO_COMMENT_REPLY_BLOCK, PTTDB_CONTENT_TYPE_COMMENT_REPLY, resource_dict);
    }

    // tail
    if(!error_code && p) {
        p_buffer = (PttUIBuffer *)p->val.p;
        min_block_id = p_buffer->block_offset;
        max_block_id = p_tail_buffer->block_offset;
        error_code = _pttui_resource_dict_get_content_block_from_db_core(tail_uuid, min_block_id, max_block_id, MONGO_COMMENT_REPLY_BLOCK, PTTDB_CONTENT_TYPE_COMMENT_REPLY, resource_dict);
    }

    //free
    bson_safe_destroy(&q_array);
    bson_safe_destroy(&q_middle);

    return error_code;
}

Err
_pttui_resource_dict_add_data(UUID the_id, int block_id, int file_id, int len, char *buf, enum PttDBContentType content_type, PttUIResourceDict *resource_dict)
{
    Err error_code = S_OK;
    // XXX buf_block need to be freed after copy to pttui-buffer
    int comment_id = 0;
    //Err error_code = bson_get_value_int32(resource_dict->b_the_id_comment_id_map, disp_uuid, &comment_id);
    error_code = pttui_id_comment_id_dict_get_comment_id(&resource_dict->id_comment_id_dict, the_id, &comment_id);
    if(error_code) return error_code;

    int the_idx = ((int)the_id[0] + block_id + file_id + N_PTTUI_RESOURCE_DICT_LINK_LIST) % N_PTTUI_RESOURCE_DICT_LINK_LIST;
    _PttUIResourceDictLinkList *p = resource_dict->data[the_idx];
    if(!p) {
        resource_dict->data[the_idx] = malloc(sizeof(_PttUIResourceDictLinkList));
        p = resource_dict->data[the_idx];
    }
    else {
        for(; p->next; p = p->next) {
            if(!memcmp(p->the_id, the_id, UUIDLEN) && p->block_id == block_id && p->file_id == file_id) return S_OK;
        }

        p->next = malloc(sizeof(_PttUIResourceDictLinkList));
        p = p->next;
    }

    p->next = NULL;
    memcpy(p->the_id, the_id, UUIDLEN);
    p->content_type = content_type;
    p->comment_id = comment_id;
    p->block_id = block_id;
    p->file_id = file_id;
    p->len = len;
    p->buf = buf;

    return S_OK;
}

Err
pttui_resource_dict_get_data(PttUIResourceDict *resource_dict, UUID the_id, int block_id, int file_id, int *len, char **buf)
{
    int the_idx = ((int)the_id[0] + block_id + file_id + N_PTTUI_RESOURCE_DICT_LINK_LIST) % N_PTTUI_RESOURCE_DICT_LINK_LIST;
    _PttUIResourceDictLinkList *p = resource_dict->data[the_idx];
    for(; p; p = p->next) {
        if(!memcmp(the_id, p->the_id, UUIDLEN) && block_id == p->block_id && file_id == p->file_id) {
            *len = p->len;
            *buf = p->buf;
            break;
        }
    }

    if(!p) {
        *len = 0;
        *buf = NULL;
        return S_ERR_NOT_EXISTS;
    }

    return S_OK;
}

Err
pttui_resource_dict_get_link_list(PttUIResourceDict *resource_dict, UUID the_id, int block_id, int file_id, _PttUIResourceDictLinkList **dict_link_list)
{
    int the_idx = ((int)the_id[0] + block_id + file_id + N_PTTUI_RESOURCE_DICT_LINK_LIST) % N_PTTUI_RESOURCE_DICT_LINK_LIST;
    _PttUIResourceDictLinkList *p = resource_dict->data[the_idx];
    for(; p; p = p->next) {
        if(!memcmp(the_id, p->the_id, UUIDLEN) && block_id == p->block_id && file_id == p->file_id) break;
    }
    *dict_link_list = p;

    if(!p) return S_ERR_NOT_EXISTS;

    return S_OK;
}


Err
_pttui_resource_dict_get_content_block_from_db_core(UUID uuid, int min_block_id, int max_block_id, enum MongoDBId mongo_db_id, enum PttDBContentType content_type, PttUIResourceDict *resource_dict)
{    
    Err error_code = S_OK;
    bson_t *q = BCON_NEW(
        "the_id", BCON_BINARY(uuid, UUIDLEN),
        "block_id", "{",
            "$gte", BCON_INT32(min_block_id),
            "$lte", BCON_INT32(max_block_id),
        "}"        
        );

    int max_n_content_block = max_block_id - min_block_id + 1;

    error_code = _pttui_resource_dict_get_content_block_from_db_core2(q, max_n_content_block, mongo_db_id, content_type, resource_dict);

    // free

    bson_safe_destroy(&q);

    return error_code;
}

Err
_pttui_resource_dict_get_content_block_from_db_core2(bson_t *q, int max_n_content_block, enum MongoDBId mongo_db_id, enum PttDBContentType content_type, PttUIResourceDict *resource_dict)
{
    Err error_code = S_OK;

    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "the_id", BCON_BOOL(true),
        "block_id", BCON_BOOL(true),
        "len_block", BCON_BOOL(true),
        "buf_block", BCON_BOOL(true)
        );

    bson_t **b_content_blocks = malloc(sizeof(bson_t *) * max_n_content_block);
    bzero(b_content_blocks, sizeof(bson_t *) * max_n_content_block);
    int n_content_block = 0;

    error_code = read_content_blocks_by_query_to_bsons(q, fields, max_n_content_block, mongo_db_id, b_content_blocks, &n_content_block);

    if(!error_code) {
        error_code = _pttui_resource_dict_content_block_db_to_dict(b_content_blocks, n_content_block, content_type, resource_dict);
    }

    // free
    bson_safe_destroy(&fields);
    safe_free_b_list(&b_content_blocks, n_content_block);

    return error_code;
}

Err
pttui_resource_dict_get_main_from_file(PttQueue *queue, PttUIResourceDict *resource_dict)
{
    if(!queue->n_queue) return S_OK;

    Err error_code = S_OK;
    PttLinkList *p = queue->head;
    PttUIBuffer *p_buffer = NULL;
    for(; p; p = p->next) {
        p_buffer = (PttUIBuffer *)p->val.p;
        error_code = _pttui_resource_dict_get_content_block_from_file_core(p_buffer, resource_dict);
        if(error_code) break;
    }

    return error_code;
}

Err
pttui_resource_dict_get_comment_from_file(PttQueue *queue, PttUIResourceDict *resource_dict GCC_UNUSED)
{    
    if(!queue->n_queue) return S_OK;

    return S_ERR;
}

Err
pttui_resource_dict_get_comment_reply_from_file(PttQueue *queue, PttUIResourceDict *resource_dict)
{
    if(!queue->n_queue) return S_OK;

    Err error_code = S_OK;
    PttLinkList *p = queue->head;
    PttUIBuffer *p_buffer = NULL;
    for(; p; p = p->next) {
        p_buffer = (PttUIBuffer *)p->val.p;
        error_code = _pttui_resource_dict_get_content_block_from_file_core(p_buffer, resource_dict);
        if(error_code) break;
    }

    return error_code;
}

Err
_pttui_resource_dict_get_content_block_from_file_core(PttUIBuffer *buffer, PttUIResourceDict *resource_dict)
{
    char *buf = NULL;
    int len = 0;
    Err error_code = pttdb_file_get_data(resource_dict->main_id, buffer->content_type, buffer->the_id, buffer->block_offset, buffer->file_offset, &buf, &len);

    error_code = _pttui_resource_dict_add_data(buffer->the_id, buffer->block_offset, buffer->file_offset, len, buf, buffer->content_type, resource_dict);

    // do not free buf because buf is used in resource_dict and freed in safe_destroy_resource_dict .
    //safe_free((void **)&buf); 

    return error_code;
}

Err
pttui_resource_dict_save_to_tmp_file(PttUIResourceDict *resource_dict)
{
    Err error_code = S_OK;

    char dir_prefix[MAX_FILENAME_SIZE] = {};

    _PttUIResourceDictLinkList *p_dict_link_list = NULL;
    int j = 0;

    for(int i = 0; i < N_PTTUI_RESOURCE_DICT_LINK_LIST; i++) {
        if(!resource_dict->data[i]) continue;
        for(j = 0, p_dict_link_list = resource_dict->data[i]; p_dict_link_list; j++, p_dict_link_list = p_dict_link_list->next) {
            error_code = _pttui_resource_dict_save_to_tmp_file(p_dict_link_list, resource_dict->main_id);
            if(error_code) break;
        }
        if(error_code) break;
    }

    return error_code;
}

Err
_pttui_resource_dict_save_to_tmp_file(_PttUIResourceDictLinkList *dict_link_list, UUID main_id)
{
    char filename[MAX_FILENAME_SIZE] = {};

    Err error_code = pttdb_file_save_data(
        main_id,
        dict_link_list->content_type,
        dict_link_list->the_id,
        dict_link_list->block_id,
        dict_link_list->file_id,
        dict_link_list->buf,
        dict_link_list->len
        );

    return error_code;
}

/**********
 * buffer-info integrate to resource dict
 **********/

/**
 * @brief [brief description]
 * @details ref: _modified_pttui_buffer_info_to_resource_info in pttui_buffer.c
 * 
 * @param head [description]
 * @param tail [description]
 * @param resource_dict [description]
 */ 
Err
pttui_resource_dict_integrate_with_modified_pttui_buffer_info(PttUIBuffer *head, PttUIBuffer *tail, PttUIResourceDict *resource_dict)
{
    Err error_code = S_OK;
    int max_buf_size = 0;
    char *tmp_buf = NULL;
    int len_tmp_buf = 0;
    int line_offset_tmp_buf = 0;

    _PttUIResourceDictLinkList *current_dict = NULL;
    char *p_dict_buf = NULL;
    int len_dict_buf = 0;
    int dict_buf_offset = 0;
    int line_offset_dict_buf = 0;    

    char *p_next_dict_buf = NULL;
    int dict_buf_next_offset = 0;

    int the_rest_len = 0;
    for (PttUIBuffer *current_buffer = head; current_buffer && current_buffer != tail->next; current_buffer = current_buffer->next) {
        if(!current_buffer->is_modified) continue;

        if(!current_dict || current_buffer->block_offset != current_dict->block_id || current_buffer->file_offset != current_dict->file_id || memcmp(current_buffer->the_id, current_dict->the_id, UUIDLEN)) {
            if(current_dict) {
                the_rest_len = len_dict_buf - (p_dict_buf - current_dict->buf);
                error_code = safe_strcat(&tmp_buf, &max_buf_size, MAX_BUF_SIZE, &len_tmp_buf, p_dict_buf, the_rest_len);

                safe_free((void **)&current_dict->buf);
                current_dict->buf = malloc(len_tmp_buf + 1);
                memcpy(current_dict->buf, tmp_buf, len_tmp_buf);
                current_dict->buf[len_tmp_buf] = 0;
                current_dict->len = len_tmp_buf;
            }

            max_buf_size = MAX_BUF_SIZE;
            tmp_buf = realloc(tmp_buf, max_buf_size);
            len_tmp_buf = 0;
            line_offset_tmp_buf = 0;

            error_code = pttui_resource_dict_get_link_list(resource_dict, current_buffer->the_id, current_buffer->block_offset, current_buffer->file_offset, &current_dict);
            if(error_code) break;

            p_dict_buf = current_dict->buf;
            len_dict_buf = current_dict->len;
            dict_buf_offset = 0;
            line_offset_dict_buf = 0;

            p_next_dict_buf = NULL;
            dict_buf_next_offset = 0;
        }

        int start_i = current_buffer->is_new ? line_offset_tmp_buf : line_offset_dict_buf;
        int end_i = current_buffer->is_new ? current_buffer->line_offset : current_buffer->load_line_offset;

        for(int i = start_i; i < end_i; i++) {
            // 1. get the next buf to know the len of current-buf
            error_code = pttui_resource_dict_get_next_buf(p_dict_buf, dict_buf_offset, len_dict_buf, &p_next_dict_buf, &dict_buf_next_offset);

            // 2. save to the tmp-buf
            error_code = safe_strcat(&tmp_buf, &max_buf_size, MAX_BUF_SIZE, &len_tmp_buf, p_dict_buf, p_next_dict_buf - p_dict_buf);
            line_offset_tmp_buf++;

            // 3. move the current-buf to the next-buf
            p_dict_buf = p_next_dict_buf;
            dict_buf_offset = dict_buf_next_offset;
            line_offset_dict_buf++;            
        }

        if(current_buffer->is_to_delete && current_buffer->is_new) {
        }
        else if(current_buffer->is_to_delete) {
            // move the current-buf to the next-buf
            error_code = pttui_resource_dict_get_next_buf(p_dict_buf, dict_buf_offset, len_dict_buf, &p_next_dict_buf, &dict_buf_next_offset);
            p_dict_buf = p_next_dict_buf;
            dict_buf_offset = dict_buf_next_offset;
            line_offset_dict_buf++;
        }
        else if(current_buffer->is_new) {
            // save the current-buffer to the tmp-buf
            error_code = safe_strcat(&tmp_buf, &max_buf_size, MAX_BUF_SIZE, &len_tmp_buf, current_buffer->buf, current_buffer->len_no_nl);

            error_code = safe_strcat(&tmp_buf, &max_buf_size, MAX_BUF_SIZE, &len_tmp_buf, PTTUI_NEWLINE, LEN_PTTUI_NEWLINE);
            
            line_offset_tmp_buf++;
        }
        else {
            // 1. get the next buf.
            error_code = pttui_resource_dict_get_next_buf(p_dict_buf, dict_buf_offset, len_dict_buf, &p_next_dict_buf, &dict_buf_next_offset);

            // 2. save the current-buffer (not current-buf) to the tmp-buf.
            error_code = safe_strcat(&tmp_buf, &max_buf_size, MAX_BUF_SIZE, &len_tmp_buf, current_buffer->buf, current_buffer->len_no_nl);

            error_code = safe_strcat(&tmp_buf, &max_buf_size, MAX_BUF_SIZE, &len_tmp_buf, PTTUI_NEWLINE, LEN_PTTUI_NEWLINE);
            line_offset_tmp_buf++;
            
            // 3. move the current-buf to the next-buf.
            p_dict_buf = p_next_dict_buf;
            dict_buf_offset = dict_buf_next_offset;
            line_offset_dict_buf++;
        }

        if (error_code) break;
    }

    if(current_dict) {
        the_rest_len = len_dict_buf - (p_dict_buf - current_dict->buf);
        error_code = safe_strcat(&tmp_buf, &max_buf_size, MAX_BUF_SIZE, &len_tmp_buf, p_dict_buf, the_rest_len);

        safe_free((void **)&current_dict->buf);
        current_dict->buf = malloc(len_tmp_buf + 1);
        memcpy(current_dict->buf, tmp_buf, len_tmp_buf);
        current_dict->buf[len_tmp_buf] = 0;
        current_dict->len = len_tmp_buf;
    }

    // free
    safe_free((void **)&tmp_buf);

    return error_code;
}

Err
pttui_resource_dict_get_next_buf(char *p_buf, int buf_offset, int len, char **p_next_buf, int *buf_next_offset)
{
    int tmp_next_offset = 0;
    char *tmp_next_buf = NULL;
    for(tmp_next_offset = buf_offset, tmp_next_buf = p_buf; tmp_next_offset < len && tmp_next_buf && *tmp_next_buf != '\n'; tmp_next_offset++, tmp_next_buf++);

    if(tmp_next_offset != len) {
        tmp_next_offset++;
        tmp_next_buf++;
    }

    *p_next_buf = tmp_next_buf;
    *buf_next_offset = tmp_next_offset;

    return S_OK;
}

Err
pttui_resource_dict_reset_file_info(PttUIResourceDict *resource_dict, FileInfo *file_info)
{
    _PttUIResourceDictLinkList *p = NULL;
    CommentInfo *p_comment = NULL;
    ContentBlockInfo *p_content_block = NULL;

    int j = 0;
    for(int i = 0; i < N_PTTUI_RESOURCE_DICT_LINK_LIST; i++) {
        p = resource_dict->data[i];
        for(j = 0; p; p = p->next, j++) {
            switch(p->content_type) {
            case PTTDB_CONTENT_TYPE_MAIN:
                p_content_block = file_info->main_blocks + p->block_id;
                break;
            case PTTDB_CONTENT_TYPE_COMMENT_REPLY:
                p_comment = file_info->comments + p->comment_id;
                p_content_block = p_comment->comment_reply_blocks + p->block_id;
                break;
            default:
                p_content_block = NULL;
                break;
            }

            if(!p_content_block) continue;

            // comment->n-comment-reply-line
            if(p->content_type == PTTDB_CONTENT_TYPE_COMMENT_REPLY) {
                p_comment->n_comment_reply_total_line += (p_content_block->n_line - p_content_block->n_line_in_db);
            }

            // p-content-block
            p_content_block->n_line_in_db = p_content_block->n_line;
            p_content_block->n_new_line = 0;
            p_content_block->n_to_delete_line = 0;

            // XXX not split file for now.
            p_content_block->n_file = 1;
            p_content_block->file_n_line = realloc(p_content_block->file_n_line, sizeof(int) * p_content_block->n_file);
            p_content_block->file_n_line[p->file_id] = p_content_block->n_line_in_db;

            p_content_block->storage_type = PTTDB_STORAGE_TYPE_FILE;
        }
    }

    return S_OK;
}

Err
log_pttui_resource_dict(PttUIResourceDict *resource_dict, char *prompt)
{
    char *disp_uuid = NULL;
    _PttUIResourceDictLinkList *p_dict_link_list = NULL;
    int j = 0;
    for(int i = 0; i < N_PTTUI_RESOURCE_DICT_LINK_LIST; i++) {
        if(!resource_dict->data[i]) continue;
        for(j = 0, p_dict_link_list = resource_dict->data[i]; p_dict_link_list; j++, p_dict_link_list = p_dict_link_list->next) {
            disp_uuid = display_uuid(p_dict_link_list->the_id);
            fprintf(stderr, "%s: (%d/%d): (%s, comment-id: %d block-id: %d file-id: %d)\n", prompt, i, j, disp_uuid, p_dict_link_list->comment_id, p_dict_link_list->block_id, p_dict_link_list->file_id);
            free(disp_uuid);
        }
    }

    fprintf(stderr, "%s: done\n", prompt);

    return S_OK;
}
