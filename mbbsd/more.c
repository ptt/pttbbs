#include "bbs.h"
#include "livepost.h"

/*
 * more.c
 * a mini pager in 130 lines of code, or stub of the huge pager pmore.
 * Author: Hung-Te Lin (piaip), April 2008.
 *
 * Copyright (c) 2008 Hung-Te Lin <piaip@csie.ntu.edu.tw>
 * All rights reserved.
 * Distributed under BSD license (GPL compatible).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 */

/*
 * Pager keys.
 *
 * more(fpath, YEA) is a plain pager: the pager's own keys (navigation,
 * search, ...) and pager_common_cmds (leave, help, ^T). Readers (posts,
 * mails, the announce) call more_ex() with their key bindings
 * (more_hooks_t.layers), which leave the pager by more_leave() with what
 * more_ex() returns to them, e.g. more_list_nav_cmds to read the next item.
 */

// The running pager: cmd_ctx_t.priv of the key bindings (whose layers have
// no priv of their own) and the ctx of the pmore callbacks.
typedef struct {
    bool at_top, at_end;    // the first (last) page is shown; kept by the pager
    const char *fpath;
    const more_hooks_t *hooks;
    int retval;             // more_ex() returns this once a key binding leaves
    int action;             // PAGER_DO_*: to do after leaving the pager
} pager_run_t;

#define PAGER_RUN(ctx) ((pager_run_t *)(ctx)->priv)

// The pager's own commands, which take the screen; they run after leaving
// pmore, see pager_run_action().
enum {
    PAGER_DO_NONE = 0,
    PAGER_DO_COPY2TMP,
    PAGER_DO_CHESS,
    PAGER_DO_BBSLUA,
};

#define PAGER_MAX_LAYERS (PSB_MAX_CMD_LAYERS)

void
more_leave(cmd_ctx_t *ctx, int retval)
{
    pager_run_t *run = PAGER_RUN(ctx);
    ctx->quit = true;
    run->retval = retval;
}

int
more_at_top(cmd_ctx_t *ctx)
{
    return PAGER_RUN(ctx)->at_top;
}

int
more_at_end(cmd_ctx_t *ctx)
{
    return PAGER_RUN(ctx)->at_end;
}

void *
more_caller_ctx(cmd_ctx_t *ctx)
{
    const more_hooks_t *hooks = PAGER_RUN(ctx)->hooks;
    return hooks ? hooks->ctx : NULL;
}

/*
 * Leaves the pager for the reader's live mode: shows the last page and fills
 * more_hooks_t.live (entered, ...). Returns false if there is no live mode.
 */
bool
more_leave_live(cmd_ctx_t *ctx)
{
#ifdef USE_PMORE
    const more_hooks_t *hooks = PAGER_RUN(ctx)->hooks;
    if (!hooks || !hooks->live)
        return false;
    pmore_go_live();
    if (!hooks->live->entered)
        return false;
    ctx->redraw = false;
    more_leave(ctx, FULLUPDATE);
    return true;
#else
    (void)ctx;
    return false;
#endif
}

static int
pager_leave_to(cmd_ctx_t *ctx, int action)
{
    more_leave(ctx, FULLUPDATE);
    PAGER_RUN(ctx)->action = action;
    return 0;
}

static int
pager_cmd_quit(cmd_ctx_t *ctx)
{
    ctx->quit = true;
    PAGER_RUN(ctx)->retval = FULLUPDATE;
    return 0;
}

static int
pager_cmd_copy2tmp(cmd_ctx_t *ctx)
{
    return pager_leave_to(ctx, PAGER_DO_COPY2TMP);
}

#ifdef USE_PMORE
static int pager_cmd_help(cmd_ctx_t *ctx);
#endif

static const cmd_t pager_common_cmds[] = {
    { KEY_LEFT, "離開", "離開目前文章", pager_cmd_quit, 0, CMD_PRIO_QUIT },
    { 'q', NULL, NULL, pager_cmd_quit, 0, CMD_PRIO_NONE },
#ifdef USE_PMORE
    { 'h', "說明", "顯示按鍵說明", pager_cmd_help, 0, CMD_PRIO_USAGE },
    { 'H', NULL, NULL, pager_cmd_help, 0, CMD_PRIO_NONE },
    { '?', NULL, NULL, pager_cmd_help, 0, CMD_PRIO_NONE },
#endif
    { Ctrl('T'), "存入暫存檔", "將目前文章存入個人暫存檔", pager_cmd_copy2tmp, PERM_BASIC, CMD_PRIO_LOW },
    { Ctrl('K'), NULL, NULL, pager_cmd_copy2tmp, PERM_BASIC, CMD_PRIO_NONE },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

// Shared key bindings for readers (more_ex) ////////////////////////////////

static int more_cmd_read_next(cmd_ctx_t *ctx) { more_leave(ctx, READ_NEXT); return 0; }
static int more_cmd_read_prev(cmd_ctx_t *ctx) { more_leave(ctx, READ_PREV); return 0; }
static int more_cmd_relate_next(cmd_ctx_t *ctx) { more_leave(ctx, RET_READ_RELATE_NEXT); return 0; }
static int more_cmd_relate_prev(cmd_ctx_t *ctx) { more_leave(ctx, RET_READ_RELATE_PREV); return 0; }
static int more_cmd_relate_first(cmd_ctx_t *ctx) { more_leave(ctx, RET_READ_RELATE_FIRST); return 0; }
static int more_cmd_author_next(cmd_ctx_t *ctx) { more_leave(ctx, RET_READ_AUTHOR_NEXT); return 0; }
static int more_cmd_author_prev(cmd_ctx_t *ctx) { more_leave(ctx, RET_READ_AUTHOR_PREV); return 0; }

// Past the ends: read the next (previous) item; else the pager scrolls.
static int
more_cmd_next_at_end(cmd_ctx_t *ctx)
{
    return more_at_end(ctx) ? more_cmd_read_next(ctx) : PSB_NA;
}

static int
more_cmd_prev_at_top(cmd_ctx_t *ctx)
{
    return more_at_top(ctx) ? more_cmd_read_prev(ctx) : PSB_NA;
}

static int
more_cmd_relate_next_at_end(cmd_ctx_t *ctx)
{
    return more_at_end(ctx) ? more_cmd_relate_next(ctx) : PSB_NA;
}

const cmd_t more_list_nav_cmds[] = {
    { 'f', "下篇文章", "閱讀列表中的下一篇文章", more_cmd_read_next, 0, CMD_PRIO_LOW },
    { 'F', NULL, NULL, more_cmd_read_next, 0, CMD_PRIO_NONE },
    { 'b', "前篇文章", "閱讀列表中的上一篇文章", more_cmd_read_prev, 0, CMD_PRIO_LOW },
    { 'B', NULL, NULL, more_cmd_read_prev, 0, CMD_PRIO_NONE },
    { ' ', NULL, "向下翻一頁 (到底讀下一篇)", more_cmd_next_at_end, 0, CMD_PRIO_NAV },
    { KEY_DOWN, NULL, "向下捲動一行 (到底讀下一篇)", more_cmd_next_at_end, 0, CMD_PRIO_NAV },
    { KEY_ENTER, NULL, NULL, more_cmd_next_at_end, 0, CMD_PRIO_NONE },
    { KEY_UP, NULL, "向上捲動一行 (到頂讀上一篇)", more_cmd_prev_at_top, 0, CMD_PRIO_NAV },
    { Ctrl('H'), NULL, "向上翻一頁 (到頂讀上一篇)", more_cmd_prev_at_top, 0, CMD_PRIO_NAV },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

const cmd_t more_thread_nav_cmds[] = {
    { ']', "同主題下篇", "閱讀同主題的下一篇文章", more_cmd_relate_next, 0, CMD_PRIO_NORM },
    { '+', NULL, NULL, more_cmd_relate_next, 0, CMD_PRIO_NONE },
    { '[', "同主題前篇", "閱讀同主題的上一篇文章", more_cmd_relate_prev, 0, CMD_PRIO_NORM },
    { '-', NULL, NULL, more_cmd_relate_prev, 0, CMD_PRIO_NONE },
    { '=', "同主題首篇", "閱讀同主題的第一篇文章", more_cmd_relate_first, 0, CMD_PRIO_LOW },
    { 'a', "同作者下篇", "閱讀同作者的下一篇文章", more_cmd_author_next, 0, CMD_PRIO_LOW },
    { 'A', "同作者前篇", "閱讀同作者的上一篇文章", more_cmd_author_prev, 0, CMD_PRIO_LOW },
    { 't', NULL, "向下翻一頁 (到底讀同主題下一篇)", more_cmd_relate_next_at_end, 0, CMD_PRIO_NAV },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static int more_cmd_chess(cmd_ctx_t *ctx) { return pager_leave_to(ctx, PAGER_DO_CHESS); }
#if defined(USE_BBSLUA) && !defined(DISABLE_BBSLUA_IN_PAGER)
static int more_cmd_bbslua(cmd_ctx_t *ctx) { return pager_leave_to(ctx, PAGER_DO_BBSLUA); }
#endif

const cmd_t more_reader_cmds[] = {
    { 'z', "棋局打譜", "進入棋局重播/打譜模式", more_cmd_chess, PERM_BASIC, CMD_PRIO_LOW },
    { 'Z', NULL, NULL, more_cmd_chess, PERM_BASIC, CMD_PRIO_NONE },
#if defined(USE_BBSLUA) && !defined(DISABLE_BBSLUA_IN_PAGER)
    { 'L', "執行BBSLua", "執行文章內嵌的 BBSLua 程式", more_cmd_bbslua, PERM_BASIC, CMD_PRIO_LOW },
    { 'l', NULL, NULL, more_cmd_bbslua, PERM_BASIC, CMD_PRIO_NONE },
#endif
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

/*
 * Fills layers (of size entries, NULL terminated): the caller's key bindings,
 * pager_common_cmds, then extra (NULL terminated, may be NULL).
 */
static void
pager_get_layers(const pager_run_t *run, cmd_layer_t *layers, int size,
                 const cmd_layer_t *extra)
{
    const more_hooks_t *hooks = run->hooks;
    int n = 0;

    if (hooks && hooks->layers) {
        for (const cmd_layer_t *l = hooks->layers; l->cmds && n < size - 2; l++)
            layers[n++] = *l;
    }
    layers[n++] = (cmd_layer_t){ pager_common_cmds, NULL };
    for (; extra && extra->cmds && n < size - 1; extra++)
        layers[n++] = *extra;
    layers[n] = (cmd_layer_t){ NULL, NULL };
}

#ifdef USE_PMORE
static int
pager_cmd_help(cmd_ctx_t *ctx)
{
    static const cmd_layer_t pmore_layers[] = {
        { pmore_cmds, NULL },
        { pmore_movie_cmds, NULL },
        { NULL, NULL }
    };
    const pager_run_t *run = PAGER_RUN(ctx);
    const more_hooks_t *hooks = run->hooks;
    cmd_layer_t layers[PAGER_MAX_LAYERS];
    screen_backup_t old_screen;

    scr_dump(&old_screen);
    pager_get_layers(run, layers, ARRAY_SIZE(layers), pmore_layers);
    int sel_key = cmd_show_help_layers(
        (hooks && hooks->title) ? hooks->title : "文章瀏覽", layers);
    scr_restore(&old_screen);
    if (sel_key == 'h' || sel_key == 'H') {
        if (IS_ENABLED(CONFIG_ANGEL) &&
            HasBasicUserPerm(PERM_LOGINOK) &&
            strcmp(cuser.myangel, "-") != 0) {
            CallAngel();
        }
        ctx->redraw = true;
        return 0;
    }
    if (sel_key > 0 && sel_key != ctx->key) {
        vk_ungetc(sel_key);
    }
    ctx->redraw = true;
    return 0;
}

// The mini pager (no USE_PMORE) dispatches the same layers itself.
static int
common_pager_key_handler(int ch, void *ctx)
{
    pager_run_t *run = (pager_run_t *)ctx;
    cmd_layer_t layers[PAGER_MAX_LAYERS];
    pager_get_layers(run, layers, ARRAY_SIZE(layers), NULL);
    cmd_ctx_t cctx = { .key = ch, .priv = run };
    cmd_dispatch_layers(layers, &cctx, NULL);
    if (cctx.quit)
        return run->retval ? run->retval : FULLUPDATE;
    if (cctx.redraw)
        return -1;
    return 0;
}

static void
common_pager_position(bool at_top, bool at_end, void *ctx)
{
    pager_run_t *run = (pager_run_t *)ctx;
    run->at_top = at_top;
    run->at_end = at_end;
}
#endif

/* Runs what a key binding left the pager for (pager_run_t.action). */
static void
pager_run_action(int action, const char *fpath)
{
    switch (action)
    {
	case PAGER_DO_COPY2TMP:
	    if (HasUserPerm(PERM_BASIC))
	    {
		char buf[PATHLEN];
		getdata(b_lines - 1, 0, "把這篇文章收入到暫存檔？[y/N] ",
			buf, 4, LCECHO);
		if (buf[0] != 'y')
		    break;
		setuserfile(buf, ask_tmpbuf(b_lines - 1));
		Copy(fpath, buf);
	    }
	    break;

	case PAGER_DO_CHESS:
	    if (HasUserPerm(PERM_BASIC))
		ChessReplayGame(fpath);
	    break;

#if defined(USE_BBSLUA) && !defined(DISABLE_BBSLUA_IN_PAGER)
	case PAGER_DO_BBSLUA:
	    // check permission again
	    if (HasUserPerm(PERM_BASIC))
		bbslua(fpath);
	    break;
#endif
    }
}

#ifndef USE_PMORE ///////////////////////////////////////////////////////////

typedef struct {
    int retval;
    int *lineno;
    int lines;
} pager_ctx_t;

static int
minimore_cmd_up(cmd_ctx_t *ctx) {
    pager_ctx_t *cx = (pager_ctx_t *)ctx->priv;
    (*cx->lineno)--;
    return 0;
}

static int
minimore_cmd_pgup(cmd_ctx_t *ctx) {
    pager_ctx_t *cx = (pager_ctx_t *)ctx->priv;
    *cx->lineno -= t_lines - 2;
    return 0;
}

static int
minimore_cmd_pgdn(cmd_ctx_t *ctx) {
    pager_ctx_t *cx = (pager_ctx_t *)ctx->priv;
    *cx->lineno += t_lines - 2;
    return 0;
}

static int
minimore_cmd_down(cmd_ctx_t *ctx) {
    pager_ctx_t *cx = (pager_ctx_t *)ctx->priv;
    (*cx->lineno)++;
    return 0;
}

static int
minimore_cmd_home(cmd_ctx_t *ctx) {
    pager_ctx_t *cx = (pager_ctx_t *)ctx->priv;
    *cx->lineno = 0;
    return 0;
}

static int
minimore_cmd_end(cmd_ctx_t *ctx) {
    pager_ctx_t *cx = (pager_ctx_t *)ctx->priv;
    *cx->lineno = cx->lines - (t_lines - 1);
    return 0;
}

static int
minimore_cmd_quit(cmd_ctx_t *ctx) {
    pager_ctx_t *cx = (pager_ctx_t *)ctx->priv;
    cx->retval = FULLUPDATE;
    return 0;
}

static const cmd_t minimore_nav_cmds[] = {
    { KEY_LEFT, "結束", "離開閱讀", minimore_cmd_quit, 0, CMD_PRIO_MAX },
    { 'q', NULL, NULL, minimore_cmd_quit, 0, CMD_PRIO_NONE },
    { KEY_UP, "上移", "向上捲動一行", minimore_cmd_up, 0, CMD_PRIO_NAV },
    { 'k', NULL, NULL, minimore_cmd_up, 0, CMD_PRIO_NONE },
    { Ctrl('P'), NULL, NULL, minimore_cmd_up, 0, CMD_PRIO_NONE },
    { KEY_DOWN, "下移", "向下捲動一行", minimore_cmd_down, 0, CMD_PRIO_NAV },
    { 'j', NULL, NULL, minimore_cmd_down, 0, CMD_PRIO_NONE },
    { Ctrl('N'), NULL, NULL, minimore_cmd_down, 0, CMD_PRIO_NONE },
    { KEY_PGUP, "上頁", "向上捲動一頁", minimore_cmd_pgup, 0, CMD_PRIO_NAV },
    { Ctrl('B'), NULL, NULL, minimore_cmd_pgup, 0, CMD_PRIO_NONE },
    { KEY_PGDN, "下頁", "向下捲動一頁", minimore_cmd_pgdn, 0, CMD_PRIO_NAV },
    { Ctrl('F'), NULL, NULL, minimore_cmd_pgdn, 0, CMD_PRIO_NONE },
    { ' ', NULL, NULL, minimore_cmd_pgdn, 0, CMD_PRIO_NONE },
    { KEY_RIGHT, NULL, NULL, minimore_cmd_pgdn, 0, CMD_PRIO_NONE },
    { KEY_HOME, NULL, "移至文章開頭", minimore_cmd_home, 0, CMD_PRIO_NONE },
    { Ctrl('A'), NULL, NULL, minimore_cmd_home, 0, CMD_PRIO_NONE },
    { KEY_END, NULL, "移至文章結尾", minimore_cmd_end, 0, CMD_PRIO_NONE },
    { Ctrl('E'), NULL, NULL, minimore_cmd_end, 0, CMD_PRIO_NONE },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

// minimore: a mini pager in exactly 130 lines
#define PAGER_MAXLINES (2048)
int more(const char *fpath, int promptend)
{
    return more_ex(fpath, promptend, NULL);
}

int
more_ex(const char *fpath, int promptend, const more_hooks_t *hooks)
{
    FILE *fp = fopen(fpath, "rt");
    int  lineno = 0, lines = 0, oldlineno = -1;
    int  i = 0, abort = 0, showall = 0, colorize = 0, vk = 0;
    int  lpos[PAGER_MAXLINES] = {0}; // line position
    char buf [ANSILINELEN];

    if (!fp) return -1;
    clear();

    if (promptend == NA) {	    // quick print one page
	for (i = 0; i < t_lines-1; i++)
	    if (!fgets(buf, sizeof(buf), fp))
		break;
	    else
		outs(TEMP_STORAGE_TO_MB(buf));
	fclose(fp);
	return 0;
    }
    pager_run_t run = { .fpath = fpath, .hooks = hooks };

    // YEA mode: pre-read
    while (lines < PAGER_MAXLINES-1 &&
	   fgets(buf, sizeof(buf), fp) != NULL)
	lpos[++lines] = ftell(fp);
    rewind(fp);

    while (!abort)
    {
	if (oldlineno != lineno)    // seek and print
	{
	    clear();
	    showall = 0;
	    oldlineno = lineno;
	    fseek(fp, lpos[lineno], SEEK_SET);

	    for (i = 0, buf[0] = 0; i < t_lines-1; i++, buf[0] = 0)
	    {
		if (!showall)
		{
		    fgets(buf, sizeof(buf), fp);
		    storage_to_mb(buf, buf, sizeof(buf));
		    if (lineno + i == 0 &&
			(strncmp(buf, STR_AUTHOR1, strlen(STR_AUTHOR1))==0 ||
			 strncmp(buf, STR_AUTHOR2, strlen(STR_AUTHOR2))==0))
			colorize = 1;
		}

		if (!buf[0])
		{
		    outs("\n");
		    showall = 1;
		} else {
		    // dirty code to render heeader
		    if (colorize && lineno+i < 4 && *buf &&
			    *buf != '\n' && strchr(buf, ':'))
		    {
			char *q1 = strchr(buf, ':');
			int    l = t_columns - 2 - stream_width(buf);
			char *q2 = mbs_strstr(buf, STR_POST1);

			chomp(buf);
			if (q2 == NULL) q2 = mbs_strstr(buf, STR_POST2);
			if (q2 && q2 > buf) { *(q2-1) = 0; q2 = strchr(q2, ':'); }
			else q2 = q1;

			*q1++ = 0;	*q2++ = 0;
			if (q1 == q2)	 q2 = NULL;

			outs(ANSI_COLOR(34;47) " ");
			outs(buf); outs(" " ANSI_REVERSE);
			outs(q1);  prints("%*s", l, ""); q1 += strlen(q1);

			if (q2) {
			    outs(ANSI_COLOR(0;34;47) " ");  outs(q1+1);
			    outs(" " ANSI_REVERSE);	    outs(q2);
			}
			outs(ANSI_RESET"\n");
		    } else
			outs(buf);
		}
	    }
	    if (lineno + i >= lines)
		showall = 1;

	    // print prompt bar
	    SNPRINTF(buf, "  瀏覽 P.%d  ", 1 + (lineno / (t_lines-2)));
	    vs_footer(buf,
	    " (→↓[PgUp][PgDn][Home][End])游標移動\t(←/q)結束");
	}
	// process key
	run.at_top = (lineno == 0);
	run.at_end = showall;
	vk = vkey();
	pager_ctx_t cx = {
	    .retval = 0,
	    .lineno = &lineno,
	    .lines = lines,
	};
	const cmd_layer_t extra[] = {
	    { minimore_nav_cmds, &cx },
	    { bbs_global_cmds, NULL },
	    { NULL, NULL }
	};
	cmd_layer_t layers[PAGER_MAX_LAYERS];
	pager_get_layers(&run, layers, ARRAY_SIZE(layers), extra);
	cmd_ctx_t cctx = {
	    .key = vk,
	    .curr = lineno,
	    .total = lines,
	    .priv = &run,
	};
	cmd_dispatch_layers(layers, &cctx, " 文章瀏覽 ");
	if (cx.retval != 0)
	    abort = cx.retval;
	if (cctx.quit)
	    abort = run.retval ? run.retval : FULLUPDATE;
	if (cctx.redraw)
	    oldlineno = -1;
	if (lineno + (t_lines-1) >= lines)
	    lineno = lines-(t_lines-1);
	if (lineno < 0)
	    lineno = 0;
    }
    fclose(fp);
    pager_run_action(run.action, fpath);
    return abort > 0 ? abort : 0;
}

#else	// USE_PMORE ////////////////////////////////////////////////////////

static int
common_pmore_footer_handler(int ratio GCC_UNUSED, void *ctx)
{
    cmd_layer_t layers[PAGER_MAX_LAYERS];
    pager_get_layers((pager_run_t *)ctx, layers, ARRAY_SIZE(layers), NULL);
    vs_cmd_bar(VS_FOOTER, NULL, layers);
    return 0;
}

/* use new pager: piaip's more. */
static const struct pmore_callbacks common_pager_cb = {
    .process_key = common_pager_key_handler,
    .footer = common_pmore_footer_handler,
    .position = common_pager_position,
};

/* pmore start: go straight to live mode if asked (live_ctx_t.start). */
static int
common_pager_start(void *ctx)
{
    live_ctx_t *live = ((pager_run_t *)ctx)->hooks->live;
    if (!live->start)
        return 0;
    pmore_go_live();
    return live->entered ? FULLUPDATE : 0;
}

/* pmore_go_live(): hand the last page over to the reader's live mode. */
static void
common_pager_on_live(int row, off_t shown, const comment_ctx_t *cmt,
                     const char *op_author, void *ctx)
{
    live_ctx_t *live = ((pager_run_t *)ctx)->hooks->live;
    live->entered = true;
    live->row = row;
    live->shown = shown;
    strlcpy(live->op_author, op_author, sizeof(live->op_author));
    live->cmt = *cmt;
    live->cmt.op_author = live->op_author;
}

int
more(const char *fpath, int promptend)
{
    return more_ex(fpath, promptend, NULL);
}

int
more_ex(const char *fpath, int promptend, const more_hooks_t *hooks)
{
    pager_run_t run = { .fpath = fpath, .hooks = hooks };
    struct pmore_callbacks cb = common_pager_cb;

    if (hooks && hooks->live) {
        cb.start = common_pager_start;
        cb.on_live = common_pager_on_live;
    }
    int r = pmore2(fpath, promptend, &run, &cb);
    pager_run_action(run.action, fpath);
    return r;
}

static int
memory_pager_exit_handler(int r, const void *ctx GCC_UNUSED)
{
    // TODO: port some functionality from `pager_run_action'.
    return r;
}

int
more_inmemory(void *content, int size, int promptend)
{
    pager_run_t run = { 0 };
    struct pmore_callbacks cb = common_pager_cb;
    int r = pmore2_inmemory(content, size, promptend, &run, &cb);
    return memory_pager_exit_handler(r, NULL);
}

#endif // USE_PMORE /////////////////////////////////////////////////////////

