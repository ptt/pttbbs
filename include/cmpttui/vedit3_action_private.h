/* $Id$ */
#ifndef VEDIT3_ACTION_PRIVATE_H
#define VEDIT3_ACTION_PRIVATE_H

#include "ptterr.h"
#include "cmpttui/vedit3.h"
#include "cmpttui/pttui_util.h"
#include "cmpttui/pttui_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "config.h"
    
#include "vtkbd.h"
#include "proto.h"

#define VEDIT3_ACTION_N_GET_KEY 1
#define NS_SLEEP_WAIT_BUFFER_SAVE 50000000 // 50 ms
#define N_ITER_WAIT_BUFFER_SAVE 100         // wait for 5 secs

Err _vedit3_action_get_key(int *ch);

Err _vedit3_action_move_up_ensure_top_of_window();

Err _vedit3_action_move_down_ensure_end_of_window();

Err _vedit3_action_move_pgup_get_expected_top_line_buffer(VEdit3EditorStatus *editor_status, FileInfo *file_info, PttUIState *current_state, PttUIState *expected_state, int *n_pre_line);

Err _vedit3_action_move_pgdn_get_expected_top_line_buffer(VEdit3EditorStatus *editor_status, FileInfo *file_info, PttUIState *current_state, PttUIState *expected_state, int *n_next_line);

Err _vedit3_action_to_store_main(int ch, bool *is_end);
Err _vedit3_action_to_store_comment(int ch, bool *is_end);
Err _vedit3_action_to_store_comment_reply(int ch, bool *is_end);

Err _vedit3_action_ensure_current_col(int current_col);

Err _vedit3_action_insert_dchar_core(const char *dchar);
Err _vedit3_action_insert_char_core(int ch);
Err _vedit3_action_ensure_buffer_wrap();

Err _vedit3_action_delete_char_core();
Err _vedit3_action_concat_next_line();

// function to new line
Err _vedit3_action_buffer_split_core(PttUIBuffer *current_buffer, int pos, int indent, PttUIBuffer **new_buffer);

// function to remove line
Err _vedit3_action_delete_line_core(PttUIBuffer *buffer);

#ifdef __cplusplus
}
#endif

#endif /* VEDIT3_ACTION_PRIVATE_H */

