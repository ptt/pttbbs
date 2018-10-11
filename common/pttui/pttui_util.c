#include "cmpttui/pttui_util.h"

/**********
 * pttui-state
 **********/
Err
pttui_set_expected_state(UUID main_id, enum PttDBContentType top_line_content_type, UUID top_line_id, int top_line_block_offset, int top_line_line_offset, int top_line_comment_offset, int n_window_line)
{

    Err error_code = pttui_thread_lock_wrlock(LOCK_PTTUI_EXPECTED_STATE);
    if (error_code) return error_code;

    memcpy(PTTUI_STATE.main_id, main_id, UUIDLEN);
    PTTUI_STATE.top_line_content_type = top_line_content_type;
    memcpy(PTTUI_STATE.top_line_id, top_line_id, UUIDLEN);
    PTTUI_STATE.top_line_block_offset = top_line_block_offset;
    PTTUI_STATE.top_line_line_offset = top_line_line_offset;
    PTTUI_STATE.top_line_comment_offset = top_line_comment_offset;
    PTTUI_STATE.n_window_line = n_window_line;

    pttui_thread_lock_unlock(LOCK_PTTUI_EXPECTED_STATE);

    return S_OK;
}

Err
pttui_get_expected_state(PttUIState *expected_state)
{
    Err error_code = pttui_thread_lock_rdlock(LOCK_PTTUI_EXPECTED_STATE);
    if (error_code) return error_code;

    memcpy(expected_state, &PTTUI_STATE, sizeof(PttUIState));

    pttui_thread_lock_unlock(LOCK_PTTUI_EXPECTED_STATE);

    return error_code;
}


Err
pttui_set_buffer_state(PttUIState *state)
{
    Err error_code = pttui_thread_lock_wrlock(LOCK_PTTUI_BUFFER_STATE);
    if (error_code) return error_code;

    memcpy(&PTTUI_BUFFER_STATE, state, sizeof(PttUIState));

    // free
    error_code = pttui_thread_lock_unlock(LOCK_PTTUI_BUFFER_STATE);
    if (error_code) return error_code;

    return S_OK;
}

Err
pttui_get_buffer_state(PttUIState *state)
{
    Err error_code = pttui_thread_lock_rdlock(LOCK_PTTUI_BUFFER_STATE);
    if (error_code) return error_code;

    memcpy(state, &PTTUI_BUFFER_STATE, sizeof(PttUIState));

    // free
    error_code = pttui_thread_lock_unlock(LOCK_PTTUI_BUFFER_STATE);
    if (error_code) return error_code;

    return S_OK;
}

/**********
 * char
 **********/
Err
pttui_next_non_space_char(char *buf, int len, char **p_buf)
{
    char *tmp_buf = buf;
    int i = 0;
    for(i = 0; i < len && *tmp_buf == ' '; i++, tmp_buf++);
    if(i == len) {
        *p_buf = NULL;
        return S_OK;
    }

    *p_buf = tmp_buf;

    return S_OK;
}

Err
pttui_first_word(char *buf, int len, int *len_word)
{
    char *tmp_buf = buf;
    int i = 0;
    for(i = 0; i < len && *tmp_buf != ' '; i++, tmp_buf++);

    *len_word = i;

    return S_OK;
}

Err
pttui_raw_shift_right(char *s, int len)
{
    int i;

    for (i = len - 1; i >= 0; --i) s[i + 1] = s[i];

    return S_OK;
}

Err
pttui_raw_shift_left(char *s, int len)
{
    int i;
    for (i = 0; i < len && s[i] != 0; ++i) s[i] = s[i + 1];

    return S_OK;
}

/**
 * @brief [brief description]
 * @details ref: ansi2n in edit.c
 *
 * @param ansix [description]
 * @param buf [description]
 * @param nx [description]
 */
Err
pttui_ansi2n(int ansix, char *buf, int *nx)
{
    char *tmp = buf;
    char ch;

    while (*tmp) {
        if (*tmp == KEY_ESC) {
            // XXX tmp may be out of range
            while ((ch = *tmp) && !isalpha((int)ch)) tmp++;

            if (ch) tmp++;

            continue;
        }

        if (ansix <= 0) break;

        tmp++;
        ansix--;
    }

    *nx = tmp - buf;

    return S_OK;
}

/**
 * @brief [brief description]
 * @details n2ansi in edit.c
 *
 * @param nx [description]
 * @param buf [description]
 * @param ansix [description]
 */
Err
pttui_n2ansi(int nx, char *buf, int *ansix)
{
    char *tmp = buf;
    char *nxp = buf + nx;
    char ch;

    int tmp_ansix = 0;

    while (*tmp) {
        if (*tmp == KEY_ESC) {
            while ((ch = *tmp) && !isalpha((int)ch)) tmp++;

            if (ch) tmp++;

            continue;
        }

        if (tmp >= nxp) break;

        tmp++;
        tmp_ansix++;
    }

    *ansix = tmp_ansix;

    return S_OK;
}

/**
 * @brief [brief description]
 * @details ref: mchar_len in edit.c
 *          return int directly because of aligning with strlen
 * 
 * @param char [description]
 * @return [description]
 */
int
pttui_mchar_len_ne(unsigned char *str)
{
  return ((str[0] != '\0' && str[1] != '\0' && IS_BIG5(str[0], str[1])) ?
            2 :
            1);
}

/**
 * @brief [brief description]
 * @details [long description]
 * 
 * @param str [description]
 * @param pos [description]
 * @param dir [description]
 * @param new_pos [description]
 */
Err
pttui_fix_cursor(char *str, int pos, enum PttUIFixCursorDir dir, int *new_pos)
{
    int tmp_newpos = 0, w = 0;
    while (*str != '\0' && tmp_newpos < pos) {
        w = pttui_mchar_len_ne((unsigned char *) str);
        str += w;
        tmp_newpos += w;
    }
    if (dir == PTTUI_FIX_CURSOR_DIR_LEFT && tmp_newpos > pos) tmp_newpos -= w;

    *new_pos = tmp_newpos;

    return S_OK;
}

/**
 * @brief [brief description]
 * @details ref: phone_mode_switch in edit.c
 *
 * @param is_phone [description]
 * @param phone_mode [description]
 */
inline Err
pttui_phone_mode_switch(bool *is_phone, int *phone_mode)
{
    *is_phone ^= 1;
    if (*is_phone) {
        if (!*phone_mode) *phone_mode = 2;
    }

    return S_OK;
}

/**
 * @brief [brief description]
 * @details ref: phone_char in edit.c
 *
 * @param c [description]
 * @param phone_mode [description]
 * @param ret [description]
 */
Err
pttui_phone_char(char c, int phone_mode, char **ret)
{
    char *tmp_ret = NULL;
    if (phone_mode > 0 && phone_mode < 20) {
        if (tolower(c) < 'a' ||
                (tolower(c) - 'a') >= (int)strlen(BIG5[phone_mode - 1]) / 2) {
            tmp_ret = NULL;
        }
        else {
            tmp_ret = (char *)BIG5[phone_mode - 1] + (tolower(c) - 'a') * 2;
        }
    }
    else if (phone_mode >= 20) {
        if (c == '.') c = '/';

        if (c < '/' || c > '9') {
            tmp_ret = NULL;
        }
        else {
            tmp_ret = (char *)table[phone_mode - 20] + (c - '/') * 2;
        }
    }
    else {
        tmp_ret = NULL;
    }

    *ret = tmp_ret;

    return S_OK;
}

inline Err
pttui_phone_mode_filter(char ch, bool is_phone, int *phone_mode, char *ret, bool *is_redraw_everything)
{
    if (!is_phone) {
        *ret = 0;
        return S_OK;
    }

    int orig_phone_mode = *phone_mode;

    switch (ch) {
    case 'z':
    case 'Z':
        if (orig_phone_mode < 20) {
            *phone_mode = 20;
        }
        else {
            *phone_mode = 2;
        }

        *ret = ch;
        return S_OK;
        break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        if (orig_phone_mode < 20) {

            *phone_mode = ch - '0' + 1;
            *is_redraw_everything = true;
            *ret = ch;
            return S_OK;
        }
        break;
    case '-':
        if (orig_phone_mode < 20) {
            *phone_mode = 11;

            *is_redraw_everything = true;
            *ret = ch;
            return S_OK;
        }
        break;
    case '=':
        if (orig_phone_mode < 20) {
            *phone_mode = 12;

            *is_redraw_everything = true;
            *ret = ch;
            return S_OK;
        }
        break;
    case '`':
        if (orig_phone_mode < 20) {
            *phone_mode = 13;

            *is_redraw_everything = true;
            *ret = ch;
            return S_OK;
        }
        break;
    case '/':
        if (orig_phone_mode >= 20) {
            (*phone_mode) += 4;
            if (*phone_mode > 27) *phone_mode -= 8;

            *is_redraw_everything = true;
            *ret = ch;
            return S_OK;
        }
        break;
    case '*':
        if (orig_phone_mode >= 20) {
            (*phone_mode)++;
            if ((*phone_mode - 21) % 4 == 3) *phone_mode -= 4;

            *is_redraw_everything = true;
            *ret = ch;
            return S_OK;
        }
        break;
    default:
        break;
    }

    *ret = 0;
    return S_OK;
}


/**
 * @brief
 * @details ref: detect_attr in edit.c
 *
 * @param ps [description]
 * @param len [description]
 * @param p_attr [description]
 */
Err
pttui_detect_attr(const char *ps, size_t len, int *p_attr)
{
    int attr = 0;
#ifdef PMORE_USE_ASCII_MOVIE
    if (mf_movieFrameHeader((unsigned char*)ps, (unsigned char*)ps + len)) attr |= PTTUI_ATTR_MOVIECODE;
#endif

#ifdef USE_BBSLUA
    if (bbslua_isHeader(ps, ps + len))
    {
        attr |= PTTUI_ATTR_BBSLUA;
        if (!curr_buf->synparser)
        {
            curr_buf->synparser = 1;
            // if you need indent, toggle by hotkey.
            // enabling indent by default may cause trouble to copy pasters
            // curr_buf->indent_mode = 1;
        }
    }
#endif
    return attr;
}

int pttui_syn_lua_keyword_ne(const char *text, int n, char *wlen)
{
    int i = 0;
    const char * const *tbl = NULL;
    if (*text >= 'A' && *text <= 'Z')
    {
        // normal identifier
        while (n-- > 0 && (isalnum(*text) || *text == '_'))
        {
            text++;
            (*wlen) ++;
        }
        return 0;
    }
    if (*text >= '0' && *text <= '9')
    {
        // digits
        while (n-- > 0 && (isdigit(*text) || *text == '.' || *text == 'x'))
        {
            text++;
            (*wlen) ++;
        }
        return 5;
    }
    if (*text == '#')
    {
        text++;
        (*wlen) ++;
        // length of identifier
        while (n-- > 0 && (isalnum(*text) || *text == '_'))
        {
            text++;
            (*wlen) ++;
        }
        return -2;
    }

    // ignore non-identifiers
    if (!(*text >= 'a' && *text <= 'z'))
        return 0;

    // 1st, try keywords
    for (i = 0; luaKeywords[i] && *text >= *luaKeywords[i]; i++)
    {
        int l = strlen(luaKeywords[i]);
        if (n < l)
            continue;
        if (isalnum(text[l]))
            continue;
        if (strncmp(text, luaKeywords[i], l) == 0)
        {
            *wlen = l;
            return 3;
        }
    }
    for (i = 0; luaDataKeywords[i] && *text >= *luaDataKeywords[i]; i++)
    {
        int l = strlen(luaDataKeywords[i]);
        if (n < l)
            continue;
        if (isalnum(text[l]))
            continue;
        if (strncmp(text, luaDataKeywords[i], l) == 0)
        {
            *wlen = l;
            return 2;
        }
    }
    for (i = 0; luaFunctions[i] && *text >= *luaFunctions[i]; i++)
    {
        int l = strlen(luaFunctions[i]);
        if (n < l)
            continue;
        if (isalnum(text[l]))
            continue;
        if (strncmp(text, luaFunctions[i], l) == 0)
        {
            *wlen = l;
            return 6;
        }
    }
    for (i = 0; luaLibs[i]; i++)
    {
        int l = strlen(luaLibs[i]);
        if (n < l)
            continue;
        if (text[l] != '.' && text[l] != ':')
            continue;
        if (strncmp(text, luaLibs[i], l) == 0)
        {
            *wlen = l + 1;
            text += l; text ++;
            n -= l; n--;
            break;
        }
    }

    tbl = luaLibAPI[i];
    if (!tbl)
    {
        // calcualte wlen
        while (n-- > 0 && (isalnum(*text) || *text == '_'))
        {
            text++;
            (*wlen) ++;
        }
        return 0;
    }

    for (i = 0; tbl[i]; i++)
    {
        int l = strlen(tbl[i]);
        if (n < l)
            continue;
        if (isalnum(text[l]))
            continue;
        if (strncmp(text, tbl[i], l) == 0)
        {
            *wlen += l;
            return 6;
        }
    }
    // luaLib. only
    return -6;
}


/**
 * @brief [brief description]
 * @details ref: edit_outs_attr_n in mbbsd/edit.c
 *
 * @param text [description]
 * @param n [description]
 * @param attr [description]
 */
Err
pttui_edit_outs_attr_n(const char *text, int n, int attr)
{
    int    column = 0;
    unsigned char inAnsi = 0;
    unsigned char ch;
    int doReset = 0;
    const char *reset = ANSI_RESET;

    // syntax attributes
    char fComment = 0,
         fSingleQuote = 0,
         fDoubleQuote = 0,
         fSquareQuote = 0,
         fWord = 0;

    // movie syntax rendering
#ifdef ENABLE_PMORE_ASCII_MOVIE_SYNTAX
    char movie_attrs[WRAPMARGIN + 10] = {0};
    char *pmattr = movie_attrs, mattr = 0;
#endif

#ifdef COLORED_SELECTION
    if ((attr & PTTUI_ATTR_SELECTED) &&
            (attr & ~PTTUI_ATTR_SELECTED))
    {
        reset = ANSI_COLOR(0;7;36);
        doReset = 1;
        outs(reset);
    }
    else
#endif // if not defined, color by  priority - selection first
        if (attr & PTTUI_ATTR_SELECTED)
        {
            reset = ANSI_COLOR(0;7);
            doReset = 1;
            outs(reset);
        }
        else if (attr & PTTUI_ATTR_MOVIECODE)
        {
            reset = ANSI_COLOR(0;36);
            doReset = 1;
            outs(reset);
#ifdef ENABLE_PMORE_ASCII_MOVIE_SYNTAX
            syn_pmore_render((char*)text, n, movie_attrs);
#endif
        }
        else if (attr & PTTUI_ATTR_BBSLUA)
        {
            reset = ANSI_COLOR(0;1;31);
            doReset = 1;
            outs(reset);
        }
        else if (attr & PTTUI_ATTR_COMMENT)
        {
            reset = ANSI_COLOR(0;1;34);
            doReset = 1;
            outs(reset);
        }

#ifdef DBCSAWARE
    /* 0 = N/A, 1 = leading byte printed, 2 = ansi in middle */
    unsigned char isDBCS = 0;
#endif

    while ((ch = *text++) && (++column < t_columns) && n-- > 0)
    {
#ifdef ENABLE_PMORE_ASCII_MOVIE_SYNTAX
        mattr = *pmattr++;
#endif

        if (inAnsi == 1)
        {
            if (ch == ESC_CHR) {
                outc('*');
            }
            else
            {
                outc(ch);

                if (!ANSI_IN_ESCAPE(ch))
                {
                    inAnsi = 0;
                    outs(reset);
                }
            }

        }
        else if (ch == ESC_CHR)
        {
            inAnsi = 1;
#ifdef DBCSAWARE
            if (isDBCS == 1)
            {
                isDBCS = 2;
                outs(ANSI_COLOR(1;33) "?");
                outs(reset);
            }
#endif
            outs(ANSI_COLOR(1) "*");
        }
        else
        {
#ifdef DBCSAWARE
            if (isDBCS == 1) {
                isDBCS = 0;
            }
            else if (isDBCS == 2)
            {
                /* ansi in middle. */
                outs(ANSI_COLOR(0;33) "?");
                outs(reset);
                isDBCS = 0;
                continue;
            }
            else if (IS_BIG5_HI(ch))
            {
                isDBCS = 1;
                // peak next char
                if (n > 0 && *text == ESC_CHR) {
                    continue;
                }
            }
#endif

            // XXX Lua Parser!
            //if (!attr && curr_buf->synparser && !fComment)
            if (!attr && false && !fComment)
            {
                // syntax highlight!
                if (fSquareQuote) {
                    if (ch == ']' && n > 0 && *(text) == ']')
                    {
                        fSquareQuote = 0;
                        doReset = 0;
                        // directly print quotes
                        outc(ch); outc(ch);
                        text++, n--;
                        outs(ANSI_RESET);
                        continue;
                    }
                } else if (fSingleQuote) {
                    if (ch == '\'')
                    {
                        fSingleQuote = 0;
                        doReset = 0;
                        // directly print quotes
                        outc(ch);
                        outs(ANSI_RESET);
                        continue;
                    }
                } else if (fDoubleQuote) {
                    if (ch == '"')
                    {
                        fDoubleQuote = 0;
                        doReset = 0;
                        // directly print quotes
                        outc(ch);
                        outs(ANSI_RESET);
                        continue;
                    }
                } else if (ch == '-' && n > 0 && *(text) == '-') {
                    fComment = 1;
                    doReset = 1;
                    outs(ANSI_COLOR(0;1;34));
                } else if (ch == '[' && n > 0 && *(text) == '[') {
                    fSquareQuote = 1;
                    doReset = 1;
                    fWord = 0;
                    outs(ANSI_COLOR(1;35));
                } else if (ch == '\'' || ch == '"') {
                    if (ch == '"') {
                        fDoubleQuote = 1;
                    }
                    else {
                        fSingleQuote = 1;
                    }
                    doReset = 1;
                    fWord = 0;
                    outs(ANSI_COLOR(1;35));
                } else {
                    // normal words
                    if (fWord)
                    {
                        // inside a word.
                        if (--fWord <= 0) {
                            fWord = 0;
                            doReset = 0;
                            outc(ch);
                            outs(ANSI_RESET);
                            continue;
                        }
                    } else if (isalnum(tolower(ch)) || ch == '#') {
                        char attr[] = ANSI_COLOR(0;1;37);
                        int x = pttui_syn_lua_keyword_ne(text - 1, n + 1, &fWord);
                        if (fWord > 0) fWord --;
                        if (x != 0)
                        {
                            // sorry, fixed string here.
                            // 7 = *[0;1;3?
                            if (x < 0) {  attr[4] = '0'; x = -x; }
                            attr[7] = '0' + x;
                            outs(attr);
                            doReset = 1;
                        }
                        if (!fWord)
                        {
                            outc(ch);
                            outs(ANSI_RESET);
                            doReset = 0;
                            continue;
                        }
                    }
                }
            }
            outc(ch);

#ifdef ENABLE_PMORE_ASCII_MOVIE_SYNTAX
            // pmore Movie Parser!
            if (attr & VEDIT3_ATTR_MOVIECODE)
            {
                // only render when attribute was changed.
                if (mattr != *pmattr)
                {
                    if (*pmattr)
                    {
                        prints(ANSI_COLOR(1;3%d),
                               8 - ((mattr - 1) % 7 + 1) );
                    } else {
                        outs(ANSI_RESET);
                    }
                }
            }
#endif // ENABLE_PMORE_ASCII_MOVIE_SYNTAX
        }
    }

    // this must be ANSI_RESET, not "reset".
    if (inAnsi || doReset) outs(ANSI_RESET);

    return S_OK;
}

int
pttui_ctrl_key_ne(int ch)
{
    switch(KEY_ESC_arg) {
    case ',':
        ch = Ctrl(']');
        break;
    case '.':
        ch = Ctrl('T');
        break;
    case 'v':
        ch = KEY_PGUP;
        break;
    case 'a':
    case 'A':
        ch = Ctrl('V');
        break;
    case 'X':
        ch = Ctrl('X');
        break;
    case 'q':
        ch = Ctrl('Q');
        break;
    case 'o':
        ch = Ctrl('O');
        break;
    case 's':
        ch = Ctrl('S');
        break;
    case 'S':
        // XXX remove lua-parser for now
        //curr_buf->synparser = !curr_buf->synparser;
        break;
    default:
        break;
    }

    return ch;

}

Err
pttui_visible_window_height(bool is_phone)
{
    return is_phone ? b_lines - 1 : b_lines;
}
