/* $Id$ */
#ifndef PTTUI_BUFFER_H
#define PTTUI_BUFFER_H

#include "ptterr.h"
#include "cmpttui/pttui_state.h"
#include "cmpttdb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOFT_N_PTTUI_BUFFER 100
#define HARD_N_PTTUI_BUFFER 250
#define N_SHRINK_PTTUI_BUFFER 600

#define N_TO_DELETE_SAVE_PTTUI_BUFFER_TO_TMP_FILE 40
#define N_NEW_SAVE_PTTUI_BUFFER_TO_TMP_FILE 100

#define INVALID_LINE_OFFSET -1
#define INVALID_LINE_OFFSET_PRE_END -2
#define INVALID_LINE_OFFSET_NEXT_END -3
#define INVALID_LINE_OFFSET_NEW -4

#define INVALID_FILE_OFFSET 0 // XXX hack for saving as file-name

typedef struct PttUIBuffer {
    struct PttUIBuffer *next;
    struct PttUIBuffer *pre;

    UUID the_id;
    enum PttDBContentType content_type;    
    int block_offset;
    int line_offset;
    int comment_offset;
    
    int load_line_offset;
    int load_line_pre_offset;
    int load_line_next_offset;

    int file_offset;
    int file_line_offset;
    int file_line_pre_offset;
    int file_line_next_offset;
    
    enum PttDBStorageType storage_type;

    bool is_modified;
    bool is_new;
    bool is_to_delete;

    int len_no_nl;
    char *buf;
} PttUIBuffer;

typedef struct PttUIBufferInfo {
    PttUIBuffer *head;
    PttUIBuffer *tail;
    int n_buffer;

    int n_to_delete;
    int n_new;

    bool is_to_save;
    bool is_saved;

    PttUIBuffer *to_delete_buffer_head;
    PttUIBuffer *to_delete_buffer_tail;
    int n_to_delete_buffer;

    UUID main_id;
} PttUIBufferInfo;

bool pttui_buffer_is_begin_ne(PttUIBuffer *buffer, PttUIBuffer *head);
bool pttui_buffer_is_end_ne(PttUIBuffer *buffer, PttUIBuffer *tail);
PttUIBuffer * pttui_buffer_next_ne(PttUIBuffer *buffer, PttUIBuffer *tail);
PttUIBuffer * pttui_buffer_pre_ne(PttUIBuffer *buffer, PttUIBuffer *head);

Err safe_free_pttui_buffer(PttUIBuffer **buffer);

Err destroy_pttui_buffer_info(PttUIBufferInfo *buffer_info);

Err pttui_buffer_is_begin_of_file(PttUIBuffer *buffer, FileInfo *file_info, bool *is_begin);
Err pttui_buffer_is_eof(PttUIBuffer *buffer, FileInfo *file_info, bool *is_eof);

Err sync_pttui_buffer_info(PttUIBufferInfo *buffer_info, PttUIBuffer *current_buffer, PttUIState *state, FileInfo *file_info, PttUIBuffer **new_buffer);

Err resync_all_pttui_buffer_info(PttUIBufferInfo *buffer_info, PttUIState *state, FileInfo *file_info, PttUIBuffer **new_buffer);

Err extend_pttui_buffer_info(FileInfo *file_info, PttUIBufferInfo *buffer_info, PttUIBuffer *current_buffer);

Err pttui_buffer_insert_buffer(PttUIBuffer *current_buffer, PttUIBuffer *next_buffer);

Err check_and_save_pttui_buffer_info_to_tmp_file(PttUIBufferInfo *buffer_info, FileInfo *file_info);

Err save_pttui_buffer_info_to_tmp_file(PttUIBufferInfo *buffer_info, FileInfo *file_info);

Err save_pttui_buffer_info_to_db(PttUIBufferInfo *buffer_info, FileInfo *file_info, char *user, char *ip);

Err check_and_shrink_pttui_buffer_info(PttUIBufferInfo *buffer_info, FileInfo *file_info);

Err pttui_buffer_add_to_delete_buffer_list(PttUIBuffer *buffer, PttUIBufferInfo *buffer_info);

Err pttui_buffer_wrlock_file_info(bool *is_lock_file_info);
Err pttui_buffer_wrunlock_file_info(bool is_lock_file_info);
Err pttui_buffer_rdlock_file_info();
Err pttui_buffer_unlock_file_info();
Err pttui_buffer_lock_wr_buffer_info(bool *is_lock_wr_buffer_info);
Err pttui_buffer_unlock_wr_buffer_info(bool is_lock_wr_buffer_info);
Err pttui_buffer_wrlock_buffer_info(bool *is_lock_buffer_info);
Err pttui_buffer_wrunlock_buffer_info(bool is_lock_buffer_info);
Err pttui_buffer_rdlock_buffer_info();
Err pttui_buffer_unlock_buffer_info();

#ifdef __cplusplus
}
#endif

#endif /* PTTUI_BUFFER_H */

