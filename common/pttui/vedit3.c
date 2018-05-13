#include "cmpttui/vedit3.h"
#include "cmpttui/vedit3_private.h"

VEdit3EditorStatus DEFAULT_VEDIT3_EDITOR_STATUS = {
    true,                // is-insert
    false,               // is-ansi
    false,               // is-indent
    false,               // is-phone
    false,               // is-raw
    true,                // is-mbcs

    0,                   // current-line
    0,                   // current-col
    0,                   // edit_margin
    0,                   // last-margin

    false,               // is own lock buffer info
    false,               // is redraw everything

    false,               // is save
    false,               // is end

    NULL,                // current-buffer

    0,                   // mode0
    0                    // destuid0
};

VEdit3EditorStatus VEDIT3_EDITOR_STATUS = {};

char *_CONST_VEDIT3_LOADINGS[] = {
    ANSI_COLOR(0;37)".",
    ANSI_COLOR(0;37)".",
    ANSI_COLOR(0;37)"."
};
char *_VEDIT3_LOADING_DOT0 = NULL;
char *_VEDIT3_LOADING_DOT1 = NULL;
char *_VEDIT3_LOADING_DOT2 = NULL;

char VEDIT3_EMPTY_LINE[] = "\n";
int LEN_VEDIT3_EMPTY_LINE = 2;
char VEDIT3_END_LINE[] = "~\n";
int LEN_VEDIT3_END_LINE = 3;

int
vedit3_wrapper(const char *fpath, int saveheader, char title[TTLEN + 1], int flags, fileheader_t *fhdr, boardheader_t *bp)
{
    int money = 0;

    char poster[IDLEN + 1] = {};
    char board[IDLEN + 1] = {};
    char origin[MAX_ORIGIN_LEN] = {};
    char web_link[MAX_WEB_LINK_LEN] = {};
    char title_d[TTLEN + 1] = {};
    aidu_t aid = 0;
    time64_t create_milli_timestamp = 0;

    UUID main_id = {};

    Err error_code = migrate_get_file_info(fhdr, bp, poster, board, title_d, origin, &aid, web_link, &create_milli_timestamp);
    if (error_code) return EDIT_ABORTED;

    error_code = migrate_file_to_pttdb(fpath, poster, board, title, origin, aid, web_link, create_milli_timestamp, main_id);

    error_code = vedit3(main_id, title, flags, &money);
    if (error_code) return EDIT_ABORTED;

    error_code = migrate_pttdb_to_file(main_id, fpath);
    if (error_code) return EDIT_ABORTED;

    return money;
}

/**
 * @brief
 * @details REPL 環境.
 *          1. user 的 input.
 *          2. 根據 user 的 input. 丟到 expected state 裡.
 *
 * @param main_id [description]
 * @param title [description]
 * @param edflags [description]
 * @param money [description]
 */
Err
vedit3(UUID main_id, char *title, int edflags, int *money)
{
    STATINC(STAT_VEDIT);

    Err error_code = _vedit3_init_user();
    if (error_code) return error_code;

    // init
    error_code = _vedit3_init_editor(main_id);

    // edit
    if (!error_code) {
        error_code = pttui_thread_set_expected_state(PTTUI_THREAD_STATE_EDIT);
    }

    if (!error_code) {
        error_code = _vedit3_repl_init();
    }

    // repl
    if (!error_code) {
        error_code = _vedit3_repl(money);
    }

    // back to start
    Err error_code_set_expected_state = pttui_thread_set_expected_state(PTTUI_THREAD_STATE_START);
    if (error_code_set_expected_state) error_code = S_ERR_ABORT_BBS;

    Err error_code_sync = pttui_thread_wait_buffer_loop(PTTUI_THREAD_STATE_START, N_ITER_VEDIT3_WAIT_BUFFER_THREAD_LOOP);
    if (error_code_sync) error_code = S_ERR_ABORT_BBS;

    if(!error_code) {
        error_code = _vedit3_destroy_editor();
    }

    return error_code;
}

/**********
 * init editor
 **********/

Err
_vedit3_init_editor(UUID main_id)
{
    Err error_code = _vedit3_edit_msg();
    if (error_code) return error_code;

    error_code = _vedit3_init_dots();
    if (error_code) return error_code;

    error_code = _vedit3_loading();
    if (error_code) return error_code;

    error_code = _vedit3_init_file_info(main_id); // XXX may result in race-condition
    if (error_code) return error_code;

    error_code = pttui_set_expected_state(main_id, PTTDB_CONTENT_TYPE_MAIN, PTTUI_FILE_INFO.main_content_id, 0, 0, 0, b_lines);
    if (error_code) return error_code;

    error_code = pttui_thread_set_expected_state(PTTUI_THREAD_STATE_INIT_EDIT);
    if (error_code) return error_code;

    error_code = vedit3_wait_buffer_init();

    return error_code;
}

Err
_vedit3_init_user()
{

    VEDIT3_EDITOR_STATUS.mode0 = currutmp->mode;
    VEDIT3_EDITOR_STATUS.destuid0 = currutmp->destuid;

    currutmp->mode = EDITING;
    currutmp->destuid = currstat;

    return S_OK;
}

Err
_vedit3_init_file_info(UUID main_id)
{
    char dir_prefix[MAX_FILENAME_SIZE] = {};

    // XXX disp-buffer and disp-screen may need old file-info?    
    Err error_code = pttui_thread_lock_wrlock(LOCK_PTTUI_FILE_INFO);
    if(error_code) return error_code;

    init_pttdb_file();

    error_code = destroy_file_info(&PTTUI_FILE_INFO);
    if (error_code) return error_code;

    error_code = construct_file_info(main_id, &PTTUI_FILE_INFO);

    Err error_code_lock = pttui_thread_lock_unlock(LOCK_PTTUI_FILE_INFO);
    if(!error_code && error_code_lock) error_code = error_code_lock;

    return error_code;
}

Err
vedit3_wait_buffer_init()
{
    Err error_code = S_OK;
    int ret_sleep = 0;
    PttUIState current_state = {};

    struct timespec req = {0, NS_DEFAULT_SLEEP_VEDIT3_WAIT_BUFFER_INIT};
    struct timespec rem = {};

    int i = 0;
    for (i = 0; i < N_ITER_VEDIT3_WAIT_BUFFER_INIT; i++) {
        error_code = pttui_get_buffer_state(&current_state);
        if (error_code) break;

        if (!memcmp(&PTTUI_STATE, &current_state, sizeof(PttUIState))) break; // XXX maybe re-edit too quickly

        ret_sleep = nanosleep(&req, &rem);
        if (ret_sleep) {
            error_code = S_ERR_SLEEP;
            break;
        }
    }
    if (i == N_ITER_VEDIT3_WAIT_BUFFER_INIT) error_code = S_ERR_BUSY;

    return error_code;
}

/**********
 * VEdit3 repl
 **********/
Err
_vedit3_repl(int *money)
{
    Err error_code = S_OK;
    *money = 0;

    // repl init

    bool is_end = false;

    struct timespec req = {0, NS_DEFAULT_SLEEP_VEDIT3_REPL};
    struct timespec rem = {};

    int ret_sleep = 0;
    while (true) {
        // action
        // check healthy
        error_code = _vedit3_check_healthy();
        if (error_code) break;

        error_code = vedit3_action_to_store(&is_end);
        if (error_code) break;

        error_code = _vedit3_store_to_render();
        if (error_code) break;

        if (is_end) break;

        ret_sleep = nanosleep(&req, &rem);
        if (ret_sleep) {
            error_code = S_ERR_SLEEP;
            break;
        }
    }

    /*
    struct timespec req = {10, 0};
    struct timespec rem = {};

    int ret_sleep = nanosleep(&req, &rem);
    */

    return error_code;
}

Err
_vedit3_repl_init() {
    Err error_code = S_OK;

    // init editor-status
    memcpy(&VEDIT3_EDITOR_STATUS, &DEFAULT_VEDIT3_EDITOR_STATUS, sizeof(VEdit3EditorStatus));

    error_code = vedit3_repl_lock_buffer_info();
    VEDIT3_EDITOR_STATUS.current_buffer = PTTUI_BUFFER_TOP_LINE;

    // disp-screen
    error_code = _vedit3_disp_screen(0, b_lines - 1);

    if (!error_code) {
        move(VEDIT3_EDITOR_STATUS.current_line, VEDIT3_EDITOR_STATUS.current_col);
        refresh();
    }

    // free
    Err error_code_lock = vedit3_repl_unlock_buffer_info();
    if (error_code_lock) error_code = S_ERR_ABORT_BBS;

    return S_OK;
}

Err
_vedit3_check_healthy()
{
    return S_OK;
}

Err
_vedit3_store_to_render()
{
    // XXX check why redraw-everything makes different len in comments    
    Err error_code = S_OK;

    int ch = 0;
    if(VEDIT3_EDITOR_STATUS.current_buffer->content_type == PTTDB_CONTENT_TYPE_COMMENT) {
        ch = 0;
    }
    else if(VEDIT3_EDITOR_STATUS.is_ansi) {
        error_code = pttui_n2ansi(VEDIT3_EDITOR_STATUS.current_col, VEDIT3_EDITOR_STATUS.current_buffer->buf, &ch);
    }
    else {
        ch = VEDIT3_EDITOR_STATUS.current_col;
    }

    VEDIT3_EDITOR_STATUS.edit_margin = (ch < t_columns - 1) ? 0 : (ch / (t_columns - 8) * (t_columns - 8));
    if(VEDIT3_EDITOR_STATUS.edit_margin != VEDIT3_EDITOR_STATUS.last_margin) VEDIT3_EDITOR_STATUS.is_redraw_everything = true;
    VEDIT3_EDITOR_STATUS.last_margin = VEDIT3_EDITOR_STATUS.edit_margin;

    if (VEDIT3_EDITOR_STATUS.is_redraw_everything) {
        VEDIT3_EDITOR_STATUS.is_redraw_everything = false;
        error_code = _vedit3_disp_screen(0, b_lines - 1);
    }
    else {
        error_code = _vedit3_disp_line(VEDIT3_EDITOR_STATUS.current_line, VEDIT3_EDITOR_STATUS.current_buffer->buf, VEDIT3_EDITOR_STATUS.current_buffer->len_no_nl, VEDIT3_EDITOR_STATUS.current_buffer->content_type);
        outs(ANSI_RESET ANSI_CLRTOEND);
    }
    error_code = _vedit3_edit_msg();

    if(!VEDIT3_EDITOR_STATUS.is_ansi) ch -= VEDIT3_EDITOR_STATUS.edit_margin;

    move(VEDIT3_EDITOR_STATUS.current_line, ch);
    refresh();

    return error_code;
}

/*********
 * VEdit3 disp screen
 *********/

/**
 * @brief refresh screen from start-line, add VEDIT3_END_LINE if end_line is not the end of the screen.
 * @details refresh screen from start-tline.
 *          add VEDIT3_END_LINE if end_line is not the end of the screen
 *
 * @param start_line [description]
 * @param end_line the end-of-line to refresh(included)
 */
Err
_vedit3_disp_screen(int start_line, int end_line)
{
    Err error_code = vedit3_repl_lock_buffer_info();
    if (error_code) return error_code;

    // assuming VEDIT3_DISP_TOP_LINE_BUFFER is within expectation, with lock
    PttUIBuffer *p_buffer = PTTUI_BUFFER_TOP_LINE;

    // iterate to the start line
    int i = 0;
    for (i = 0; i < start_line && p_buffer; i++, p_buffer = pttui_buffer_next_ne(p_buffer, PTTUI_BUFFER_INFO.tail));
    if (i != start_line) error_code = S_ERR;

    // disp between start_line and end_line (end-line included)
    if (!error_code) {
        for (; i <= end_line && p_buffer; i++, p_buffer = pttui_buffer_next_ne(p_buffer, PTTUI_BUFFER_INFO.tail)) {
            if (!p_buffer->buf) {
                error_code = _vedit3_disp_line(i, VEDIT3_EMPTY_LINE, LEN_VEDIT3_EMPTY_LINE, p_buffer->content_type);
                if (error_code) break;

                continue;
            }
            error_code = _vedit3_disp_line(i, p_buffer->buf, p_buffer->len_no_nl, p_buffer->content_type);
            if (error_code) break;
        }
    }

    // disp
    if (!error_code) {
        for (; i <= end_line; i++) {
            error_code = _vedit3_disp_line(i, VEDIT3_END_LINE, LEN_VEDIT3_END_LINE, PTTDB_CONTENT_TYPE_OTHER);
            if (error_code) break;
        }
    }

    outs(ANSI_RESET ANSI_CLRTOEND);

    if (!error_code) {
        refresh();
    }

    // free
    Err error_code_lock = vedit3_repl_unlock_buffer_info();
    if (error_code_lock) error_code = S_ERR_ABORT_BBS;

    return error_code;
}

/**
 * @brief
 * @details ref: the end of vedit2 in edit.c
 *
 * @param line [description]
 * @param buf [description]
 * @param len [description]
 */
Err
_vedit3_disp_line(int line, char *buf, int len, enum PttDBContentType content_type)
{
    Err error_code = S_OK;

    move(line, 0);
    clrtoeol();
    move(line, 0);
    int attr = (int)PTTUI_ATTR_NORMAL;
    int detected_attr = 0;

    if(VEDIT3_EDITOR_STATUS.is_ansi) {
        outs(buf);
    }
    else if(VEDIT3_EDITOR_STATUS.edit_margin >= len) {
        outs("");
    }
    else if(content_type == PTTDB_CONTENT_TYPE_COMMENT) {
        // XXX hack for comment, because comment is supposed to fit in the 1st column-page.
        if(VEDIT3_EDITOR_STATUS.edit_margin) {
            outs("");
        }
        else {
            outs(buf);
        }
    }
    else {
        error_code = pttui_detect_attr(buf, len, &detected_attr);
        attr |= detected_attr;
        pttui_edit_outs_attr_n(buf + VEDIT3_EDITOR_STATUS.edit_margin, strlen(buf + VEDIT3_EDITOR_STATUS.edit_margin), attr);
    }

    return S_OK;
}


/**
 * @brief
 * @details ref: edit_ansi_outs_n in edit.c
 *
 * @param str [description]
 * @param n [description]
 * @param attr [description]
 */
Err
_vedit3_edit_ansi_outs_n(const char *str, int n, int attr GCC_UNUSED)
{
    char c;
    while (n-- > 0 && (c = *str++)) {
        if (c == ESC_CHR && *str == '*')
        {
            // ptt prints
            /* Because moving within ptt_prints is too hard
             * let's just display it as-is.
             */
            outc('*');
        } else {
            outc(c);
        }
    }

    return S_OK;
}

/*********
 * VEdit3 Buffer
 *********/

Err
vedit3_init_buffer()
{
    Err error_code = S_OK;

    PttUIState expected_state = {};
    error_code = pttui_get_expected_state(&expected_state);
    if (error_code) return error_code;

    if (!memcmp(expected_state.main_id, PTTUI_FILE_INFO.main_id, UUIDLEN) &&
        !memcmp(expected_state.main_id, PTTUI_BUFFER_STATE.main_id, UUIDLEN)) return S_OK;

    error_code = resync_all_pttui_buffer_info(&PTTUI_BUFFER_INFO, &expected_state, &PTTUI_FILE_INFO, &PTTUI_BUFFER_TOP_LINE);
    if (error_code) return error_code;

    error_code = pttui_set_buffer_state(&expected_state);

    return error_code;
}

Err
vedit3_buffer()
{
    Err error_code = S_OK;

    if(PTTUI_BUFFER_INFO.is_saved) return S_OK;

    if(PTTUI_BUFFER_INFO.is_to_save) return save_pttui_buffer_info_to_db(&PTTUI_BUFFER_INFO, &PTTUI_FILE_INFO, (char *)cuser.userid, fromhost);

    PttUIState expected_state = {};

    error_code = pttui_get_expected_state(&expected_state);
    if (error_code) return error_code;

    if (!memcmp(&expected_state, &PTTUI_BUFFER_STATE, sizeof(PttUIState))) {
        error_code = check_and_save_pttui_buffer_info_to_tmp_file(&PTTUI_BUFFER_INFO, &PTTUI_FILE_INFO);
        return S_OK;
    }

    // sync disp buffer to tmp_disp_buffer while checking the alignment of orig_expected_state and new_expected_state
    PttUIBuffer *new_top_line_buffer = NULL;
    error_code = sync_pttui_buffer_info(&PTTUI_BUFFER_INFO, PTTUI_BUFFER_TOP_LINE, &expected_state, &PTTUI_FILE_INFO, &new_top_line_buffer);
    if(error_code) return error_code;

    // XXX may result in race-condition. need to bind with buffer-info.
    PTTUI_BUFFER_TOP_LINE = new_top_line_buffer;

    error_code = pttui_set_buffer_state(&expected_state);
    if(error_code) return error_code;

    error_code = extend_pttui_buffer_info(&PTTUI_FILE_INFO, &PTTUI_BUFFER_INFO, PTTUI_BUFFER_TOP_LINE);
    if(error_code) return error_code;

    return S_OK;
}

/*********
 * VEdit3 Misc
 *********/

Err
vedit3_wait_buffer_state_sync(int n_iter) {
    Err error_code = S_OK;

    PttUIState current_state = {};
    struct timespec req = {0, NS_DEFAULT_SLEEP_VEDIT3_WAIT_BUFFER_SYNC};
    struct timespec rem = {};

    int ret_sleep = 0;
    Err error_code_get_current_state = S_OK;
    for(int i = 0; i < n_iter; i++){
        error_code_get_current_state = pttui_get_buffer_state(&current_state);
        if(error_code_get_current_state) continue;

        if(!memcmp(&current_state, &PTTUI_STATE, sizeof(PttUIState))) break;
        ret_sleep = nanosleep(&req, &rem);
        if(ret_sleep) {
            error_code = S_ERR_SLEEP;
            break;
        }
    }

    return error_code;
}

/**
 * @brief
 * @details ref: edit_msg in edit.c
 */
Err
_vedit3_edit_msg()
{
    char buf[MAX_TEXTLINE_SIZE] = {};
    int n = VEDIT3_EDITOR_STATUS.current_col;

    /*
    if (VEDIT3_EDITOR_STATUS.is_ansi)
    //n = n2ansi(n, curr_buf->currline);

    if (VEDIT3_EDITOR_STATUS.is_phone) show_phone_mode_panel();
    */

    snprintf(buf, sizeof(buf),
             FOOTER_VEDIT3_INFIX
             FOOTER_VEDIT3_POSTFIX,
             VEDIT3_EDITOR_STATUS.is_insert ? VEDIT3_PROMPT_INSERT : VEDIT3_PROMPT_REPLACE,
             VEDIT3_EDITOR_STATUS.is_ansi ? 'A' : 'a',
             VEDIT3_EDITOR_STATUS.is_indent ? 'I' : 'i',
             VEDIT3_EDITOR_STATUS.is_phone ? 'P' : 'p',
             VEDIT3_EDITOR_STATUS.is_raw ? 'R' : 'r',
             VEDIT3_EDITOR_STATUS.current_buffer_line + 1,
             n + 1);

    vs_footer(FOOTER_VEDIT3_PREFIX, buf);

    return S_OK;
}

Err
_vedit3_init_dots()
{
    _VEDIT3_LOADING_DOT0 = _CONST_VEDIT3_LOADINGS[0];
    _VEDIT3_LOADING_DOT1 = _CONST_VEDIT3_LOADINGS[1];
    _VEDIT3_LOADING_DOT2 = _CONST_VEDIT3_LOADINGS[2];

    return S_OK;
}

Err
_vedit3_loading()
{
    char buf[MAX_TEXTLINE_SIZE] = {};
    sprintf(buf, LOADING "%s%s%s%s\n", _VEDIT3_LOADING_DOT0, _VEDIT3_LOADING_DOT1, _VEDIT3_LOADING_DOT2, ANSI_RESET);
    mvouts(b_lines - 1, 0, buf);
    refresh();
    _vedit3_loading_rotate_dots();

    return S_OK;
}

Err
_vedit3_loading_rotate_dots()
{
    char *tmp = _VEDIT3_LOADING_DOT0;
    _VEDIT3_LOADING_DOT0 = _VEDIT3_LOADING_DOT2;
    _VEDIT3_LOADING_DOT2 = _VEDIT3_LOADING_DOT1;
    _VEDIT3_LOADING_DOT1 = tmp;

    return S_OK;
}

/**********
 * lock
 **********/

Err
vedit3_repl_wrlock_file_info_buffer_info(bool *is_lock_file_info, bool *is_lock_wr_buffer_info, bool *is_lock_buffer_info)
{
    *is_lock_file_info = false;
    *is_lock_wr_buffer_info = false;
    *is_lock_buffer_info = false;

    Err error_code = vedit3_repl_wrlock_file_info();
    if(error_code) return error_code;

    *is_lock_file_info = true;

    error_code = vedit3_repl_lock_wr_buffer_info();
    if(error_code) return error_code;

    *is_lock_wr_buffer_info = true;

    error_code = vedit3_repl_wrlock_buffer_info();
    if(error_code) return error_code;

    *is_lock_buffer_info = true;

    return S_OK;
}

Err
vedit3_repl_wrunlock_file_info_buffer_info(bool is_lock_file_info, bool is_lock_wr_buffer_info, bool is_lock_buffer_info)
{
    Err error_code = S_OK;

    Err error_code_lock = S_OK;
    if(is_lock_buffer_info) {
        error_code_lock = vedit3_repl_wrunlock_buffer_info();
        if(error_code_lock) error_code = error_code_lock;
    }

    if(is_lock_wr_buffer_info) {
        error_code_lock = vedit3_repl_unlock_wr_buffer_info();
        if(!error_code && error_code_lock) error_code = error_code_lock;
    }

    if(is_lock_file_info) {
        error_code_lock = vedit3_repl_wrunlock_file_info();
        if(!error_code && error_code_lock) error_code = error_code_lock;
    }

    return error_code;
}

Err
vedit3_repl_lock_wr_buffer_info()
{
    return pttui_thread_lock_wrlock(LOCK_PTTUI_WR_BUFFER_INFO);
}

Err
vedit3_repl_unlock_wr_buffer_info()
{
    return pttui_thread_lock_unlock(LOCK_PTTUI_WR_BUFFER_INFO);
}

/**
 * @brief [brief description]
 * @details XXX require: 1. wrlock_file_info 2. lock_wr_buffer_info
 * 
 * @param error_code [description]
 */
Err
vedit3_repl_wrlock_buffer_info()
{
    Err error_code = pttui_thread_lock_wrlock(LOCK_PTTUI_BUFFER_INFO);
    if (error_code) return error_code;

    VEDIT3_EDITOR_STATUS.is_own_wrlock_buffer_info = true;

    return S_OK;
}

Err
vedit3_repl_wrunlock_buffer_info()
{
    VEDIT3_EDITOR_STATUS.is_own_wrlock_buffer_info = false;

    return pttui_thread_lock_unlock(LOCK_PTTUI_BUFFER_INFO);
}

Err
vedit3_repl_lock_buffer_info()
{
    Err error_code = pttui_thread_lock_rdlock(LOCK_PTTUI_BUFFER_INFO);
    if (error_code) return error_code;

    VEDIT3_EDITOR_STATUS.is_own_lock_buffer_info = true;

    return S_OK;
}

Err
vedit3_repl_unlock_buffer_info()
{
    pthread_rwlock_t *p_lock = NULL;

    Err error_code = pttui_thread_lock_get_lock(LOCK_PTTUI_BUFFER_INFO, &p_lock);

    if (!error_code && p_lock->__data.__nr_readers == 1) {
        VEDIT3_EDITOR_STATUS.is_own_lock_buffer_info = false;
    }

    error_code = pttui_thread_lock_unlock(LOCK_PTTUI_BUFFER_INFO);

    return error_code;
}

Err
vedit3_repl_wrlock_file_info()
{
    Err error_code = pttui_thread_lock_wrlock(LOCK_PTTUI_FILE_INFO);
    if (error_code) return error_code;

    return S_OK;
}

Err
vedit3_repl_wrunlock_file_info()
{
    Err error_code = pttui_thread_lock_unlock(LOCK_PTTUI_FILE_INFO);

    return error_code;
}

/**********
 * destroy editor
 **********/
Err
_vedit3_destroy_editor()
{
    bool is_lock_file_info = false;
    bool is_lock_wr_buffer_info = false;
    bool is_lock_buffer_info = false;

    Err error_code = vedit3_repl_wrlock_file_info_buffer_info(&is_lock_file_info, &is_lock_wr_buffer_info, &is_lock_buffer_info);
    if(error_code) return error_code;

    destroy_pttdb_file();

    PTTUI_BUFFER_TOP_LINE = NULL;
    error_code = destroy_file_info(&PTTUI_FILE_INFO);

    error_code = destroy_pttui_buffer_info(&PTTUI_BUFFER_INFO);

    bzero(&PTTUI_STATE, sizeof(PttUIState));
    bzero(&PTTUI_BUFFER_STATE, sizeof(PttUIState));

    memcpy(&VEDIT3_EDITOR_STATUS, &DEFAULT_VEDIT3_EDITOR_STATUS, sizeof(VEdit3EditorStatus));

    error_code = vedit3_repl_wrunlock_file_info_buffer_info(is_lock_file_info, is_lock_wr_buffer_info, is_lock_buffer_info);

    return error_code;
}
