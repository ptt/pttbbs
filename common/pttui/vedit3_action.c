#include "cmpttui/vedit3_action.h"
#include "cmpttui/vedit3_action_private.h"

Err
vedit3_action_to_store(bool *is_end)
{
    Err error_code = S_OK;
    int ch;

    for(int i = 0; i < VEDIT3_ACTION_N_GET_KEY; i++) {
        error_code = _vedit3_action_get_key(&ch);
        if (error_code) break;

        switch(VEDIT3_EDITOR_STATUS.current_buffer->content_type) {
        case PTTDB_CONTENT_TYPE_MAIN:
            error_code = _vedit3_action_to_store_main(ch, is_end);
            break;
        case PTTDB_CONTENT_TYPE_COMMENT:
            error_code = _vedit3_action_to_store_comment(ch, is_end);
            break;
        case PTTDB_CONTENT_TYPE_COMMENT_REPLY:
            error_code = _vedit3_action_to_store_comment_reply(ch, is_end);
            break;
        default:
            break;
        }
    }
    if (error_code == S_ERR_NO_KEY) error_code = S_OK;

    return error_code;
}


Err
_vedit3_action_to_store_main(int ch, bool *is_end) {
    Err error_code = S_OK;
    if (ch < 0x100 && isprint2(ch)) {
        error_code = vedit3_action_insert_char(ch);
        return error_code;
    }

    // XXX remove lua-parser for now
    // if(ch == KEY_ESC && KEY_ESC_arg == 'S') VEDIT3_EDITOR_STATUS.is_syntax_parser = !VEDIT3_EDITOR_STATUS.is_syntax_parser;

    // ch as ctrl
    if(ch == KEY_ESC) ch = pttui_ctrl_key_ne(ch);

    // ctrl-command
    switch (ch) {
    case KEY_F10:
    case Ctrl('X'): // save and exit
        error_code = vedit3_action_save_and_exit(is_end);
        break;
    case KEY_F5:    // goto-line
        error_code = vedit3_action_goto();
        break;
    case KEY_F8:    // users
        error_code = vedit3_action_t_users();
        break;
    case Ctrl('W'):
        error_code = vedit3_action_block_cut();
        break;
    case Ctrl('Q'): // quit
        error_code = vedit3_action_exit(is_end);
        break;
    case Ctrl('C'): // ansi-code
        error_code = vedit3_action_insert_ansi_code();
        break;
    case KEY_ESC: // escape
        error_code = vedit3_action_escape();
        break;
    case Ctrl('S'): // search-str
    case KEY_F3:
        error_code = vedit3_action_search();
        break;
    case Ctrl('U'): // insert-esc
        error_code = vedit3_action_insert_char(ESC_CHR);
        break;
    case Ctrl('V'): // toggle ansi-color
        error_code = vedit3_action_toggle_ansi();
        break;
    case Ctrl('I'): // insert-tab
        error_code = vedit3_action_insert_tab();
        break;
    case KEY_ENTER: // new-line
        error_code = vedit3_action_insert_new_line();
        break;
    case Ctrl('G'): // edit-assistant
        error_code = vedit3_action_edit_assist();
        break;
    case Ctrl('P'): // toogle-phone-mode
        error_code = vedit3_action_toggle_phone_mode();
        break;
    case KEY_F1:
    case Ctrl('Z'): // help
        error_code = vedit3_action_show_help();
        break;
    case Ctrl('L'): // redraw
        error_code = vedit3_action_redraw();
        break;
    case KEY_LEFT:
        error_code = vedit3_action_move_left();
        break;
    case KEY_RIGHT:
        error_code = vedit3_action_move_right();
        break;
    case KEY_UP:
        error_code = vedit3_action_move_up();
        break;
    case KEY_DOWN:
        error_code = vedit3_action_move_down();
        break;
    case Ctrl('B'):
    case KEY_PGUP:
        error_code = vedit3_action_move_pgup();
        break;
    case Ctrl('F'):
    case KEY_PGDN:
        error_code = vedit3_action_move_pgdn();
        break;
    case KEY_END:
    case Ctrl('E'):
        error_code = vedit3_action_move_end_line();
        break;
    case KEY_HOME:
    case Ctrl('A'):
        error_code = vedit3_action_move_begin_line();
        break;
    case Ctrl(']'):
        error_code = vedit3_action_move_start_file();
        break;
    case Ctrl('T'):
        error_code = vedit3_action_move_tail_file();
        break;
    case Ctrl('O'):
        case KEY_INS:
        error_code = vedit3_action_toggle_insert();
        break;
    case KEY_BS:
        error_code = vedit3_action_backspace();
        break;
    case Ctrl('D'):
    case KEY_DEL:
        error_code = vedit3_action_delete_char();
        break;
    case Ctrl('Y'):
        error_code = vedit3_action_delete_line();
        break;
    case Ctrl('K'):
        error_code = vedit3_action_delete_end_of_line();
        break;
    default:
        break;
    }

    return error_code;
}

Err
_vedit3_action_to_store_comment(int ch, bool *is_end) {
    Err error_code = S_OK;
    // ctrl-command
    switch (ch) {
    case KEY_F10:
    case Ctrl('X'): // save and exit
        *is_end = true;
        break;
    case KEY_F5:    // goto-line
        break;
    case KEY_F8:    // users
        error_code = vedit3_action_t_users();
        break;
    case Ctrl('W'):
        break;
    case Ctrl('Q'): // quit
        break;
    case Ctrl('C'): // ansi-code
        break;
    case Ctrl('S'): // search-str
    case KEY_F3:
        break;
    case Ctrl('V'): // toggle ansi-color
        error_code = vedit3_action_toggle_ansi();
        break;
    case KEY_F1:
    case Ctrl('Z'): // help
        error_code = vedit3_action_show_help();
        break;
    case Ctrl('L'): // redraw
        error_code = vedit3_action_redraw();
        break;
    case KEY_LEFT:
    case KEY_UP:
        error_code = vedit3_action_move_up();
        break;
    case KEY_RIGHT:
    case KEY_DOWN:
        error_code = vedit3_action_move_down();
        break;
    case Ctrl('B'):
    case KEY_PGUP:
        error_code = vedit3_action_move_pgup();
        break;
    case Ctrl('F'):
    case KEY_PGDN:
        error_code = vedit3_action_move_pgdn();
        break;
    case Ctrl(']'):
        error_code = vedit3_action_move_start_file();
        break;
    case Ctrl('T'):
        error_code = vedit3_action_move_tail_file();
        break;
    case Ctrl('O'):
    case KEY_INS:
        error_code = vedit3_action_toggle_insert();
        break;
    case KEY_ENTER:
    case Ctrl('R'):
        error_code = vedit3_action_comment_init_comment_reply();
        break;
    case Ctrl('D'):
    case KEY_DEL:
    case Ctrl('Y'):
    case Ctrl('K'):
        error_code = vedit3_action_comment_delete_comment();
        break;
    default:
        break;
    }

    return error_code;
}

Err
_vedit3_action_to_store_comment_reply(int ch, bool *is_end) {
    Err error_code = S_OK;
    if (ch < 0x100 && isprint2(ch)) {
        error_code = vedit3_action_insert_char(ch);
        return error_code;
    }

    // ch as ctrl
    switch (ch) {
    case KEY_UP:
    case KEY_DOWN:
        break;
    case KEY_ESC:
        break;
    }


    // ctrl-command
    switch (ch) {
    case KEY_F10:
    case Ctrl('X'): // save and exit
        *is_end = true;
        break;
    case KEY_F5:    // goto-line
        break;

    case KEY_F8:    // users
        error_code = vedit3_action_t_users();
        break;
    case Ctrl('W'):
        break;

    case Ctrl('Q'): // quit
        break;

    case Ctrl('C'): // ansi-code
        break;

    case KEY_ESC:   // escape
            break;

    case Ctrl('S'): // search-str
    case KEY_F3:
        break;

    /*
    case Ctrl('U'): // insert-esc
        error_code = vedit3_action_insert_char(ESC_CHR);
        break;
    */
    case Ctrl('V'): // toggle ansi-color
        error_code = vedit3_action_toggle_ansi();
        break;
    /*
    case Ctrl('I'): // insert-tab
        error_code = vedit3_action_insert_tab();
        break;

    case KEY_ENTER: // new-line
        error_code = vedit3_action_insert_new_line();
        break;

    case Ctrl('G'): // edit-assistant
        break;
    
    case Ctrl('P'): // toogle-phone-mode
        error_code = vedit3_action_toggle_phone_mode();
        break;
    */
    case KEY_F1:
    case Ctrl('Z'): // help
        error_code = vedit3_action_show_help();
        break;
    case Ctrl('L'): // redraw
        error_code = vedit3_action_redraw();
        break;
    case KEY_LEFT:
        error_code = vedit3_action_move_left();
        break;
    case KEY_RIGHT:
        error_code = vedit3_action_move_right();
        break;
    case KEY_UP:
        error_code = vedit3_action_move_up();
        break;
    case KEY_DOWN:
        error_code = vedit3_action_move_down();
        break;
    case Ctrl('B'):
    case KEY_PGUP:
        error_code = vedit3_action_move_pgup();
        break;
    case Ctrl('F'):
    case KEY_PGDN:
        error_code = vedit3_action_move_pgdn();
        break;
    case KEY_END:
    case Ctrl('E'):
        error_code = vedit3_action_move_end_line();
        break;
    case KEY_HOME:
    case Ctrl('A'):
        error_code = vedit3_action_move_begin_line();
        break;
    /*
    case Ctrl(']'):
        error_code = vedit3_action_move_start_file();
        break;
    case Ctrl('T'):
        error_code = vedit3_action_move_tail_file();
        break;
    case Ctrl('O'):
        case KEY_INS:
        error_code = vedit3_action_toggle_insert();
        break;
    */
    case KEY_BS:
        error_code = vedit3_action_backspace();
        break;
    case Ctrl('D'):
    case KEY_DEL:
        error_code = vedit3_action_delete_char();
        break;
    /*
    case Ctrl('Y'):
        error_code = vedit3_action_delete_line();
        break;
    case Ctrl('K'):
        error_code = vedit3_action_delete_end_of_line();
        break;
    */
    default:
        break;
    }

    return error_code;
}

Err
_vedit3_action_get_key(int *ch)
{
    int is_available = vkey_poll(0);
    if (!is_available) return S_ERR_NO_KEY;

    int tmp = vkey();

    *ch = tmp;

    return S_OK;
}


/********************
 * action-command
 ********************/

Err
vedit3_action_save_and_exit(bool *is_end)
{
    Err error_code = S_OK;

    PTTUI_BUFFER_INFO.is_to_save = true;

    bool tmp_is_end = false;
    struct timespec req = {0, NS_SLEEP_WAIT_BUFFER_SAVE};
    struct timespec rem = {};

    int ret_sleep = 0;
    int i = 0;
    for(i = 0; i < N_ITER_WAIT_BUFFER_SAVE; i++) {
        if(PTTUI_BUFFER_INFO.is_saved) tmp_is_end = true;

        if(tmp_is_end) break;

        ret_sleep = nanosleep(&req, &rem);
        if(ret_sleep) {
            error_code = S_ERR;
            break;
        }
    }    

    if(i == N_ITER_WAIT_BUFFER_SAVE) error_code = S_ERR_SAVE;

    *is_end = true;

    return error_code;
}

Err
vedit3_action_goto()
{
    return S_OK;
}

Err
vedit3_action_block_cut()
{
    return S_OK;
}

Err
vedit3_action_exit(bool *is_end)
{
    *is_end = true;

    return S_OK;
}

Err
vedit3_action_insert_ansi_code()
{
    return S_OK;
}

Err
vedit3_action_escape()
{
    return S_OK;
}

Err
vedit3_action_search()
{
    return S_OK;
}

Err
vedit3_action_toggle_insert()
{
    return S_OK;    
}

Err
vedit3_action_insert_tab()
{
    return S_OK;
}

Err
vedit3_action_edit_assist()
{
    return S_OK;
}

Err
vedit3_action_toggle_phone_mode()
{
    return S_OK;
}

Err
vedit3_action_comment_init_comment_reply()
{
    return S_OK;
}

Err
vedit3_action_comment_delete_comment()
{
    return S_OK;
}

/**
 * @brief [brief description]
 * @details refer: Ctrl('V') in vedit2 in edit.c
 */
Err
vedit3_action_toggle_ansi()
{
    VEDIT3_EDITOR_STATUS.is_ansi ^= 1;

    // XXX block selection

    VEDIT3_EDITOR_STATUS.is_redraw_everything = true;

    return S_OK;
}

/**
 * @brief [brief description]
 * @details ref: KEY_F8 in vedit2 in edit.c
 *
 */
Err
vedit3_action_t_users()
{
    t_users();
    VEDIT3_EDITOR_STATUS.is_redraw_everything = true;

    return S_OK;
}

Err
vedit3_action_insert_char(int ch)
{
    Err error_code = S_OK;
    char *pstr = NULL;
    if(VEDIT3_EDITOR_STATUS.is_phone) {
        error_code = pttui_phone_char(ch, VEDIT3_EDITOR_STATUS.phone_mode, &pstr);
        if(error_code) return error_code;
    }   

    bool is_lock_file_info = false;
    bool is_lock_wr_buffer_info = false;
    bool is_lock_buffer_info = false;

    PttUIBuffer *tmp_tail = NULL;
    error_code = vedit3_repl_wrlock_file_info_buffer_info(&is_lock_file_info, &is_lock_wr_buffer_info, &is_lock_buffer_info);

    if(!error_code) {
        error_code = pstr ? _vedit3_action_insert_dchar_core(pstr) : _vedit3_action_insert_char_core(ch);
    }

    tmp_tail = PTTUI_BUFFER_INFO.tail;

    Err error_code_lock = vedit3_repl_wrunlock_file_info_buffer_info(is_lock_file_info, is_lock_wr_buffer_info, is_lock_buffer_info);
    if(!error_code && error_code_lock) error_code = error_code_lock;

    if(!error_code && VEDIT3_EDITOR_STATUS.current_col > VEDIT3_EDITOR_STATUS.current_buffer->len_no_nl && pttui_buffer_next_ne(VEDIT3_EDITOR_STATUS.current_buffer, tmp_tail)) {
        VEDIT3_EDITOR_STATUS.current_col = VEDIT3_EDITOR_STATUS.current_col - VEDIT3_EDITOR_STATUS.current_buffer->len_no_nl;
        error_code = vedit3_action_move_down();
    }

    return error_code;
}

Err
vedit3_action_insert_new_line()
{
    PttUIBuffer *new_buffer = NULL;

    bool is_lock_file_info = false;
    bool is_lock_wr_buffer_info = false;
    bool is_lock_buffer_info = false;

    Err error_code = vedit3_repl_wrlock_file_info_buffer_info(&is_lock_file_info, &is_lock_wr_buffer_info, &is_lock_buffer_info);

    if(!error_code) {
        error_code = _vedit3_action_buffer_split_core(VEDIT3_EDITOR_STATUS.current_buffer, VEDIT3_EDITOR_STATUS.current_col, 0, &new_buffer);
    }

    Err error_code_lock = vedit3_repl_wrunlock_file_info_buffer_info(is_lock_file_info, is_lock_wr_buffer_info, is_lock_buffer_info);
    if(!error_code && error_code_lock) error_code = error_code_lock;

    if(!error_code) {
        error_code = vedit3_action_move_down();
    }

    if(!error_code) {
        error_code = vedit3_action_move_begin_line();
    }

    VEDIT3_EDITOR_STATUS.is_redraw_everything = true;

    return error_code;
}

Err
vedit3_action_move_right()
{
    Err error_code = S_OK;
    int ansi_current_col = 0;
    int mbcs_current_col = 0;
    int orig_current_col = VEDIT3_EDITOR_STATUS.current_col;
    // within the same line
    if (VEDIT3_EDITOR_STATUS.current_col < VEDIT3_EDITOR_STATUS.current_buffer->len_no_nl) {
        // TODO use function-map to replace if-else
        if (VEDIT3_EDITOR_STATUS.is_ansi) {
            error_code = pttui_n2ansi(VEDIT3_EDITOR_STATUS.current_col, VEDIT3_EDITOR_STATUS.current_buffer->buf, &ansi_current_col);
            ansi_current_col++;
            error_code = pttui_ansi2n(ansi_current_col, VEDIT3_EDITOR_STATUS.current_buffer->buf, &VEDIT3_EDITOR_STATUS.current_col);
        }
        else {
            VEDIT3_EDITOR_STATUS.current_col++;
        }

        if (VEDIT3_EDITOR_STATUS.is_mbcs) {
            error_code = pttui_fix_cursor(VEDIT3_EDITOR_STATUS.current_buffer->buf, VEDIT3_EDITOR_STATUS.current_col, PTTUI_FIX_CURSOR_DIR_RIGHT, &mbcs_current_col);
            VEDIT3_EDITOR_STATUS.current_col = mbcs_current_col;
        }

        return S_OK;
    }

    // need to move to next line

    // move to next line
    error_code = vedit3_action_move_down();

    VEDIT3_EDITOR_STATUS.current_col = 0;

    return error_code;
}

Err
vedit3_action_move_left()
{
    Err error_code = S_OK;
    int ansi_current_col = 0;
    int mbcs_current_col = 0;
    int orig_current_col = VEDIT3_EDITOR_STATUS.current_col;
    // within the same line
    if (VEDIT3_EDITOR_STATUS.current_col) {
        // TODO use function-map to replace if-else
        if (VEDIT3_EDITOR_STATUS.is_ansi) {
            error_code = pttui_n2ansi(VEDIT3_EDITOR_STATUS.current_col, VEDIT3_EDITOR_STATUS.current_buffer->buf, &ansi_current_col);
            ansi_current_col--;
            error_code = pttui_ansi2n(ansi_current_col, VEDIT3_EDITOR_STATUS.current_buffer->buf, &VEDIT3_EDITOR_STATUS.current_col);


        }
        else {
            VEDIT3_EDITOR_STATUS.current_col--;
        }

        if (VEDIT3_EDITOR_STATUS.is_mbcs) {
            error_code = pttui_fix_cursor(VEDIT3_EDITOR_STATUS.current_buffer->buf, VEDIT3_EDITOR_STATUS.current_col, PTTUI_FIX_CURSOR_DIR_LEFT, &mbcs_current_col);
            VEDIT3_EDITOR_STATUS.current_col = mbcs_current_col;
        }

        return S_OK;
    }

    // begin of the file. (pos: 0, 0) no need to do anything (move_up take cake of line only, not the col)
    if(VEDIT3_EDITOR_STATUS.current_buffer_line == 0 && VEDIT3_EDITOR_STATUS.current_col == 0) return S_OK;

    // move to previous line
    error_code = vedit3_action_move_up();

    VEDIT3_EDITOR_STATUS.current_col = VEDIT3_EDITOR_STATUS.current_buffer->len_no_nl;

    return error_code;
}

Err
vedit3_action_move_up()
{
    Err error_code = S_OK;
    // is begin of file
    bool is_begin = false;
    error_code = pttui_buffer_is_begin_of_file(VEDIT3_EDITOR_STATUS.current_buffer, &PTTUI_FILE_INFO, &is_begin);
    if (error_code) return error_code;
    if (is_begin) return S_OK;

    // check begin-of-window
    error_code = _vedit3_action_move_up_ensure_top_of_window();
    if (error_code) return error_code;

    // move next
    error_code = vedit3_repl_lock_buffer_info();
    if (error_code) return error_code;

    PttUIBuffer *pre_buffer = pttui_buffer_pre_ne(VEDIT3_EDITOR_STATUS.current_buffer, PTTUI_BUFFER_INFO.head);
    if(!pre_buffer) error_code = S_ERR;
    if(!error_code) {
        VEDIT3_EDITOR_STATUS.current_buffer = pre_buffer;
    }

    Err error_code_lock = vedit3_repl_unlock_buffer_info();
    if (!error_code && error_code_lock) error_code = S_ERR_EDIT_LOCK;

    if (VEDIT3_EDITOR_STATUS.current_line > 0) VEDIT3_EDITOR_STATUS.current_line--;
    VEDIT3_EDITOR_STATUS.current_buffer_line--;

    Err error_code2 = _vedit3_action_ensure_current_col(VEDIT3_EDITOR_STATUS.current_col);
    if(!error_code && error_code2) error_code = error_code2;

    VEDIT3_EDITOR_STATUS.is_redraw_everything = true;

    return error_code;
}

Err
vedit3_action_move_down()
{
    Err error_code = S_OK;
    // is end of file
    bool is_eof = false;
    error_code = pttui_buffer_is_eof(VEDIT3_EDITOR_STATUS.current_buffer, &PTTUI_FILE_INFO, &is_eof);
    if (error_code) return error_code;
    if (is_eof) return S_OK;

    // check end-of-window
    error_code = _vedit3_action_move_down_ensure_end_of_window();
    if (error_code) return error_code;

    // move next
    error_code = vedit3_repl_lock_buffer_info();
    if (error_code) return error_code;

    PttUIBuffer *next_buffer = pttui_buffer_next_ne(VEDIT3_EDITOR_STATUS.current_buffer, PTTUI_BUFFER_INFO.tail);
    if(!next_buffer) error_code = S_ERR;
    if(!error_code) {
        VEDIT3_EDITOR_STATUS.current_buffer = next_buffer;
    }

    Err error_code_lock = vedit3_repl_unlock_buffer_info();
    if (error_code_lock) error_code = S_ERR_EDIT_LOCK;

    if (VEDIT3_EDITOR_STATUS.current_line < b_lines - 1) VEDIT3_EDITOR_STATUS.current_line++;
    VEDIT3_EDITOR_STATUS.current_buffer_line++;

    Err error_code2 = _vedit3_action_ensure_current_col(VEDIT3_EDITOR_STATUS.current_col);
    if(!error_code && error_code2) error_code = error_code2;

    VEDIT3_EDITOR_STATUS.is_redraw_everything = true;

    return error_code;
}

Err
vedit3_action_move_end_line()
{
    VEDIT3_EDITOR_STATUS.current_col = VEDIT3_EDITOR_STATUS.current_buffer->len_no_nl;

    return S_OK;
}

Err
vedit3_action_move_begin_line()
{
    VEDIT3_EDITOR_STATUS.current_col = 0;

    return S_OK;
}

Err
vedit3_action_move_pgup()
{
    Err error_code = S_OK;
    bool is_begin = false;
    int current_line = VEDIT3_EDITOR_STATUS.current_line;
    int current_col = VEDIT3_EDITOR_STATUS.current_col;
    for(int i = 0; i < b_lines - 1; i++) {
        error_code = vedit3_action_move_up();
        if(error_code) break;
        error_code = pttui_buffer_is_begin_of_file(VEDIT3_EDITOR_STATUS.current_buffer, &PTTUI_FILE_INFO, &is_begin);
        if(error_code) break;
        if(is_begin) break;
    }
    if(error_code) return error_code;

    if(!is_begin) {
        for(int i = 0; i < current_line; i++) {
            error_code = vedit3_action_move_up();
            if(error_code) break;
        }
        if(error_code) return error_code;

        for(int i = 0; i < current_line; i++) {
            error_code = vedit3_action_move_down();
            if(error_code) break;
        }
        if(error_code) return error_code;
    }

    error_code = _vedit3_action_ensure_current_col(current_col);

    return error_code;
}

Err
vedit3_action_move_pgdn()
{
    Err error_code = S_OK;
    bool is_eof = false;
    int current_line = VEDIT3_EDITOR_STATUS.current_line;
    int current_col = VEDIT3_EDITOR_STATUS.current_col;
    for(int i = 0; i < b_lines - 1; i++) {
        error_code = vedit3_action_move_down();
        if(error_code) break;
        error_code = pttui_buffer_is_eof(VEDIT3_EDITOR_STATUS.current_buffer, &PTTUI_FILE_INFO, &is_eof);
        if(error_code) break;
        if(is_eof) break;
    }
    if(error_code) return error_code;

    if(!is_eof) {
        for(int i = b_lines - 1; i > current_line; i--) {
            error_code = vedit3_action_move_down();
            if(error_code) break;
        }
        if(error_code) return error_code;

        for(int i = b_lines - 1; i > current_line; i--) {
            error_code = vedit3_action_move_up();
            if(error_code) break;
        }
        if(error_code) return error_code;
    }

    error_code = _vedit3_action_ensure_current_col(current_col);

    return error_code;
}

Err
vedit3_action_move_start_file()
{
    return S_OK;
}

Err
vedit3_action_move_tail_file()
{
    return S_OK;
}

Err
vedit3_action_redraw()
{
    VEDIT3_EDITOR_STATUS.is_redraw_everything = true;

    return S_OK;
}

Err
vedit3_action_show_help()
{
    more("etc/ve.hlp", true);

    VEDIT3_EDITOR_STATUS.is_redraw_everything = true;

    return S_OK;
}

/**
 * @brief 
 * @details ref: case KEY_BS in edit.c
 *          1. is-ansi: is-ansi == false
 *          2. is current-line == 0 and current-col == 0: no change
 *          3. current-col == 0: move to previous-line, try to concat the next line.
 *          4. move-left. do delete-char.
 */
Err
vedit3_action_backspace()
{
    Err error_code = S_OK;
    // XXX remove block cancel for now
    //block_cancel();

    // is-ansi: is-ansi = false
    if(VEDIT3_EDITOR_STATUS.is_ansi) {
        VEDIT3_EDITOR_STATUS.is_ansi = false;
        VEDIT3_EDITOR_STATUS.is_redraw_everything = true;
        return S_OK;
    }

    // current-buffer-line == 0 and current-col == 0: no change
    if(VEDIT3_EDITOR_STATUS.current_buffer_line == 0 && VEDIT3_EDITOR_STATUS.current_col == 0) return S_OK;

    // current-col == 0: move to previous-line, try to concat the next line.s
    bool is_lock_file_info = false;
    bool is_lock_wr_buffer_info = false;
    bool is_lock_buffer_info = false;
    Err error_code_lock = S_OK;

    if(VEDIT3_EDITOR_STATUS.current_col == 0) {
        error_code = vedit3_action_move_left();
        if(error_code) return error_code;

        error_code = vedit3_repl_wrlock_file_info_buffer_info(&is_lock_file_info, &is_lock_wr_buffer_info, &is_lock_buffer_info);

        if(!error_code) {
            error_code = _vedit3_action_concat_next_line();
        }

        error_code_lock = vedit3_repl_wrunlock_file_info_buffer_info(is_lock_file_info, is_lock_wr_buffer_info, is_lock_buffer_info);
        if(!error_code && error_code_lock) error_code = error_code_lock;

        return error_code;
    }

    error_code = vedit3_action_move_left();
    if(error_code) return error_code;

    error_code = vedit3_action_delete_char();

    return error_code;
}

Err
vedit3_action_delete_char()
{
    Err error_code = S_OK;
    Err error_code_lock = S_OK;

    bool is_lock_file_info = false;
    bool is_lock_wr_buffer_info = false;
    bool is_lock_buffer_info = false;

    error_code = vedit3_repl_wrlock_file_info_buffer_info(&is_lock_file_info, &is_lock_wr_buffer_info, &is_lock_buffer_info);

    if(!error_code && VEDIT3_EDITOR_STATUS.current_col == VEDIT3_EDITOR_STATUS.current_buffer->len_no_nl) { // last char
        error_code = _vedit3_action_concat_next_line();

        error_code_lock = vedit3_repl_wrunlock_file_info_buffer_info(is_lock_file_info, is_lock_wr_buffer_info, is_lock_buffer_info);
        if(!error_code && error_code_lock) error_code = error_code_lock;

        return error_code;
    }

    // not last char

    int w = VEDIT3_EDITOR_STATUS.is_mbcs ? pttui_mchar_len_ne((unsigned char*)(VEDIT3_EDITOR_STATUS.current_buffer->buf + VEDIT3_EDITOR_STATUS.current_col)) : 1;

    if(!error_code) {
        for(int i = 0; i < w; i++) {
            error_code = _vedit3_action_delete_char_core();
            if(error_code) break;
        }
    }

    int ansi_current_col = 0;
    if(!error_code && VEDIT3_EDITOR_STATUS.is_ansi) {
        error_code = pttui_n2ansi(VEDIT3_EDITOR_STATUS.current_col, VEDIT3_EDITOR_STATUS.current_buffer->buf, &ansi_current_col);
    }

    if(!error_code && VEDIT3_EDITOR_STATUS.is_ansi) {
        error_code = pttui_ansi2n(ansi_current_col, VEDIT3_EDITOR_STATUS.current_buffer->buf, &VEDIT3_EDITOR_STATUS.current_col);
    }

    VEDIT3_EDITOR_STATUS.current_buffer->is_modified = true;

    error_code_lock = vedit3_repl_wrunlock_file_info_buffer_info(is_lock_file_info, is_lock_wr_buffer_info, is_lock_buffer_info);
    if(!error_code && error_code_lock) error_code = error_code_lock;

    return S_OK;
}

Err
vedit3_action_delete_line()
{
    VEDIT3_EDITOR_STATUS.current_col = 0;

    return vedit3_action_delete_end_of_line();
}

Err
vedit3_action_delete_end_of_line()
{
    VEDIT3_EDITOR_STATUS.current_buffer->len_no_nl = VEDIT3_EDITOR_STATUS.current_col;
    VEDIT3_EDITOR_STATUS.current_buffer->buf[VEDIT3_EDITOR_STATUS.current_col] = 0;
    return S_OK;
}


/********************
 * action-util
 ********************/

Err
_vedit3_action_insert_dchar_core(const char *dchar)
{
    if (!VEDIT3_EDITOR_STATUS.is_own_wrlock_buffer_info) return S_ERR_EDIT_LOCK;

    Err error_code = _vedit3_action_insert_char_core(*dchar);
    Err error_code2 = _vedit3_action_insert_char_core(*(dchar+1));

    if(error_code) return error_code;
    if(error_code2) return error_code2;

    return S_OK;
}

/**
 * @brief
 * @details ref: insert_char in edit.c
 *          XXX Design decision: 1. Insert in the middle of the sentences.
 *                               2. ignore DBCSAWARE, need to be taken care of in other place.
 *
 *
 * @param ch [description]
 */

Err
_vedit3_action_insert_char_core(int ch)
{
    Err error_code = S_OK;

    if (!VEDIT3_EDITOR_STATUS.is_own_wrlock_buffer_info) return S_ERR_EDIT_LOCK;

    PttUIBuffer *current_buffer = VEDIT3_EDITOR_STATUS.current_buffer;

    assert(VEDIT3_EDITOR_STATUS.current_col <= current_buffer->len_no_nl);
//#ifdef DEBUG
    //assert(curr_buf->currline->mlength == WRAPMARGIN);
//#endif

    // XXX remove block-cancel for now
    // block_cancel();


    int current_col_n2ansi = 0;
    if (VEDIT3_EDITOR_STATUS.current_col < current_buffer->len_no_nl && !VEDIT3_EDITOR_STATUS.is_insert) {
        // no need to change len
        current_buffer->buf[VEDIT3_EDITOR_STATUS.current_col++] = ch;

        /* Thor: ansi 編輯, 可以overwrite, 不蓋到 ansi code */
        if (VEDIT3_EDITOR_STATUS.is_ansi && !error_code) {
            error_code = pttui_n2ansi(VEDIT3_EDITOR_STATUS.current_col, current_buffer->buf, &current_col_n2ansi);
        }

        if (VEDIT3_EDITOR_STATUS.is_ansi && !error_code) {
            error_code = pttui_ansi2n(current_col_n2ansi, current_buffer->buf, &VEDIT3_EDITOR_STATUS.current_col);
        }
    }
    else { // insert-mode
        error_code = pttui_raw_shift_right(current_buffer->buf + VEDIT3_EDITOR_STATUS.current_col, current_buffer->len_no_nl - VEDIT3_EDITOR_STATUS.current_col + 1);

        current_buffer->buf[VEDIT3_EDITOR_STATUS.current_col++] = ch;
        ++(current_buffer->len_no_nl);
        current_buffer->buf[current_buffer->len_no_nl] = 0;
    }
    current_buffer->is_modified = true;

    error_code = _vedit3_action_ensure_buffer_wrap();

//#ifdef DEBUG
//    assert(curr_buf->currline->mlength == WRAPMARGIN);
//#endif

    return error_code;
}

Err
_vedit3_action_ensure_buffer_wrap()
{
    Err error_code = S_OK;

    if (!VEDIT3_EDITOR_STATUS.is_own_wrlock_buffer_info) return S_ERR_EDIT_LOCK;

    PttUIBuffer *current_buffer = VEDIT3_EDITOR_STATUS.current_buffer;

    if (current_buffer->len_no_nl < WRAPMARGIN) {
        return S_OK;
    }

    bool is_wordwrap = true;
    char *s = current_buffer->buf + current_buffer->len_no_nl - 1;
    while (s != current_buffer->buf && *s == ' ') s--;
    while (s != current_buffer->buf && *s != ' ') s--;
    if (s == current_buffer->buf) { // if only 1 word
        is_wordwrap = false;
        s = current_buffer->buf + (current_buffer->len_no_nl - 2);
    }

    PttUIBuffer *new_buffer = NULL;

    error_code = _vedit3_action_buffer_split_core(current_buffer, s - current_buffer->buf + 1, 0, &new_buffer);

    int new_buffer_len_no_nl = new_buffer ? new_buffer->len_no_nl : 0;
    if (!error_code && is_wordwrap && new_buffer && new_buffer_len_no_nl >= 1) {
        if (new_buffer->buf[new_buffer_len_no_nl - 1] != ' ') {
            new_buffer->buf[new_buffer_len_no_nl] = ' ';
            new_buffer->len_no_nl++;
            new_buffer->buf[new_buffer_len_no_nl] = '\0';
        }
    }

    VEDIT3_EDITOR_STATUS.is_redraw_everything = true;

    return error_code;
}

Err
_vedit3_action_ensure_current_col(int current_col) {
    VEDIT3_EDITOR_STATUS.current_col = (current_col < VEDIT3_EDITOR_STATUS.current_buffer->len_no_nl || VEDIT3_EDITOR_STATUS.current_buffer->content_type == PTTDB_CONTENT_TYPE_COMMENT) ? current_col : VEDIT3_EDITOR_STATUS.current_buffer->len_no_nl;

    return S_OK;
}

/**
 * @brief [brief description]
 * @details [long description]
 * 
 * @param current_buffer [description]
 * @param pos [description]
 * @param indent [description]
 * @param new_buffer [description]
 */
Err
_vedit3_action_buffer_split_core(PttUIBuffer *current_buffer, int pos, int indent, PttUIBuffer **new_buffer)
{
    Err error_code = S_OK;

    if (!VEDIT3_EDITOR_STATUS.is_own_wrlock_buffer_info) return S_ERR_EDIT_LOCK;

    // XXX should not happen.
    if(pos > current_buffer->len_no_nl) return S_OK;

    *new_buffer = malloc(sizeof(PttUIBuffer));
    PttUIBuffer *p_new_buffer = *new_buffer;
    bzero(p_new_buffer, sizeof(PttUIBuffer));

    memcpy(p_new_buffer->the_id, current_buffer->the_id, UUIDLEN);
    p_new_buffer->content_type = current_buffer->content_type;
    p_new_buffer->block_offset = current_buffer->block_offset;
    p_new_buffer->line_offset = current_buffer->line_offset + 1;
    p_new_buffer->comment_offset = current_buffer->comment_offset;
    p_new_buffer->load_line_offset = INVALID_LINE_OFFSET_NEW;
    p_new_buffer->load_line_pre_offset = current_buffer->load_line_offset;
    p_new_buffer->load_line_next_offset = current_buffer->load_line_next_offset;
    current_buffer->load_line_next_offset = INVALID_LINE_OFFSET_NEW;

    p_new_buffer->storage_type = PTTDB_STORAGE_TYPE_OTHER;

    p_new_buffer->is_modified = true;
    p_new_buffer->is_new = true;
    current_buffer->is_modified = true;

    p_new_buffer->len_no_nl = current_buffer->len_no_nl - pos + indent;
    p_new_buffer->buf = malloc(MAX_TEXTLINE_SIZE + 1);
    memset(p_new_buffer->buf, ' ', indent);

    char *p_buf = current_buffer->buf + pos;
    // XXX indent-mode
    /*
    if (curr_buf->indent_mode) {
        ptr = next_non_space_char(ptr);
        p->len = strlen(ptr) + spcs;
    }
    */
    memcpy(p_new_buffer->buf + indent, p_buf, current_buffer->len_no_nl - pos);
    p_new_buffer->buf[p_new_buffer->len_no_nl] = 0;

    current_buffer->len_no_nl = pos;
    current_buffer->buf[current_buffer->len_no_nl] = 0;

    error_code = pttui_buffer_insert_buffer(current_buffer, p_new_buffer);

    // buffer after new_buffer
    for(PttUIBuffer *p_buffer2 = pttui_buffer_next_ne(p_new_buffer, PTTUI_BUFFER_INFO.tail); p_buffer2 && p_buffer2->content_type == p_new_buffer->content_type && p_buffer2->block_offset == p_new_buffer->block_offset && p_buffer2->comment_offset == p_new_buffer->comment_offset; p_buffer2->line_offset++, p_buffer2 = pttui_buffer_next_ne(p_buffer2, PTTUI_BUFFER_INFO.tail));

    // file-info
    ContentBlockInfo *p_content_block = NULL;
    switch(current_buffer->content_type) {
    case PTTDB_CONTENT_TYPE_MAIN:
        PTTUI_FILE_INFO.n_main_line++;

        p_content_block = PTTUI_FILE_INFO.main_blocks + current_buffer->block_offset;
        p_content_block->n_new_line++;
        p_content_block->n_line++;
        break;
    case PTTDB_CONTENT_TYPE_COMMENT_REPLY:
        p_content_block = PTTUI_FILE_INFO.comments[current_buffer->comment_offset].comment_reply_blocks + current_buffer->block_offset;

        p_content_block->n_new_line++;
        p_content_block->n_line++;
        break;
    default:
        break;
    }

    // buffer-info
    if(PTTUI_BUFFER_INFO.tail == current_buffer) {
        PTTUI_BUFFER_INFO.tail = pttui_buffer_next_ne(current_buffer, PTTUI_BUFFER_INFO.tail);
    }
    PTTUI_BUFFER_INFO.n_buffer++;
    PTTUI_BUFFER_INFO.n_new++;

    VEDIT3_EDITOR_STATUS.is_redraw_everything = true;

    return error_code;
}

/**
 * @brief [brief description]
 * @details given that current-buffer is not eof, want to check the end of window and do corresponding works to maintain the state
 */
Err
_vedit3_action_move_up_ensure_top_of_window()
{
    // XXX assuming current-buffer <= end-line-buffer
    // need to move down
    if (VEDIT3_EDITOR_STATUS.current_line != 0) return S_OK;

    UUID main_id = {};
    memcpy(main_id, PTTUI_STATE.main_id, UUIDLEN);
    int n_window_line = PTTUI_STATE.n_window_line;

    UUID new_id = {};
    enum PttDBContentType new_content_type = PTTDB_CONTENT_TYPE_MAIN;
    int new_block_offset = 0;
    int new_line_offset = 0;
    int new_comment_offset = 0;
    enum PttDBStorageType _dummy = PTTDB_STORAGE_TYPE_MONGO;

    Err error_code = file_info_get_pre_line(&PTTUI_FILE_INFO, PTTUI_STATE.top_line_id, PTTUI_STATE.top_line_content_type, PTTUI_STATE.top_line_block_offset, PTTUI_STATE.top_line_line_offset, PTTUI_STATE.top_line_comment_offset, new_id, &new_content_type, &new_block_offset, &new_line_offset, &new_comment_offset, &_dummy);
    if (error_code) return error_code;

    error_code = pttui_set_expected_state(main_id, new_content_type, new_id, new_block_offset, new_line_offset, new_comment_offset, n_window_line);
    if (error_code) return error_code;

    error_code = vedit3_wait_buffer_state_sync(DEFAULT_ITER_VEDIT3_WAIT_BUFFER_STATE_SYNC);

    return error_code;
}

/**
 * @brief [brief description]
 * @details given that current-buffer is not eof, want to check the end of window and do corresponding works to maintain the state
 */
Err
_vedit3_action_move_down_ensure_end_of_window()
{
    // XXX assuming current-buffer <= end-line-buffer
    // need to move down
    if (VEDIT3_EDITOR_STATUS.current_line != b_lines - 1) return S_OK;

    UUID main_id = {};
    memcpy(main_id, PTTUI_STATE.main_id, UUIDLEN);
    int n_window_line = PTTUI_STATE.n_window_line;

    UUID new_id = {};
    enum PttDBContentType new_content_type = PTTDB_CONTENT_TYPE_MAIN;
    int new_block_offset = 0;
    int new_line_offset = 0;
    int new_comment_offset = 0;
    enum PttDBStorageType _dummy = PTTDB_STORAGE_TYPE_MONGO;

    Err error_code = file_info_get_next_line(&PTTUI_FILE_INFO, PTTUI_STATE.top_line_id, PTTUI_STATE.top_line_content_type, PTTUI_STATE.top_line_block_offset, PTTUI_STATE.top_line_line_offset, PTTUI_STATE.top_line_comment_offset, new_id, &new_content_type, &new_block_offset, &new_line_offset, &new_comment_offset, &_dummy);
    if (error_code) return error_code;

    error_code = pttui_set_expected_state(main_id, new_content_type, new_id, new_block_offset, new_line_offset, new_comment_offset, n_window_line);
    if (error_code) return error_code;

    error_code = vedit3_wait_buffer_state_sync(DEFAULT_ITER_VEDIT3_WAIT_BUFFER_STATE_SYNC);
    if (error_code) return error_code;

    return error_code;
}

Err
_vedit3_action_delete_char_core()
{
    if (!VEDIT3_EDITOR_STATUS.is_own_wrlock_buffer_info) return S_ERR_EDIT_LOCK;

    if(!VEDIT3_EDITOR_STATUS.current_buffer->len_no_nl) return S_OK;

    Err error_code = pttui_raw_shift_left(VEDIT3_EDITOR_STATUS.current_buffer->buf + VEDIT3_EDITOR_STATUS.current_col, VEDIT3_EDITOR_STATUS.current_buffer->len_no_nl - VEDIT3_EDITOR_STATUS.current_col + 1);
    if(error_code) return error_code;

    VEDIT3_EDITOR_STATUS.current_buffer->len_no_nl--;

    VEDIT3_EDITOR_STATUS.current_buffer->is_modified = true;    

    return S_OK;
}

/**
 * @brief [brief description]
 * @details assume in the end of line (current_col == current_buffer->len_no_nl)
 *          1. if no next: return
 *          2. if no next-non-space-char: delete next line
 *          3. if no overflow:
 *             3.1 concat
 *             3.2 delete next line
 *          4. 4.1. get first_word
 *             4.2. 如果 len + first_word >= WRAPMARGIN: no change.
 *             4.3. else: concat first-word.
 *                        if no second-word: delete next-line
 *                        else: shift-line.
 */
Err
_vedit3_action_concat_next_line()
{
    Err error_code = S_OK;

    if (!VEDIT3_EDITOR_STATUS.is_own_wrlock_buffer_info) return S_ERR_EDIT_LOCK;

    PttUIBuffer *current_buffer = VEDIT3_EDITOR_STATUS.current_buffer;
    PttUIBuffer *p_next_buffer = pttui_buffer_next_ne(current_buffer, PTTUI_BUFFER_INFO.tail);

    // 1. if no next: return
    if(!p_next_buffer) return S_OK;

    // 2. if no next-non-space-char: delete next char
    char *p_non_space_buf = NULL;
    error_code = pttui_next_non_space_char(p_next_buffer->buf, p_next_buffer->len_no_nl, &p_non_space_buf);
    if(error_code) return error_code;

    if(!p_non_space_buf) {
        error_code = _vedit3_action_delete_line_core(p_next_buffer);
        return error_code;
    }

    // 3. if no overflow
    int overflow = current_buffer->len_no_nl + p_next_buffer->len_no_nl - WRAPMARGIN;
    int non_space_offset = p_non_space_buf - p_next_buffer->buf;
    int len_word = 0;
    if(overflow < 0) {
        memcpy(current_buffer->buf + VEDIT3_EDITOR_STATUS.current_col, p_next_buffer->buf, p_next_buffer->len_no_nl);
        current_buffer->len_no_nl += p_next_buffer->len_no_nl;
        current_buffer->buf[current_buffer->len_no_nl] = 0;
        current_buffer->is_modified = true;

        error_code = _vedit3_action_delete_line_core(p_next_buffer);

        return error_code;
    }

    // 4. get first word
    error_code = pttui_first_word(p_non_space_buf, p_next_buffer->len_no_nl - non_space_offset, &len_word);
    if(error_code) return error_code;

    len_word += non_space_offset;

    // 4.2: if len + first_word >= WRAPMARGIN: no change
    if(current_buffer->len_no_nl + len_word >= WRAPMARGIN) return S_OK;

    // 4.3: concat first-word. shift next line.
    memcpy(current_buffer->buf + VEDIT3_EDITOR_STATUS.current_col, p_next_buffer->buf, len_word);
    current_buffer->len_no_nl += len_word;
    current_buffer->buf[current_buffer->len_no_nl] = 0;
    current_buffer->is_modified = true;

    error_code = pttui_next_non_space_char(p_next_buffer->buf + len_word, p_next_buffer->len_no_nl - len_word, &p_non_space_buf);
    if(error_code) return error_code;

    if(!p_non_space_buf) {
        error_code = _vedit3_action_delete_line_core(p_next_buffer);
        return error_code;
    }

    // shift next line.
    char *p_buf = p_next_buffer->buf;
    char *p_buf2 = p_non_space_buf;
    len_word = p_buf2 - p_buf;
    int n_shift = p_next_buffer->len_no_nl - len_word;
    for(int i = 0; *p_buf2 && i < n_shift; i++, p_buf++, p_buf2++) {
        *p_buf = *p_buf2;
    }
    p_next_buffer->len_no_nl -= len_word;
    p_next_buffer->is_modified = true;
    p_next_buffer->buf[p_next_buffer->len_no_nl] = 0;

    VEDIT3_EDITOR_STATUS.is_redraw_everything = true;

    return S_OK;
}

Err
_vedit3_action_delete_line_core(PttUIBuffer *buffer)
{
    if (!VEDIT3_EDITOR_STATUS.is_own_wrlock_buffer_info) return S_ERR_EDIT_LOCK;

    buffer->is_to_delete = true;
    buffer->is_modified = true;

    // buffer after buffer
    for (PttUIBuffer *p_buffer2 = pttui_buffer_next_ne(buffer, PTTUI_BUFFER_INFO.tail); p_buffer2 && p_buffer2->content_type == buffer->content_type && p_buffer2->block_offset == buffer->block_offset && p_buffer2->comment_offset == buffer->comment_offset; p_buffer2->line_offset--, p_buffer2 = pttui_buffer_next_ne(p_buffer2, PTTUI_BUFFER_INFO.tail));

    // file-info
    ContentBlockInfo *p_content_block = NULL;
    switch (buffer->content_type) {
    case PTTDB_CONTENT_TYPE_MAIN:
        PTTUI_FILE_INFO.n_main_line--;

        p_content_block = PTTUI_FILE_INFO.main_blocks + buffer->block_offset;

        p_content_block->n_to_delete_line++;
        p_content_block->n_line--;
        break;
    case PTTDB_CONTENT_TYPE_COMMENT_REPLY:
        p_content_block = PTTUI_FILE_INFO.comments[buffer->comment_offset].comment_reply_blocks + buffer->block_offset;

        p_content_block->n_to_delete_line++;
        p_content_block->n_line--;
        break;
    default:
        break;
    }

    // buffer-info
    if (PTTUI_BUFFER_INFO.head == buffer) {
        PTTUI_BUFFER_INFO.head = pttui_buffer_next_ne(buffer, PTTUI_BUFFER_INFO.tail);
    }

    if (PTTUI_BUFFER_INFO.tail == buffer) {
        PTTUI_BUFFER_INFO.tail = pttui_buffer_pre_ne(buffer, PTTUI_BUFFER_INFO.head);
    }

    PTTUI_BUFFER_INFO.n_to_delete++;    

    VEDIT3_EDITOR_STATUS.is_redraw_everything = true;

    return S_OK;
}

Err
_vedit3_action_save()
{
    return S_OK;
}