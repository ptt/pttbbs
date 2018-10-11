/* $Id$ */
#ifndef VEDIT3_H
#define VEDIT3_H

#include "ptterr.h"
#include "ptt_const.h"
#include "cmpttui/pttui_state.h"
#include "cmpttui/pttui_buffer.h"
#include "cmpttui/pttui_thread.h"
#include "cmpttui/pttui_thread_lock.h"

#include "cmpttdb.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>
#include <string.h>

#include "common.h"
#include "ansi.h"
#include "vtuikit.h"

#include "var.h"

#define DEFAULT_ITER_VEDIT3_WAIT_BUFFER_STATE_SYNC 200

typedef struct VEdit3EditorStatus {
    bool is_insert;
    bool is_ansi;
    bool is_indent;
    bool is_phone;
    bool is_raw;
    bool is_mbcs;

    int phone_mode;

    int current_line;
    int current_buffer_line;
    int current_col;
    int edit_margin;
    int last_margin;

    bool is_own_lock_buffer_info;
    bool is_own_wrlock_buffer_info;
    bool is_redraw_everything;
    bool is_scroll_up;
    bool is_scroll_down;

    bool is_end;

    PttUIBuffer *current_buffer;

    int mode0;
    int destuid0;
} VEdit3EditorStatus; 

extern VEdit3EditorStatus DEFAULT_VEDIT3_EDITOR_STATUS;
extern VEdit3EditorStatus VEDIT3_EDITOR_STATUS;

int vedit3_wrapper(const char *fpath, int saveheader, char title[TTLEN + 1], int flags, fileheader_t *fhdr, boardheader_t *bp);

Err vedit3(UUID main_id, char *title, int edflags, int *money);

Err vedit3_init_buffer();

Err vedit3_buffer();

Err vedit3_repl_wrlock_file_info_buffer_info(bool *is_lock_file_info, bool *is_lock_wr_buffer_info, bool *is_lock_buffer_info);

Err vedit3_repl_wrunlock_file_info_buffer_info(bool is_lock_file_info, bool is_lock_wr_buffer_info, bool is_lock_buffer_info);

Err vedit3_repl_rdlock_file_info_buffer_info(bool *is_lock_file_info, bool *is_lock_buffer_info);

Err vedit3_repl_unlock_file_info_buffer_info(bool is_lock_file_info, bool is_lock_buffer_info);

Err vedit3_repl_lock_wr_buffer_info();

Err vedit3_repl_unlock_wr_buffer_info();

Err vedit3_repl_lock_buffer_info();

Err vedit3_repl_unlock_buffer_info();

Err vedit3_repl_wrlock_buffer_info();

Err vedit3_repl_wrunlock_buffer_info();

Err vedit3_repl_wrlock_file_info();

Err vedit3_repl_wrunlock_file_info();

Err vedit3_repl_rdlock_file_info();

Err vedit3_repl_unlock_file_info();

// wait-buffer
Err vedit3_wait_buffer_init();

Err vedit3_wait_buffer_state_sync(int n_iter);

Err vedit3_wait_buffer_thread_loop(enum PttUIThreadState expected_state);

#ifdef __cplusplus
}
#endif

#endif /* VEDIT3_H */
