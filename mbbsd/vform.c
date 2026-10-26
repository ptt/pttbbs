/*
 * vform.c
 * Generic Form & Wizard Framework for VT100 / pttbbs
 *
 * Author: Hung-Te Lin (piaip)
 */

#include "bbs.h"
#include "vform.h"
#include "psb.h"

// -----------------------------------------------------------------------------
// Internal Data Helpers
// -----------------------------------------------------------------------------

static inline void *
vform_field_ptr(const vform_field_t *field, void *data)
{
    return (char *)data + field->offset;
}

static inline const void *
vform_field_cptr(const vform_field_t *field, const void *data)
{
    return (const char *)data + field->offset;
}

static long
vform_get_int_val(const vform_field_t *field, const void *data)
{
    const void *p = vform_field_cptr(field, data);
    switch (field->size) {
        case 1:  return *(const int8_t *)p;
        case 2:  return *(const int16_t *)p;
        case 4:  return *(const int32_t *)p;
        case 8:  return *(const int64_t *)p;
        default: return *(const int *)p;
    }
}

static void
vform_set_int_val(const vform_field_t *field, void *data, long val)
{
    void *p = vform_field_ptr(field, data);
    switch (field->size) {
        case 1:  *(int8_t *)p = (int8_t)val; break;
        case 2:  *(int16_t *)p = (int16_t)val; break;
        case 4:  *(int32_t *)p = (int32_t)val; break;
        case 8:  *(int64_t *)p = (int64_t)val; break;
        default: *(int *)p = (int)val; break;
    }
}

int
vform_format_field(const vform_field_t *field, const void *data,
                   char *buf, size_t bufsz)
{
    if (!field || !data || !buf || bufsz == 0)
        return 0;

    buf[0] = '\0';

    switch (field->type) {
        case VFORM_TEXT: {
            const char *src = (const char *)vform_field_cptr(field, data);
            strlcpy(buf, src ? src : "", bufsz);
            break;
        }
        case VFORM_PASSWORD: {
            const char *src = (const char *)vform_field_cptr(field, data);
            size_t len = src ? strlen(src) : 0;
            if (len > bufsz - 1)
                len = bufsz - 1;
            memset(buf, '*', len);
            buf[len] = '\0';
            break;
        }
        case VFORM_INT: {
            long val = vform_get_int_val(field, data);
            snprintf(buf, bufsz, "%ld", val);
            break;
        }
        case VFORM_BOOL: {
            bool b = vform_get_int_val(field, data) != 0;
            strlcpy(buf, b ? "是 (Y)" : "否 (N)", bufsz);
            break;
        }
        case VFORM_SELECT: {
            int idx = (int)vform_get_int_val(field, data);
            if (field->options && idx >= 0 && idx < field->num_options && field->options[idx])
                strlcpy(buf, field->options[idx], bufsz);
            else
                snprintf(buf, bufsz, "%d", idx);
            break;
        }
        case VFORM_NOTE: {
            strlcpy(buf, field->hint ? field->hint : "", bufsz);
            break;
        }
        case VFORM_CUSTOM: {
            strlcpy(buf, "[自訂輸入]", bufsz);
            break;
        }
        case VFORM_USER: {
            if (field->size == sizeof(user_t)) {
                const user_t *u = (const user_t *)vform_field_cptr(field, data);
                if (u && u->unum > 0 && u->unum <= MAX_USERS) {
                    const char *name = getuserid(u->unum);
                    strlcpy(buf, name ? name : "", bufsz);
                }
            } else if (field->size == sizeof(int)) {
                int uid = (int)vform_get_int_val(field, data);
                if (uid > 0 && uid <= MAX_USERS) {
                    const char *name = getuserid(uid);
                    strlcpy(buf, name ? name : "", bufsz);
                }
            } else {
                const char *src = (const char *)vform_field_cptr(field, data);
                strlcpy(buf, src ? src : "", bufsz);
            }
            break;
        }
        default:
            break;
    }

    return strlen(buf);
}

const char *
vform_validate_field(const vform_field_t *field, const void *data, void *priv)
{
    if (!field || !data)
        return NULL;

    if (field->type == VFORM_INT) {
        long val = vform_get_int_val(field, data);
        if (field->min_val || field->max_val) {
            if (val < field->min_val || val > field->max_val)
                return "數值超出允許範圍";
        }
    } else if (field->type == VFORM_SELECT) {
        int idx = (int)vform_get_int_val(field, data);
        if (idx < 0 || idx >= field->num_options)
            return "無效的選項";
    }
    else if (field->type == VFORM_USER) {
        char inbuf[IDLEN + 1] = "";
        if (field->size == sizeof(user_t)) {
            const user_t *u = (const user_t *)vform_field_cptr(field, data);
            if (u && u->unum > 0 && u->unum <= MAX_USERS) {
                const char *name = getuserid(u->unum);
                if (name) strlcpy(inbuf, name, sizeof(inbuf));
            }
        } else if (field->size == sizeof(int)) {
            int uid = (int)vform_get_int_val(field, data);
            if (uid > 0 && uid <= MAX_USERS) {
                const char *name = getuserid(uid);
                if (name) strlcpy(inbuf, name, sizeof(inbuf));
            }
        } else {
            const char *src = (const char *)vform_field_cptr(field, data);
            if (src) strlcpy(inbuf, src, sizeof(inbuf));
        }
        if (inbuf[0] != 0 && searchuser(inbuf, NULL) <= 0)
            return ERR_UID;
    }

    if (field->validator)
        return field->validator(field, vform_field_cptr(field, data), priv);

    return NULL;
}

// -----------------------------------------------------------------------------
// Keyboard Peek Interceptor
// -----------------------------------------------------------------------------
typedef struct {
    int exit_key;
} vform_key_ctx_t;

static int
vform_peek_cb(int key, VGET_RUNTIME *prt GCC_UNUSED, void *instance)
{
    vform_key_ctx_t *ctx = (vform_key_ctx_t *)instance;
    if (key == '\t' || key == KEY_TAB ||
        key == KEY_UP || key == KEY_DOWN || key == KEY_STAB ||
        key == KEY_ENTER || key == KEY_CR || key == KEY_LF ||
        key == KEY_ESC || key == Ctrl('C') || key == Ctrl('S') || key == Ctrl('X') || key == KEY_F10) {
        ctx->exit_key = key;
        return VGETCB_END;
    }
    return VGETCB_NONE;
}

// -----------------------------------------------------------------------------
// Wizard Mode (Step-by-step with Backtracking)
// -----------------------------------------------------------------------------
int
vform_run_wizard(const vform_def_t *def, void *data, void *priv)
{
    if (!def || !data || def->num_fields <= 0)
        return 0;

    VREFSCR scr = vscr_save();
    int curr = 0;
    int dir = 1;

    VGET_CALLBACKS cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.peek = vform_peek_cb;

    while (curr >= 0 && curr < def->num_fields) {
        const vform_field_t *f = &def->fields[curr];

        // Check visibility condition
        if (f->is_visible && !f->is_visible(f, data, priv)) {
            curr += dir;
            continue;
        }

        // Informational notes only
        if (f->type == VFORM_NOTE) {
            curr += dir;
            continue;
        }

        clear();
        vs_hdr(def->title);

        // Step progress header
        move(1, 0);
        prints(ANSI_COLOR(1;36) " [ 項目 %d / %d ] " ANSI_COLOR(1;37) "%s" ANSI_RESET,
               curr + 1, def->num_fields, f->label ? f->label : "");
        if (f->hint && *f->hint)
            prints(" (%s)", f->hint);
        outs("\n");

        // Display notes file if specified and exists
        if (f->notes_file && *f->notes_file && dashs(f->notes_file) > 0) {
            int top = 3;
            int max_lines = b_lines - 7;
            if (max_lines > 0)
                show_file(f->notes_file, top, max_lines, SHOWFILE_ALLOW_ALL);
        }

        // Show current value
        char cur_val[SZ_COLS(STRLEN)];
        vform_format_field(f, data, cur_val, sizeof(cur_val));

        move(b_lines - 4, 0);
        clrtoeol();
        prints("  原先設定: %-30.30s\n", cur_val);

        // Draw input prompt & footer
        move(b_lines - 3, 0);
        clrtoeol();
        prints(ANSI_COLOR(1) ">> %s: " ANSI_RESET, f->label ? f->label : "");

        move(b_lines - 1, 0);
        clrtoeol();
        vs_footer(def->title, " (Enter)確認下一項 (↑)回上一項 (Ctrl-C/q)放棄");

        vform_key_ctx_t kctx;
        memset(&kctx, 0, sizeof(kctx));

        int prompt_col = 3 + strlen(f->label ? f->label : "") + 2;

        // Input dispatch by type
        if (f->type == VFORM_TEXT || f->type == VFORM_PASSWORD) {
            char inbuf[SZ_COLS(STRLEN)];
            const char *src = (const char *)vform_field_cptr(f, data);
            strlcpy(inbuf, src ? src : "", sizeof(inbuf));

            move(b_lines - 3, prompt_col);
            int in_len = f->size > 0 ? (int)f->size : STRLEN;
            if (in_len <= 0) in_len = STRLEN;
            if (in_len > STRLEN) in_len = STRLEN;

            vgetstring_sz(inbuf, sizeof(inbuf), in_len,
                          f->vget_flags | (f->type == VFORM_PASSWORD ? VGET_PASSWORD : 0),
                          inbuf, &cbs, &kctx);

            int key = kctx.exit_key;
            if (key == 0) key = KEY_ENTER;

            if (key == KEY_UP || key == KEY_STAB) {
                if (curr > 0) {
                    dir = -1;
                    curr--;
                    continue;
                } else {
                    bell();
                    continue;
                }
            }

            if (key == KEY_ESC || key == Ctrl('C')) {
                if (vans("確定要放棄填寫嗎？[y/N]: ") == 'y') {
                    vscr_restore(scr);
                    return 0;
                }
                continue;
            }

            // Enter, Tab, Down: validate and commit
            const char *err = f->validator ? f->validator(f, inbuf, priv) : NULL;
            if (err) {
                vmsg(err);
                continue;
            }

            strlcpy((char *)vform_field_ptr(f, data), inbuf, f->size);
            mbs_safe_trim((char *)vform_field_ptr(f, data));
            dir = 1;
            curr++;
            continue;
        } else if (f->type == VFORM_USER) {
            char inbuf[IDLEN + 1] = "";
            char defbuf[IDLEN + 1] = "";
            if (f->size == sizeof(user_t)) {
                const user_t *u = (const user_t *)vform_field_cptr(f, data);
                if (u && u->unum > 0 && u->unum <= MAX_USERS) {
                    const char *name = getuserid(u->unum);
                    if (name) strlcpy(defbuf, name, sizeof(defbuf));
                }
            } else if (f->size == sizeof(int)) {
                int uid = (int)vform_get_int_val(f, data);
                if (uid > 0 && uid <= MAX_USERS) {
                    const char *name = getuserid(uid);
                    if (name) strlcpy(defbuf, name, sizeof(defbuf));
                }
            } else {
                const char *src = (const char *)vform_field_cptr(f, data);
                if (src) strlcpy(defbuf, src, sizeof(defbuf));
            }

            move(b_lines - 3, prompt_col);
            int exit_key = usercomplete_vform("", inbuf, defbuf);

            if (exit_key == KEY_ESC || exit_key == Ctrl('C') ||
                (inbuf[1] == Ctrl('C') && inbuf[0] == 0)) {
                if (vans("─好放棄修改間[y/N]: ") == 'y') {
                    vscr_restore(scr);
                    return 0;
                }
                continue;
            }

            if (exit_key == KEY_UP || exit_key == KEY_STAB) {
                if (curr > 0) {
                    dir = -1;
                    curr--;
                    continue;
                }
            }

            if (inbuf[1] == Ctrl('C') && inbuf[0] == 0) {
                if (vans("４４定要放棄修杜跚？[y/N]: ") == 'y') {
                    vscr_restore(scr);
                    return 0;
                }
                continue;
            }

            char exact_id[IDLEN + 1] = "";
            int uid = 0;
            if (inbuf[0] != 0) {
                uid = searchuser(inbuf, exact_id);
                if (uid <= 0) {
                    vmsg(ERR_UID);
                    continue;
                }
            }

            uint8_t old_val[64];
            size_t copy_sz = f->size < sizeof(old_val) ? f->size : sizeof(old_val);
            memcpy(old_val, vform_field_ptr(f, data), copy_sz);

            if (f->size == sizeof(user_t)) {
                user_t *target = (user_t *)vform_field_ptr(f, data);
                if (uid > 0) {
                    userec_t u;
                    if (passwd_query(uid, &u) == 0) {
                        target->unum = uid;
                        target->firstlogin = u.firstlogin;
                    } else {
                        target->unum = uid;
                        target->firstlogin = 0;
                    }
                } else {
                    memset(target, 0, sizeof(user_t));
                }
            } else if (f->size == sizeof(int)) {
                vform_set_int_val(f, data, uid);
            } else {
                strlcpy((char *)vform_field_ptr(f, data), exact_id, f->size);
            }

            const char *err = f->validator ? f->validator(f, vform_field_cptr(f, data), priv) : NULL;
            if (err) {
                memcpy(vform_field_ptr(f, data), old_val, copy_sz);
                vmsg(err);
                continue;
            }
            dir = 1;
            curr++;
            continue;
        } else if (f->type == VFORM_INT) {
            char inbuf[32];
            snprintf(inbuf, sizeof(inbuf), "%ld", vform_get_int_val(f, data));

            move(b_lines - 3, prompt_col);
            vgetstring_sz(inbuf, sizeof(inbuf), sizeof(inbuf),
                          f->vget_flags | VGET_DIGITS, inbuf, &cbs, &kctx);

            int key = kctx.exit_key;
            if (key == 0) key = KEY_ENTER;

            if (key == KEY_UP || key == KEY_STAB) {
                if (curr > 0) {
                    dir = -1;
                    curr--;
                    continue;
                } else {
                    bell();
                    continue;
                }
            }

            if (key == KEY_ESC || key == Ctrl('C')) {
                if (vans("確定要放棄填寫嗎？[y/N]: ") == 'y') {
                    vscr_restore(scr);
                    return 0;
                }
                continue;
            }

            long val = atol(inbuf);
            if (f->min_val || f->max_val) {
                if (val < f->min_val || val > f->max_val) {
                    vmsgf("數值必須在 %d 到 %d 之間", f->min_val, f->max_val);
                    continue;
                }
            }

            vform_set_int_val(f, data, val);
            const char *err = f->validator ? f->validator(f, vform_field_cptr(f, data), priv) : NULL;
            if (err) {
                vmsg(err);
                continue;
            }

            dir = 1;
            curr++;
            continue;
        } else if (f->type == VFORM_BOOL) {
            bool cur_b = vform_get_int_val(f, data) != 0;
            prints("[%s] (y/n/空白鍵): ", cur_b ? "Y/n" : "y/N");

            int ch = vkey();
            if (ch == KEY_UP || ch == KEY_STAB) {
                if (curr > 0) {
                    dir = -1;
                    curr--;
                    continue;
                } else {
                    bell();
                    continue;
                }
            }

            if (ch == KEY_ESC || ch == Ctrl('C') || ch == 'q' || ch == 'Q') {
                if (vans("確定要放棄填寫嗎？[y/N]: ") == 'y') {
                    vscr_restore(scr);
                    return 0;
                }
                continue;
            }

            if (ch == 'y' || ch == 'Y')
                vform_set_int_val(f, data, 1);
            else if (ch == 'n' || ch == 'N')
                vform_set_int_val(f, data, 0);
            else if (ch == ' ')
                vform_set_int_val(f, data, !cur_b);
            else if (ch == '\r' || ch == '\n' || ch == KEY_ENTER)
                ;
            else
                continue;

            dir = 1;
            curr++;
            continue;
        } else if (f->type == VFORM_SELECT) {
            int cur_idx = (int)vform_get_int_val(f, data);

            move(b_lines - 4, 0);
            clrtoeol();
            outs("可選項目: ");
            for (int k = 0; k < f->num_options; k++) {
                prints("(%d)%s ", k + 1, f->options && f->options[k] ? f->options[k] : "");
            }

            move(b_lines - 3, 0);
            clrtoeol();
            prints("請選擇 [1-%d] (目前: %d): ", f->num_options, cur_idx + 1);

            int ch = vkey();
            if (ch == KEY_UP || ch == KEY_STAB) {
                if (curr > 0) {
                    dir = -1;
                    curr--;
                    continue;
                } else {
                    bell();
                    continue;
                }
            }

            if (ch == KEY_ESC || ch == Ctrl('C') || ch == 'q' || ch == 'Q') {
                if (vans("確定要放棄填寫嗎？[y/N]: ") == 'y') {
                    vscr_restore(scr);
                    return 0;
                }
                continue;
            }

            if (ch >= '1' && ch < '1' + f->num_options) {
                vform_set_int_val(f, data, ch - '1');
            } else if (ch == ' ') {
                vform_set_int_val(f, data, (cur_idx + 1) % f->num_options);
            }

            dir = 1;
            curr++;
            continue;
        } else if (f->type == VFORM_CUSTOM) {
            if (f->custom_fn) {
                int r = f->custom_fn(f, data, priv);
                if (r == VFORM_RET_PREV) {
                    if (curr > 0) {
                        dir = -1;
                        curr--;
                        continue;
                    }
                } else if (r == VFORM_RET_ABORT) {
                    vscr_restore(scr);
                    return 0;
                }
            }
            dir = 1;
            curr++;
            continue;
        }
    }

    // Submission / Confirmation phase
    while (1) {
        if (def->form_validator) {
            const char *err = def->form_validator(def, data, priv);
            if (err) {
                vmsg(err);
                dir = -1;
                curr = def->num_fields - 1;
                return vform_run_wizard(def, data, priv);
            }
        }

        if (def->show_summary) {
            clear();
            vs_hdr(def->title);
            move(2, 0);
            outs(ANSI_COLOR(1;33) "請確認您所填寫的資料：" ANSI_RESET "\n\n");

            int row = 4;
            for (int i = 0; i < def->num_fields && row < b_lines - 3; i++) {
                const vform_field_t *f = &def->fields[i];
                if (f->is_visible && !f->is_visible(f, data, priv))
                    continue;
                if (f->type == VFORM_NOTE)
                    continue;

                char val_str[SZ_COLS(STRLEN)];
                vform_format_field(f, data, val_str, sizeof(val_str));
                move(row++, 4);
                prints("%-16.16s: " ANSI_COLOR(1;37) "%s" ANSI_RESET,
                       f->label ? f->label : "", val_str);
            }
        }

        const char *prompt = def->submit_prompt ? def->submit_prompt :
            "以上資料是否正確？(Y/n/q) [Y]: ";
        int ans = vans(prompt);

        if (ans == 'y' || ans == '\r' || ans == '\n' || ans == '\0') {
            vscr_restore(scr);
            return 1;
        }
        if (ans == 'q') {
            vscr_restore(scr);
            return 0;
        }
        if (ans == 'n') {
            // Backtrack to the last field
            dir = -1;
            curr = def->num_fields - 1;
            return vform_run_wizard(def, data, priv);
        }
    }

    vscr_restore(scr);
    return 1;
}

// Helper to fit string into a fixed-width display slot with DBCS-safe truncation and ellipsis
static void
vform_fit_display(char *dst, size_t dstsz, const char *src, int max_cols)
{
    if (!dst || dstsz == 0)
        return;
    if (!src)
        src = "";

    int total_w = stream_width(src);
    if (total_w <= max_cols) {
        size_t len = strlen(src);
        if (len >= dstsz)
            len = dstsz - 1;
        memcpy(dst, src, len);
        size_t out_i = len;
        int cur_w = total_w;
        while (cur_w < max_cols && out_i + 1 < dstsz) {
            dst[out_i++] = ' ';
            cur_w++;
        }
        dst[out_i] = '\0';
        return;
    }

    int target_w = max_cols - 3;
    if (target_w < 0)
        target_w = 0;

    int cur_w = 0;
    size_t in_len = strlen(src);
    size_t i = 0, out_i = 0;

    while (i < in_len && out_i + 8 < dstsz) {
        int char_b = mb_bytes(src + i);
        int char_w = mb_width(src + i);
        if (char_b <= 0)
            char_b = 1;
        if (char_w < 0)
            char_w = 0;

        if (cur_w + char_w > target_w)
            break;

        for (int k = 0; k < char_b && i < in_len; k++)
            dst[out_i++] = src[i++];
        cur_w += char_w;
    }

    if (out_i + 4 < dstsz) {
        dst[out_i++] = '.';
        dst[out_i++] = '.';
        dst[out_i++] = '.';
        cur_w += 3;
    }

    while (cur_w < max_cols && out_i + 1 < dstsz) {
        dst[out_i++] = ' ';
        cur_w++;
    }

    dst[out_i] = '\0';
}

static int
vform_next_editable_field(const vform_def_t *def, const void *data, void *priv, int curr)
{
    int next = curr + 1;
    while (next < def->num_fields &&
           ((def->fields[next].is_visible && !def->fields[next].is_visible(&def->fields[next], data, priv)) ||
            def->fields[next].type == VFORM_NOTE)) {
        next++;
    }
    return next < def->num_fields ? next : -1;
}

static int
vform_prev_editable_field(const vform_def_t *def, const void *data, void *priv, int curr)
{
    int prev = curr - 1;
    while (prev >= 0 &&
           ((def->fields[prev].is_visible && !def->fields[prev].is_visible(&def->fields[prev], data, priv)) ||
            def->fields[prev].type == VFORM_NOTE)) {
        prev--;
    }
    return prev;
}

static bool
vform_is_input_field(const vform_field_t *f)
{
    return f && (f->type == VFORM_TEXT ||
                 f->type == VFORM_PASSWORD ||
                 f->type == VFORM_INT ||
                 f->type == VFORM_USER);
}

static bool
vform_is_field_empty(const vform_field_t *f, const void *data)
{
    if (!f || !data)
        return true;
    if (f->type == VFORM_TEXT || f->type == VFORM_PASSWORD) {
        const char *s = (const char *)vform_field_cptr(f, data);
        return (!s || !*s);
    }
    if (f->type == VFORM_USER) {
        if (f->size == sizeof(user_t)) {
            const user_t *u = (const user_t *)vform_field_cptr(f, data);
            return (!u || u->unum <= 0);
        } else if (f->size == sizeof(int)) {
            return (vform_get_int_val(f, data) <= 0);
        } else {
            const char *s = (const char *)vform_field_cptr(f, data);
            return (!s || !*s);
        }
    }
    if (f->type == VFORM_INT) {
        return (vform_get_int_val(f, data) == 0);
    }
    return false;
}

// -----------------------------------------------------------------------------
// Interactive In-Place Form Table (Action Bar + Help System)
// -----------------------------------------------------------------------------

#define VFORM_MAX_ROWS 64

typedef struct {
    const vform_def_t *def;
    void *data;
    void *priv;
    int curr;
    bool dirty;
    bool in_edit_flow;
    bool chain_mode;
    int max_lbl_w;
    int start_y;
    int hint_row;
    int row_to_field[VFORM_MAX_ROWS];
    int field_to_row[VFORM_MAX_ROWS];
    VREFSCR scr;
    bool done;
    int result;
} vform_inplace_ctx_t;

static int vform_cmd_enter(cmd_ctx_t *cctx);
static int vform_cmd_toggle(cmd_ctx_t *cctx);
static int vform_cmd_save(cmd_ctx_t *cctx);
static int vform_cmd_quit(cmd_ctx_t *cctx);
static int vform_cmd_abort(cmd_ctx_t *cctx);
static int vform_cmd_help(cmd_ctx_t *cctx);
static int vform_cmd_up(cmd_ctx_t *cctx);
static int vform_cmd_down(cmd_ctx_t *cctx);
static int vform_cmd_home(cmd_ctx_t *cctx);
static int vform_cmd_end(cmd_ctx_t *cctx);
static int vform_cmd_set_yes(cmd_ctx_t *cctx);
static int vform_cmd_set_no(cmd_ctx_t *cctx);

static const cmd_t vform_inplace_cmds[] = {
    // Navigation
    { KEY_UP, "上移", "移動至上一欄位", vform_cmd_up, 0, CMD_PRIO_HIGH },
    { 'k', NULL, NULL, vform_cmd_up, 0, CMD_PRIO_NONE },
    { KEY_STAB, NULL, NULL, vform_cmd_up, 0, CMD_PRIO_NONE },

    { KEY_DOWN, "下移", "移動至下一欄位", vform_cmd_down, 0, CMD_PRIO_HIGH },
    { 'j', NULL, NULL, vform_cmd_down, 0, CMD_PRIO_NONE },
    { '\t', NULL, NULL, vform_cmd_down, 0, CMD_PRIO_NONE },
    { KEY_TAB, NULL, NULL, vform_cmd_down, 0, CMD_PRIO_NONE },

    // Edit / Submit
    { KEY_ENTER, "編輯", "編輯目前欄位", vform_cmd_enter, 0, CMD_PRIO_HIGH },
    { KEY_CR, NULL, NULL, vform_cmd_enter, 0, CMD_PRIO_NONE },
    { KEY_LF, NULL, NULL, vform_cmd_enter, 0, CMD_PRIO_NONE },

    // Toggle options
    { ' ', "切換", "切換選項或進入編輯", vform_cmd_toggle, 0, CMD_PRIO_HIGH },
    { 'y', NULL, NULL, vform_cmd_set_yes, 0, CMD_PRIO_NONE },
    { 'Y', NULL, NULL, vform_cmd_set_yes, 0, CMD_PRIO_NONE },
    { 'n', NULL, NULL, vform_cmd_set_no, 0, CMD_PRIO_NONE },
    { 'N', NULL, NULL, vform_cmd_set_no, 0, CMD_PRIO_NONE },

    // Save
    { Ctrl('X'), "儲存", "儲存表單設定", vform_cmd_save, 0, CMD_PRIO_HIGH },
    { 's', NULL, NULL, vform_cmd_save, 0, CMD_PRIO_NONE },
    { 'S', NULL, NULL, vform_cmd_save, 0, CMD_PRIO_NONE },
    { Ctrl('S'), NULL, NULL, vform_cmd_save, 0, CMD_PRIO_NONE },
    { KEY_F10, NULL, NULL, vform_cmd_save, 0, CMD_PRIO_NONE },

    // Abort
    { Ctrl('C'), "放棄", "放棄變更並離開表單", vform_cmd_abort, 0, CMD_PRIO_HIGH },

    // Home / End
    { KEY_HOME, NULL, "移動至第一欄位", vform_cmd_home, 0, CMD_PRIO_NONE },
    { KEY_END, NULL, "移動至最後欄位", vform_cmd_end, 0, CMD_PRIO_NONE },

    // Quit (right-aligned)
    { 'q', "離開", "離開表單(詢問是否儲存)", vform_cmd_quit, 0, CMD_PRIO_QUIT },
    { 'Q', NULL, NULL, vform_cmd_quit, 0, CMD_PRIO_NONE },
    { KEY_ESC, NULL, NULL, vform_cmd_quit, 0, CMD_PRIO_NONE },

    // Help (right-aligned)
    { 'h', "說明", "顯示操作說明", vform_cmd_help, 0, CMD_PRIO_USAGE },
    { 'H', NULL, NULL, vform_cmd_help, 0, CMD_PRIO_NONE },
    { '?', NULL, NULL, vform_cmd_help, 0, CMD_PRIO_NONE },
    { KEY_F1, NULL, NULL, vform_cmd_help, 0, CMD_PRIO_NONE },

    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static void
vform_on_select(cmd_ctx_t *cctx, int clicked_row_idx)
{
    vform_inplace_ctx_t *cx = (vform_inplace_ctx_t *)cctx->priv;
    int y = cx->start_y + clicked_row_idx;
    if (y >= 0 && y < VFORM_MAX_ROWS && cx->row_to_field[y] >= 0) {
        cx->curr = cx->row_to_field[y];
    }
}

typedef struct {
    int exit_key;
    int sel; // 0 = save, 1 = abandon, 2 = continue
} vform_confirm_key_ctx_t;

static void
vform_draw_confirm_options(int sel)
{
    move(b_lines - 1, 0);
    clrtoeol();
    if (sel == 0) {
        prints(ANSI_COLOR(1;33) "[Y/S]儲存" ANSI_RESET " (N/A/Q)放棄 (E)繼續編輯");
    } else if (sel == 1) {
        prints("(Y/S)儲存 " ANSI_COLOR(1;33) "[N/A/Q]放棄" ANSI_RESET " (E)繼續編輯");
    } else {
        prints("(Y/S)儲存 (N/A/Q)放棄 " ANSI_COLOR(1;33) "[E]繼續編輯" ANSI_RESET);
    }
}

static int
vform_confirm_peek_cb(int key, VGET_RUNTIME *prt GCC_UNUSED, void *instance)
{
    vform_confirm_key_ctx_t *ctx = (vform_confirm_key_ctx_t *)instance;

    if (key == KEY_MOUSE) {
        const vtkbd_mouse_t *m = vkey_get_mouse();
        if (m && !m->is_motion && !m->is_release) {
            if (m->button == MOUSE_BTN_WHEEL_UP) {
                ctx->sel = (ctx->sel + 2) % 3;
                vform_draw_confirm_options(ctx->sel);
                return VGETCB_NEXT;
            } else if (m->button == MOUSE_BTN_WHEEL_DOWN) {
                ctx->sel = (ctx->sel + 1) % 3;
                vform_draw_confirm_options(ctx->sel);
                return VGETCB_NEXT;
            } else if (m->button == MOUSE_BTN_LEFT) {
                if (m->y == b_lines - 1) {
                    if (m->x < 10) {
                        ctx->sel = 0;
                        ctx->exit_key = 's';
                        return VGETCB_END;
                    } else if (m->x < 22) {
                        ctx->sel = 1;
                        ctx->exit_key = 'a';
                        return VGETCB_END;
                    } else {
                        ctx->sel = 2;
                        ctx->exit_key = 'e';
                        return VGETCB_END;
                    }
                } else if (m->y == b_lines) {
                    ctx->exit_key = KEY_ENTER;
                    return VGETCB_END;
                }
            }
        }
        return VGETCB_NEXT;
    }

    if (key == KEY_LEFT || key == KEY_UP || key == KEY_STAB) {
        ctx->sel = (ctx->sel + 2) % 3;
        vform_draw_confirm_options(ctx->sel);
        return VGETCB_NEXT;
    }

    if (key == KEY_RIGHT || key == KEY_DOWN || key == '	' || key == KEY_TAB || key == ' ') {
        ctx->sel = (ctx->sel + 1) % 3;
        vform_draw_confirm_options(ctx->sel);
        return VGETCB_NEXT;
    }

    if (key == KEY_ENTER || key == KEY_CR || key == KEY_LF) {
        ctx->exit_key = KEY_ENTER;
        return VGETCB_END;
    }

    if (key == 'y' || key == 'Y' || key == 's' || key == 'S') {
        ctx->sel = 0;
        ctx->exit_key = 's';
        return VGETCB_END;
    }

    if (key == 'n' || key == 'N' || key == 'a' || key == 'A' || key == 'q' || key == 'Q') {
        ctx->sel = 1;
        ctx->exit_key = 'a';
        return VGETCB_END;
    }

    if (key == 'e' || key == 'E') {
        ctx->sel = 2;
        ctx->exit_key = 'e';
        return VGETCB_END;
    }

    if (key == KEY_ESC || key == Ctrl('C')) {
        ctx->exit_key = key;
        return VGETCB_END;
    }

    bell();
    return VGETCB_NEXT;
}

static const char *
vform_validate_all(vform_inplace_ctx_t *cx, int *first_err_idx)
{
    if (first_err_idx)
        *first_err_idx = -1;
    for (int i = 0; i < cx->def->num_fields; i++) {
        const vform_field_t *f = &cx->def->fields[i];
        if (f->is_visible && !f->is_visible(f, cx->data, cx->priv))
            continue;
        const char *err = vform_validate_field(f, cx->data, cx->priv);
        if (err) {
            if (first_err_idx)
                *first_err_idx = i;
            return err;
        }
    }
    if (cx->def->form_validator) {
        const char *err = cx->def->form_validator(cx->def, cx->data, cx->priv);
        if (err)
            return err;
    }
    return NULL;
}

static int
vform_confirm_save(vform_inplace_ctx_t *cx, char default_choice)
{
    int sel = 0;
    if (default_choice == 's' || default_choice == 'y') {
        sel = 0;
    } else if (default_choice == 'a' || default_choice == 'n' || default_choice == 'q') {
        sel = 1;
    } else {
        sel = 2;
    }

    vform_draw_confirm_options(sel);

    move(b_lines, 0);
    clrtoeol();
    outs("確定要儲存變更嗎？ ");

    char ans[4] = "";
    vform_confirm_key_ctx_t kctx;
    memset(&kctx, 0, sizeof(kctx));
    kctx.sel = sel;

    VGET_CALLBACKS cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.peek = vform_confirm_peek_cb;

    vgetstring_sz(ans, sizeof(ans), 2, VGET_LOWERCASE, "", &cbs, &kctx);

    if (kctx.exit_key == KEY_ESC || kctx.exit_key == Ctrl('C')) {
        return -1;
    }

    sel = kctx.sel;

    switch (sel) {
    case 0: { // Save (Y/S)
        int err_idx = -1;
        const char *err = vform_validate_all(cx, &err_idx);
        if (err) {
            vmsg(err);
            if (err_idx >= 0) {
                cx->curr = err_idx;
                const vform_field_t *err_f = &cx->def->fields[err_idx];
                if (vform_is_input_field(err_f) || err_f->type == VFORM_BOOL)
                    cx->in_edit_flow = true;
            }
            return -1;
        }
        return 1;
    }

    case 1: // Abandon (N/A)
        return 0;

    case 2: // Continue (E)
    default:
        return -1;
    }
}

static int
vform_cmd_up(cmd_ctx_t *cctx)
{
    vform_inplace_ctx_t *cx = (vform_inplace_ctx_t *)cctx->priv;
    cx->in_edit_flow = false;
    cx->chain_mode = false;
    int prev = vform_prev_editable_field(cx->def, cx->data, cx->priv, cx->curr);
    if (prev >= 0)
        cx->curr = prev;
    else
        bell();
    cctx->redraw = true;
    return 0;
}

static int
vform_cmd_down(cmd_ctx_t *cctx)
{
    vform_inplace_ctx_t *cx = (vform_inplace_ctx_t *)cctx->priv;
    cx->in_edit_flow = false;
    cx->chain_mode = false;
    int next = vform_next_editable_field(cx->def, cx->data, cx->priv, cx->curr);
    if (next >= 0)
        cx->curr = next;
    else
        bell();
    cctx->redraw = true;
    return 0;
}

static int
vform_cmd_home(cmd_ctx_t *cctx)
{
    vform_inplace_ctx_t *cx = (vform_inplace_ctx_t *)cctx->priv;
    cx->in_edit_flow = false;
    cx->chain_mode = false;
    int first = vform_next_editable_field(cx->def, cx->data, cx->priv, -1);
    if (first >= 0)
        cx->curr = first;
    cctx->redraw = true;
    return 0;
}

static int
vform_cmd_end(cmd_ctx_t *cctx)
{
    vform_inplace_ctx_t *cx = (vform_inplace_ctx_t *)cctx->priv;
    cx->in_edit_flow = false;
    cx->chain_mode = false;
    int last = vform_prev_editable_field(cx->def, cx->data, cx->priv, cx->def->num_fields);
    if (last >= 0)
        cx->curr = last;
    cctx->redraw = true;
    return 0;
}

static int
vform_cmd_set_yes(cmd_ctx_t *cctx)
{
    vform_inplace_ctx_t *cx = (vform_inplace_ctx_t *)cctx->priv;
    const vform_field_t *cur_f = &cx->def->fields[cx->curr];
    if (cur_f->type == VFORM_BOOL) {
        long cur_val = vform_get_int_val(cur_f, cx->data);
        if (cur_val != 1) {
            vform_set_int_val(cur_f, cx->data, 1);
            cx->dirty = true;
        }
        cctx->redraw = true;
        return 0;
    }
    return -1;
}

static int
vform_cmd_set_no(cmd_ctx_t *cctx)
{
    vform_inplace_ctx_t *cx = (vform_inplace_ctx_t *)cctx->priv;
    const vform_field_t *cur_f = &cx->def->fields[cx->curr];
    if (cur_f->type == VFORM_BOOL) {
        long cur_val = vform_get_int_val(cur_f, cx->data);
        if (cur_val != 0) {
            vform_set_int_val(cur_f, cx->data, 0);
            cx->dirty = true;
        }
        cctx->redraw = true;
        return 0;
    }
    return -1;
}

static int
vform_cmd_toggle(cmd_ctx_t *cctx)
{
    vform_inplace_ctx_t *cx = (vform_inplace_ctx_t *)cctx->priv;
    const vform_field_t *cur_f = &cx->def->fields[cx->curr];
    cx->in_edit_flow = false;
    cx->chain_mode = false;
    if (cur_f->type == VFORM_BOOL) {
        long cur_val = vform_get_int_val(cur_f, cx->data);
        vform_set_int_val(cur_f, cx->data, cur_val ? 0 : 1);
        cx->dirty = true;
        cctx->redraw = true;
        return 0;
    } else if (cur_f->type == VFORM_SELECT) {
        int cur_idx = (int)vform_get_int_val(cur_f, cx->data);
        if (cur_f->num_options > 1) {
            vform_set_int_val(cur_f, cx->data, (cur_idx + 1) % cur_f->num_options);
            cx->dirty = true;
        }
        cctx->redraw = true;
        return 0;
    } else if (vform_is_input_field(cur_f) || (cur_f->type == VFORM_CUSTOM && cur_f->custom_fn)) {
        return vform_cmd_enter(cctx);
    }
    bell();
    return 0;
}

static int
vform_cmd_save(cmd_ctx_t *cctx)
{
    vform_inplace_ctx_t *cx = (vform_inplace_ctx_t *)cctx->priv;
    cx->in_edit_flow = false;

    int err_idx = -1;
    const char *err = vform_validate_all(cx, &err_idx);
    if (err) {
        vmsg(err);
        if (err_idx >= 0) {
            cx->curr = err_idx;
            const vform_field_t *err_f = &cx->def->fields[err_idx];
            if (vform_is_input_field(err_f) || err_f->type == VFORM_BOOL)
                cx->in_edit_flow = true;
        }
        cctx->redraw = true;
        return 0;
    }

    int r = vform_confirm_save(cx, 's');
    if (r >= 0) {
        cx->done = true;
        cx->result = r;
        cctx->quit = true;
    } else {
        cctx->redraw = true;
    }
    return 0;
}

static int
vform_cmd_quit(cmd_ctx_t *cctx)
{
    vform_inplace_ctx_t *cx = (vform_inplace_ctx_t *)cctx->priv;
    cx->in_edit_flow = false;
    if (!cx->dirty) {
        cx->done = true;
        cx->result = 0;
        cctx->quit = true;
        return 0;
    }
    int r = vform_confirm_save(cx, 'e');
    if (r >= 0) {
        cx->done = true;
        cx->result = r;
        cctx->quit = true;
    } else {
        cctx->redraw = true;
    }
    return 0;
}

static int
vform_cmd_abort(cmd_ctx_t *cctx)
{
    vform_inplace_ctx_t *cx = (vform_inplace_ctx_t *)cctx->priv;
    cx->in_edit_flow = false;
    if (!cx->dirty) {
        cx->done = true;
        cx->result = 0;
        cctx->quit = true;
        return 0;
    }
    int r = vform_confirm_save(cx, 'a');
    if (r >= 0) {
        cx->done = true;
        cx->result = r;
        cctx->quit = true;
    } else {
        cctx->redraw = true;
    }
    return 0;
}

static int
vform_cmd_help(cmd_ctx_t *cctx)
{
    screen_backup_t old_screen;
    scr_dump(&old_screen);
    int sel_key = cmd_show_help_layers(cctx->caption, cctx->active_layers);
    scr_restore(&old_screen);
    if (sel_key > 0 && sel_key != cctx->key) {
        cctx->key = sel_key;
        cctx->redispatch = true;
        return 0;
    }
    cctx->redraw = true;
    return 0;
}

static int
vform_cmd_enter(cmd_ctx_t *cctx)
{
    vform_inplace_ctx_t *cx = (vform_inplace_ctx_t *)cctx->priv;
    const vform_field_t *cur_f = &cx->def->fields[cx->curr];
    int next = vform_next_editable_field(cx->def, cx->data, cx->priv, cx->curr);
    bool is_last = (next < 0);

    if (cur_f->type == VFORM_NOTE) {
        if (next >= 0) {
            cx->curr = next;
            cx->in_edit_flow = cx->chain_mode;
        } else {
            cx->in_edit_flow = false;
            return vform_cmd_save(cctx);
        }
        cctx->redraw = true;
        return 0;
    }

    if (cur_f->type == VFORM_BOOL) {
        int edit_row = (cx->curr >= 0 && cx->curr < VFORM_MAX_ROWS) ? cx->field_to_row[cx->curr] : -1;
        if (edit_row < 0) {
            edit_row = cx->start_y;
            for (int j = 0; j < cx->curr; j++) {
                if (!cx->def->fields[j].is_visible || cx->def->fields[j].is_visible(&cx->def->fields[j], cx->data, cx->priv))
                    edit_row++;
            }
        }

        int temp_val = (vform_get_int_val(cur_f, cx->data) != 0) ? 1 : 0;
        int orig_val = temp_val;
        int cursor_col = 3 + cx->max_lbl_w + 2 + stream_width("請輸入是(Y)或否(N): [");

        // Sub-footer status hint during prompt
        int hint_y = cx->hint_row > 0 ? cx->hint_row : (b_lines - 1);
        move(hint_y, 0);
        clrtoeol();
        if (cur_f->hint && *cur_f->hint)
            prints("  " ANSI_COLOR(1;36) "說明: " ANSI_RESET "%s", cur_f->hint);
        else
            prints("  " ANSI_COLOR(1;36) "說明: " ANSI_RESET "請輸入 y 或 n，按空白鍵切換，按 Enter 確認");
        if (hint_y != b_lines - 1) {
            move(b_lines - 1, 0);
            clrtoeol();
        }

        while (1) {
            move(edit_row, 0);
            clrtoeol();
            prints(ANSI_COLOR(1;33) " > " ANSI_RESET);
            prints("%-*s: " ANSI_COLOR(1;33) "請輸入是(Y)或否(N): " ANSI_RESET "[" VCLR_INPUT_FIELD "%c" ANSI_RESET "]",
                   cx->max_lbl_w, cur_f->label ? cur_f->label : "", temp_val ? 'Y' : 'N');

            move(edit_row, cursor_col);
            int ch = vkey();

            if (ch == 'y' || ch == 'Y') {
                temp_val = 1;
                continue;
            } else if (ch == 'n' || ch == 'N') {
                temp_val = 0;
                continue;
            } else if (ch == ' ' || ch == KEY_LEFT || ch == KEY_RIGHT) {
                temp_val = !temp_val;
                continue;
            } else if (ch == KEY_UP || ch == KEY_STAB) {
                vform_set_int_val(cur_f, cx->data, temp_val);
                if (orig_val != temp_val) cx->dirty = true;
                int prev = vform_prev_editable_field(cx->def, cx->data, cx->priv, cx->curr);
                if (prev >= 0)
                    cx->curr = prev;
                cx->in_edit_flow = false;
                cx->chain_mode = false;
                cctx->redraw = true;
                return 0;
            } else if (ch == KEY_DOWN || ch == '	' || ch == KEY_TAB) {
                vform_set_int_val(cur_f, cx->data, temp_val);
                if (orig_val != temp_val) cx->dirty = true;
                if (!is_last) {
                    cx->curr = next;
                    cx->in_edit_flow = cx->chain_mode;
                } else {
                    cx->in_edit_flow = false;
                }
                cctx->redraw = true;
                return 0;
            } else if (ch == KEY_ESC || ch == Ctrl('C')) {
                cx->in_edit_flow = false;
                cx->chain_mode = false;
                cctx->redraw = true;
                return 0;
            } else if (ch == Ctrl('X') || ch == Ctrl('S') || ch == KEY_F10) {
                vform_set_int_val(cur_f, cx->data, temp_val);
                if (orig_val != temp_val)
                    cx->dirty = true;
                cx->in_edit_flow = false;
                cx->chain_mode = false;
                return vform_cmd_save(cctx);
            } else if (ch == '\r' || ch == '\n' || ch == KEY_ENTER) {
                vform_set_int_val(cur_f, cx->data, temp_val);
                if (orig_val != temp_val)
                    cx->dirty = true;

                if (is_last) {
                    cx->in_edit_flow = false;
                    return vform_cmd_save(cctx);
                } else {
                    cx->curr = next;
                    cx->in_edit_flow = cx->chain_mode;
                    cctx->redraw = true;
                    return 0;
                }
            } else {
                bell();
                continue;
            }
        }
    }

    if (cur_f->type == VFORM_SELECT) {
        if (is_last) {
            return vform_cmd_save(cctx);
        } else {
            cx->curr = next;
            cx->in_edit_flow = cx->chain_mode;
            cctx->redraw = true;
            return 0;
        }
    }

    int edit_row = (cx->curr >= 0 && cx->curr < VFORM_MAX_ROWS) ? cx->field_to_row[cx->curr] : -1;
    if (edit_row < 0) {
        edit_row = cx->start_y;
        for (int j = 0; j < cx->curr; j++) {
            if (!cx->def->fields[j].is_visible || cx->def->fields[j].is_visible(&cx->def->fields[j], cx->data, cx->priv))
                edit_row++;
        }
    }
    int data_col = 3 + cx->max_lbl_w + 3;

    VGET_CALLBACKS cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.peek = vform_peek_cb;

    if (cur_f->type == VFORM_TEXT || cur_f->type == VFORM_PASSWORD) {
        char inbuf[SZ_COLS(STRLEN)];
        const char *src = (const char *)vform_field_cptr(cur_f, cx->data);
        strlcpy(inbuf, src ? src : "", sizeof(inbuf));

        // Erase '[' with a space so it does not display during editing
        move(edit_row, data_col - 1);
        outc(' ');

        int in_len = cur_f->size > 0 ? (int)cur_f->size : 32;
        if (in_len <= 0) in_len = 32;
        int max_len = (80 > data_col + 1) ? (80 - data_col - 1) : 32;
        if (in_len > max_len) in_len = max_len;

        vform_key_ctx_t kctx;
        memset(&kctx, 0, sizeof(kctx));
        vgetstring_sz(inbuf, sizeof(inbuf), in_len,
                      cur_f->vget_flags | (cur_f->type == VFORM_PASSWORD ? VGET_PASSWORD : 0),
                      inbuf, &cbs, &kctx);

        if (kctx.exit_key == KEY_ESC || kctx.exit_key == Ctrl('C')) {
            cx->in_edit_flow = false;
            cx->chain_mode = false;
            cctx->redraw = true;
            return 0;
        }

        if (kctx.exit_key == KEY_UP || kctx.exit_key == KEY_STAB) {
            const char *err = cur_f->validator ? cur_f->validator(cur_f, inbuf, cx->priv) : NULL;
            if (err) {
                vmsg(err);
                cx->in_edit_flow = true;
                cctx->redraw = true;
                return 0;
            }
            const char *old_txt = (const char *)vform_field_cptr(cur_f, cx->data);
            if (!old_txt || strcmp(old_txt, inbuf) != 0)
                cx->dirty = true;
            strlcpy((char *)vform_field_ptr(cur_f, cx->data), inbuf, cur_f->size);
            mbs_safe_trim((char *)vform_field_ptr(cur_f, cx->data));

            int prev = vform_prev_editable_field(cx->def, cx->data, cx->priv, cx->curr);
            if (prev >= 0)
                cx->curr = prev;
            cx->in_edit_flow = false;
            cx->chain_mode = false;
            cctx->redraw = true;
            return 0;
        }

        if (kctx.exit_key == Ctrl('X') || kctx.exit_key == Ctrl('S') || kctx.exit_key == KEY_F10) {
            cx->in_edit_flow = false;
            cx->chain_mode = false;
            const char *err = cur_f->validator ? cur_f->validator(cur_f, inbuf, cx->priv) : NULL;
            if (err) {
                vmsg(err);
                cctx->redraw = true;
                return 0;
            }
            const char *old_txt = (const char *)vform_field_cptr(cur_f, cx->data);
            if (!old_txt || strcmp(old_txt, inbuf) != 0)
                cx->dirty = true;
            strlcpy((char *)vform_field_ptr(cur_f, cx->data), inbuf, cur_f->size);
            mbs_safe_trim((char *)vform_field_ptr(cur_f, cx->data));
            return vform_cmd_save(cctx);
        }

        const char *err = cur_f->validator ? cur_f->validator(cur_f, inbuf, cx->priv) : NULL;
        if (err) {
            vmsg(err);
            cx->in_edit_flow = true;
            cctx->redraw = true;
            return 0;
        }
        const char *old_txt = (const char *)vform_field_cptr(cur_f, cx->data);
        if (!old_txt || strcmp(old_txt, inbuf) != 0)
            cx->dirty = true;
        strlcpy((char *)vform_field_ptr(cur_f, cx->data), inbuf, cur_f->size);
        mbs_safe_trim((char *)vform_field_ptr(cur_f, cx->data));
        int next_f = vform_next_editable_field(cx->def, cx->data, cx->priv, cx->curr);
        if (next_f >= 0) {
            cx->curr = next_f;
            cx->in_edit_flow = cx->chain_mode;
        } else {
            cx->in_edit_flow = false;
            return vform_cmd_save(cctx);
        }
        cctx->redraw = true;
        return 0;
    } else if (cur_f->type == VFORM_USER) {
        char inbuf[IDLEN + 1] = "";
        char defbuf[IDLEN + 1] = "";
        if (cur_f->size == sizeof(user_t)) {
            const user_t *u = (const user_t *)vform_field_cptr(cur_f, cx->data);
            if (u && u->unum > 0 && u->unum <= MAX_USERS) {
                const char *name = getuserid(u->unum);
                if (name) strlcpy(defbuf, name, sizeof(defbuf));
            }
        } else if (cur_f->size == sizeof(int)) {
            int uid = (int)vform_get_int_val(cur_f, cx->data);
            if (uid > 0 && uid <= MAX_USERS) {
                const char *name = getuserid(uid);
                if (name) strlcpy(defbuf, name, sizeof(defbuf));
            }
        } else {
            const char *src = (const char *)vform_field_cptr(cur_f, cx->data);
            if (src) strlcpy(defbuf, src, sizeof(defbuf));
        }

        move(edit_row, data_col - 1);
        outc(' ');
        move(edit_row, data_col);

        int exit_key = usercomplete_vform("", inbuf, defbuf);

        if (exit_key == KEY_ESC || exit_key == Ctrl('C') ||
            (inbuf[1] == Ctrl('C') && inbuf[0] == 0)) {
            cx->in_edit_flow = false;
            cx->chain_mode = false;
            cctx->redraw = true;
            return 0;
        }

        mbs_safe_trim(inbuf);

        char exact_id[IDLEN + 1] = "";
        int uid = 0;
        if (inbuf[0] != 0) {
            uid = searchuser(inbuf, exact_id);
            if (uid <= 0) {
                vmsg(ERR_UID);
                cx->in_edit_flow = true;
                cctx->redraw = true;
                return 0;
            }
        }

        uint8_t old_val[64];
        size_t copy_sz = cur_f->size < sizeof(old_val) ? cur_f->size : sizeof(old_val);
        memcpy(old_val, vform_field_ptr(cur_f, cx->data), copy_sz);

        if (cur_f->size == sizeof(user_t)) {
            user_t *target = (user_t *)vform_field_ptr(cur_f, cx->data);
            if (uid > 0) {
                userec_t u;
                if (passwd_query(uid, &u) == 0) {
                    target->unum = uid;
                    target->firstlogin = u.firstlogin;
                } else {
                    target->unum = uid;
                    target->firstlogin = 0;
                }
            } else {
                memset(target, 0, sizeof(user_t));
            }
        } else if (cur_f->size == sizeof(int)) {
            vform_set_int_val(cur_f, cx->data, uid);
        } else {
            strlcpy((char *)vform_field_ptr(cur_f, cx->data), exact_id, cur_f->size);
        }

        const char *err = cur_f->validator ? cur_f->validator(cur_f, vform_field_cptr(cur_f, cx->data), cx->priv) : NULL;
        if (err) {
            memcpy(vform_field_ptr(cur_f, cx->data), old_val, copy_sz);
            vmsg(err);
            cx->in_edit_flow = true;
            cctx->redraw = true;
            return 0;
        }
        if (memcmp(vform_field_ptr(cur_f, cx->data), old_val, copy_sz) != 0)
            cx->dirty = true;

        if (exit_key == KEY_UP || exit_key == KEY_STAB) {
            int prev = vform_prev_editable_field(cx->def, cx->data, cx->priv, cx->curr);
            if (prev >= 0)
                cx->curr = prev;
            cx->in_edit_flow = false;
            cx->chain_mode = false;
            cctx->redraw = true;
            return 0;
        }

        if (exit_key == Ctrl('X') || exit_key == Ctrl('S') || exit_key == KEY_F10) {
            cx->in_edit_flow = false;
            cx->chain_mode = false;
            return vform_cmd_save(cctx);
        }

        int next_f = vform_next_editable_field(cx->def, cx->data, cx->priv, cx->curr);
        if (next_f >= 0) {
            cx->curr = next_f;
            cx->in_edit_flow = cx->chain_mode;
        } else {
            cx->in_edit_flow = false;
            return vform_cmd_save(cctx);
        }
        cctx->redraw = true;
        return 0;
    } else if (cur_f->type == VFORM_INT) {
        char inbuf[32];
        snprintf(inbuf, sizeof(inbuf), "%ld", vform_get_int_val(cur_f, cx->data));

        move(edit_row, data_col - 1);
        outc(' ');

        vform_key_ctx_t kctx;
        memset(&kctx, 0, sizeof(kctx));
        vgetstring_sz(inbuf, sizeof(inbuf), 32,
                      cur_f->vget_flags | VGET_DIGITS, inbuf, &cbs, &kctx);

        if (kctx.exit_key == KEY_ESC || kctx.exit_key == Ctrl('C')) {
            cx->in_edit_flow = false;
            cx->chain_mode = false;
            cctx->redraw = true;
            return 0;
        }

        if (kctx.exit_key == KEY_UP || kctx.exit_key == KEY_STAB) {
            long val = atol(inbuf);
            if (cur_f->min_val || cur_f->max_val) {
                if (val < cur_f->min_val || val > cur_f->max_val) {
                    vmsgf("數值必須在 %d 到 %d 之間", cur_f->min_val, cur_f->max_val);
                    cx->in_edit_flow = true;
                    cctx->redraw = true;
                    return 0;
                }
            }
            if (vform_get_int_val(cur_f, cx->data) != val)
                cx->dirty = true;
            vform_set_int_val(cur_f, cx->data, val);
            const char *err = cur_f->validator ? cur_f->validator(cur_f, vform_field_cptr(cur_f, cx->data), cx->priv) : NULL;
            if (err) {
                vmsg(err);
                cx->in_edit_flow = true;
                cctx->redraw = true;
                return 0;
            }
            int prev = vform_prev_editable_field(cx->def, cx->data, cx->priv, cx->curr);
            if (prev >= 0)
                cx->curr = prev;
            cx->in_edit_flow = false;
            cx->chain_mode = false;
            cctx->redraw = true;
            return 0;
        }

        if (kctx.exit_key == Ctrl('X') || kctx.exit_key == Ctrl('S') || kctx.exit_key == KEY_F10) {
            cx->in_edit_flow = false;
            cx->chain_mode = false;
            long val = atol(inbuf);
            if (cur_f->min_val || cur_f->max_val) {
                if (val < cur_f->min_val || val > cur_f->max_val) {
                    vmsgf("數值必須在 %d 到 %d 之間", cur_f->min_val, cur_f->max_val);
                    cctx->redraw = true;
                    return 0;
                }
            }
            if (vform_get_int_val(cur_f, cx->data) != val)
                cx->dirty = true;
            vform_set_int_val(cur_f, cx->data, val);
            const char *err = cur_f->validator ? cur_f->validator(cur_f, vform_field_cptr(cur_f, cx->data), cx->priv) : NULL;
            if (err) {
                vmsg(err);
                cctx->redraw = true;
                return 0;
            }
            return vform_cmd_save(cctx);
        }

        long val = atol(inbuf);
        if (cur_f->min_val || cur_f->max_val) {
            if (val < cur_f->min_val || val > cur_f->max_val) {
                vmsgf("數值必須在 %d 到 %d 之間", cur_f->min_val, cur_f->max_val);
                cx->in_edit_flow = true;
                cctx->redraw = true;
                return 0;
            }
        }
        if (vform_get_int_val(cur_f, cx->data) != val)
            cx->dirty = true;
        vform_set_int_val(cur_f, cx->data, val);
        const char *err = cur_f->validator ? cur_f->validator(cur_f, vform_field_cptr(cur_f, cx->data), cx->priv) : NULL;
        if (err) {
            vmsg(err);
            cx->in_edit_flow = true;
            cctx->redraw = true;
            return 0;
        }
        int next_f = vform_next_editable_field(cx->def, cx->data, cx->priv, cx->curr);
        if (next_f >= 0) {
            cx->curr = next_f;
            cx->in_edit_flow = cx->chain_mode;
        } else {
            cx->in_edit_flow = false;
            return vform_cmd_save(cctx);
        }
        cctx->redraw = true;
        return 0;
    } else if (cur_f->type == VFORM_CUSTOM && cur_f->custom_fn) {
        int r = cur_f->custom_fn(cur_f, cx->data, cx->priv);
        if (r != VFORM_RET_ABORT) {
            cx->dirty = true;
            if (r == VFORM_RET_PREV) {
                int prev = vform_prev_editable_field(cx->def, cx->data, cx->priv, cx->curr);
                if (prev >= 0)
                    cx->curr = prev;
                cx->in_edit_flow = false;
                cx->chain_mode = false;
            } else {
                int next_f = vform_next_editable_field(cx->def, cx->data, cx->priv, cx->curr);
                if (next_f >= 0) {
                    cx->curr = next_f;
                    cx->in_edit_flow = cx->chain_mode;
                } else {
                    cx->in_edit_flow = false;
                    return vform_cmd_save(cctx);
                }
            }
        } else {
            cx->in_edit_flow = false;
            cx->chain_mode = false;
        }
        cctx->redraw = true;
        return 0;
    }

    return 0;
}

int
vform_run_inplace(const vform_def_t *def, void *data, void *priv)
{
    if (!def || !data || def->num_fields <= 0)
        return 0;

    VREFSCR scr = vscr_save();
    vform_inplace_ctx_t cx;
    memset(&cx, 0, sizeof(cx));
    cx.def = def;
    cx.data = data;
    cx.priv = priv;
    cx.scr = scr;

    // Find the first visible and editable field
    while (cx.curr < def->num_fields &&
           ((def->fields[cx.curr].is_visible && !def->fields[cx.curr].is_visible(&def->fields[cx.curr], data, priv)) ||
            def->fields[cx.curr].type == VFORM_NOTE)) {
        cx.curr++;
    }
    if (cx.curr >= def->num_fields)
        cx.curr = 0;

    cx.chain_mode = false;
    if (cx.curr < def->num_fields) {
        const vform_field_t *f0 = &def->fields[cx.curr];
        if (vform_is_input_field(f0) && vform_is_field_empty(f0, data)) {
            cx.in_edit_flow = true;
            cx.chain_mode = true;
        }
    }

    // Calculate max label width
    cx.max_lbl_w = 8;
    for (int i = 0; i < def->num_fields; i++) {
        if (def->fields[i].label) {
            int w = strlen(def->fields[i].label);
            if (w > cx.max_lbl_w)
                cx.max_lbl_w = w;
        }
    }
    if (cx.max_lbl_w > 24) cx.max_lbl_w = 24;

    int sub_lines = 0;
    if (def->subtitle && *def->subtitle) {
        sub_lines = 1;
        for (const char *p = def->subtitle; *p; p++) {
            if (*p == '\n') sub_lines++;
        }
    }
    cx.start_y = sub_lines ? (1 + sub_lines + 1) : 3;

    const cmd_layer_t layers[] = {
        { vform_inplace_cmds, &cx },
        { bbs_global_cmds, NULL },
        { NULL, NULL }
    };

    while (!cx.done) {
        clear();
        vs_hdr(def->title);
        if (def->subtitle && *def->subtitle) {
            int sub_y = 1;
            const char *p = def->subtitle;
            while (*p) {
                const char *nl = strchr(p, '\n');
                int len = nl ? (int)(nl - p) : (int)strlen(p);
                move(sub_y++, 0);
                clrtoeol();
                if (len <= 78)
                    prints("  %.*s", len, p);
                else
                    prints("%.*s", len, p);
                if (!nl) break;
                p = nl + 1;
            }
        }

        for (int y = 0; y < VFORM_MAX_ROWS; y++)
            cx.row_to_field[y] = -1;
        for (int i = 0; i < VFORM_MAX_ROWS; i++)
            cx.field_to_row[i] = -1;

        // Render all fields
        int row = cx.start_y;
        for (int i = 0; i < def->num_fields; i++) {
            const vform_field_t *f = &def->fields[i];
            if (f->is_visible && !f->is_visible(f, data, priv))
                continue;

            if (row >= b_lines - 2)
                break;

            if (row < VFORM_MAX_ROWS)
                cx.row_to_field[row] = i;
            if (i < VFORM_MAX_ROWS)
                cx.field_to_row[i] = row;

            move(row, 0);
            clrtoeol();

            char val_str[SZ_COLS(STRLEN)];
            vform_format_field(f, data, val_str, sizeof(val_str));

            char disp_val[SZ_COLS(STRLEN)];
            vform_fit_display(disp_val, sizeof(disp_val), val_str, 32);

            if (i == cx.curr) {
                prints(ANSI_COLOR(1;33) " > " ANSI_RESET);
                if (f->type == VFORM_BOOL && cx.in_edit_flow) {
                    bool b = vform_get_int_val(f, data) != 0;
                    prints("%-*s: " ANSI_COLOR(1;33) "請輸入是(Y)或否(N): " ANSI_RESET "[" VCLR_INPUT_FIELD "%c" ANSI_RESET "]",
                           cx.max_lbl_w, f->label ? f->label : "", b ? 'Y' : 'N');
                } else {
                    prints("%-*s: [", cx.max_lbl_w, f->label ? f->label : "");
                    prints(VCLR_INPUT_FIELD "%s" ANSI_RESET, disp_val);
                    prints("]");
                }
            } else {
                prints("   %-*s: [%s]",
                       cx.max_lbl_w, f->label ? f->label : "", disp_val);
            }
            row++;
        }

        cx.hint_row = (row > cx.start_y) ? (row + 1) : (cx.start_y + 2);
        if (cx.hint_row > b_lines - 1)
            cx.hint_row = b_lines - 1;

        // Draw help hint at form's last field + 2 rows
        move(cx.hint_row, 0);
        clrtoeol();
        const vform_field_t *cur_f = &def->fields[cx.curr];
        if (cur_f->hint && *cur_f->hint)
            prints("  " ANSI_COLOR(1;36) "說明: " ANSI_RESET "%s", cur_f->hint);
        else if (cur_f->type == VFORM_BOOL)
            prints("  " ANSI_COLOR(1;36) "說明: " ANSI_RESET "請輸入 y 或 n，按空白鍵切換，按 Enter 確認");

        if (cx.hint_row != b_lines - 1) {
            move(b_lines - 1, 0);
            clrtoeol();
        }
        vs_cmd_bar(VS_FOOTER, def->title, layers);

        if (cx.in_edit_flow && (vform_is_input_field(cur_f) || cur_f->type == VFORM_BOOL)) {
            cmd_ctx_t cctx = {
                .key = KEY_ENTER,
                .curr = -1,
                .priv = &cx,
                .header_lines = cx.start_y,
                .visible_rows = (row > cx.start_y) ? (row - cx.start_y) : 0,
                .total = def->num_fields,
                .on_select = vform_on_select,
                .active_layers = layers,
                .caption = def->title,
            };
            vform_cmd_enter(&cctx);
            if (cx.done) break;
            continue;
        }

        cx.in_edit_flow = false;

        int ch = vkey();
        cmd_ctx_t cctx = {
            .key = ch,
            .curr = -1,
            .priv = &cx,
            .header_lines = cx.start_y,
            .visible_rows = (row > cx.start_y) ? (row - cx.start_y) : 0,
            .total = def->num_fields,
            .on_select = vform_on_select,
            .active_layers = layers,
            .caption = def->title,
        };

        cmd_dispatch_layers(layers, &cctx, def->title);
        if (cx.done || cctx.quit) break;
    }

    vscr_restore(scr);
    return cx.result;
}

// -----------------------------------------------------------------------------
// Main Dispatcher
// -----------------------------------------------------------------------------
int
vform_run(const vform_def_t *def, void *data, void *priv)
{
    if (!def || !data)
        return 0;

    vform_mode_t mode = def->mode;
    if (mode == VFORM_MODE_AUTO) {
        mode = VFORM_MODE_INPLACE;
        for (int i = 0; i < def->num_fields; i++) {
            if (def->fields[i].notes_file && *def->fields[i].notes_file) {
                mode = VFORM_MODE_WIZARD;
                break;
            }
        }
        if (def->num_fields > (b_lines - 6))
            mode = VFORM_MODE_WIZARD;
    }

    if (mode == VFORM_MODE_WIZARD)
        return vform_run_wizard(def, data, priv);
    else
        return vform_run_inplace(def, data, priv);
}
