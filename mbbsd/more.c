#include "bbs.h"

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

#ifdef USE_SYSOP_EDIT
static int
check_sysop_edit_perm(const char *fpath)
{
    if (!HasUserPerm(PERM_SYSOP) ||
	strcmp(fpath, "etc/ve.hlp") == 0)
	return 0;

    if (fpath && *fpath) {
        if (strstr(fpath, BBSHOME) == fpath)
            fpath += (strlen(BBSHOME) + 1);

        // allow only files in board, man or home.
        if (!(strstr(fpath, "boards/") == fpath ||
              strstr(fpath, "home/") == fpath ||
              strstr(fpath, "man/") == fpath))
            return 0;
    }

#ifdef BN_SECURITY
    if (strcmp(currboard, BN_SECURITY) == 0)
	return 0;
#endif // BN_SECURITY

    return 1;
}
#endif

typedef struct {
    int retval;
    int *lineno;
    int lines;
    int showall;
} pager_ctx_t;

static int pager_cmd_chess(cmd_ctx_t *ctx) { ((pager_ctx_t *)ctx->priv)->retval = RET_DOCHESSREPLAY; return 0; }
#if defined(USE_BBSLUA) && !defined(DISABLE_BBSLUA_IN_PAGER)
static int pager_cmd_bbslua(cmd_ctx_t *ctx) { ((pager_ctx_t *)ctx->priv)->retval = RET_DOBBSLUA; return 0; }
#endif
static int pager_cmd_query(cmd_ctx_t *ctx) { ((pager_ctx_t *)ctx->priv)->retval = RET_DOQUERYINFO; return 0; }
static int pager_cmd_copy2tmp(cmd_ctx_t *ctx) { ((pager_ctx_t *)ctx->priv)->retval = RET_COPY2TMP; return 0; }
static int pager_cmd_edit(cmd_ctx_t *ctx) {
#ifdef USE_SYSOP_EDIT
    if (check_sysop_edit_perm("")) {
        ((pager_ctx_t *)ctx->priv)->retval = RET_DOSYSOPEDIT;
        return 0;
    }
#endif
    ((pager_ctx_t *)ctx->priv)->retval = RET_EDITPOST;
    return 0;
}
static int pager_cmd_edittitle(cmd_ctx_t *ctx) { ((pager_ctx_t *)ctx->priv)->retval = RET_EDITTITLE; return 0; }
static int pager_cmd_comment(cmd_ctx_t *ctx) { ((pager_ctx_t *)ctx->priv)->retval = RET_DOCOMMENT; return 0; }
static int pager_cmd_vote(cmd_ctx_t *ctx) { ((pager_ctx_t *)ctx->priv)->retval = RET_DOVOTE; return 0; }
static int pager_cmd_reply(cmd_ctx_t *ctx) { ((pager_ctx_t *)ctx->priv)->retval = RET_DOREPLY; return 0; }
static int pager_cmd_replyall(cmd_ctx_t *ctx) { ((pager_ctx_t *)ctx->priv)->retval = RET_DOREPLYALL; return 0; }
static int pager_cmd_selectbrd(cmd_ctx_t *ctx) { ((pager_ctx_t *)ctx->priv)->retval = RET_SELECTBRD; return 0; }
static int pager_cmd_selectaid(cmd_ctx_t *ctx) { ((pager_ctx_t *)ctx->priv)->retval = RET_SELECTAID; return 0; }
static int pager_cmd_author_prev(cmd_ctx_t *ctx) { ((pager_ctx_t *)ctx->priv)->retval = AUTHOR_PREV; return 0; }
static int pager_cmd_author_next(cmd_ctx_t *ctx) { ((pager_ctx_t *)ctx->priv)->retval = AUTHOR_NEXT; return 0; }
static int pager_cmd_read_next(cmd_ctx_t *ctx) { ((pager_ctx_t *)ctx->priv)->retval = READ_NEXT; return 0; }
static int pager_cmd_read_prev(cmd_ctx_t *ctx) { ((pager_ctx_t *)ctx->priv)->retval = READ_PREV; return 0; }
static int pager_cmd_relate_next(cmd_ctx_t *ctx) { ((pager_ctx_t *)ctx->priv)->retval = RELATE_NEXT; return 0; }
static int pager_cmd_relate_prev(cmd_ctx_t *ctx) { ((pager_ctx_t *)ctx->priv)->retval = RELATE_PREV; return 0; }
static int pager_cmd_relate_first(cmd_ctx_t *ctx) { ((pager_ctx_t *)ctx->priv)->retval = RELATE_FIRST; return 0; }

static int
pager_cmd_quit(cmd_ctx_t *ctx)
{
    ((pager_ctx_t *)ctx->priv)->retval = FULLUPDATE;
    ctx->quit = true;
    return 0;
}

#ifdef USE_PMORE
static int pager_cmd_help(cmd_ctx_t *ctx);
#endif

static const cmd_t pager_reading_cmds[] = {
    { 'y', "回應", "回覆文章至看板或信箱", pager_cmd_replyall, 0, CMD_PRIO_HIGH },
    { 'X', "推文", "給予文章評論/評分", pager_cmd_comment, 0, CMD_PRIO_HIGH },
    { '%', NULL, NULL, pager_cmd_vote, 0, CMD_PRIO_NONE },
    { 's', "切換看板", "搜尋並切換至其他看板", pager_cmd_selectbrd, PERM_BASIC, CMD_PRIO_LOW },
    { '#', "代碼搜尋", "以文章代碼(AID)搜尋文章", pager_cmd_selectaid, PERM_BASIC, CMD_PRIO_LOW },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static const cmd_t pager_mail_cmds[] = {
    { 'y', "回信", "回覆信件至信箱", pager_cmd_reply, 0, CMD_PRIO_HIGH },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static const cmd_t pager_common_cmds[] = {
    { KEY_LEFT, "離開", "離開目前文章", pager_cmd_quit, 0, CMD_PRIO_QUIT },
    { 'q', NULL, NULL, pager_cmd_quit, 0, CMD_PRIO_NONE },
#ifdef USE_PMORE
    { 'h', "說明", "顯示按鍵說明", pager_cmd_help, 0, CMD_PRIO_USAGE },
    { '?', NULL, NULL, pager_cmd_help, 0, CMD_PRIO_NONE },
#endif
    { 'r', "回信回文", "回覆給作者或回文", pager_cmd_reply, 0, CMD_PRIO_NORM },
    { 'R', NULL, NULL, pager_cmd_reply, 0, CMD_PRIO_NONE },
    { ']', "同主題下篇", "閱讀同主題的下一篇文章", pager_cmd_relate_next, 0, CMD_PRIO_NORM },
    { '+', NULL, NULL, pager_cmd_relate_next, 0, CMD_PRIO_NONE },
    { '[', "同主題前篇", "閱讀同主題的上一篇文章", pager_cmd_relate_prev, 0, CMD_PRIO_NORM },
    { '-', NULL, NULL, pager_cmd_relate_prev, 0, CMD_PRIO_NONE },
    { '=', "同主題首篇", "閱讀同主題的第一篇文章", pager_cmd_relate_first, 0, CMD_PRIO_LOW },
    { 'f', "下篇文章", "閱讀列表中的下一篇文章", pager_cmd_read_next, 0, CMD_PRIO_LOW },
    { 'F', NULL, NULL, pager_cmd_read_next, 0, CMD_PRIO_NONE },
    { 'b', "前篇文章", "閱讀列表中的上一篇文章", pager_cmd_read_prev, 0, CMD_PRIO_LOW },
    { 'B', NULL, NULL, pager_cmd_read_prev, 0, CMD_PRIO_NONE },
    { 'a', "同作者下篇", "閱讀同作者的下一篇文章", pager_cmd_author_next, 0, CMD_PRIO_LOW },
    { 'A', "同作者前篇", "閱讀同作者的上一篇文章", pager_cmd_author_prev, 0, CMD_PRIO_LOW },
    { 'Q', "查詢資訊", "查詢文章代碼(AID)與檔案資訊", pager_cmd_query, 0, CMD_PRIO_LOW },
    { 'E', "修改文章", "編輯目前文章內容", pager_cmd_edit, 0, CMD_PRIO_LOW },
    { 'T', "修改標題", "修改目前文章標題", pager_cmd_edittitle, 0, CMD_PRIO_LOW },
    { Ctrl('T'), "存入暫存檔", "將目前文章存入個人暫存檔", pager_cmd_copy2tmp, PERM_BASIC, CMD_PRIO_LOW },
    { Ctrl('K'), NULL, NULL, pager_cmd_copy2tmp, PERM_BASIC, CMD_PRIO_NONE },
    { 'z', "棋局打譜", "進入棋局重播/打譜模式", pager_cmd_chess, PERM_BASIC, CMD_PRIO_LOW },
#if defined(USE_BBSLUA) && !defined(DISABLE_BBSLUA_IN_PAGER)
    { 'L', "執行BBSLua", "執行文章內嵌的 BBSLua 程式", pager_cmd_bbslua, PERM_BASIC, CMD_PRIO_LOW },
    { 'l', NULL, NULL, pager_cmd_bbslua, PERM_BASIC, CMD_PRIO_NONE },
#endif
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static const cmd_t pager_empty_cmds[] = {
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

#ifdef USE_PMORE
static int
pager_cmd_help(cmd_ctx_t *ctx)
{
    screen_backup_t old_screen;
    scr_dump(&old_screen);
    pager_ctx_t *cx = (pager_ctx_t *)ctx->priv;
    const cmd_layer_t layers[] = {
        { (currstat == READING) ? pager_reading_cmds :
          (currstat == RMAIL) ? pager_mail_cmds : pager_empty_cmds, cx },
        { pager_common_cmds, cx },
        { pmore_cmds, NULL },
        { pmore_movie_cmds, NULL },
        { NULL, NULL }
    };
    int sel_key = cmd_show_help_layers((currstat == RMAIL) ? "信件閱讀" : "文章閱讀", layers);
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
#endif

#ifdef USE_PMORE
// The mini pager (no USE_PMORE) dispatches the same layers itself.
static int
common_pager_key_handler(int ch, void *ctx GCC_UNUSED)
{
    pager_ctx_t cx = { .retval = DONOTHING };
    const cmd_layer_t layers[] = {
        { (currstat == READING) ? pager_reading_cmds :
          (currstat == RMAIL) ? pager_mail_cmds : pager_empty_cmds, &cx },
        { pager_common_cmds, &cx },
        { NULL, NULL }
    };
    cmd_ctx_t cctx = { .key = ch, .priv = &cx };
    cmd_dispatch_layers(layers, &cctx, NULL);
    if (cctx.redraw)
        return -1;
    return cx.retval;
}
#endif

static int
common_pager_exit_handler(int r, const char *fpath)
{
    // post processing
    switch(r)
    {
#ifdef USE_SYSOP_EDIT
	case RET_DOSYSOPEDIT:
	    r = FULLUPDATE;
	    if (!check_sysop_edit_perm(fpath))
		break;
	    file_appendf("log/security",
		    "%u %s %d %s admin edit file=%s\n",
		    (int)now, Cdate(&now), getpid(), cuser.userid, fpath);
	    veditfile(fpath);
	    break;
#endif

	case RET_COPY2TMP:
	    r = FULLUPDATE;
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

	case RET_SELECTBRD:
	    r = FULLUPDATE;
	    if (currstat == READING)
		r = Select();
	    break;

	case RET_DOCHESSREPLAY:
	    r = FULLUPDATE;
	    if (HasUserPerm(PERM_BASIC))
		ChessReplayGame(fpath);
	    break;

#if defined(USE_BBSLUA) && !defined(DISABLE_BBSLUA_IN_PAGER)
	case RET_DOBBSLUA:
	    r = FULLUPDATE;
	    // check permission again
	    if (HasUserPerm(PERM_BASIC))
		bbslua(fpath);
	    break;
#endif
    }
    return r;
}

#ifndef USE_PMORE ///////////////////////////////////////////////////////////

static int
minimore_cmd_up(cmd_ctx_t *ctx) {
    pager_ctx_t *cx = (pager_ctx_t *)ctx->priv;
    if (*cx->lineno == 0)
        cx->retval = READ_PREV;
    (*cx->lineno)--;
    return 0;
}

static int
minimore_cmd_pgup(cmd_ctx_t *ctx) {
    pager_ctx_t *cx = (pager_ctx_t *)ctx->priv;
    if (*cx->lineno == 0)
        cx->retval = READ_PREV;
    *cx->lineno -= t_lines - 2;
    return 0;
}

static int
minimore_cmd_pgdn(cmd_ctx_t *ctx) {
    pager_ctx_t *cx = (pager_ctx_t *)ctx->priv;
    if (cx->showall)
        cx->retval = READ_NEXT;
    *cx->lineno += t_lines - 2;
    return 0;
}

static int
minimore_cmd_down(cmd_ctx_t *ctx) {
    pager_ctx_t *cx = (pager_ctx_t *)ctx->priv;
    if (cx->showall)
        cx->retval = READ_NEXT;
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
	vk = vkey();
	pager_ctx_t cx = {
	    .retval = 0,
	    .lineno = &lineno,
	    .lines = lines,
	    .showall = showall,
	};
	const cmd_layer_t layers[] = {
	    { minimore_nav_cmds, &cx },
	    { (currstat == READING) ? pager_reading_cmds : (currstat == RMAIL) ? pager_mail_cmds : pager_empty_cmds, &cx },
	    { pager_common_cmds, &cx },
	    { bbs_global_cmds, NULL },
	    { NULL, NULL }
	};
	cmd_ctx_t cctx = {
	    .key = vk,
	    .curr = lineno,
	    .total = lines,
	    .priv = &cx,
	};
	cmd_dispatch_layers(layers, &cctx, " 文章瀏覽 ");
	if (cx.retval != 0)
	    abort = cx.retval;
	if (cctx.redraw)
	    oldlineno = -1;
	if (lineno + (t_lines-1) >= lines)
	    lineno = lines-(t_lines-1);
	if (lineno < 0)
	    lineno = 0;
    }
    fclose(fp);
    return abort > 0 ? common_pager_exit_handler(abort, fpath) : 0;
}

void
more_on_live_comment(uint32_t bid GCC_UNUSED, aidu_t aid GCC_UNUSED, const char *text GCC_UNUSED,
                     uint64_t fsize GCC_UNUSED)
{
    // Not supported.
}

void
more_live_resubscribe(void)
{
}

int
more_ex(const char *fpath, int promptend, const more_hooks_t *hooks GCC_UNUSED)
{
    return more(fpath, promptend);
}

#else	// USE_PMORE ////////////////////////////////////////////////////////

static int
common_pmore_footer_handler(int ratio GCC_UNUSED,
                            void *ctx GCC_UNUSED)
{
    pager_ctx_t cx = { .retval = DONOTHING };
    const cmd_layer_t layers[] = {
        { (currstat == READING) ? pager_reading_cmds :
          (currstat == RMAIL) ? pager_mail_cmds : pager_empty_cmds, &cx },
        { pager_common_cmds, &cx },
        { NULL, NULL }
    };
    vs_cmd_bar(VS_FOOTER, NULL, layers);
    return 0;
}

////////////////////////////////////////////////////////////////////////////
// Live mode (實況)
//
// Pressing 'v' in pmore shows the last page and exits pmore with RET_LIVE;
// more() then runs live_run(), which owns the screen: new comments of the
// article (IPC PubSub) are printed below the last page and scrolled up once
// the screen is full, with a dedicated status bar. 'B' toggles the bell and
// any other key leaves the pager.
////////////////////////////////////////////////////////////////////////////

#define LIVE_MAX_PENDING_COMMENTS 128
#define LIVE_CATCHUP_MAX        (16 * 1024) // max bytes of missed comments to replay
#define LIVE_STATUS_REFRESH_MS  1000

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
# define MAP_ANONYMOUS MAP_ANON
#endif

typedef struct {
    uint64_t fsize;     // article size after this comment (0: unknown)
    char     text[IPC_TEXT_LEN];
} live_comment_t;

typedef struct {
    uint32_t bid;
    aidu_t   aid;
    bool     active;    // subscribed; comments are being queued
    int      head;
    int      tail;
    int      count;
    live_comment_t *comments;
} live_provider_t;

static live_provider_t s_live;
static bool s_live_bell = false;    // (B) bell notification, kept per session

static void
live_provider_free(void)
{
    if (s_live.comments) {
        size_t sz = sizeof(live_comment_t) * LIVE_MAX_PENDING_COMMENTS;
        munmap(s_live.comments, sz);
        s_live.comments = NULL;
    }
    s_live.head = s_live.tail = s_live.count = 0;
}

static int
live_provider_alloc(void)
{
    if (s_live.comments)
        return 0;
    size_t sz = sizeof(live_comment_t) * LIVE_MAX_PENDING_COMMENTS;
    s_live.comments = (live_comment_t *)mmap(
        NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (s_live.comments == MAP_FAILED) {
        s_live.comments = NULL;
        return -1;
    }
    s_live.head = s_live.tail = s_live.count = 0;
    return 0;
}

static const live_comment_t *
live_provider_pop(void)
{
    if (!s_live.comments || s_live.count == 0)
        return NULL;

    const live_comment_t *c = &s_live.comments[s_live.head];
    s_live.head = (s_live.head + 1) % LIVE_MAX_PENDING_COMMENTS;
    s_live.count--;
    return c;
}

static void
get_live_target(const char *fpath, uint32_t *out_bid, aidu_t *out_aid)
{
    *out_bid = 0;
    *out_aid = 0;
    if (!fpath || !strstr(fpath, "boards/"))
        return;
    const char *last_slash = strrchr(fpath, '/');
    if (!last_slash)
        return;
    const char *fname = last_slash + 1;
    aidu_t aid = fn2aidu(fname);
    if (aid == 0)
        return;

    int bid = currbid;
    if (bid <= 0 && currboard[0]) {
        bid = get_board_bid(currboard);
    }
    if (bid <= 0)
        return;

    *out_bid = (uint32_t)bid;
    *out_aid = aidu_raw(aid);
}

/*
 * IPC handler for IPC_TYPE_LIVE_COMMENT: queue the comment and post
 * VKEY_EV_PUBSUB, which only live_run() asks for. Packets for other posts or
 * after we left live mode (already in flight when unsubscribing) are dropped.
 */
void
more_on_live_comment(uint32_t bid, aidu_t aid, const char *text, uint64_t fsize)
{
    if (!s_live.active || !s_live.comments ||
        s_live.bid != bid || aidu_raw(s_live.aid) != aidu_raw(aid) ||
        !text || !*text)
        return;

    if (s_live.count >= LIVE_MAX_PENDING_COMMENTS) {
        /* Drop oldest comment when queue is full */
        s_live.head = (s_live.head + 1) % LIVE_MAX_PENDING_COMMENTS;
        s_live.count--;
    }
    s_live.comments[s_live.tail].fsize = fsize;
    STRLCPY(s_live.comments[s_live.tail].text, text);
    s_live.tail = (s_live.tail + 1) % LIVE_MAX_PENDING_COMMENTS;
    s_live.count++;
    vkey_post_event(VKEY_EV_PUBSUB);
}

/* Called after the IPC link is re-established: the daemon has no memory of
 * our subscriptions, so re-subscribe or live updates silently stop. */
void
more_live_resubscribe(void)
{
    if (s_live.active)
        ipc_sub(s_live.bid, aidu_raw(s_live.aid));
}

typedef struct {
    int     row;        // next screen row to print on; >= b_lines: scroll
    time4_t start;
    int     received;
    off_t   synced;     // file bytes already on screen (0: unknown)
} live_view_t;

/* Move to the row for the next line, scrolling once the screen is full. */
static void
live_next_row(live_view_t *v)
{
    if (v->row < b_lines) {
        move(v->row++, 0);
    } else {
        /* Scroll the whole screen; the status bar is redrawn afterwards. */
        move(b_lines, 0);
        clrtoeol();
        scroll();
        move(b_lines - 1, 0);
    }
}

static void
live_print_line(live_view_t *v, const char *line)
{
    live_next_row(v);
    outs(line);
    outs(ANSI_RESET);
    clrtoeol();
}

/* Full-width separator marking where live mode starts; "LIVE" style colors,
 * unlike any comment, pmore footer or the live status bar. */
#define LIVE_BANNER_COLOR   ANSI_COLOR(1;33;41)
#define LIVE_BANNER_TEXT    "實 況 開 始"

static void
live_print_banner(live_view_t *v)
{
    int width = t_columns - 1;
    int len = (int)strlen(LIVE_BANNER_TEXT);   // Big5: bytes == columns
    int left = (width - len) / 2;
    if (left < 0)
        left = 0;
    int right = width - len - left;

    live_next_row(v);
    outs(LIVE_BANNER_COLOR);
    nblank(left);
    outs(LIVE_BANNER_TEXT);
    if (right > 0)
        nblank(right);
    outs(ANSI_RESET);
    clrtoeol();
}

static void
live_print_text(live_view_t *v, const char *text)
{
    char buf[IPC_TEXT_LEN];
    STRLCPY(buf, text);

    char *line = buf;
    while (line) {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = '\0';
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\r')
            line[--len] = '\0';
        if (*line || next)
            live_print_line(v, line);
        line = next;
    }
}

/*
 * pmore showed the file as it was when the article was opened, which may be
 * long ago: replay the complete lines appended since then (new comments).
 * Called after subscribing, so nothing falls in between; comments delivered
 * by IPC are then skipped by their fsize token if v->synced already covers
 * them. (Until comments have unique ids, an article edit shifting the file
 * may make this skip or repeat a few.)
 */
static void
live_catchup(live_view_t *v, const char *fpath, off_t shown)
{
    int fd = open(fpath, O_RDONLY);
    if (fd < 0)
        return;

    struct stat st;
    if (fstat(fd, &st) != 0 || shown <= 0 || st.st_size < shown) {
        /* Unknown, or the file was replaced (edited): don't filter. */
        v->synced = 0;
        close(fd);
        return;
    }
    if (st.st_size == shown) {
        close(fd);
        return;
    }

    off_t from = shown;
    bool partial_head = false;
    if (st.st_size - from > LIVE_CATCHUP_MAX) {
        from = st.st_size - LIVE_CATCHUP_MAX;
        partial_head = true;
    }

    size_t len = (size_t)(st.st_size - from);
    char *buf = (char *)malloc(len + 1);
    ssize_t n = buf ? pread(fd, buf, len, from) : -1;
    close(fd);
    if (n <= 0) {
        free(buf);
        return;
    }
    buf[n] = '\0';

    char *line = buf;
    if (partial_head) {
        char *nl = strchr(line, '\n');
        line = nl ? nl + 1 : buf + n;
    }
    while (*line) {
        char *nl = strchr(line, '\n');
        if (!nl)
            break;  // incomplete line being written: its PUB will follow
        *nl = '\0';
        size_t l = strlen(line);
        if (l > 0 && line[l - 1] == '\r')
            line[l - 1] = '\0';

        live_print_line(v, line);
        line = nl + 1;
    }
    v->synced = from + (line - buf);
    free(buf);
}

static void
live_flush(live_view_t *v)
{
    const live_comment_t *c;
    int shown = 0;

    while ((c = live_provider_pop()) != NULL) {
        if (c->fsize && v->synced && (off_t)c->fsize <= v->synced)
            continue;   // already shown (pmore page or catch-up)
        live_print_text(v, c->text);
        shown++;
    }
    if (shown) {
        v->received += shown;
        if (s_live_bell)
            bell();
    }
}

static void
live_draw_status(const live_view_t *v, bool can_comment)
{
    int elapsed = (int)(now - v->start);
    if (elapsed < 0)
        elapsed = 0;
    AUTO_STR(part1, ANSI_COLOR(1;37;42) " 實況中 %02d:%02d [+%d] ",
            elapsed / 60, elapsed % 60, v->received);
    AUTO_STR(part2, " %s(B)鈴聲%s (任意鍵)離開 %s\t(T/M/S)調時",
            can_comment ? "(X)留言" : "",
            s_live_bell ? "[打開]" : "[關閉]", Cdate_mdHM(&now));
    vs_footer(part1, part2);
}

static void
inc_timer(time4_t *start, int delta)
{
    if (delta > 0 && now - *start > delta)
        *start += delta;

    if (delta < 0 && now - *start < 8 * 60 * 60)
        *start += delta;
}

/*
 * 'X': comment through the caller (comment() needs the article's .DIR
 * context). Prompts may draw above the status bar, so save and restore the
 * screen. Nothing scrolls while typing: the prompt's vkey() calls don't take
 * live events, so comments queue up and are flushed once we are back; our
 * own comment comes back the same way, confirming it was posted.
 */
static int
live_comment(const more_hooks_t *hooks)
{
    screen_backup_t old_screen;

    scr_dump(&old_screen);
    int r = hooks->comment(hooks->ctx);
    scr_restore(&old_screen);
    return r;
}

/* Runs live mode after pmore returned RET_LIVE; returns a pager result. */
static int
live_run(const char *fpath, const more_hooks_t *hooks)
{
    bool can_comment = hooks && hooks->comment;
    int result = FULLUPDATE;

    int rows = 0;
    off_t shown = 0;
    pmore_get_live_view(&rows, &shown);

    if (!ipc_is_connected())
        ipc_check_reconnect();
    if (live_provider_alloc() < 0 ||
        ipc_sub(s_live.bid, aidu_raw(s_live.aid)) != 0) {
        /* The ACK may merely be late (busy daemon); cancel so we don't leave
         * a phantom subscription receiving every comment. */
        ipc_unsub(s_live.bid, aidu_raw(s_live.aid));
        live_provider_free();
        vmsg("實況服務暫時無法使用，請稍後再試。");
        return FULLUPDATE;
    }
    s_live.active = true;

    live_view_t v;
    memset(&v, 0, sizeof(v));
    v.row = rows;
    v.synced = shown;
    syncnow();
    v.start = now;
    live_print_banner(&v);
    live_catchup(&v, fpath, shown);
    bool cont = true;

    while (cont) {
        syncnow();
        live_draw_status(&v, can_comment);

        if (!vkey_poll_ex(LIVE_STATUS_REFRESH_MS, VKEY_EV_PUBSUB))
            continue;   // tick: refresh elapsed time

        int ch = vkey_ex(VKEY_EV_PUBSUB);
        switch (ch) {
        case I_EV_PUBSUB:
            live_flush(&v);
            break;

        case 'b':
        case 'B':
            s_live_bell = !s_live_bell;
            break;

        case '%':
        case 'X':
        case 'x':
            if (!can_comment) {
                cont = false;
                break;
            }
            if (live_comment(hooks) == DIRCHANGED) {
                /* e.g. the article is gone: leave and let the caller reload */
                result = DIRCHANGED;
                cont = false;
            }
            break;

        case 'T':
            inc_timer(&v.start, 60*10);
            break;
        case 't':
            inc_timer(&v.start, -60*10);
            break;

        case 'M':
            inc_timer(&v.start, 60);
            break;
        case 'm':
            inc_timer(&v.start, -60);
            break;

        case 'S':
            inc_timer(&v.start, 1);
            break;
        case 's':
            inc_timer(&v.start, -1);
            break;

        default:
            cont = false;
            break;
        }
    }

    ipc_unsub(s_live.bid, aidu_raw(s_live.aid));
    s_live.active = false;
    live_provider_free();
    return result;
}

static int
common_pager_can_live(void *ctx GCC_UNUSED)
{
    return s_live.bid != 0 && aidu_raw(s_live.aid) != 0;
}

/* use new pager: piaip's more. */
static const struct pmore_callbacks common_pager_cb = {
    .process_key = common_pager_key_handler,
    .footer = common_pmore_footer_handler,
    .can_live = common_pager_can_live,
};

int
more(const char *fpath, int promptend)
{
    return more_ex(fpath, promptend, NULL);
}

int
more_ex(const char *fpath, int promptend, const more_hooks_t *hooks)
{
    /* more() may be re-entered from a pager command; keep the outer target. */
    uint32_t outer_bid = s_live.bid;
    aidu_t outer_aid = s_live.aid;

    get_live_target(fpath, &s_live.bid, &s_live.aid);

    struct pmore_callbacks cb = common_pager_cb;
    if (s_live.bid == 0 || aidu_raw(s_live.aid) == 0)
        cb.can_live = NULL;

    int r = pmore2(fpath, promptend, (void *)fpath, &cb);
    if (r == RET_LIVE)
        r = live_run(fpath, hooks);

    s_live.bid = outer_bid;
    s_live.aid = outer_aid;

    return common_pager_exit_handler(r, fpath);
}

static int
memory_pager_exit_handler(int r, const void *ctx GCC_UNUSED)
{
    // TODO: port some functionality from `common_pager_exit_handler'.
    return r;
}

int
more_inmemory(void *content, int size, int promptend)
{
    struct pmore_callbacks cb = common_pager_cb;
    cb.can_live = NULL;     // in-memory content is never live
    int r = pmore2_inmemory(content, size, promptend, NULL, &cb);
    return memory_pager_exit_handler(r, NULL);
}

#endif // USE_PMORE /////////////////////////////////////////////////////////

