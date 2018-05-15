/* $Id$ */
#ifndef PTTUI_BUFFER_PRIVATE_H
#define PTTUI_BUFFER_PRIVATE_H

#include "ptterr.h"
#include "cmpttui/pttui_const.h"
#include "cmpttui/pttui_state.h"
#include "cmpttui/pttui_buffer.h"
#include "cmpttui/pttui_var.h"
#include "cmpttui/pttui_resource_info.h"
#include "cmpttui/pttui_resource_dict.h"
#include "cmpttui/pttui_thread_lock.h"
#include "cmpttdb.h"

#ifdef __cplusplus
extern "C" {
#endif

/***
 * sync
 ***/
Err _sync_pttui_buffer_info_is_pre(PttUIState *state, PttUIBuffer *buffer, bool *is_pre);

bool _pttui_buffer_is_pre_ne(PttUIBuffer *buffer_a, PttUIBuffer *buffer_b);

bool _pttui_buffer_is_load_line_pre_ne(PttUIBuffer *buffer_a, PttUIBuffer *buffer_b);

Err _pttui_buffer_is_pre_core(enum PttDBContentType content_type_a, int comment_id_a, int block_id_a, int line_id_a, enum PttDBContentType content_type_b, int comment_id_b, int block_id_b, int line_id_b, bool *is_pre);


Err _sync_pttui_buffer_info_get_buffer(PttUIState *state, PttUIBuffer *current_buffer, bool is_pre, PttUIBuffer **new_buffer, PttUIBufferInfo *buffer_info);

/***
 * init buffer
 ***/
Err _pttui_buffer_init_buffer_no_buf_from_file_info(PttUIState *state, FileInfo *file_info, PttUIBuffer **buffer);

/***
 * extend 
 ***/
Err _extend_pttui_buffer(FileInfo *file_info, PttUIBuffer *head_buffer, PttUIBuffer *tail_buffer, PttUIBuffer *current_buffer, PttUIBuffer **new_head_buffer, PttUIBuffer **new_tail_buffer, int *n_new_buffer, PttUIBufferInfo *buffer_info);

Err _extend_pttui_buffer_count_extra_pre_range(PttUIBuffer *buffer, int *n_extra_range, PttUIBufferInfo *buffer_info);
Err _extend_pttui_buffer_count_extra_next_range(PttUIBuffer *buffer, int *n_extra_range, PttUIBufferInfo *buffer_info);

Err _extend_pttui_buffer_extend_pre_buffer(FileInfo *file_info, PttUIBuffer *head_buffer, int n_buffer, PttUIBuffer **new_head_buffer, int *ret_n_buffer);
Err _extend_pttui_buffer_extend_pre_buffer_no_buf(PttUIBuffer *current_buffer, FileInfo *file_info, int n_buffer, PttUIBuffer **new_head_buffer, int *ret_n_buffer);
Err _extend_pttui_buffer_extend_pre_buffer_no_buf_core(PttUIBuffer *current_buffer, FileInfo *file_info, PttUIBuffer **new_buffer);
Err _extend_pttui_buffer_extend_pre_buffer_no_buf_main(PttUIBuffer *current_buffer, FileInfo *file_info, PttUIBuffer **new_buffer);
Err _extend_pttui_buffer_extend_pre_buffer_no_buf_comment(PttUIBuffer *current_buffer, FileInfo *file_info, PttUIBuffer **new_buffer);
Err _extend_pttui_buffer_extend_pre_buffer_no_buf_comment_reply(PttUIBuffer *current_buffer, FileInfo *file_info, PttUIBuffer **new_buffer);

Err _extend_pttui_buffer_extend_next_buffer(FileInfo *file_info, PttUIBuffer *tail_buffer, int n_buffer, PttUIBuffer **new_tail_buffer, int *ret_n_buffer);
Err _extend_pttui_buffer_extend_next_buffer_no_buf(PttUIBuffer *current_buffer, FileInfo *file_info, int n_buffer, PttUIBuffer **new_tail_buffer, int *ret_n_buffer);
Err _extend_pttui_buffer_extend_next_buffer_no_buf_core(PttUIBuffer *current_buffer, FileInfo *file_info, PttUIBuffer **new_buffer);
Err _extend_pttui_buffer_extend_next_buffer_no_buf_main(PttUIBuffer *current_buffer, FileInfo *file_info, PttUIBuffer **new_buffer);
Err _extend_pttui_buffer_extend_next_buffer_no_buf_comment(PttUIBuffer *current_buffer, FileInfo *file_info, PttUIBuffer **new_buffer);
Err _extend_pttui_buffer_extend_next_buffer_no_buf_comment_reply(PttUIBuffer *current_buffer, FileInfo *file_info, PttUIBuffer **new_buffer);


/***
 * load-line
 ***/
Err _pttui_buffer_default_load_line(PttUIBuffer *buffer);
Err _pttui_buffer_set_load_line_from_content_block_info(PttUIBuffer *buffer, ContentBlockInfo *content_block);
Err _pttui_buffer_load_line_pre_from_content_block_info(PttUIBuffer *pre_buffer, PttUIBuffer *current_buffer, ContentBlockInfo *content_block);
Err _pttui_buffer_load_line_next_from_content_block_info(PttUIBuffer *next_buffer, PttUIBuffer *current_buffer, ContentBlockInfo *content_block);

/***
 * file-offset
 ***/
Err _pttui_buffer_default_file_offset(PttUIBuffer *buffer);
Err _pttui_buffer_set_file_offset_from_content_block_info(PttUIBuffer *buffer, ContentBlockInfo *content_block);
Err _pttui_buffer_file_offset_pre_from_content_block_info(PttUIBuffer *pre_buffer, PttUIBuffer *current_buffer, ContentBlockInfo *content_block);
Err _pttui_buffer_file_offset_next_from_content_block_info(PttUIBuffer *next_buffer, PttUIBuffer *current_buffer, ContentBlockInfo *content_block);

/***
 * buffer-info / resource-info / resource-dict
 ***/

Err _pttui_buffer_info_to_resource_info(PttUIBuffer *head, PttUIBuffer *tail, PttUIResourceInfo *resource_info);
Err _pttui_buffer_info_set_buf_from_resource_dict(PttUIBuffer *head, PttUIBuffer *tail, PttUIResourceDict *resource_dict);

Err _modified_pttui_buffer_info_to_resource_info(PttUIBuffer *head, PttUIBuffer *tail, PttUIResourceInfo *resource_info);

Err _remove_deleted_pttui_buffer_in_buffer_info(PttUIBufferInfo *buffer_info);

Err _reset_pttui_buffer_info(PttUIBufferInfo *buffer_info, FileInfo *file_info);

/***
 * shrink
 ***/
Err _sync_pttui_buffer_info_is_to_shrink(PttUIBufferInfo *buffer_info, bool *is_to_shrink);

Err
_sync_pttui_buffer_info_shrink_head(PttUIBufferInfo *buffer_info);

Err _sync_pttui_buffer_info_shrink_tail(PttUIBufferInfo *buffer_info);

#ifdef __cplusplus
}
#endif

#endif /* PTTUI_BUFFER_H */

