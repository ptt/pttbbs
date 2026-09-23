#include "bbs.h"
#include "daemons.h"
#include "psb.h"

// Page & Service Browser
//
// A generic framework for displaying pre-generated data by a simplified
// page-view user interface to support various services.
//
// Author: Hung-Te Lin (piaip)
// --------------------------------------------------------------------------
// Copyright (c) 2010 Hung-Te Lin <piaip@csie.ntu.edu.tw>
// All rights reserved.
// Distributed under BSD license (GPL compatible).
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright
//     notice, this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
// --------------------------------------------------------------------------

static int
psb_default_header(PSB_CTX *ctx GCC_UNUSED) {
    vs_draw_hdr2("P&S Browser", BBSNAME);
    return 0;
}

static int
psb_default_footer(PSB_CTX *ctx GCC_UNUSED) {
    vs_footer(" 頁面瀏覽 ",
              " (↑/↓/PgUp/PgDn/0-9)移動 (Enter/→)選擇 \t(q/←)離開");
    return 0;
}

static int
psb_default_renderer(int i, PSB_CTX *ctx) {
    prints("   %s(Demo) %5d / %5d Item\n", (i == ctx->cmd.curr) ? "*" : " ", i, ctx->cmd.total);
    return 0;
}

static int
psb_default_cursor(int y, PSB_CTX *ctx GCC_UNUSED) {
    cursor_show(y, 0);
    return 0;
}

///////////////////////////////////////////////////////////////////////////
// Layer 0: PSB Base Navigation Commands

static int
psb_cmd_quit(cmd_ctx_t *ctx) {
    ctx->quit = true;
    return 0;
}

static int
psb_cmd_home(cmd_ctx_t *ctx) {
    ctx->curr = 0;
    return 0;
}

static int
psb_cmd_end(cmd_ctx_t *ctx) {
    ctx->curr = (ctx->total > 0) ? ctx->total - 1 : 0;
    return 0;
}

static int
psb_cmd_pgup(cmd_ctx_t *ctx) {
    if (ctx->rows > 0 && ctx->curr / ctx->rows > 0)
        ctx->curr -= ctx->rows;
    else
        ctx->curr = 0;
    return 0;
}

static int
psb_cmd_pgdn(cmd_ctx_t *ctx) {
    if (ctx->rows > 0 && ctx->curr + ctx->rows < ctx->total)
        ctx->curr += ctx->rows;
    else
        ctx->curr = (ctx->total > 0) ? ctx->total - 1 : 0;
    return 0;
}

static int
psb_cmd_up(cmd_ctx_t *ctx) {
    if (ctx->curr > 0)
        ctx->curr--;
    return 0;
}

static int
psb_cmd_down(cmd_ctx_t *ctx) {
    if (ctx->curr + 1 < ctx->total)
        ctx->curr++;
    return 0;
}

static int
psb_cmd_num(cmd_ctx_t *ctx) {
    int key = ctx->key;
    if (key >= '0' && key <= '9') {
        int newval = search_num(key, ctx->total);
        ctx->redraw_footer_lines = 1;
        if (newval >= 0 && newval < ctx->total)
            ctx->curr = newval;
        return 0;
    }
    return PSB_NA;
}

const cmd_t psb_base_cmds[] = {
    { KEY_UP, NULL, "向上移動一列", psb_cmd_up, 0, CMD_PRIO_NONE, true },
    { KEY_DOWN, NULL, "向下移動一列", psb_cmd_down, 0, CMD_PRIO_NONE, true },
    { 'k', NULL, NULL, psb_cmd_up, 0, CMD_PRIO_NONE, true },
    { 'p', NULL, NULL, psb_cmd_up, 0, CMD_PRIO_NONE, true },
    { Ctrl('P'), NULL, NULL, psb_cmd_up, 0, CMD_PRIO_NONE, true },
    { 'j', NULL, NULL, psb_cmd_down, 0, CMD_PRIO_NONE, true },
    { 'n', NULL, NULL, psb_cmd_down, 0, CMD_PRIO_NONE, true },
    { Ctrl('N'), NULL, NULL, psb_cmd_down, 0, CMD_PRIO_NONE, true },
    { KEY_PGUP, "上頁", "向上翻一頁", psb_cmd_pgup, 0, CMD_PRIO_NAV, true },
    { KEY_PGDN, "下頁", "向下翻一頁", psb_cmd_pgdn, 0, CMD_PRIO_NAV, true },
    { ' ', NULL, NULL, psb_cmd_pgdn, 0, CMD_PRIO_NONE, true },
    { Ctrl('B'), NULL, NULL, psb_cmd_pgup, 0, CMD_PRIO_NONE, true },
    { 'P', NULL, NULL, psb_cmd_pgup, 0, CMD_PRIO_NONE, true },
    { Ctrl('F'), NULL, NULL, psb_cmd_pgdn, 0, CMD_PRIO_NONE, true },
    { 'N', NULL, NULL, psb_cmd_pgdn, 0, CMD_PRIO_NONE, true },
    { KEY_HOME, NULL, "移至第一筆", psb_cmd_home, 0, CMD_PRIO_NONE, true },
    { '0', NULL, NULL, psb_cmd_home, 0, CMD_PRIO_NONE, true },
    { KEY_END, NULL, "移至最後一筆", psb_cmd_end, 0, CMD_PRIO_NONE, true },
    { '$', NULL, NULL, psb_cmd_end, 0, CMD_PRIO_NONE, true },
    { '1', "跳號", "輸入編號跳轉", psb_cmd_num, 0, CMD_PRIO_NAV, true },
    { '2', NULL, NULL, psb_cmd_num, 0, CMD_PRIO_NONE, true },
    { '3', NULL, NULL, psb_cmd_num, 0, CMD_PRIO_NONE, true },
    { '4', NULL, NULL, psb_cmd_num, 0, CMD_PRIO_NONE, true },
    { '5', NULL, NULL, psb_cmd_num, 0, CMD_PRIO_NONE, true },
    { '6', NULL, NULL, psb_cmd_num, 0, CMD_PRIO_NONE, true },
    { '7', NULL, NULL, psb_cmd_num, 0, CMD_PRIO_NONE, true },
    { '8', NULL, NULL, psb_cmd_num, 0, CMD_PRIO_NONE, true },
    { '9', NULL, NULL, psb_cmd_num, 0, CMD_PRIO_NONE, true },
    { KEY_LEFT, "離開", "離開本畫面", psb_cmd_quit, 0, CMD_PRIO_MAX },
    { 'q', NULL, NULL, psb_cmd_quit, 0, CMD_PRIO_NONE },
    { 'Q', NULL, NULL, psb_cmd_quit, 0, CMD_PRIO_NONE },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

///////////////////////////////////////////////////////////////////////////
// Layer 1: BBS Global Common Commands

static int
bbs_cmd_za(cmd_ctx_t *ctx) {
    if (ZA_Select())
        ctx->quit = true;
    else
        ctx->redraw = true;
    return 0;
}

static int
bbs_cmd_ctrl_u(cmd_ctx_t *ctx) {
    if (currutmp && currutmp->mode != EDITING && currutmp->mode != LUSERS && currutmp->mode) {
        t_users();
        ctx->redraw = true;
    }
    return 0;
}

static int
bbs_cmd_ctrl_r(cmd_ctx_t *ctx) {
    ofo_my_write();
    ctx->redraw = true;
    return 0;
}

const cmd_t bbs_global_cmds[] = {
    { Ctrl('Z'), "隨處切換(ZA)", "快速切換到文章列表、分類、信箱、使用者名單等", bbs_cmd_za, 0, CMD_PRIO_NONE },
    { Ctrl('U'), "線上使用者", "查看線上使用者名單", bbs_cmd_ctrl_u, PERM_BASIC, CMD_PRIO_NONE },
    { Ctrl('R'), "回應水球", "即時回應剛收到的水球", bbs_cmd_ctrl_r, 0, CMD_PRIO_NONE },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

bool
psb_check_perm(int perm) {
    if (!perm)
        return true;
    if ((perm & PERM_BM) && ((currmode & MODE_BOARD) || HasUserPerm(PERM_SYSOP)))
        return true;
    if ((perm & PERM_SYSSUBOP) && (HasUserPerm(PERM_SYSOP) || GROUPOP()))
        return true;
    if ((perm & PERM_SYSSUPERSUBOP) &&
        (HasUserPerm(PERM_SYSOP) || (HasUserPerm(PERM_SYSSUPERSUBOP) && GROUPOP())))
        return true;
    perm &= ~(PERM_BM | PERM_SYSSUBOP | PERM_SYSSUPERSUBOP);
    if (!perm)
        return false;
    if (perm == PERM_BASIC)
        return HasUserPerm(PERM_BASIC);
    return HasBasicUserPerm(perm);
}

static int
cmd_dispatch_single_layer(const cmd_layer_t *layer, cmd_ctx_t *ctx, void *default_priv) {
    const cmd_t *cmd;
    if (!layer || !layer->cmds)
        return PSB_NA;
    ctx->priv = layer->priv ? layer->priv : default_priv;

    for (cmd = layer->cmds; cmd->key || cmd->func; cmd++) {
        if (cmd->key != ctx->key || !cmd->func || !psb_check_perm(cmd->permission) ||
            (cmd->need_item && ctx->total <= 0))
            continue;
        if (cmd->func(ctx) == PSB_NA)
            continue;
        return 0;
    }
    return PSB_NA;
}

int
cmd_dispatch_layers(const cmd_layer_t *layers, cmd_ctx_t *ctx,
                    const char *caption) {
    const cmd_layer_t *layer;
    void *default_priv = ctx->priv;
    bool redispatched = false;
    ctx->active_layers = layers;
    ctx->caption = caption;

    while (1) {
        int ret = PSB_NA;
        ctx->redispatch = false;
        for (layer = layers; layer && layer->cmds; layer++) {
            ret = cmd_dispatch_single_layer(layer, ctx, default_priv);
            if (ret != PSB_NA)
                break;
        }
        if (ctx->redispatch && !redispatched) {
            redispatched = true;
            continue;
        }
        ctx->priv = default_priv;
        return ret;
    }
}

static void
psb_build_default_layers(PSB_CTX *psbctx, cmd_layer_t *buf, size_t max_layers) {
    size_t idx = 0;
    if (psbctx->layers) {
        const cmd_layer_t *l = psbctx->layers;
        while (l->cmds && idx + 1 < max_layers) {
            buf[idx++] = *l++;
        }
    } else {
        if (psbctx->cmds && idx + 1 < max_layers) {
            buf[idx].cmds = psbctx->cmds;
            buf[idx].priv = psbctx->cmd.priv;
            idx++;
        }
        if (idx + 1 < max_layers) {
            buf[idx].cmds = bbs_global_cmds;
            buf[idx].priv = NULL;
            idx++;
        }
        if (idx + 1 < max_layers) {
            buf[idx].cmds = psb_base_cmds;
            buf[idx].priv = NULL;
            idx++;
        }
    }
    buf[idx].cmds = NULL;
    buf[idx].priv = NULL;
}

static void
psb_init_defaults(PSB_CTX *psbctx) {
    // pre-setup
    assert(psbctx);
    if (!psbctx->header)
        psbctx->header = psb_default_header;
    if (!psbctx->footer)
        psbctx->footer = psb_default_footer;
    if (!psbctx->renderer)
        psbctx->renderer = psb_default_renderer;
    if (!psbctx->cursor)
        psbctx->cursor = psb_default_cursor;

    psbctx->cmd.reload = true;
    psbctx->cmd.redraw = true;
    psbctx->cmd.redraw_header_lines = 0;
    psbctx->cmd.redraw_footer_lines = 0;
    psbctx->cmd.quit = false;
    psbctx->cached_base = -1;

    if (psbctx->cmd.total < 0)
        psbctx->cmd.total = 0;
    if (psbctx->cmd.total == 0)
        psbctx->cmd.curr = 0;
    else if (psbctx->cmd.curr >= psbctx->cmd.total)
        psbctx->cmd.curr = psbctx->cmd.total - 1;
    else if (psbctx->cmd.curr < 0)
        psbctx->cmd.curr = 0;
    assert(psbctx->header_lines > 0 &&
           psbctx->footer_lines);
}

int
psb_file_loader(PSB_CTX *psbctx) {
    if (!psbctx->filename || !psbctx->item_size)
        return 0;
    if (psbctx->cmd.reload) {
        psbctx->cmd.total = get_num_records(psbctx->filename, psbctx->item_size);
        return 0;
    }
    if (psbctx->window_buf && psbctx->cmd.total > 0) {
        get_records(psbctx->filename, psbctx->window_buf,
                    psbctx->item_size, psbctx->cmd.base + 1, psbctx->cmd.rows);
    }
    return 0;
}

void
psb_sync_cache(PSB_CTX *psbctx) {
    int rows = psbctx->cmd.rows;
    bool force = psbctx->cmd.reload;

    if (force) {
        psbctx->cached_base = -1;
        psbctx->cmd.redraw = true;
        if (psbctx->loader)
            psbctx->loader(psbctx);
        psbctx->cmd.reload = false;
        rows = t_lines - psbctx->header_lines - psbctx->footer_lines;
        assert(rows > 0);
        psbctx->cmd.rows = rows;
    }
    if (psbctx->cmd.quit)
        return;

    if (psbctx->cmd.total <= 0) {
        psbctx->cmd.curr = 0;
        psbctx->cmd.base = 0;
    } else {
        if (psbctx->cmd.curr >= psbctx->cmd.total)
            psbctx->cmd.curr = psbctx->cmd.total - 1;
        if (psbctx->cmd.curr < 0)
            psbctx->cmd.curr = 0;
        if (psbctx->cmd.curr < psbctx->cmd.base || psbctx->cmd.curr >= psbctx->cmd.base + rows)
            psbctx->cmd.base = (psbctx->cmd.curr / rows) * rows;
    }

    if (psbctx->col_measurer && psbctx->cols > 0) {
        if (force || t_columns != psbctx->cached_cols) {
            int sum_w = 0;
            for (int c = 0; c < psbctx->cols && c < PSB_MAX_COLS; c++) {
                int max_w = psbctx->col_measurer(-1, c, psbctx);
                for (int i = 0; i < psbctx->cmd.total; i++) {
                    int w = psbctx->col_measurer(i, c, psbctx);
                    if (w > max_w)
                        max_w = w;
                }
                psbctx->col_widths[c] = max_w;
                sum_w += max_w;
            }
            int avail = t_columns - psbctx->col_paddings;
            if (avail > 0 && sum_w > avail) {
                int last = (psbctx->cols < PSB_MAX_COLS ? psbctx->cols : PSB_MAX_COLS) - 1;
                int prev_w = sum_w - psbctx->col_widths[last];
                if (avail - prev_w >= 8) {
                    psbctx->col_widths[last] = avail - prev_w;
                } else {
                    psbctx->col_widths[last] = 8;
                    if (last > 0 && avail > 8)
                        psbctx->col_widths[0] = avail - 8;
                }
            }
        }
    }

    if (psbctx->cmd.base != psbctx->cached_base ||
        rows != psbctx->cached_rows ||
        t_columns != psbctx->cached_cols) {
        if (psbctx->loader &&
            (psbctx->cmd.base != psbctx->cached_base || rows != psbctx->cached_rows))
            psbctx->loader(psbctx);
        if (psbctx->cmd.quit)
            return;
        psbctx->cached_base = psbctx->cmd.base;
        psbctx->cached_rows = rows;
        psbctx->cached_cols = t_columns;
        psbctx->cmd.redraw = true;
    }
}

static void
psb_on_select(cmd_ctx_t *ctx, int new_curr) {
    PSB_CTX *psbctx = container_of(ctx, PSB_CTX, cmd);
    if (psbctx->cmd.total <= 0)
        return;
    int old_curr = psbctx->cmd.curr;
    psbctx->cmd.curr = new_curr;
    if (old_curr != new_curr) {
        int base = psbctx->cmd.base;
        int rows = psbctx->cmd.rows;
        if (old_curr >= base && old_curr < base + rows && old_curr < psbctx->cmd.total) {
            int y = psbctx->header_lines + (old_curr - base);
            cursor_clear(y, 0);
            move(y, 0);
            clrtoeol();
            psbctx->renderer(old_curr, psbctx);
        }
        if (new_curr >= base && new_curr < base + rows && new_curr < psbctx->cmd.total) {
            int y = psbctx->header_lines + (new_curr - base);
            move(y, 0);
            clrtoeol();
            psbctx->renderer(new_curr, psbctx);
            move(y, 0);
            psbctx->cursor(y, psbctx);
        }
        refresh();
    }
}

int
psb_main(PSB_CTX *psbctx)
{
    cmd_layer_t active_layers[PSB_MAX_CMD_LAYERS];
    int old_curr = -1;

    psb_init_defaults(psbctx);

    while (!psbctx->cmd.quit) {
        int i;
        int rows = t_lines - psbctx->header_lines - psbctx->footer_lines;
        int base;

        assert(rows > 0);
        psbctx->cmd.rows = rows;
        psb_sync_cache(psbctx);
        if (psbctx->cmd.quit)
            break;
        rows = psbctx->cmd.rows;
        psb_build_default_layers(psbctx, active_layers, PSB_MAX_CMD_LAYERS);

        base = psbctx->cmd.base;
        bool full = psbctx->cmd.redraw;
        bool draw_header = full || (psbctx->cmd.redraw_header_lines > 0) ||
                           (psbctx->cmd.redraw_footer_lines > t_lines - psbctx->header_lines);
        bool draw_footer = full || (psbctx->cmd.redraw_footer_lines > 0) ||
                           (psbctx->cmd.redraw_header_lines > t_lines - psbctx->footer_lines);
        int top_dirty = psbctx->cmd.redraw_header_lines - psbctx->header_lines;
        int bot_dirty = psbctx->cmd.redraw_footer_lines - psbctx->footer_lines;

        if (full)
            clear();

        if (draw_header) {
            move(0, 0);
            if (!full)
                clrtoln(psbctx->header_lines);
            psbctx->header(psbctx);
        }

        if (psbctx->cmd.total == 0) {
            if (full || top_dirty > 0) {
                move(psbctx->header_lines, 0);
                if (!full)
                    clrtoln(psbctx->header_lines + rows);
                if (psbctx->empty_renderer) {
                    psbctx->empty_renderer(psbctx);
                }
            }
        } else {
            int max_i = psbctx->cmd.total - base;
            if (max_i > rows)
                max_i = rows;
            for (i = 0; i < max_i; i++) {
                bool dirty_row = full ||
                                 (i < top_dirty) ||
                                 (i >= rows - bot_dirty) ||
                                 (old_curr != psbctx->cmd.curr &&
                                  (base + i == old_curr || base + i == psbctx->cmd.curr));
                if (!dirty_row)
                    continue;
                if (!full && old_curr != psbctx->cmd.curr && base + i == old_curr)
                    cursor_clear(psbctx->header_lines + i, 0);
                move(psbctx->header_lines + i, 0);
                if (!full)
                    clrtoeol();
                psbctx->renderer(base + i, psbctx);
            }
            if (!full && max_i < rows) {
                move(psbctx->header_lines + max_i, 0);
                clrtoln(psbctx->header_lines + rows);
            }
        }

        if (draw_footer) {
            move(t_lines - psbctx->footer_lines, 0);
            if (!full)
                clrtobot();
            if (psbctx->footer)
                psbctx->footer(psbctx);

            if (psbctx->allow_pbs_version_message) {
                prints(ANSI_COLOR(0;1;30) "%*s" ANSI_RESET, t_columns-2,
                       "-- Powered by P&S Browser System");
            }
        }
        psbctx->cmd.redraw = false;
        psbctx->cmd.redraw_header_lines = 0;
        psbctx->cmd.redraw_footer_lines = 0;

        old_curr = psbctx->cmd.curr;
        if (psbctx->cmd.total > 0) {
            i = psbctx->header_lines + psbctx->cmd.curr - base;
            move(i, 0);
            psbctx->cursor(i, psbctx);
        } else {
            i = psbctx->header_lines;
            move(i, 0);
            psbctx->cursor(i, psbctx);
        }
        int vis = psbctx->cmd.total - base;
        if (vis > rows) vis = rows;
        if (vis < 0) vis = 0;
        psbctx->cmd.header_lines = psbctx->header_lines;
        psbctx->cmd.visible_rows = vis;
        psbctx->cmd.on_select = psb_on_select;
        int old_newmail = ISNEWMAIL(currutmp);
        psbctx->cmd.key = vkey();
        if (psbctx->cmd.key == EOF) {
            psbctx->cmd.quit = true;
            break;
        }

        int ret = PSB_NA;
        if (psbctx->on_key) {
            ret = psbctx->on_key(psbctx);
        }
        if (ret == PSB_NA) {
            ret = cmd_dispatch_layers(active_layers, &psbctx->cmd, psbctx->cmd.caption);
        }

        if (ISNEWMAIL(currutmp) != old_newmail && !psbctx->cmd.redraw && !psbctx->cmd.reload) {
            psbctx->cmd.redraw_header_lines = psbctx->header_lines;
        }
        if (ZA_Waiting()) {
            psbctx->cmd.quit = true;
        }
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////
// Time Capsule: Edit History

#ifndef PVEH_LIMIT_NUMBER
#define PVEH_LIMIT_NUMBER   (199)
#endif

typedef struct {
    const char *subject;
    const char *filebase;
    int leave_for_recycle_bin;
    int rev_base;
    int base_as_current;
    time4_t *timestamps;
} pveh_ctx;

static int
pveh_header(PSB_CTX *ctx) {
    pveh_ctx *cx = (pveh_ctx*) ctx->cmd.priv;
    vs_draw_hdr2("【" TIME_CAPSULE_NAME ": 編輯歷史】", cx->subject);
    move(1, 0);
    outs("請注意本系統不會永久保留所有的編輯歷史。");
    outs("\n");
    return 0;
}

static int
pveh_footer(PSB_CTX *ctx GCC_UNUSED) {
    vs_footer(" 編輯歷史 ",
              " (↑↓)移動 (Enter/r/→)選擇 (x)存入信箱 "
              "(~)" RECYCLE_BIN_NAME
              "\t(q/←)跳出");
    move(b_lines-1, 0);
    return 0;
}

static void
pveh_solve_rev_filename(int rev, int i, char *fname, size_t sz_fname,
                        pveh_ctx *cx) {
    if (cx->base_as_current && i == 0)
        strlcpy(fname, cx->filebase, sz_fname);
    else
        timecapsule_get_by_revision(
                cx->filebase, rev + cx->rev_base, fname, sz_fname);
}

static int
pveh_renderer(int i, PSB_CTX *ctx) {
    const char *subject = "";
    char fname[PATHLEN];
    time4_t ftime = 0;
    const time4_t INVALID_TS = -1;
    pveh_ctx *cx = (pveh_ctx*) ctx->cmd.priv;
    int curr = ctx->cmd.curr, total = ctx->cmd.total;
    int rev = total - i; // i/curr = 0 based, rev = 1 based

    if (cx->timestamps[i] == 0) {
        pveh_solve_rev_filename(rev, i, fname, sizeof(fname), cx);
        ftime = dasht(fname);
        // dasht returns 0 (same as 'never read' here) so we have to give ftime
        // a special value so it won't re-read next time.
        if (!ftime)
            ftime = INVALID_TS;
        cx->timestamps[i] = ftime;
    } else {
        ftime = cx->timestamps[i];
    }

    // ftime=-1 means 'invalid, but we've checked'. Check explicitly so we don't
    // need to worry if time4_is_valid considered -1 as ok or not.
    bool ts_is_valid = time4_is_valid(ftime) && (ftime != INVALID_TS);
    if (ts_is_valid)
        subject = Cdate(&ftime);
    else
        subject = "(記錄已過保留期限/已清除)";

    prints("   %s%s  版本: ",
           (i == curr) ? ANSI_COLOR(1;41;37) : "",
           ts_is_valid ? "" : ANSI_COLOR(1;30));
    if (cx->base_as_current && i == 0)
        outs("[目前版本]");
    else
        prints("#%09d", rev + cx->rev_base);
    prints("  時間: %-*s" ANSI_RESET "\n", t_columns - 31, subject);
    return 0;
}

static int
pveh_cmd_view(cmd_ctx_t *ctx) {
    char fname[PATHLEN];
    pveh_ctx *cx = (pveh_ctx *)ctx->priv;
    int rev = ctx->total - ctx->curr;
    pveh_solve_rev_filename(rev, ctx->curr, fname, sizeof(fname), cx);
    more(fname, YEA);
    ctx->redraw = true;
    return 0;
}

static int
pveh_cmd_recycle_bin(cmd_ctx_t *ctx) {
    pveh_ctx *cx = (pveh_ctx *)ctx->priv;
    cx->leave_for_recycle_bin = 1;
    ctx->quit = true;
                return 0;
}

static int
pveh_cmd_mail(cmd_ctx_t *ctx) {
    char fname[PATHLEN];
    char ans[3];
    pveh_ctx *cx = (pveh_ctx *)ctx->priv;
    int rev = ctx->total - ctx->curr;
    pveh_solve_rev_filename(rev, ctx->curr, fname, sizeof(fname), cx);
    getdata(b_lines-2, 0, "確定要把此份文件回存至信箱嗎? [y/N]: ",
            ans, sizeof(ans), LCECHO);
    if (*ans == 'y') {
        if (mail_send_file(cuser.userid, cx->subject,
                           fname, RECYCLE_BIN_OWNER) == 0) {
            vmsg("儲存完成，請至信箱檢查備忘錄信件");
        } else
            vmsg("儲存失敗，請至 " BN_BUGREPORT " 看板報告，謝謝");
    }
    ctx->redraw_footer_lines = 3;
    return 0;
}

static const cmd_t pveh_cmds[] = {
    { KEY_ENTER, "選擇", "檢視此版本的歷史內容", pveh_cmd_view, 0, CMD_PRIO_MAX, true },
    { KEY_RIGHT, NULL, NULL, pveh_cmd_view, 0, CMD_PRIO_NONE, true },
    { 'r', NULL, NULL, pveh_cmd_view, 0, CMD_PRIO_NONE, true },
    { 'x', "存入信箱", "將此份歷史文件回存至個人信箱", pveh_cmd_mail, 0, CMD_PRIO_HIGH, true },
    { '~', RECYCLE_BIN_NAME, "切換至資源回收筒", pveh_cmd_recycle_bin, 0, CMD_PRIO_HIGH },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static int
pveh_welcome() {
    // warning screen!
    static char is_first_enter_pveh = 1;

    if (is_first_enter_pveh) {
        is_first_enter_pveh = 0;
        clear();
        move(2, 0);
        outs(ANSI_COLOR(1;31)
"  歡迎使用 Time Capsule 的編輯歷史瀏覽系統!\n\n" ANSI_RESET
"  提醒您: (1) 所有的資料僅供參考，站方不保證此處為完整的電磁記錄。\n\n"
"          (2) 所有的資料都可能不定期由系統清除掉。\n"
"              無編輯歷史不能代表沒有編輯過，也可能是被清除了\n\n"
"   Mini FAQ:\n\n"
"   Q: 怎樣才會有歷史記錄 (增加版本數)?\n"
"   A: 在系統更新後每次使用 E 編輯文章並存檔就會有記錄。推文不會增加記錄版本數\n\n"
"   Q: 通常歷史會保留多久?\n"
"   A: 看板約3~4週，信箱約2~3週\n\n"
"   Q: 檔案被刪了也可以看歷史嗎?\n"
"   A: 屍體還在看板上可以直接對<本文已被刪除>按，不然就先進"
       RECYCLE_BIN_NAME "(~)再找\n"
            );
        doupdate();
        pressanykey();
    }
    return 0;
}


int
psb_view_edit_history(const char *base, const char *subject,
                      int maxrev, int current_as_base) {
    pveh_ctx pvehctx = {
        .subject = subject,
        .filebase = base,
        .rev_base = 0,
        .base_as_current = current_as_base,
    };
    PSB_CTX ctx = {
        .cmd = {
            .curr = 0,
            .total = maxrev + pvehctx.base_as_current,
            .priv = (void*)&pvehctx,
            .caption = " 編輯歷史 ",
        },
        .header_lines = 3,
        .footer_lines = 2,
        .allow_pbs_version_message = 1,
        .header = pveh_header,
        .footer = pveh_footer,
        .renderer = pveh_renderer,
        .cmds = pveh_cmds,
    };

    pveh_welcome();

    if (maxrev > PVEH_LIMIT_NUMBER) {
        pvehctx.rev_base = maxrev - PVEH_LIMIT_NUMBER;
        ctx.cmd.total -= pvehctx.rev_base;
    }

    pvehctx.timestamps = (time4_t*) malloc (sizeof(time4_t) * ctx.cmd.total);
    if (!pvehctx.timestamps) {
        vmsgf("內部錯誤，請至" BN_BUGREPORT "看板報告，謝謝");
        return FULLUPDATE;
    }
    // load on demand!
    memset(pvehctx.timestamps, 0, sizeof(time4_t) * ctx.cmd.total);

    psb_main(&ctx);
    free(pvehctx.timestamps);
    return (pvehctx.leave_for_recycle_bin ?
            RET_RECYCLEBIN :
            FULLUPDATE);
}

///////////////////////////////////////////////////////////////////////////
// Time Capsule: Recycle Bin
#ifndef PVRB_LIMIT_NUMBER
#define PVRB_LIMIT_NUMBER   (103000/10)
#endif

typedef struct {
    const char *dirbase;
    const char *subject;
    int viewbase;
    fileheader_t *records;
} pvrb_ctx;

static int
pvrb_header(PSB_CTX *ctx) {
    pvrb_ctx *cx = (pvrb_ctx*) ctx->cmd.priv;
    vs_draw_hdr2("【" TIME_CAPSULE_NAME ": " RECYCLE_BIN_NAME "】", cx->subject);
    move(1, 0);
    outs("請注意此處的檔案將不定期清除。\n");
    vbar(ANSI_REVERSE "    編號 | 日 期 |   作  者   |   標      題");
    return 0;
}

static int
pvrb_footer(PSB_CTX *ctx GCC_UNUSED) {
    vs_footer(" 已刪檔案 ",
              " (↑/↓/PgUp/PgDn)移動 (Enter/r/→)選擇 (/a#n)搜尋 (x)存入信箱"
              "\t(q/←)跳出");
    move(b_lines-1, 0);
    return 0;
}

static int
pvrb_renderer(int i, PSB_CTX *ctx) {
    pvrb_ctx *cx = (pvrb_ctx*) ctx->cmd.priv;
    int curr = ctx->cmd.curr, total = ctx->cmd.total;
    fileheader_t *fh = &cx->records[total - i - 1];

    // TODO make this load-on-demand
    // quick display, but lack of recommend counter...
    outs("   ");
    if (i == curr)
        // prints(ANSI_COLOR(1;40;3%d), i%8);
        outs(ANSI_COLOR(1;40;31));
    prints("%06d  %-5.5s  %-12.12s %s" ANSI_RESET "\n",
           total - i, fh->date, fh->owner, fh->title);
    return 0;
}

static int
pvrb_search(char key, int curr, int total, pvrb_ctx *cx) {
    fileheader_t *fh;
    static char search_str[FNLEN] = "";
    static char search_cmd = 0;
    const char *prompt = "";
    const char *aid_str = NULL;
    aidu_t aidu = 0;
    int need_input = 1;

    if (key == 'n') {
        key = search_cmd;
        if (key) {
            need_input = 0;
            if (curr + 1 < total)
                curr ++;
        } else
            key = '/';
    }

    if (key == '#')
        prompt = "請輸入文章代碼: #";
    else if (key == '/')
        prompt = "請輸入標題關鍵字: ";
    else if (key == 'a')
        prompt = "請輸入作者關鍵字: ";
    else {
        assert(!"unknown search command");
        return PSB_NA;
    }

    assert(sizeof(search_str) >= FNLEN);
    if (need_input &&
        getdata(b_lines-1, 0, prompt, search_str, FNLEN, DOECHO) < 1)
        return PSB_NA;

    // cache for next cache.
    search_cmd = key;

    if (key == '#') {
        // AID search is very special, we have to search from begin to end.
        curr = 0;
        aid_str = search_str;
        while (*aid_str == ' ' || *aid_str == '#')
            aid_str++;
        aidu = aidc2aidu(aid_str);
        if (!aidu)
            return -2;
    }

    // the records was in reversed ordering
    for (; curr < total; curr++) {
        fh = &cx->records[total - curr - 1];
        if ((key == '/' && mbs_strcasestr(fh->title, search_str)) ||
            (key == 'a' && mbs_strcasestr(fh->owner, search_str)) ||
            (key == '#' && fn2aidu(fh->filename) == aidu)) {
            // found something. return as current index.
            return curr;
        }
    }
    return -2;
}

static int
pvrb_cmd_view(cmd_ctx_t *ctx) {
    char fname[PATHLEN];
    int maxrev;
    pvrb_ctx *cx = (pvrb_ctx *)ctx->priv;
    fileheader_t *fh = &cx->records[ctx->total - ctx->curr - 1];
    const char *err_no_rev = "抱歉，本文歷史資料已被系統清除。";

    setdirpath(fname, cx->dirbase, fh->filename);
    maxrev = timecapsule_get_max_revision_number(fname);
    if (maxrev == 1) {
        char revfname[PATHLEN];
        timecapsule_get_by_revision(
                fname, 1, revfname, sizeof(revfname));
        more(revfname, YEA);
        ctx->redraw = true;
    } else if (maxrev > 1) {
        psb_view_edit_history(fname, fh->title, maxrev, 0);
        ctx->redraw = true;
    } else {
        vmsg(err_no_rev);
        ctx->redraw_footer_lines = 1;
    }
    return 0;
}

static int
pvrb_cmd_mail(cmd_ctx_t *ctx) {
    char fname[PATHLEN];
    int maxrev;
    pvrb_ctx *cx = (pvrb_ctx *)ctx->priv;
    fileheader_t *fh = &cx->records[ctx->total - ctx->curr - 1];
    const char *err_no_rev = "抱歉，本文歷史資料已被系統清除。";

    setdirpath(fname, cx->dirbase, fh->filename);
    maxrev = timecapsule_get_max_revision_number(fname);
    if (maxrev < 1) {
        vmsg(err_no_rev);
        ctx->redraw_footer_lines = 1;
    } else {
        char revfname[PATHLEN];
        char ans[3];
        timecapsule_get_by_revision(
                fname, maxrev, revfname, sizeof(revfname));
        getdata(b_lines-2, 0, "確定要把此份文件回存至信箱嗎? [y/N]: ",
                ans, sizeof(ans), LCECHO);
        if (*ans == 'y') {
            if (mail_send_file(cuser.userid, fh->title,
                               revfname, RECYCLE_BIN_OWNER) == 0) {
                vmsg("儲存完成，請至信箱檢查備忘錄信件");
            } else {
                vmsg("儲存失敗，請至 " BN_BUGREPORT " 看板報告，謝謝");
                ctx->quit = true;
                return 0;
            }
        }
        ctx->redraw_footer_lines = 3;
    }
    return 0;
}

static int
pvrb_cmd_search(cmd_ctx_t *ctx) {
    pvrb_ctx *cx = (pvrb_ctx *)ctx->priv;
    int newloc = pvrb_search(ctx->key, ctx->curr, ctx->total, cx);
    ctx->redraw_footer_lines = 2;
    if (newloc == PSB_NA)
        return 0;
    if (newloc >= 0)
        ctx->curr = newloc;
    else
        vmsg("找不到符合的資料。");
    return 0;
}

static const cmd_t pvrb_cmds[] = {
    { KEY_ENTER, "選擇", "檢視回收筒文章內容或歷史版本", pvrb_cmd_view, 0, CMD_PRIO_MAX, true },
    { KEY_RIGHT, NULL, NULL, pvrb_cmd_view, 0, CMD_PRIO_NONE, true },
    { 'r', NULL, NULL, pvrb_cmd_view, 0, CMD_PRIO_NONE, true },
    { 'x', "存入信箱", "將文章最新版本存入個人信箱", pvrb_cmd_mail, 0, CMD_PRIO_HIGH, true },
    { '/', "搜尋", "搜尋標題關鍵字", pvrb_cmd_search, 0, CMD_PRIO_NORM, true },
    { 'a', NULL, "搜尋作者帳號", pvrb_cmd_search, 0, CMD_PRIO_NONE, true },
    { '#', NULL, "搜尋文章代碼 (AID)", pvrb_cmd_search, 0, CMD_PRIO_NONE, true },
    { 'n', NULL, "尋找下一筆符合項目", pvrb_cmd_search, 0, CMD_PRIO_NONE, true },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static int
pvrb_welcome() {
    // warning screen!
    static char is_first_enter_pvrb = 1;

    if (is_first_enter_pvrb) {
        is_first_enter_pvrb = 0;
        clear();
        move(2, 0);
        outs(ANSI_COLOR(1;36)
"  歡迎使用 " TIME_CAPSULE_NAME " " RECYCLE_BIN_NAME "!\n\n" ANSI_RESET
"  提醒您: (1) 所有的資料僅供參考，站方不保證此處為完整的電磁記錄。\n"
"          (2) 所有的資料都可能不定期由系統清除掉。\n"
"          (3) 目前提供簡易的單向搜尋: / a # 分別可搜尋標題/作者/"
              "AID文章代碼\n"
"              n 可以找下一個符合搜尋的項目 (暫無向前搜尋)\n" ANSI_RESET
"   Mini FAQ:\n\n"
"   Q: 要轉回個人信箱真麻煩! 為什麼不能作的方便點?\n"
"   A: 回收筒是提供少量救援及查驗用，刻意作成不便於大量回復以避免濫用。\n\n"
"   Q: 通常檔案會保留多久?\n"
"   A: 看板約3~4週，信箱約2~3週，另外篇數也有上限(依系統負載動態調整)。\n\n"
"   Q: 哪些地方有回收筒可用?\n"
"   A: 目前開放個人信箱(所有用戶)跟看板/精華區文章(板主限定)。\n"
"      精華區子目錄暫不支援。\n\n"
            );
        doupdate();
        pressanykey();
    }
    return 0;
}

int
psb_recycle_bin(const char *base, const char *title) {
    int nrecords = 0, viewbase = 0;
    pvrb_ctx pvrbctx = {
        .dirbase = base,
        .subject = title,
    };
    PSB_CTX ctx = {
        .cmd = {
            .curr = 0,
            .total = 0,
            .priv = (void*)&pvrbctx,
            .caption = " 已刪檔案 ",
        },
        .header_lines = 3,
        .footer_lines = 2,
        .allow_pbs_version_message = 1,
        .header = pvrb_header,
        .footer = pvrb_footer,
        .renderer = pvrb_renderer,
        .cmds = pvrb_cmds,
    };

    nrecords = timecapsule_get_max_archive_number(base, sizeof(fileheader_t));
    if (!nrecords) {
        vmsg("目前" RECYCLE_BIN_NAME "內無任何內容。");
        return FULLUPDATE;
    }

    pvrb_welcome();

    // truncate on large size
    if (nrecords > PVRB_LIMIT_NUMBER) {
        viewbase = nrecords - PVRB_LIMIT_NUMBER;
        nrecords -= viewbase;
    }
    ctx.cmd.total = nrecords;

    pvrbctx.records = (fileheader_t*) malloc (sizeof(fileheader_t) * nrecords);
    if (!pvrbctx.records) {
        vmsgf("內部錯誤，請至" BN_BUGREPORT "看板報告，謝謝");
        return FULLUPDATE;
    }
    timecapsule_get_archive_blobs(base, viewbase, nrecords, pvrbctx.records,
                                  sizeof(fileheader_t));
    fileheader_storage_to_mem(pvrbctx.records, nrecords);
    psb_main(&ctx);
    free(pvrbctx.records);
    return DIRCHANGED;
}

///////////////////////////////////////////////////////////////////////////
// Comment Management

#ifdef USE_COMMENTD
typedef struct {
    void *cmctx;
} pvcm_ctx;

static int
pvcm_header(PSB_CTX *ctx GCC_UNUSED) {
    vs_draw_hdr2("【推文管理】", "");
    move(1, 0);
    vbar(ANSI_REVERSE "  編 號 | 作  者     | 內  容");
    return 0;
}

static int
pvcm_footer(PSB_CTX *ctx GCC_UNUSED) {
    vs_footer(" 推文 ",
              " (↑/↓/PgUp/PgDn)移動 (d)刪除 (U)快速水桶\t(q/←)跳出");
    move(b_lines-1, 0);
    return 0;
}

static int
pvcm_renderer(int i, PSB_CTX *ctx) {
    pvcm_ctx *cx = (pvcm_ctx*) ctx->cmd.priv;
    int curr = ctx->curr;
    const CommentBodyReq *resp = CommentsRead(cx->cmctx, i);
    if (!resp)
        return 0;
    prints("%c %06d %-12.12s %s\n",
           (i == curr) ? '>' : ' ',
           i + 1,
           resp->userid,
           (resp->type >= 0) ? resp->msg : (ANSI_COLOR(0;30;47) "<已刪>" ANSI_RESET));
    return 0;
}

static int
pvcm_cmd_delete(cmd_ctx_t *ctx) {
    pvcm_ctx *cx = (pvcm_ctx *)ctx->priv;
    char reason[40];
    const CommentBodyReq *resp = CommentsRead(cx->cmctx, ctx->curr);
    if (!resp || resp->type < 0)
        return 0;
    if (!getdata(b_lines-2, 0, "請輸入刪除原因: ",
                 reason, sizeof(reason), DOECHO)) {
        ctx->redraw_footer_lines = 3;
        return 0;
    }
    if (vans("確定要刪除嗎？ (y/N) ") == 'y') {
        if (CommentsDeleteFromTextFile(cx->cmctx, ctx->curr, reason) != 0) {
            vmsg("刪除失敗。可能原文已被修改。");
        }
        ctx->redraw = true;
    } else {
        ctx->redraw_footer_lines = 3;
    }
    return 0;
}

static int
pvcm_cmd_acl(cmd_ctx_t *ctx) {
    pvcm_ctx *cx = (pvcm_ctx *)ctx->priv;
    const CommentKeyReq *key = CommentsGetKeyReq(cx->cmctx);
    const CommentBodyReq *resp = CommentsRead(cx->cmctx, ctx->curr);
    if (resp)
        edit_user_acl_for_board(resp->userid, key->board);
    ctx->redraw = true;
    return 0;
}

static const cmd_t pvcm_cmds[] = {
    { 'd', "刪除", "刪除選取的推文並記錄原因", pvcm_cmd_delete, 0, CMD_PRIO_MAX, true },
    { KEY_DEL, NULL, NULL, pvcm_cmd_delete, 0, CMD_PRIO_NONE, true },
    { 'U', "快速水桶", "設定該推文作者的看板水桶權限", pvcm_cmd_acl, 0, CMD_PRIO_HIGH, true },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static int
pvcm_welcome() {
    clear();
    vs_hdr2("刪除推文", "警告");
    move(2, 0);
    // This must be a outs because we have '%' inside.
    outs(ANSI_COLOR(1;31)
"  這是刪除推文的界面。\n\n" ANSI_RESET
"  提醒您: (1) 刪推文界面顯示的內容是來自於獨立的資料庫，所以不會有\n"
"              原作者修文假造推文內容的問題。但也因此，若原推文被修改\n"
"              使得內容不同時(或是假推文)則此界面就無法刪除。\n\n"
"          (2) 刪推文會從檔案前面開始找看起來作者跟內文相同的第一筆。\n"
"              目前沒辦法100%確認找到正確的位置，但起碼內文是相同的。\n\n"
        "");
    pressanykey();
    return 0;
}

int
psb_comment_manager(const char *board, const char *file) {
    pvcm_ctx pvcmctx = {
        NULL,
    };
    PSB_CTX ctx = {
        .cmd = {
            .curr = 0,
            .total = 0,
            .priv = (void*)&pvcmctx,
            .caption = " 推文管理 ",
        },
        .header_lines = 2,
        .footer_lines = 2,
        .allow_pbs_version_message = 0,
        .header = pvcm_header,
        .footer = pvcm_footer,
        .renderer = pvcm_renderer,
        .cmds = pvcm_cmds,
    };
    pvcmctx.cmctx = CommentsOpen(board, file);
    if (!pvcmctx.cmctx) {
        vmsg("系統錯誤，請至 " BN_BUGREPORT " 報告。");
        return FULLUPDATE;
    }
    ctx.cmd.total = CommentsGetCount(pvcmctx.cmctx);
    if (ctx.cmd.total){
        pvcm_welcome();
        psb_main(&ctx);
    } else {
        vmsg("此文章無推文資料。");
    }
    CommentsClose(pvcmctx.cmctx);
    return DIRCHANGED;
}
#endif

///////////////////////////////////////////////////////////////////////////
// Admin Edit

// Since admin edit is usually rarely used, no need to write dynamic allocation
// for it.
#ifndef MAX_PAE_ENTRIES
#define MAX_PAE_ENTRIES (256)
#endif

typedef struct {
    char *descs[MAX_PAE_ENTRIES];
    char *files[MAX_PAE_ENTRIES];
} pae_ctx;

static int
pae_header(PSB_CTX *ctx GCC_UNUSED) {
    vs_draw_hdr2("【系統檔案】", " 編輯系統檔案");
    outs("請選取要編輯的檔案後按 Enter 開始修改\n");
    vbar(TEMPFORMAT(STRLEN, ANSI_REVERSE
         "%5s %-36s%-30s", "編號", "名  稱", "檔  名"));
    return 0;
}

static int
pae_footer(PSB_CTX *ctx GCC_UNUSED) {
    vs_footer(" 編輯系統檔案 ",
              " (↑↓/0-9)移動 (Enter/e/r/→)編輯 (DEL/d)刪除 \t(q/←)跳出");
    move(b_lines-1, 0);
    return 0;
}

static int
pae_renderer(int i, PSB_CTX *ctx) {
    pae_ctx *cx = (pae_ctx*) ctx->cmd.priv;
    prints("  %3d %s%s%-36.36s " ANSI_COLOR(1;37) "%-30.30s" ANSI_RESET "\n",
            i+1,
            (i == ctx->cmd.curr) ? ANSI_COLOR(41) : "",
            dashf(cx->files[i]) ? ANSI_COLOR(1;36) : ANSI_COLOR(1;30),
            cx->descs[i], cx->files[i]);
    return 0;
}

static int
pae_cmd_delete(cmd_ctx_t *ctx) {
    pae_ctx *cx = (pae_ctx *)ctx->priv;
    if (vans(TEMPFORMAT(STRLEN, "確定要刪除 %s 嗎？ (y/N) ", cx->descs[ctx->curr])) == 'y') {
        unlink(cx->files[ctx->curr]);
        ctx->redraw = true;
    } else {
        ctx->redraw_footer_lines = 1;
    }
    vmsgf("系統檔案[%s]: %s", cx->files[ctx->curr],
          !dashf(cx->files[ctx->curr]) ?  "刪除成功\ " : "未刪除");
    return 0;
}

static int
pae_cmd_edit(cmd_ctx_t *ctx) {
    pae_ctx *cx = (pae_ctx *)ctx->priv;
    int result = veditfile(cx->files[ctx->curr]);
    // log file change
    if (result != EDIT_ABORTED)
    {
        log_filef("log/etc_edit.log",
                  "%s %s # %s\n",
                  cuser.userid,
                  cx->files[ctx->curr],
                  cx->descs[ctx->curr]);
    }
    vmsgf("系統檔案[%s]: %s",
          cx->files[ctx->curr],
          (result == EDIT_ABORTED) ?  "未改變" : "更新完畢");
    // FN_CONF_BANIP should be $BBSHOME/etc/banip.conf
    if (str_ends_with(cx->files[ctx->curr], path_basename(FN_CONF_BANIP))) {
        test_banip_conf(cx->files[ctx->curr]);
    }
    ctx->redraw = true;
    return 0;
}

static const cmd_t pae_cmds[] = {
    { KEY_ENTER, "編輯", "編輯選取的系統檔案", pae_cmd_edit, 0, CMD_PRIO_MAX, true },
    { KEY_RIGHT, NULL, NULL, pae_cmd_edit, 0, CMD_PRIO_NONE, true },
    { 'r', NULL, NULL, pae_cmd_edit, 0, CMD_PRIO_NONE, true },
    { 'e', NULL, NULL, pae_cmd_edit, 0, CMD_PRIO_NONE, true },
    { 'E', NULL, NULL, pae_cmd_edit, 0, CMD_PRIO_NONE, true },
    { KEY_DEL, "刪除", "刪除選取的系統檔案", pae_cmd_delete, 0, CMD_PRIO_HIGH, true },
    { 'd', NULL, NULL, pae_cmd_delete, 0, CMD_PRIO_NONE, true },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

int
psb_admin_edit() {
    int i;
    char buf[PATHLEN*2];
    FILE *fp;
    pae_ctx paectx = { {0}, };
    PSB_CTX ctx = {
        .cmd = {
            .curr = 0,
            .total = 0,
            .priv = (void*)&paectx,
            .caption = " 系統檔案 ",
        },
        .header_lines = 4,
        .footer_lines = 2,
        .allow_pbs_version_message = 1,

        .header = pae_header,
        .footer = pae_footer,
        .renderer = pae_renderer,
        .cmds = pae_cmds,
    };

    fp = fopen(FN_CONF_EDITABLE, "rt");
    if (!fp) {
	// you can find a sample in sample/etc/editable
	vmsgf("未設定可編輯檔案列表[%s]，請洽系統站長。", FN_CONF_EDITABLE);
	return 0;
    }

    // load the editable file.
    // format: filename [ \t]* description
    while (ctx.cmd.total < MAX_PAE_ENTRIES &&
           fgets(buf, sizeof(buf), fp)) {
        char *k = buf, *v = buf;
        if (!*buf || strchr("#./ \t\n\r", *buf))
            continue;

        // change \t to ' '.
        while (*v) if (*v++ == '\t') *(v-1) = ' ';
        v = strchr(buf, ' ');
        if (v == NULL)
            continue;

        // see if someone is trying to crack
        k = strstr(buf, "..");
        if (k && k < v)
            continue;

	// reject anything outside etc/ folder.
        if (strncmp(buf, "etc/", strlen("etc/")) != 0)
            continue;

        // adjust spaces
        chomp(buf);
        k = buf; *v++ = 0;
        while (*v == ' ') v++;
        trim(k);
        trim(v);

        // add into context
        paectx.files[ctx.cmd.total] = strdup(k);
        paectx.descs[ctx.cmd.total] = strdup(v);
        ctx.cmd.total++;
    }
    if (ctx.cmd.total >= MAX_PAE_ENTRIES)
        vmsg("注意: 您的系統設定已超過或接近預設上限，請洽系統站長加大設定");

    psb_main(&ctx);

    for (i = 0; i < ctx.cmd.total; i++) {
        free(paectx.files[i]);
        free(paectx.descs[i]);
    }
    return 0;
}

