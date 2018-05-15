/* $Id$ */
#ifndef PTTUI_UTIL_H
#define PTTUI_UTIL_H

#include "ptterr.h"
#include "cmpttdb.h"
#include "cmpttui/pttui_const.h"
#include "cmpttui/pttui_var.h"
#include "cmpttui/pttui_thread_lock.h"
#include "cmpttui/pttui_lua_bbs.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <dirent.h>    
#include <string.h>
#include <fcntl.h>    
#include <unistd.h>    
#include "common.h"
#include "ansi.h"
#include "vtuikit.h"
#include "vtkbd.h"
#include "grayout.h"
    
#include "var.h"

// XXX hack for DBCSAWARE in edit
#ifndef IS_BIG5_HI
#define IS_BIG5_HI(x) (0x81 <= (x) && (x) <= 0xfe)
#endif    
#ifndef IS_BIG5_LOS
#define IS_BIG5_LOS(x) (0x40 <= (x) && (x) <= 0x7e)
#endif    
#ifndef IS_BIG5_LOE
#define IS_BIG5_LOE(x) (0x80 <= (x) && (x) <= 0xfe)
#endif    
#ifndef IS_BIG5_LO
#define IS_BIG5_LO(x) (IS_BIG5_LOS(x) || IS_BIG5_LOE(x))
#endif
#ifndef IS_BIG5    
#define IS_BIG5(hi,lo) (IS_BIG5_HI(hi) && IS_BIG5_LO(lo))
#endif

enum PttUIAttr {
    PTTUI_ATTR_NORMAL   = 0x00,
    PTTUI_ATTR_SELECTED = 0x01, // selected (reverse)
    PTTUI_ATTR_MOVIECODE= 0x02, // pmore movie
    PTTUI_ATTR_BBSLUA   = 0x04, // BBS Lua (header)
    PTTUI_ATTR_COMMENT  = 0x08, // comment syntax
};

enum PttUIFixCursorDir {
    PTTUI_FIX_CURSOR_DIR_RIGHT,
    PTTUI_FIX_CURSOR_DIR_LEFT,
};

Err pttui_set_expected_state(UUID main_id, enum PttDBContentType top_line_content_type, UUID top_line_id, int top_line_block_offset, int top_line_line_offset, int top_line_comment_offset, int n_window_line);
Err pttui_get_expected_state(PttUIState *expected_state);
Err pttui_set_buffer_state(PttUIState *state);
Err pttui_get_buffer_state(PttUIState *state);

inline Err pttui_phone_mode_switch(bool *is_phone, int *phone_mode);
Err pttui_phone_char(char c, int phone_mode, char **ret);
inline Err phone_mode_filter(char ch, int phone_mode, int last_phone_mode, char *ret);

int pttui_mchar_len_ne(unsigned char *str);

Err pttui_fix_cursor(char *str, int pos, enum PttUIFixCursorDir dir, int *new_pos);

Err pttui_next_non_space_char(char *buf, int len, char **p_buf);
Err pttui_first_word(char *buf, int len, int *len_word);

Err pttui_detect_attr(const char *ps, size_t len, int *p_attr);

int pttui_syn_lua_keyword_ne(const char *text, int n, char *wlen);

Err pttui_raw_shift_right(char *s, int len);
Err pttui_raw_shift_left(char *s, int len);
Err pttui_ansi2n(int ansix, char *buf, int *nx);
Err pttui_n2ansi(int nx, char *buf, int *ansix);

Err pttui_edit_outs_attr_n(const char *text, int n, int attr);

Err pttui_visible_window_height(bool is_phone);

int pttui_ctrl_key_ne(int ch);

#ifdef __cplusplus
}
#endif

#endif /* PTTUI_UTIL_H */

