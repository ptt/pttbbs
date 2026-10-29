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

/* use new pager: piaip's more. */
static const struct pmore_callbacks common_pager_cb = {
    .process_key = common_pager_key_handler,
    .footer = common_pmore_footer_handler,
};

int
more(const char *fpath, int promptend)
{
    int r = pmore2(fpath, promptend, (void *)fpath, &common_pager_cb);
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
    int r = pmore2_inmemory(content, size, promptend, NULL, &common_pager_cb);
    return memory_pager_exit_handler(r, NULL);
}

#endif // USE_PMORE /////////////////////////////////////////////////////////

