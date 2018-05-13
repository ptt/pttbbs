#include "cmpttui/pttui_buffer.h"
#include "cmpttui/pttui_buffer_private.h"

bool
pttui_buffer_is_begin_ne(PttUIBuffer *buffer, PttUIBuffer *head) {
    PttUIBuffer *pre = pttui_buffer_pre_ne(buffer, head);
    return pre == NULL;
}

bool
pttui_buffer_is_end_ne(PttUIBuffer *buffer, PttUIBuffer *tail) {
    PttUIBuffer *next = pttui_buffer_next_ne(buffer, tail);
    return next == NULL;
}

PttUIBuffer *
pttui_buffer_next_ne(PttUIBuffer *buffer, PttUIBuffer *tail) {
    if (!buffer) return NULL;

    if(buffer == tail) return NULL;

    // basic operation, use ->next directly
    PttUIBuffer *p_next = buffer->next;
    for (; p_next && p_next->is_to_delete; p_next = p_next->next);

    return p_next;
}

PttUIBuffer *
pttui_buffer_pre_ne(PttUIBuffer *buffer, PttUIBuffer *head) {
    if(!buffer) return NULL;

    if(buffer == head) return NULL;

    // basic operation, use ->pre directly
    PttUIBuffer *p_pre = buffer->pre;
    for(; p_pre && p_pre->is_to_delete; p_pre = p_pre->pre);

    return p_pre;
}

Err
safe_free_pttui_buffer(PttUIBuffer **buffer)
{
    PttUIBuffer *p_buffer = *buffer;
    if(!p_buffer) return S_OK;

    // basic operation, use ->pre and ->next directly
    PttUIBuffer *p_pre_buffer = p_buffer->pre;
    PttUIBuffer *p_next_buffer = p_buffer->next;
    if(p_pre_buffer) p_pre_buffer->next = p_next_buffer;
    if(p_next_buffer) p_next_buffer->pre = p_pre_buffer;

    if(p_buffer->buf) free(p_buffer->buf);
    free(p_buffer);
    *buffer = NULL;

    return S_OK;
}

Err
destroy_pttui_buffer_info(PttUIBufferInfo *buffer_info)
{
    // basic operation, use ->next directly
    PttUIBuffer *p_buffer = buffer_info->head;
    PttUIBuffer *tmp = NULL;
    while (p_buffer != NULL) {
        tmp = p_buffer;
        p_buffer = p_buffer->next;
        safe_free_pttui_buffer(&tmp);
    }

    bzero(buffer_info, sizeof(PttUIBufferInfo));

    return S_OK;
}

Err
pttui_buffer_is_begin_of_file(PttUIBuffer *buffer, FileInfo *file_info GCC_UNUSED, bool *is_begin)
{
    if (buffer->content_type == PTTDB_CONTENT_TYPE_MAIN &&
        buffer->block_offset == 0 &&
        buffer->line_offset == 0) {
        *is_begin = true;
    }
    else {
        *is_begin = false;
    }

    return S_OK;
}

Err
pttui_buffer_is_eof(PttUIBuffer *buffer, FileInfo *file_info, bool *is_eof)
{
    Err error_code = pttui_buffer_rdlock_file_info();
    if(error_code) return error_code;

    if (!file_info->n_comment &&
        buffer->block_offset == file_info->n_main_block - 1 &&
        buffer->line_offset == file_info->main_blocks[buffer->block_offset].n_line - 1) {
        *is_eof = true;
    }
    else if (file_info->n_comment &&
        buffer->comment_offset == file_info->n_comment - 1 &&
        buffer->content_type == PTTDB_CONTENT_TYPE_COMMENT &&
        file_info->comments[buffer->comment_offset].n_comment_reply_block == 0) {
        *is_eof = true;
    }
    else if (file_info->n_comment &&
        buffer->comment_offset == file_info->n_comment - 1 &&
        buffer->content_type == PTTDB_CONTENT_TYPE_COMMENT_REPLY &&
        file_info->comments[buffer->comment_offset].n_comment_reply_block &&
        buffer->block_offset == file_info->comments[buffer->comment_offset].n_comment_reply_block - 1 &&
        buffer->line_offset == file_info->comments[buffer->comment_offset].comment_reply_blocks[buffer->block_offset].n_line - 1) {
        *is_eof = true;
    }
    else {
        *is_eof = false;
    }

    Err error_code_lock = pttui_buffer_unlock_file_info();
    if(!error_code && error_code_lock) error_code = error_code_lock;

    return error_code;
}

/**********
 * sync pttui buffer info
 **********/
Err
sync_pttui_buffer_info(PttUIBufferInfo *buffer_info, PttUIBuffer *current_buffer, PttUIState *state, FileInfo *file_info, PttUIBuffer **new_buffer)
{
    Err error_code = S_OK;

    // determine new buffer of the expected-state
    bool tmp_is_pre = false;

    error_code = pttui_buffer_rdlock_buffer_info();
    if(error_code) return error_code;

    error_code = _sync_pttui_buffer_info_is_pre(state, current_buffer, &tmp_is_pre);

    if(!error_code) {
        error_code = _sync_pttui_buffer_info_get_buffer(state, current_buffer, tmp_is_pre, new_buffer, buffer_info);
    }

    Err error_code_lock = pttui_buffer_unlock_buffer_info();
    if(!error_code && error_code_lock) error_code = error_code_lock;

    if(error_code) return error_code;

    // found buffer in the current buffer-info
    if(*new_buffer) return S_OK;

    // not found buffer in the current buffer-info => resync-all
    return resync_all_pttui_buffer_info(buffer_info, state, file_info, new_buffer);
}

Err
_sync_pttui_buffer_info_is_pre(PttUIState *state, PttUIBuffer *buffer, bool *is_pre)
{
    // content-type as main
    if (state->top_line_content_type == PTTDB_CONTENT_TYPE_MAIN && buffer->content_type != PTTDB_CONTENT_TYPE_MAIN) {
        *is_pre = true;
        return S_OK;
    }

    if (state->top_line_content_type != PTTDB_CONTENT_TYPE_MAIN && buffer->content_type == PTTDB_CONTENT_TYPE_MAIN) {
        *is_pre = false;
        return S_OK;
    }

    // both are as main
    if (state->top_line_content_type == PTTDB_CONTENT_TYPE_MAIN && buffer->content_type == PTTDB_CONTENT_TYPE_MAIN) {
        if (state->top_line_block_offset != buffer->block_offset) {
            *is_pre = state->top_line_block_offset < buffer->block_offset ? true : false;
        }
        else {
            *is_pre = state->top_line_line_offset < buffer->line_offset ? true : false;
        }
        return S_OK;
    }

    /***
     * both are not main
     */
    // comment-offset
    if (state->top_line_comment_offset < buffer->comment_offset) {
        *is_pre = true;
        return S_OK;
    }

    if (state->top_line_comment_offset > buffer->comment_offset) {
        *is_pre = false;
        return S_OK;
    }

    // same comment-offset, compare content-type
    if (state->top_line_content_type == PTTDB_CONTENT_TYPE_COMMENT && buffer->content_type != PTTDB_CONTENT_TYPE_COMMENT) {
        *is_pre = true;
        return S_OK;
    }

    if (state->top_line_content_type != PTTDB_CONTENT_TYPE_COMMENT && buffer->content_type == PTTDB_CONTENT_TYPE_COMMENT) {
        *is_pre = false;
        return S_OK;
    }

    // both are comment-reply
    if (state->top_line_content_type == PTTDB_CONTENT_TYPE_COMMENT_REPLY && buffer->content_type == PTTDB_CONTENT_TYPE_COMMENT_REPLY) {
        if (state->top_line_block_offset != buffer->block_offset) {
            *is_pre = state->top_line_block_offset < buffer->block_offset ? true : false;
        }
        else {
            *is_pre = state->top_line_line_offset < buffer->line_offset ? true : false;
        }
        return S_OK;
    }

    *is_pre = false;
    return S_OK;
}

Err
_sync_pttui_buffer_info_get_buffer(PttUIState *state, PttUIBuffer *current_buffer, bool is_pre, PttUIBuffer **new_buffer, PttUIBufferInfo *buffer_info)
{
    PttUIBuffer *p_buffer = current_buffer;

    while (p_buffer != NULL) {
        if (state->top_line_content_type == p_buffer->content_type &&
            state->top_line_block_offset == p_buffer->block_offset &&
            state->top_line_line_offset == p_buffer->line_offset &&
            state->top_line_comment_offset == p_buffer->comment_offset) {
            break;
        }

        p_buffer = is_pre ? pttui_buffer_pre_ne(p_buffer, buffer_info->head) : pttui_buffer_next_ne(p_buffer, buffer_info->tail);        
    }

    *new_buffer = p_buffer;

    return S_OK;
}

/**
 * @brief [brief description]
 * @details 1. destroy pttui_buffer_info.
 *          2. initialize one buffer.
 *          3. extend pre-buffer based on the new buffer.
 *          4. extend next-buffer based on the new buffer.
 *          5. setup buffer to buffer_info.
 * 
 * 
 * @param buffer_info [description]
 * @param state [description]
 * @param file_info [description]
 * @param new_buffer [description]
 */
Err
resync_all_pttui_buffer_info(PttUIBufferInfo *buffer_info, PttUIState *state, FileInfo *file_info, PttUIBuffer **new_buffer)
{
    Err error_code = S_OK;

    // 1. save all buffer to tmp_file

    if(!error_code) {
        error_code = save_pttui_buffer_info_to_tmp_file(buffer_info, file_info);
    }

    // 2. destroy buffer_info
    bool is_lock_wr_buffer_info = false;
    bool is_lock_buffer_info = false;

    if(!error_code) {
        error_code = pttui_buffer_lock_wr_buffer_info(&is_lock_wr_buffer_info);
    }

    if(!error_code) {
        error_code = pttui_buffer_wrlock_buffer_info(&is_lock_buffer_info);
    }

    if(!error_code) {
        error_code = destroy_pttui_buffer_info(buffer_info);
    }

    // 3. init-one-buf
    PttUIBuffer *tmp_buffer = NULL;
    PttUIBuffer *tmp_head = NULL;
    PttUIBuffer *tmp_tail = NULL;
    int n_buffer = 0;

    if(!error_code) {
        error_code = _pttui_buffer_init_buffer_no_buf_from_file_info(state, file_info, &tmp_buffer);
    }

    if(!error_code) {
        n_buffer++;        
        *new_buffer = tmp_buffer;
    }

    // 4. extend pttui buffer
    if(!error_code) {
        error_code = _extend_pttui_buffer(file_info, tmp_buffer, tmp_buffer, tmp_buffer, &tmp_head, &tmp_tail, &n_buffer, buffer_info);
    }

    // 5. set to buffer-info

    if(!error_code) {
        buffer_info->head = tmp_head;
        buffer_info->tail = tmp_tail;
        buffer_info->n_buffer = n_buffer;
        memcpy(buffer_info->main_id, state->main_id, UUIDLEN);
    }

    // unlock
    Err error_code_lock = pttui_buffer_wrunlock_buffer_info(is_lock_buffer_info);
    if(!error_code && error_code_lock) error_code = error_code_lock;

    error_code_lock = pttui_buffer_unlock_wr_buffer_info(is_lock_wr_buffer_info);
    if(!error_code && error_code_lock) error_code = error_code_lock;

    return error_code;
}

/**********
 * init buffer
 **********/

/**
 * @brief [brief description]
 * @details XXX ASSUME no new-line in the file-info (used only in resync-all)
 * 
 * @param state [description]
 * @param file_info [description]
 * @param buffer [description]
 * @param o [description]
 * @param r [description]
 */
Err
_pttui_buffer_init_buffer_no_buf_from_file_info(PttUIState *state, FileInfo *file_info, PttUIBuffer **buffer)
{
    *buffer = malloc(sizeof(PttUIBuffer));
    PttUIBuffer *p_buffer = *buffer;
    if(!p_buffer) return S_ERR_MALLOC;
    
    bzero(p_buffer, sizeof(PttUIBuffer));

    memcpy(p_buffer->the_id, state->top_line_id, UUIDLEN);
    p_buffer->content_type = state->top_line_content_type;
    p_buffer->block_offset = state->top_line_block_offset;
    p_buffer->line_offset = state->top_line_line_offset;
    p_buffer->comment_offset = state->top_line_comment_offset;

    CommentInfo *p_comment_info = NULL;
    ContentBlockInfo *p_content_block = NULL;

    Err error_code = S_OK;
    Err error_code2 = S_OK;
    switch (p_buffer->content_type) {
    case PTTDB_CONTENT_TYPE_MAIN:
        p_content_block = file_info->main_blocks + p_buffer->block_offset;
        p_buffer->storage_type = p_content_block->storage_type;
        p_buffer->load_line_next_offset = p_buffer->load_line_offset < p_content_block->n_line_in_db - 1 ? p_buffer->load_line_offset + 1 : INVALID_LINE_OFFSET_NEXT_END;

        error_code2 = _pttui_buffer_set_load_line_from_content_block_info(p_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;
        error_code2 = _pttui_buffer_set_file_offset_from_content_block_info(p_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;
        break;
    case PTTDB_CONTENT_TYPE_COMMENT:
        p_buffer->storage_type = PTTDB_STORAGE_TYPE_MONGO;

        error_code2 = _pttui_buffer_default_load_line(p_buffer);
        if(!error_code && error_code2) error_code = error_code2;
        error_code2 = _pttui_buffer_default_file_offset(p_buffer);
        if(!error_code && error_code2) error_code = error_code2;
        break;
    case PTTDB_CONTENT_TYPE_COMMENT_REPLY:
        p_comment_info = file_info->comments + p_buffer->comment_offset;
        p_content_block = p_comment_info->comment_reply_blocks + p_buffer->block_offset;
        p_buffer->storage_type = p_content_block->storage_type;
        p_buffer->load_line_next_offset = p_buffer->load_line_offset < p_content_block->n_line_in_db - 1 ? p_buffer->load_line_offset + 1 : INVALID_LINE_OFFSET_NEXT_END;

        error_code2 = _pttui_buffer_set_load_line_from_content_block_info(p_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;
        error_code2 = _pttui_buffer_set_file_offset_from_content_block_info(p_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;
        break;
    default:
        break;
    }

    return error_code;
}

/**********
 * extend pttui buffer
 **********/

Err
extend_pttui_buffer_info(FileInfo *file_info, PttUIBufferInfo *buffer_info, PttUIBuffer *current_buffer)
{
    Err error_code = S_OK;
    // lock for writing buffer-info
    bool is_lock_wr_buffer_info = false;
    bool is_lock_buffer_info = false;
    PttUIBuffer *orig_head = NULL;
    PttUIBuffer *orig_tail = NULL;

    error_code = pttui_buffer_lock_wr_buffer_info(&is_lock_wr_buffer_info);

    if(!error_code) {
        orig_head = malloc(sizeof(PttUIBuffer));
        orig_tail = malloc(sizeof(PttUIBuffer));

        memcpy(orig_head, buffer_info->head, sizeof(PttUIBuffer));
        memcpy(orig_tail, buffer_info->tail, sizeof(PttUIBuffer));
    }

    PttUIBuffer *new_head_buffer = NULL;
    PttUIBuffer *new_tail_buffer = NULL;
    int n_new_buffer = 0;    

    if(!error_code) {
        error_code = _extend_pttui_buffer(file_info, orig_head, orig_tail, current_buffer, &new_head_buffer, &new_tail_buffer, &n_new_buffer, buffer_info);
    }

    if(!error_code) {
        error_code = pttui_buffer_wrlock_buffer_info(&is_lock_buffer_info);
    }

    if(!error_code) { // basic operation, use ->pre and ->next directly
        buffer_info->head->pre = orig_head->pre;
        if(orig_head->pre) orig_head->pre->next = buffer_info->head;        
        if(new_head_buffer && orig_head != new_head_buffer) {
            buffer_info->head = new_head_buffer;
        }

        buffer_info->tail->next = orig_tail->next;
        if(orig_tail->next) orig_tail->next->pre = buffer_info->tail;        
        if(new_tail_buffer && orig_tail != new_tail_buffer) {
            buffer_info->tail = new_tail_buffer;
        }

        buffer_info->n_buffer += n_new_buffer;
    }

    Err error_code_lock = pttui_buffer_unlock_buffer_info(is_lock_buffer_info);
    if(!error_code && error_code_lock) error_code = error_code_lock;

    error_code_lock = pttui_buffer_unlock_wr_buffer_info(is_lock_wr_buffer_info);
    if(!error_code && error_code_lock) error_code = error_code_lock;

    // free

    safe_free((void **)&orig_head);
    safe_free((void **)&orig_tail);

    return error_code;

}

/**
 * @brief [brief description]
 * @details XXX ASSUME head_buffer, tail_buffer, current_buffer exists
 *          XXX require LOCK_PTTUI_WR_BUFFER_INFO already locked
 *          1. count extra-pre-range
 *          2. if need extend pre: extend-pre
 *          3. if need extend next: extend-next
 * 
 * @param buffer_info [description]
 * @param current_buffer [description]
 */
Err
_extend_pttui_buffer(FileInfo *file_info, PttUIBuffer *head_buffer, PttUIBuffer *tail_buffer, PttUIBuffer *current_buffer, PttUIBuffer **new_head_buffer, PttUIBuffer **new_tail_buffer, int *n_new_buffer, PttUIBufferInfo *buffer_info)
{
    // 1. determine extra-range required to retrieve.
    // 2. do extend buffer.
    Err error_code = S_OK;

    int n_extra_range = 0;

    error_code = _extend_pttui_buffer_count_extra_pre_range(current_buffer, &n_extra_range, buffer_info);

    // extend-pre
    if(!error_code && (!head_buffer->buf || n_extra_range)) {
        error_code = _extend_pttui_buffer_extend_pre_buffer(file_info, head_buffer, n_extra_range, new_head_buffer, n_new_buffer);
    }

    if(!error_code) {
        error_code = _extend_pttui_buffer_count_extra_next_range(current_buffer, &n_extra_range, buffer_info);
    }

    // extend-next
    if (!error_code && (!tail_buffer->buf || n_extra_range)) {
        error_code = _extend_pttui_buffer_extend_next_buffer(file_info, tail_buffer, n_extra_range, new_tail_buffer, n_new_buffer);
    }

    return error_code;
}

Err
_extend_pttui_buffer_count_extra_pre_range(PttUIBuffer *buffer, int *n_extra_range, PttUIBufferInfo *buffer_info)
{
    PttUIBuffer *p_buffer = NULL;
    int i = 0;
    for (i = 0, p_buffer = buffer; i < SOFT_N_PTTUI_BUFFER && p_buffer; i++, p_buffer = pttui_buffer_pre_ne(p_buffer, buffer_info->head));

    *n_extra_range = i == SOFT_N_PTTUI_BUFFER ? 0 : (HARD_N_PTTUI_BUFFER - i);

    return S_OK;
}

Err
_extend_pttui_buffer_count_extra_next_range(PttUIBuffer *buffer, int *n_extra_range, PttUIBufferInfo *buffer_info)
{
    PttUIBuffer *p_buffer = NULL;

    int i = 0;
    for (i = 0, p_buffer = buffer; i < SOFT_N_PTTUI_BUFFER && p_buffer; i++, p_buffer = pttui_buffer_next_ne(p_buffer, buffer_info->tail));

    *n_extra_range = i == SOFT_N_PTTUI_BUFFER ? 0 : (HARD_N_PTTUI_BUFFER - i);

    return S_OK;
}

/**********
 * extend pre buffer
 **********/
Err
_extend_pttui_buffer_extend_pre_buffer(FileInfo *file_info, PttUIBuffer *head_buffer, int n_buffer, PttUIBuffer **new_head_buffer, int *ret_n_buffer)
{    
    Err error_code = S_OK;
    PttUIBuffer *start_buffer = head_buffer;

    // 1. check begin-of-file
    bool is_begin = false;
    if(start_buffer && start_buffer->buf) {
        error_code = pttui_buffer_is_begin_of_file(start_buffer, file_info, &is_begin);
        if(error_code) return error_code;
        if(is_begin) return S_OK;
    }

    // 2. extend-pre-buffer-no-buf
    PttUIResourceInfo resource_info = {};
    PttUIResourceDict resource_dict = {};
    init_pttui_resource_dict(file_info->main_id, &resource_dict);

    error_code = _extend_pttui_buffer_extend_pre_buffer_no_buf(start_buffer, file_info, n_buffer, new_head_buffer, ret_n_buffer);

    if (error_code) return error_code;

    if (start_buffer->buf) start_buffer = start_buffer->pre;

    error_code = _pttui_buffer_info_to_resource_info(*new_head_buffer, start_buffer, &resource_info);

    if (!error_code) {
        error_code = pttui_resource_info_to_resource_dict(&resource_info, &resource_dict);
    }

    if (!error_code) {
        error_code = _pttui_buffer_info_set_buf_from_resource_dict(*new_head_buffer, start_buffer, &resource_dict);
    }

    // free
    destroy_pttui_resource_info(&resource_info);

    safe_destroy_pttui_resource_dict(&resource_dict);

    return error_code;
}

Err
_extend_pttui_buffer_extend_pre_buffer_no_buf(PttUIBuffer *current_buffer, FileInfo *file_info, int n_buffer, PttUIBuffer **new_head_buffer, int *ret_n_buffer)
{
    Err error_code = S_OK;
    PttUIBuffer *new_buffer = NULL;
    int i = 0;
    for (i = 0; i < n_buffer; i++) {
        error_code = _extend_pttui_buffer_extend_pre_buffer_no_buf_core(current_buffer, file_info, &new_buffer);
        if (error_code) break;

        if (!new_buffer) break;

        new_buffer->next = current_buffer;
        current_buffer->pre = new_buffer;
        
        current_buffer = new_buffer;
        new_buffer = NULL;
    }
    *new_head_buffer = current_buffer;
    (*ret_n_buffer) += i;

    return error_code;
}

Err
_extend_pttui_buffer_extend_pre_buffer_no_buf_core(PttUIBuffer *current_buffer, FileInfo *file_info, PttUIBuffer **new_buffer)
{
    Err error_code = S_OK;
    switch (current_buffer->content_type) {
    case PTTDB_CONTENT_TYPE_MAIN:
        error_code = _extend_pttui_buffer_extend_pre_buffer_no_buf_main(current_buffer, file_info, new_buffer);
        break;
    case PTTDB_CONTENT_TYPE_COMMENT:
        error_code = _extend_pttui_buffer_extend_pre_buffer_no_buf_comment(current_buffer, file_info, new_buffer);
        break;
    case PTTDB_CONTENT_TYPE_COMMENT_REPLY:
        error_code = _extend_pttui_buffer_extend_pre_buffer_no_buf_comment_reply(current_buffer, file_info, new_buffer);
        break;
    default:
        error_code = S_ERR;
        break;
    }

    return error_code;
}

Err
_extend_pttui_buffer_extend_pre_buffer_no_buf_main(PttUIBuffer *current_buffer, FileInfo *file_info, PttUIBuffer **new_buffer)
{
    // begin-of-file
    bool is_begin = false;
    Err error_code = pttui_buffer_is_begin_of_file(current_buffer, file_info, &is_begin);
    if(error_code) return error_code;    
    if(is_begin) return S_OK;

    // same-block, but no valid pre-offset
    if(current_buffer->line_offset != 0 && current_buffer->load_line_pre_offset < 0) return S_ERR_EXTEND;

    // malloc new buffer
    *new_buffer = malloc(sizeof(PttUIBuffer));
    PttUIBuffer *tmp = *new_buffer;
    bzero(tmp, sizeof(PttUIBuffer));

    memcpy(tmp->the_id, current_buffer->the_id, UUIDLEN);
    tmp->content_type = current_buffer->content_type;
    tmp->comment_offset = 0;

    Err error_code2 = S_OK;
    ContentBlockInfo *p_content_block = file_info->main_blocks + current_buffer->block_offset;
    if (current_buffer->line_offset != 0) { // same-block
        tmp->block_offset = current_buffer->block_offset;
        tmp->line_offset = current_buffer->line_offset - 1;
        tmp->storage_type = current_buffer->storage_type;
    }
    else { // new block, referring to file-info
        tmp->block_offset = current_buffer->block_offset - 1;
        p_content_block--;

        tmp->line_offset = p_content_block->n_line - 1;
        tmp->storage_type = p_content_block->storage_type;
    }

    error_code2 = _pttui_buffer_load_line_pre_from_content_block_info(tmp, current_buffer, p_content_block);
    if(!error_code && error_code2) error_code = error_code2;
    error_code2 = _pttui_buffer_file_offset_pre_from_content_block_info(tmp, current_buffer, p_content_block);
    if(!error_code && error_code2) error_code = error_code2;

    return error_code;
}

Err
_extend_pttui_buffer_extend_pre_buffer_no_buf_comment(PttUIBuffer *current_buffer, FileInfo *file_info, PttUIBuffer **new_buffer)
{
    Err error_code = S_OK;

    *new_buffer = malloc(sizeof(PttUIBuffer));
    PttUIBuffer *tmp = *new_buffer;
    bzero(tmp, sizeof(PttUIBuffer));

    CommentInfo *p_comment = file_info->comments + current_buffer->comment_offset;
    ContentBlockInfo *p_content_block = NULL;

    Err error_code2 = S_OK;
    if (current_buffer->comment_offset == 0) { // main-block. referring to file-info
        tmp->content_type = PTTDB_CONTENT_TYPE_MAIN;
        memcpy(tmp->the_id, file_info->main_content_id, UUIDLEN);
        tmp->comment_offset = 0;

        tmp->block_offset = file_info->n_main_block - 1;
        p_content_block = file_info->main_blocks + tmp->block_offset;

        tmp->line_offset = p_content_block->n_line - 1;
        tmp->storage_type = p_content_block->storage_type;

        error_code2 = _pttui_buffer_load_line_pre_from_content_block_info(tmp, current_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;
        error_code2 = _pttui_buffer_file_offset_pre_from_content_block_info(tmp, current_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;
    }
    else {
        tmp->comment_offset = current_buffer->comment_offset - 1;
        p_comment--;

        if (p_comment->n_comment_reply_total_line) { // comment-reply-block. referring to file-info
            tmp->content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;
            memcpy(tmp->the_id, p_comment->comment_reply_id, UUIDLEN);

            tmp->block_offset = p_comment->n_comment_reply_block - 1;
            p_content_block = p_comment->comment_reply_blocks + tmp->block_offset;

            tmp->line_offset = p_content_block->n_line - 1;
            tmp->storage_type = p_content_block->storage_type;

            error_code2 = _pttui_buffer_load_line_pre_from_content_block_info(tmp, current_buffer, p_content_block);
            if(!error_code && error_code2) error_code = error_code2;
            error_code2 = _pttui_buffer_file_offset_pre_from_content_block_info(tmp, current_buffer, p_content_block);
            if(!error_code && error_code2) error_code = error_code2;
        }
        else {
            tmp->content_type = PTTDB_CONTENT_TYPE_COMMENT; // comment
            memcpy(tmp->the_id, p_comment->comment_id, UUIDLEN);

            tmp->block_offset = 0;
            tmp->line_offset = 0;
            tmp->storage_type = PTTDB_STORAGE_TYPE_MONGO;

            error_code2 = _pttui_buffer_default_load_line(tmp);
            if(!error_code && error_code2) error_code = error_code2;
            error_code2 = _pttui_buffer_default_file_offset(tmp);
            if(!error_code && error_code2) error_code = error_code2;
        }
    }

    return error_code;
}

Err
_extend_pttui_buffer_extend_pre_buffer_no_buf_comment_reply(PttUIBuffer *current_buffer, FileInfo *file_info, PttUIBuffer **new_buffer)
{
    // same-block, but no valid pre-offset
    Err error_code = S_OK;
    if(current_buffer->line_offset != 0 && current_buffer->load_line_pre_offset < 0) return S_ERR_EXTEND;

    *new_buffer = malloc(sizeof(PttUIBuffer));
    PttUIBuffer *tmp = *new_buffer;
    bzero(tmp, sizeof(PttUIBuffer));

    tmp->comment_offset = current_buffer->comment_offset;

    Err error_code2 = S_OK;
    CommentInfo *p_comment = file_info->comments + tmp->comment_offset;
    ContentBlockInfo *p_content_block = p_comment->comment_reply_blocks + current_buffer->block_offset;
    if (current_buffer->block_offset == 0 && current_buffer->line_offset == 0) { // comment
        tmp->content_type = PTTDB_CONTENT_TYPE_COMMENT;
        memcpy(tmp->the_id, p_comment->comment_id, UUIDLEN);

        tmp->block_offset = 0;
        tmp->line_offset = 0;
        tmp->storage_type = PTTDB_STORAGE_TYPE_MONGO;

        error_code = _pttui_buffer_default_load_line(tmp);
        if(!error_code && error_code2) error_code = error_code2;
        error_code = _pttui_buffer_default_file_offset(tmp);
        if(!error_code && error_code2) error_code = error_code2;
    }
    else {
        tmp->content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;
        memcpy(tmp->the_id, current_buffer->the_id, UUIDLEN);

        if (current_buffer->line_offset != 0) { // same-block
            tmp->block_offset = current_buffer->block_offset;
            tmp->line_offset = current_buffer->line_offset - 1;
            tmp->storage_type = current_buffer->storage_type;

            error_code = _pttui_buffer_load_line_pre_from_content_block_info(tmp, current_buffer, p_content_block);
            if(!error_code && error_code2) error_code = error_code2;
            error_code = _pttui_buffer_file_offset_pre_from_content_block_info(tmp, current_buffer, p_content_block);
            if(!error_code && error_code2) error_code = error_code2;
        }
        else { // different block. referring to file-info
            tmp->block_offset = current_buffer->block_offset - 1;
            p_content_block--;

            tmp->line_offset = p_content_block->n_line - 1;
            tmp->storage_type = p_content_block->storage_type;

            error_code2 = _pttui_buffer_load_line_pre_from_content_block_info(tmp, current_buffer, p_content_block);
            if(!error_code && error_code2) error_code = error_code2;
            error_code2 = _pttui_buffer_file_offset_pre_from_content_block_info(tmp, current_buffer, p_content_block);
            if(!error_code && error_code2) error_code = error_code2;
        }
    }

    return error_code;
}

/**********
 * extend next buffer
 **********/
Err
_extend_pttui_buffer_extend_next_buffer(FileInfo *file_info, PttUIBuffer *tail_buffer, int n_buffer, PttUIBuffer **new_tail_buffer, int *ret_n_buffer)
{
    Err error_code = S_OK;
    PttUIBuffer *start_buffer = tail_buffer;

    // 1. check eof
    bool is_eof = false;
    if (start_buffer && start_buffer->buf) {
        error_code = pttui_buffer_is_eof(start_buffer, file_info, &is_eof);
        if (error_code) return error_code;
        if (is_eof) return S_OK;
    }

    // 2. extend-next-buffer-no-buf
    PttUIResourceInfo resource_info = {};
    PttUIResourceDict resource_dict = {};
    init_pttui_resource_dict(file_info->main_id, &resource_dict);

    error_code = _extend_pttui_buffer_extend_next_buffer_no_buf(start_buffer, file_info, n_buffer, new_tail_buffer, ret_n_buffer);

    if (error_code) return error_code;

    if (start_buffer->buf) start_buffer = start_buffer->next; // basic operation, use ->next directly

    error_code = _pttui_buffer_info_to_resource_info(start_buffer, *new_tail_buffer, &resource_info);

    if (!error_code) {
        error_code = pttui_resource_info_to_resource_dict(&resource_info, &resource_dict);
    }

    if (!error_code) {
        error_code = _pttui_buffer_info_set_buf_from_resource_dict(start_buffer, *new_tail_buffer, &resource_dict);
    }

    // free
    destroy_pttui_resource_info(&resource_info);
    safe_destroy_pttui_resource_dict(&resource_dict);

    return error_code;
}

Err
_extend_pttui_buffer_extend_next_buffer_no_buf(PttUIBuffer *current_buffer, FileInfo *file_info, int n_buffer, PttUIBuffer **new_tail_buffer, int *ret_n_buffer)
{
    Err error_code = S_OK;
    PttUIBuffer *new_buffer = NULL;
    int i = 0;
    for (i = 0; i < n_buffer; i++) {
        error_code = _extend_pttui_buffer_extend_next_buffer_no_buf_core(current_buffer, file_info, &new_buffer);
        if (error_code) break;

        if (!new_buffer) break;

        new_buffer->pre = current_buffer;
        current_buffer->next = new_buffer;

        current_buffer = new_buffer;
        new_buffer = NULL;
    }
    *new_tail_buffer = current_buffer;
    (*ret_n_buffer) += i;

    return error_code;
}

Err
_extend_pttui_buffer_extend_next_buffer_no_buf_core(PttUIBuffer *current_buffer, FileInfo *file_info, PttUIBuffer **new_buffer)
{
    Err error_code = S_OK;
    switch (current_buffer->content_type) {
    case PTTDB_CONTENT_TYPE_MAIN:
        error_code = _extend_pttui_buffer_extend_next_buffer_no_buf_main(current_buffer, file_info, new_buffer);
        break;
    case PTTDB_CONTENT_TYPE_COMMENT:
        error_code = _extend_pttui_buffer_extend_next_buffer_no_buf_comment(current_buffer, file_info, new_buffer);
        break;
    case PTTDB_CONTENT_TYPE_COMMENT_REPLY:
        error_code = _extend_pttui_buffer_extend_next_buffer_no_buf_comment_reply(current_buffer, file_info, new_buffer);
        break;
    default:
        error_code = S_ERR;
        break;
    }

    return error_code;
}

Err
_extend_pttui_buffer_extend_next_buffer_no_buf_main(PttUIBuffer *current_buffer, FileInfo *file_info, PttUIBuffer **new_buffer)
{
    // eof
    bool is_eof = false;
    Err error_code = pttui_buffer_is_eof(current_buffer, file_info, &is_eof);
    if(error_code) return error_code;    
    if(is_eof) return S_OK;

    // same-block, but no valid next-offset
    ContentBlockInfo *p_content_block = file_info->main_blocks + current_buffer->block_offset;    
    if(current_buffer->line_offset != p_content_block->n_line - 1 && current_buffer->load_line_next_offset < 0) return S_ERR_EXTEND;

    // malloc new buffer
    *new_buffer = malloc(sizeof(PttUIBuffer));
    PttUIBuffer *tmp = *new_buffer;
    bzero(tmp, sizeof(PttUIBuffer));

    // last line of main-block. new-buffer as comment
    Err error_code2 = S_OK;
    if (current_buffer->block_offset == file_info->n_main_block - 1 &&
            current_buffer->line_offset == p_content_block->n_line - 1) {

        memcpy(tmp->the_id, file_info->comments[0].comment_id, UUIDLEN);
        tmp->content_type = PTTDB_CONTENT_TYPE_COMMENT;
        tmp->comment_offset = 0;

        tmp->block_offset = 0;
        tmp->line_offset = 0;
        tmp->storage_type = PTTDB_STORAGE_TYPE_MONGO;

        error_code2 = _pttui_buffer_default_load_line(tmp);
        if(!error_code && error_code2) error_code = error_code2;
        error_code2 = _pttui_buffer_default_file_offset(tmp);
        if(!error_code && error_code2) error_code = error_code2;

        return error_code;
    }

    // still in main-block
    memcpy(tmp->the_id, current_buffer->the_id, UUIDLEN);
    tmp->content_type = current_buffer->content_type;
    tmp->comment_offset = 0;

    if (current_buffer->line_offset != p_content_block->n_line - 1) {
        // not the last-line
        tmp->block_offset = current_buffer->block_offset;
        tmp->line_offset = current_buffer->line_offset + 1;
        tmp->storage_type = p_content_block->storage_type;

        error_code2 = _pttui_buffer_load_line_next_from_content_block_info(tmp, current_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;
        error_code2 = _pttui_buffer_file_offset_next_from_content_block_info(tmp, current_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;
    }
    else {
        // last-line, but not the last block, referring from file-info
        tmp->block_offset = current_buffer->block_offset + 1;
        p_content_block++;

        tmp->line_offset = 0;
        tmp->storage_type = p_content_block->storage_type;

        error_code2 = _pttui_buffer_load_line_next_from_content_block_info(tmp, current_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;
        error_code2 = _pttui_buffer_file_offset_next_from_content_block_info(tmp, current_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;
    }

    return error_code;
}

Err
_extend_pttui_buffer_extend_next_buffer_no_buf_comment(PttUIBuffer *current_buffer, FileInfo *file_info, PttUIBuffer **new_buffer)
{
    // eof
    bool is_eof = false;
    Err error_code = pttui_buffer_is_eof(current_buffer, file_info, &is_eof);
    if(error_code) return error_code;    
    if(is_eof) return S_OK;

    int current_buffer_comment_offset = current_buffer->comment_offset;
    CommentInfo *p_comment = file_info->comments + current_buffer_comment_offset;
    ContentBlockInfo *p_content_block = NULL;

    *new_buffer = malloc(sizeof(PttUIBuffer));
    PttUIBuffer *tmp = *new_buffer;
    bzero(tmp, sizeof(PttUIBuffer));

    Err error_code2 = S_OK;
    if (p_comment->n_comment_reply_block) {
        // with comment-reply // referring from file-info

        tmp->content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;
        memcpy(tmp->the_id, p_comment->comment_reply_id, UUIDLEN);
        tmp->comment_offset = current_buffer_comment_offset;

        tmp->block_offset = 0;
        p_content_block = p_comment->comment_reply_blocks;

        tmp->line_offset = 0;
        tmp->storage_type = p_content_block->storage_type;

        error_code2 = _pttui_buffer_load_line_next_from_content_block_info(tmp, current_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;
        error_code2 = _pttui_buffer_file_offset_next_from_content_block_info(tmp, current_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;
    }
    else {
        // next comment
        tmp->content_type = PTTDB_CONTENT_TYPE_COMMENT;

        p_comment++;

        memcpy(tmp->the_id, p_comment->comment_id, UUIDLEN);
        tmp->comment_offset = current_buffer_comment_offset + 1;

        tmp->block_offset = 0;
        tmp->line_offset = 0;
        tmp->storage_type = PTTDB_STORAGE_TYPE_MONGO;

        error_code2 = _pttui_buffer_default_load_line(tmp);
        if(!error_code && error_code2) error_code = error_code2;
        error_code2 = _pttui_buffer_default_file_offset(tmp);
        if(!error_code && error_code2) error_code = error_code2;
    }

    return error_code;
}

Err
_extend_pttui_buffer_extend_next_buffer_no_buf_comment_reply(PttUIBuffer *current_buffer, FileInfo *file_info, PttUIBuffer **new_buffer)
{
    // eof
    bool is_eof = false;
    Err error_code = pttui_buffer_is_eof(current_buffer, file_info, &is_eof);
    if(error_code) return error_code;    
    if(is_eof) return S_OK;

    CommentInfo *p_comment = file_info->comments + current_buffer->comment_offset;
    ContentBlockInfo *p_content_block = p_comment->comment_reply_blocks + current_buffer->block_offset;

    // same-block, but no valid load_line_next_offset
    if(current_buffer->line_offset != p_content_block->n_line - 1 &&
        current_buffer->load_line_next_offset < 0) return S_ERR_EXTEND;

    *new_buffer = malloc(sizeof(PttUIBuffer));
    PttUIBuffer *tmp = *new_buffer;
    bzero(tmp, sizeof(PttUIBuffer));

    Err error_code2 = S_OK;
    // same-block
    if (current_buffer->line_offset != p_content_block->n_line - 1) {
        tmp->content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;        
        memcpy(tmp->the_id, p_comment->comment_reply_id, UUIDLEN);
        tmp->comment_offset = current_buffer->comment_offset;

        tmp->block_offset = current_buffer->block_offset;
        tmp->line_offset = current_buffer->line_offset + 1;
        tmp->storage_type = p_content_block->storage_type;

        error_code2 = _pttui_buffer_load_line_next_from_content_block_info(tmp, current_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;
        error_code2 = _pttui_buffer_file_offset_next_from_content_block_info(tmp, current_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;
    }
    else if (current_buffer->block_offset != p_comment->n_comment_reply_block - 1) {
        // different block, within same comment, referring file-info
        tmp->content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;
        memcpy(tmp->the_id, p_comment->comment_reply_id, UUIDLEN);
        tmp->comment_offset = current_buffer->comment_offset;

        tmp->block_offset = current_buffer->block_offset + 1;
        p_content_block++;

        tmp->line_offset = 0;
        tmp->storage_type = p_content_block->storage_type;

        error_code2 = _pttui_buffer_load_line_next_from_content_block_info(tmp, current_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;
        error_code2 = _pttui_buffer_file_offset_next_from_content_block_info(tmp, current_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;
    }
    else {
        // different comment
        p_comment++;        

        tmp->content_type = PTTDB_CONTENT_TYPE_COMMENT;
        memcpy(tmp->the_id, p_comment->comment_id, UUIDLEN);
        tmp->comment_offset = current_buffer->comment_offset + 1;

        tmp->block_offset = 0;
        tmp->line_offset = 0;
        tmp->storage_type = PTTDB_STORAGE_TYPE_MONGO;

        error_code2 = _pttui_buffer_default_load_line(tmp);
        if(!error_code && error_code2) error_code = error_code2;
        error_code2 = _pttui_buffer_default_file_offset(tmp);
        if(!error_code && error_code2) error_code = error_code2;
    }

    return S_OK;
}

/**********
 * load-line
 **********/
Err
_pttui_buffer_default_load_line(PttUIBuffer *buffer)
{
    buffer->load_line_offset = 0;
    buffer->load_line_pre_offset = INVALID_LINE_OFFSET_PRE_END;
    buffer->load_line_next_offset = INVALID_LINE_OFFSET_NEXT_END;

    return S_OK;
}

Err
_pttui_buffer_set_load_line_from_content_block_info(PttUIBuffer *buffer, ContentBlockInfo *content_block)
{
    buffer->load_line_offset = buffer->line_offset;
    buffer->load_line_pre_offset = buffer->load_line_offset ? buffer->load_line_offset - 1 : INVALID_LINE_OFFSET_PRE_END;
    buffer->load_line_next_offset = buffer->load_line_offset < content_block->n_line - 1 ? buffer->load_line_offset + 1 : INVALID_LINE_OFFSET_NEXT_END;

    return S_OK;
}

Err
_pttui_buffer_load_line_pre_from_content_block_info(PttUIBuffer *pre_buffer, PttUIBuffer *current_buffer, ContentBlockInfo *content_block)
{    
    if(pre_buffer->content_type == current_buffer->content_type && pre_buffer->block_offset == current_buffer->block_offset) {
        // within same-block
        pre_buffer->load_line_offset = current_buffer->load_line_pre_offset;
        pre_buffer->load_line_next_offset = current_buffer->load_line_offset;
        pre_buffer->load_line_pre_offset = pre_buffer->load_line_offset ? pre_buffer->load_line_offset - 1 : INVALID_LINE_OFFSET_PRE_END;
    }
    else {
        // different-block
        pre_buffer->load_line_offset = content_block->n_line - 1;
        pre_buffer->load_line_next_offset = INVALID_LINE_OFFSET_NEXT_END;
        pre_buffer->load_line_pre_offset = pre_buffer->load_line_offset ? pre_buffer->load_line_offset - 1 : INVALID_LINE_OFFSET_PRE_END;
    }

    return S_OK;
}

Err
_pttui_buffer_load_line_next_from_content_block_info(PttUIBuffer *next_buffer, PttUIBuffer *current_buffer, ContentBlockInfo *content_block)
{
    if(next_buffer->content_type == current_buffer->content_type && next_buffer->block_offset == current_buffer->block_offset) {
        // within same-block
        next_buffer->load_line_offset = current_buffer->load_line_next_offset;
        next_buffer->load_line_pre_offset = current_buffer->load_line_offset;
        next_buffer->load_line_next_offset = next_buffer->load_line_offset < content_block->n_line - 1 ? next_buffer->load_line_offset + 1 : INVALID_LINE_OFFSET_NEXT_END;
    }
    else {
        // different-block
        next_buffer->load_line_offset = 0;
        next_buffer->load_line_pre_offset = INVALID_LINE_OFFSET_PRE_END;
        next_buffer->load_line_next_offset = next_buffer->load_line_offset < content_block->n_line - 1 ? next_buffer->load_line_offset + 1 : INVALID_LINE_OFFSET_NEXT_END;
    }

    return S_OK;
}

/**********
 * file-offset
 **********/
Err
_pttui_buffer_default_file_offset(PttUIBuffer *buffer)
{
    buffer->file_offset = INVALID_FILE_OFFSET;
    buffer->file_line_offset = INVALID_LINE_OFFSET;
    buffer->file_line_pre_offset = INVALID_LINE_OFFSET_PRE_END;
    buffer->file_line_next_offset = INVALID_LINE_OFFSET_NEXT_END;

    return S_OK;
}

Err
_pttui_buffer_set_file_offset_from_content_block_info(PttUIBuffer *buffer, ContentBlockInfo *content_block)
{
    if (buffer->storage_type == PTTDB_STORAGE_TYPE_MONGO) return _pttui_buffer_default_file_offset(buffer);

    int total_line = 0;
    int *p_file_n_line = content_block->file_n_line;
    int new_n_line = 0;
    int i = 0;
    for (i = 0; i < content_block->n_file; i++, p_file_n_line++) {
        new_n_line = *p_file_n_line;
        if (total_line + new_n_line > buffer->load_line_offset) {
            buffer->file_offset = i;
            buffer->file_line_offset = buffer->load_line_offset - total_line;
            break;
        }
        total_line += new_n_line;
    }
    if (i == content_block->n_file) return S_ERR;

    buffer->file_line_pre_offset = buffer->file_line_offset ? buffer->file_line_offset - 1 : INVALID_LINE_OFFSET_PRE_END;
    buffer->file_line_next_offset = buffer->file_line_offset < content_block->file_n_line[buffer->file_offset] - 1 ? buffer->file_line_offset + 1 : INVALID_LINE_OFFSET_NEXT_END;

    return S_OK;
}

Err
_pttui_buffer_file_offset_pre_from_content_block_info(PttUIBuffer *pre_buffer, PttUIBuffer *current_buffer, ContentBlockInfo *content_block)
{
    if(pre_buffer->storage_type == PTTDB_STORAGE_TYPE_MONGO) return _pttui_buffer_default_file_offset(pre_buffer);

    if(pre_buffer->content_type == current_buffer->content_type && pre_buffer->block_offset == current_buffer->block_offset) {
        // same-block
        if(current_buffer->file_line_offset) {
            // same-file
            pre_buffer->file_offset = current_buffer->file_offset;
            pre_buffer->file_line_offset = current_buffer->file_line_pre_offset;
            pre_buffer->file_line_next_offset = current_buffer->file_line_offset;
            pre_buffer->file_line_pre_offset = pre_buffer->file_line_offset ? pre_buffer->file_line_offset - 1 : INVALID_LINE_OFFSET_PRE_END;
        }
        else {
            // diff-file
            pre_buffer->file_offset = current_buffer->file_offset - 1;
            pre_buffer->file_line_offset = content_block->file_n_line[pre_buffer->file_offset] - 1;
            pre_buffer->file_line_pre_offset = pre_buffer->file_line_offset ? pre_buffer->file_line_offset - 1 : INVALID_LINE_OFFSET_PRE_END;
            pre_buffer->file_line_next_offset = INVALID_LINE_OFFSET_NEXT_END;
        }
    }
    else {
        // diff-block
        pre_buffer->file_offset = content_block->n_file - 1;
        pre_buffer->file_line_offset = content_block->file_n_line[pre_buffer->file_offset] - 1;
        pre_buffer->file_line_pre_offset = pre_buffer->file_line_offset ? pre_buffer->file_line_offset - 1 : INVALID_LINE_OFFSET_PRE_END;
        pre_buffer->file_line_next_offset = INVALID_LINE_OFFSET_NEXT_END;
    }

    if(pre_buffer->file_offset < 0 || pre_buffer->file_offset >= content_block->n_file || pre_buffer->file_line_offset < 0 || pre_buffer->file_line_offset >= content_block->file_n_line[pre_buffer->file_offset]) return S_ERR;


    return S_OK;
}

Err
_pttui_buffer_file_offset_next_from_content_block_info(PttUIBuffer *next_buffer, PttUIBuffer *current_buffer, ContentBlockInfo *content_block)
{
    if(next_buffer->storage_type == PTTDB_STORAGE_TYPE_MONGO) return _pttui_buffer_default_file_offset(next_buffer);

    if(next_buffer->content_type == current_buffer->content_type && next_buffer->block_offset == current_buffer->block_offset) {
        // same-block
        if(current_buffer->file_line_offset < content_block->file_n_line[current_buffer->file_offset] - 1) {
            // same-file
            next_buffer->file_offset = current_buffer->file_offset;
            next_buffer->file_line_offset = current_buffer->file_line_next_offset;
            next_buffer->file_line_pre_offset = current_buffer->file_line_offset;
            next_buffer->file_line_next_offset = next_buffer->file_line_offset < content_block->file_n_line[next_buffer->file_offset] - 1 ? next_buffer->file_line_offset + 1 : INVALID_LINE_OFFSET_NEXT_END;
        }
        else {
            // diff-file
            next_buffer->file_offset = current_buffer->file_offset + 1;
            next_buffer->file_line_offset = 0;
            next_buffer->file_line_pre_offset = INVALID_LINE_OFFSET_PRE_END;
            next_buffer->file_line_next_offset = next_buffer->file_line_offset < content_block->file_n_line[next_buffer->file_offset] - 1 ? next_buffer->file_line_offset + 1 : INVALID_LINE_OFFSET_NEXT_END;
        }
    }
    else {
        // diff-block
        next_buffer->file_offset = 0;
        next_buffer->file_line_offset = 0;
        next_buffer->file_line_pre_offset = INVALID_LINE_OFFSET_PRE_END;
        next_buffer->file_line_next_offset = next_buffer->file_line_offset < content_block->file_n_line[next_buffer->file_offset] - 1 ? next_buffer->file_line_offset + 1 : INVALID_LINE_OFFSET_NEXT_END;
    }

    if(next_buffer->file_offset < 0 || next_buffer->file_offset >= content_block->n_file || next_buffer->file_line_offset < 0 || next_buffer->file_line_offset >= content_block->file_n_line[next_buffer->file_offset]) return S_ERR;

    return S_OK;
}

/**********
 * buffer-info to resource-info
 **********/
Err
_pttui_buffer_info_to_resource_info(PttUIBuffer *head, PttUIBuffer *tail, PttUIResourceInfo *resource_info)
{
    Err error_code = S_OK;

    // basic operation, use ->next directly
    PttUIBuffer *pre_buffer = NULL;
    for (PttUIBuffer *current_buffer = head; current_buffer && current_buffer != tail->next; current_buffer = current_buffer->next) {
        if (pre_buffer &&
            current_buffer->content_type != PTTDB_CONTENT_TYPE_COMMENT &&
            pre_buffer->content_type == current_buffer->content_type &&
            pre_buffer->block_offset == current_buffer->block_offset &&
            pre_buffer->file_offset == current_buffer->file_offset) continue;

        error_code = pttui_resource_info_push_queue(current_buffer, resource_info, current_buffer->content_type, current_buffer->storage_type);
        if (error_code) break;

        pre_buffer = current_buffer;
    }

    return error_code;
}

Err
_modified_pttui_buffer_info_to_resource_info(PttUIBuffer *head, PttUIBuffer *tail, PttUIResourceInfo *resource_info)
{
    Err error_code = S_OK;

    // basic operation, use ->next directly
    PttUIBuffer *pre_buffer = NULL;
    for (PttUIBuffer *current_buffer = head; current_buffer && current_buffer != tail->next; current_buffer = current_buffer->next) {
        if(!current_buffer->is_modified) continue;
        if (pre_buffer &&
            current_buffer->content_type != PTTDB_CONTENT_TYPE_COMMENT &&
            pre_buffer->content_type == current_buffer->content_type &&
            pre_buffer->block_offset == current_buffer->block_offset &&            
            pre_buffer->file_offset == current_buffer->file_offset) continue;

        error_code = pttui_resource_info_push_queue(current_buffer, resource_info, current_buffer->content_type, current_buffer->storage_type);
        if (error_code) break;

        pre_buffer = current_buffer;
    }

    return error_code;
}


/**********
 * resource-dict to buffer-info
 **********/
Err
_pttui_buffer_info_set_buf_from_resource_dict(PttUIBuffer *head, PttUIBuffer *tail, PttUIResourceDict *resource_dict)
{
    Err error_code = S_OK;

    PttUIBuffer *p_buffer = head;
    UUID current_the_id = {};
    int current_block_id = 0;
    int len = 0;
    char *buf = NULL;
    char *p_buf = NULL;
    char *p_next_buf = NULL;
    char *p_buf_no_nl = NULL;
    int buf_offset = 0;
    int buf_next_offset = 0;
    int p_buffer_len = 0;
    int p_buffer_len_no_nl = 0;
    int i = 0;

    // pre-head
    error_code = pttui_resource_dict_get_data(resource_dict, p_buffer->the_id, p_buffer->block_offset, p_buffer->file_offset, &len, &buf);
    if(error_code) return error_code;

    memcpy(current_the_id, p_buffer->the_id, UUIDLEN);
    current_block_id = p_buffer->block_offset;
    p_buf = buf;
    buf_offset = 0;

    for(int i = 0; i < p_buffer->load_line_offset; i++) {
        if(buf_offset == len) return S_ERR; // XXX should not be here

        error_code = pttui_resource_dict_get_next_buf(p_buf, buf_offset, len, &p_next_buf, &buf_next_offset);
        if(error_code) return S_ERR;

        p_buf = p_next_buf;
        buf_offset = buf_next_offset;
    }

    // basic operation, use ->next directly
    // start
    for(; p_buffer && p_buffer != tail->next && !p_buffer->buf; p_buffer = p_buffer->next, i++) {
        if(memcmp(p_buffer->the_id, current_the_id, UUIDLEN) || current_block_id != p_buffer->block_offset) {
            error_code = pttui_resource_dict_get_data(resource_dict, p_buffer->the_id, p_buffer->block_offset, p_buffer->file_offset, &len, &buf);

            if(error_code) break;

            memcpy(current_the_id, p_buffer->the_id, UUIDLEN);
            current_block_id = p_buffer->block_offset;
            p_buf = buf;
            buf_offset = 0;
        }
        if(buf_offset == len) { // XXX should not be here
            error_code = S_ERR;
            break;
        }        
        error_code = pttui_resource_dict_get_next_buf(p_buf, buf_offset, len, &p_next_buf, &buf_next_offset);
        if(error_code) break;

        p_buffer_len = buf_next_offset - buf_offset;
        p_buffer_len_no_nl = p_buffer_len;
        p_buf_no_nl = p_buf + p_buffer_len - 1;
        for(int i_no_nl = 0; i_no_nl < 2; i_no_nl++) {
            if(p_buffer_len_no_nl && *p_buf_no_nl && (*p_buf_no_nl == '\r' || *p_buf_no_nl == '\n')) {
                p_buffer_len_no_nl--;
                p_buf_no_nl--;
            }
        }
        p_buffer->len_no_nl = p_buffer_len_no_nl;

        p_buffer->buf = malloc(MAX_TEXTLINE_SIZE + 1);
        memcpy(p_buffer->buf, p_buf, p_buffer_len_no_nl);
        p_buffer->buf[p_buffer_len_no_nl] = 0;

        p_buf = p_next_buf;
        buf_offset = buf_next_offset;
    }
    return error_code;
}

/**********
 * save to tmp file
 **********/
Err
check_and_save_pttui_buffer_info_to_tmp_file(PttUIBufferInfo *buffer_info, FileInfo *file_info)
{
    if(buffer_info->n_to_delete < N_TO_DELETE_SAVE_PTTUI_BUFFER_TO_TMP_FILE && buffer_info->n_new < N_NEW_SAVE_PTTUI_BUFFER_TO_TMP_FILE) return S_OK;

    return save_pttui_buffer_info_to_tmp_file(buffer_info, file_info);
}

Err
save_pttui_buffer_info_to_tmp_file(PttUIBufferInfo *buffer_info, FileInfo *file_info)
{
    Err error_code = S_OK;
    Err error_code_lock = S_OK;

    bool is_lock_wr_buffer_info = false;
    bool is_lock_buffer_info = false;
    bool is_lock_file_info = false;

    PttUIResourceInfo resource_info = {};
    PttUIResourceDict resource_dict = {};
    init_pttui_resource_dict(file_info->main_id, &resource_dict);

    // XXX defensive programming for not-init-buffer-info
    if(!buffer_info->head) return S_OK;
        
    if(!error_code) {
        error_code = pttui_buffer_lock_wr_buffer_info(&is_lock_wr_buffer_info);
    }

    if(!error_code) {
        error_code = _modified_pttui_buffer_info_to_resource_info(buffer_info->head, buffer_info->tail, &resource_info);
    }

    if(!error_code) {
        error_code = pttui_resource_info_to_resource_dict(&resource_info, &resource_dict);
    }

    if(!error_code) {
        error_code = pttui_resource_dict_integrate_with_modified_pttui_buffer_info(buffer_info->head, buffer_info->tail, &resource_dict);
    }

    if(!error_code) {
        error_code = pttui_resource_dict_save_to_tmp_file(&resource_dict);
    }

    if(!error_code) {
        error_code = pttui_buffer_wrlock_file_info(&is_lock_file_info);
    }

    if(!error_code) {
        error_code = pttui_resource_dict_reset_file_info(&resource_dict, file_info);
    }

    error_code_lock = pttui_buffer_wrunlock_file_info(is_lock_file_info);
    if(!error_code && error_code_lock) error_code = error_code_lock;

    if(!error_code) {
        error_code = pttui_buffer_wrlock_buffer_info(&is_lock_buffer_info);
    }

    if(!error_code) {
        error_code = _remove_deleted_pttui_buffer_in_buffer_info(buffer_info);
    }

    if(!error_code) {
        error_code = _reset_pttui_buffer_info(buffer_info, file_info);
    }

    error_code_lock = pttui_buffer_wrunlock_buffer_info(is_lock_buffer_info);
    if(!error_code && error_code_lock) error_code = error_code_lock;

    error_code_lock = pttui_buffer_unlock_wr_buffer_info(is_lock_wr_buffer_info);
    if(!error_code && error_code_lock) error_code = error_code_lock;

    // free
    destroy_pttui_resource_info(&resource_info);
    safe_destroy_pttui_resource_dict(&resource_dict);

    return error_code;
}

Err
_remove_deleted_pttui_buffer_in_buffer_info(PttUIBufferInfo *buffer_info)
{
    PttUIBuffer *current_buffer = buffer_info->head;
    PttUIBuffer *tmp = NULL;

    if(buffer_info->head->is_to_delete) buffer_info->head = pttui_buffer_next_ne(buffer_info->head, buffer_info->tail);

    if(buffer_info->tail->is_to_delete) buffer_info->tail = pttui_buffer_pre_ne(buffer_info->tail, buffer_info->head);

    while(current_buffer) {
        tmp = current_buffer;
        current_buffer = current_buffer->next;

        if(tmp->is_to_delete) {
            safe_free_pttui_buffer(&tmp);
            buffer_info->n_buffer--;
        }
    }

    return S_OK;
}

Err
_reset_pttui_buffer_info(PttUIBufferInfo *buffer_info, FileInfo *file_info)
{
    Err error_code = S_OK;
    Err error_code2 = S_OK;

    PttUIBuffer *p_buffer = buffer_info->head;
    PttUIBuffer *p_pre_buffer = NULL;

    CommentInfo *p_comment = NULL;
    ContentBlockInfo *p_content_block = NULL;

    switch(p_buffer->content_type) {
    case PTTDB_CONTENT_TYPE_MAIN:
        p_content_block = file_info->main_blocks + p_buffer->block_offset;
        break;
    case PTTDB_CONTENT_TYPE_COMMENT_REPLY:
        p_comment = file_info->comments + p_buffer->comment_offset;
        p_content_block = p_comment->comment_reply_blocks + p_buffer->block_offset;
        break;
    default:
        p_content_block = NULL;
        break;
    }

    if(p_content_block) {
        p_buffer->storage_type = p_content_block->storage_type;

        error_code2 = _pttui_buffer_set_load_line_from_content_block_info(p_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;
        error_code2 = _pttui_buffer_set_file_offset_from_content_block_info(p_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;        
        if(error_code) return error_code;
    }

    p_pre_buffer = p_buffer;
    p_buffer = p_buffer->next;

    for(; p_buffer; p_buffer = p_buffer->next) {
        switch(p_buffer->content_type) {
        case PTTDB_CONTENT_TYPE_MAIN:
            p_content_block = file_info->main_blocks + p_buffer->block_offset;
            break;
        case PTTDB_CONTENT_TYPE_COMMENT_REPLY:
            p_comment = file_info->comments + p_buffer->comment_offset;
            p_content_block = p_comment->comment_reply_blocks + p_buffer->block_offset;
            break;
        default:
            p_content_block = NULL;
            break;
        }

        if(!p_content_block) continue;

        p_buffer->storage_type = p_content_block->storage_type;

        error_code2 = _pttui_buffer_load_line_next_from_content_block_info(p_buffer, p_pre_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;
        error_code2 = _pttui_buffer_file_offset_next_from_content_block_info(p_buffer, p_pre_buffer, p_content_block);
        if(!error_code && error_code2) error_code = error_code2;

        if(error_code) break;

        p_buffer->storage_type = p_content_block->storage_type;

        p_buffer->is_modified = false;
        p_buffer->is_new = false;
        p_buffer->is_to_delete = false;

        p_pre_buffer = p_buffer;
    }

    buffer_info->n_new = 0;
    buffer_info->n_to_delete = 0;

    return error_code;
}

/**********
 * save to db
 **********/
 
Err save_pttui_buffer_info_to_db(PttUIBufferInfo *buffer_info, FileInfo *file_info, char *user, char *ip)
{
    Err error_code = save_pttui_buffer_info_to_tmp_file(buffer_info, file_info);
    if(error_code) return error_code;

    error_code = pttui_buffer_rdlock_file_info();
    if(error_code) return error_code;

    error_code = save_file_info_to_db(file_info, user, ip);

    Err error_code_lock = pttui_buffer_unlock_file_info();
    if(!error_code && error_code_lock) error_code = error_code_lock;

    buffer_info->is_saved = true;

    return error_code;    
}

/**********
 * shrink
 **********/
Err
_sync_pttui_buffer_info_count_shrink_range(PttUIBufferInfo *buffer_info, int *n_shrink_range)
{
    *n_shrink_range = buffer_info->n_buffer < N_SHRINK_PTTUI_BUFFER ? 0 : buffer_info->n_buffer - N_SHRINK_PTTUI_BUFFER;

    return S_OK;
}

Err
_sync_pttui_buffer_info_shrink_head(PttUIBufferInfo *buffer_info, int n_shrink_range)
{
    Err error_code = S_OK;
    PttUIBuffer *p_orig_head = buffer_info->head;
    PttUIBuffer *p_buffer = buffer_info->head;

    int i = 0;
    for (i = 0; i < n_shrink_range && p_buffer; i++, p_buffer = p_buffer->next);

    error_code = pttui_thread_lock_wrlock(LOCK_PTTUI_BUFFER_INFO);
    if (error_code) return error_code;

    buffer_info->head = p_buffer;
    buffer_info->n_buffer -= i;

    if (!p_buffer) { // XXX should not happen
        bzero(buffer_info, sizeof(PttUIBufferInfo));
    }

    error_code = pttui_thread_lock_unlock(LOCK_PTTUI_BUFFER_INFO);
    if (error_code) return error_code;

    p_buffer = p_orig_head;
    PttUIBuffer *tmp = NULL;
    while (p_buffer && p_buffer != buffer_info->head) {
        tmp = p_buffer->next;
        if(p_buffer->buf) free(p_buffer->buf);
        free(p_buffer);
        p_buffer = tmp;
    }

    return S_OK;
}

Err
_sync_pttui_buffer_info_shrink_tail(PttUIBufferInfo *buffer_info, int n_shrink_range)
{
    Err error_code = S_OK;
    PttUIBuffer *p_orig_tail = buffer_info->tail;
    PttUIBuffer *p_buffer = buffer_info->tail;

    int i = 0;
    for (i = 0; i < n_shrink_range && p_buffer; i++, p_buffer = p_buffer->pre);

    error_code = pttui_thread_lock_wrlock(LOCK_PTTUI_BUFFER_INFO);
    if (error_code) return error_code;

    buffer_info->tail = p_buffer;
    buffer_info->n_buffer -= i;

    if (!p_buffer) { // XXX should not happen
        bzero(buffer_info, sizeof(PttUIBufferInfo));
    }

    error_code = pttui_thread_lock_unlock(LOCK_PTTUI_BUFFER_INFO);
    if (error_code) return error_code;

    p_buffer = p_orig_tail;
    PttUIBuffer *tmp = NULL;
    while (p_buffer && p_buffer != buffer_info->tail) {
        tmp = p_buffer->pre;
        if(p_buffer->buf) free(p_buffer->buf);
        free(p_buffer);
        p_buffer = tmp;
    }

    return S_OK;
}

Err
pttui_buffer_insert_buffer(PttUIBuffer *current_buffer, PttUIBuffer *next_buffer)
{
    next_buffer->next = current_buffer->next;
    next_buffer->pre = current_buffer;

    current_buffer->next = next_buffer;
    if(next_buffer->next) {
        next_buffer->next->pre = next_buffer;
    }

    return S_OK;
}

Err
pttui_buffer_wrlock_file_info(bool *is_lock_file_info)
{
    Err error_code = pttui_thread_lock_wrlock(LOCK_PTTUI_FILE_INFO);

    *is_lock_file_info = error_code ? false : true;

    return error_code;
}

Err
pttui_buffer_wrunlock_file_info(bool is_lock_file_info)
{
    if(!is_lock_file_info) return S_OK;

    return pttui_thread_lock_unlock(LOCK_PTTUI_FILE_INFO);
}

Err
pttui_buffer_rdlock_file_info()
{
    return pttui_thread_lock_rdlock(LOCK_PTTUI_FILE_INFO);
}

Err
pttui_buffer_unlock_file_info()
{
    return pttui_thread_lock_unlock(LOCK_PTTUI_FILE_INFO);
}

Err
pttui_buffer_lock_wr_buffer_info(bool *is_lock_wr_buffer_info)
{
    *is_lock_wr_buffer_info = false;

    Err error_code = pttui_thread_lock_wrlock(LOCK_PTTUI_WR_BUFFER_INFO);
    if(error_code) return error_code;

    *is_lock_wr_buffer_info = true;

    return S_OK;
}

Err
pttui_buffer_unlock_wr_buffer_info(bool is_lock_wr_buffer_info)
{
    if(!is_lock_wr_buffer_info) return S_OK;

    return pttui_thread_lock_unlock(LOCK_PTTUI_WR_BUFFER_INFO);
}

Err
pttui_buffer_wrlock_buffer_info(bool *is_lock_buffer_info)
{
    *is_lock_buffer_info = false;

    Err error_code = pttui_thread_lock_wrlock(LOCK_PTTUI_BUFFER_INFO);
    if(error_code) return error_code;

    *is_lock_buffer_info = true;

    return S_OK;
}

Err
pttui_buffer_wrunlock_buffer_info(bool is_lock_buffer_info)
{
    if(!is_lock_buffer_info) return S_OK;

    return pttui_thread_lock_unlock(LOCK_PTTUI_BUFFER_INFO);
}

Err
pttui_buffer_rdlock_buffer_info()
{
    return pttui_thread_lock_rdlock(LOCK_PTTUI_BUFFER_INFO);
}

Err
pttui_buffer_unlock_buffer_info()
{
    return pttui_thread_lock_unlock(LOCK_PTTUI_BUFFER_INFO);
}
