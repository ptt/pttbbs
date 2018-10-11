/* $Id$ */
#ifndef VEDIT3_ACTION_H
#define VEDIT3_ACTION_H

#include "ptterr.h"
#include "cmpttdb.h"

#ifdef __cplusplus
extern "C" {
#endif

Err vedit3_action_to_store(bool *is_end);

Err vedit3_action_save_and_exit(bool *is_end);

Err vedit3_action_goto();

Err vedit3_action_block_cut();

Err vedit3_action_exit(bool *is_end);

Err vedit3_action_insert_ansi_code();

Err vedit3_action_escape();

Err vedit3_action_search();

Err vedit3_action_toggle_insert();

Err vedit3_action_insert_tab();

Err vedit3_action_toggle_ansi();

Err vedit3_action_edit_assist();

Err vedit3_action_toggle_phone_mode();

Err vedit3_action_t_users();

Err vedit3_action_insert_char(int ch);

Err vedit3_action_move_left();

Err vedit3_action_move_right();

Err vedit3_action_move_up();
Err vedit3_action_move_down();

Err vedit3_action_move_pgup();
Err vedit3_action_move_pgdn();

Err vedit3_action_move_end_line();

Err vedit3_action_move_begin_line();

Err vedit3_action_move_start_file();

Err vedit3_action_move_tail_file();

Err vedit3_action_redraw();

Err vedit3_action_show_help();

Err vedit3_action_delete_char();

Err vedit3_action_backspace();

Err vedit3_action_delete_line();

Err vedit3_action_delete_end_of_line();

Err vedit3_action_insert_new_line();

Err vedit3_action_comment_init_comment_reply();

Err vedit3_action_comment_delete_comment();

#ifdef __cplusplus
}
#endif

#endif /* VEDIT3_ACTION_H */

