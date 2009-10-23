/* $Id$ */
#include "bbs.h"

// Common Chat Window (CCW)
// piaip's new implementation for chatroom and private talk.
// Author: Hung-Te Lin (piaip)
// --------------------------------------------------------------------------
// Copyright (c) 2009 Hung-Te Lin <piaip@csie.ntu.edu.tw>
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
// Common Chat Window layout:
// [header]
// [header separate line]
// [ccw] user: message\n [CCW_INIT_LINE]
// [ccw] ...
// [ccw] ->\n            [CCW_STOP_LINE]
// [footer separate line][sep_msg]
// prompt -> [input here]
// [footer]
// --------------------------------------------------------------------------

// TODO
// 1. [done] unify talk/chat caption (eg, 聊天/談天室， or 交談/聊天室)
// 2. [done] move anti-flood and other static variables into context...
// 3. [talk done] merge all cmd processor into peek_cmd(CCW_REMOTE)
// 4. log recovery on crash?
// 5. TAB complete...
// 6. add F1 for help?
// 7. better receive buffer

#define CCW_MAX_INPUT_LEN   (STRLEN)
#define CCW_INIT_LINE       (2)
#define CCW_STOP_LINE       (t_lines-3)

// input style
#define CCW_REMOTE      (0)
#define CCW_LOCAL       (1)
#define CCW_LOCAL_MSG   (2)

///////////////////////////////////////////////////////////////////////////
// Data Structure
typedef struct CCW_CTX {
    // session
    int  abort;     // indicate session complete
    void *arg;      // private argument

    // display
    int  line;      // position of next line to append data
    int  reset_scr; // need to redraw everything
    char *sep_msg;  // quick help message on separators

    // input (vget)
    int         abort_vget;     // temporary abort from input
    const int   MAX_INPUT_LEN;  // max vget length, decided at init

    // logging
    FILE *log;      // log file handle
    char *log_fpath;// path of log file

    // network and remote
    int  local_echo;// should we echo local input?
    int  fd;        // remote connection
    char *remote_id;// remote user id
    char *local_id; // local user id

    // layout renderers
    void (*header)      (struct CCW_CTX *);
    void (*prompt)      (struct CCW_CTX *);
    void (*footer)      (struct CCW_CTX *);
    void (*separators)  (struct CCW_CTX *); // render header and footer separate lines

    // content processors
    int  (*peek_key)    (struct CCW_CTX *, int key);
    int  (*peek_cmd)    (struct CCW_CTX *, const char *buf, int local);
    void (*print_line)  (struct CCW_CTX *, const char *buf, int local); // print on screen
    void (*log_line)    (struct CCW_CTX *, const char *buf, int local); // log to file
    void (*post_input)  (struct CCW_CTX *, const char *buf, int local); // after valid input

    // session state change
    void (*init_session)(struct CCW_CTX *);
    void (*end_session )(struct CCW_CTX *);
} CCW_CTX;

///////////////////////////////////////////////////////////////////////////
// CCW helpers

// utilities declaration
void ccw_prepare_line(CCW_CTX *ctx);
int  ccw_partial_match(const char *buf, const char *cmd);

// default callback handlers
static void
ccw_header(CCW_CTX *ctx)
{
    move(0, 0); 
    SOLVE_ANSI_CACHE();
    clrtoeol();

    if (ctx->header)
    {
        ctx->header(ctx);
        return;
    }

    outs("Common Chat Window");
}

static void
ccw_footer(CCW_CTX *ctx)
{
    move(b_lines, 0);
    SOLVE_ANSI_CACHE();
    clrtoeol();

    if (ctx->footer)
    {
        ctx->footer(ctx);
        return;
    }

    vs_footer(" CCW ", " (PgUp/PgDn)回顧訊息記錄\t(Ctrl-C)離開 ");
}

static void
ccw_separators(CCW_CTX *ctx)
{
    int i;

    if (ctx->separators)
    {
        ctx->separators(ctx);
        return;
    }

    move(CCW_INIT_LINE-1, 0);
    vpad(t_columns-2, "─");
    outc('\n');

    i = ctx->sep_msg ? strlen(ctx->sep_msg) : 0;
    move(CCW_STOP_LINE, 0);
    vpad(t_columns-2-i, "─");
    if (i) outs(ctx->sep_msg);
    outc('\n');
}

static void
ccw_prompt(CCW_CTX *ctx)
{
    move(b_lines-1, 0);
    SOLVE_ANSI_CACHE();
    clrtoeol();

    if (ctx->prompt)
    {
        ctx->prompt(ctx);
        return;
    }

    prints("%s: ", ctx->local_id);
}

static void
ccw_init_session(CCW_CTX *ctx)
{
    if (ctx->init_session)
    {
        ctx->init_session(ctx);
        return;
    }
}

static void
ccw_end_session(CCW_CTX *ctx)
{
    if (ctx->end_session)
    {
        ctx->end_session(ctx);
        return;
    }
}

static int
ccw_peek_cmd(CCW_CTX *ctx, const char *buf, int local)
{
    if (ctx->peek_cmd)
        return ctx->peek_cmd(ctx, buf, local);

    if (buf[0] != '/')
        return 0;
    buf++;

    // sample command: bye
    if (ccw_partial_match(buf, "bye"))
    {
        ctx->abort = 1;
        return 1;
    }
    return 0;
}

static void 
ccw_print_line(CCW_CTX *ctx, const char *buf, int local)
{
    ccw_prepare_line(ctx);
    if (ctx->print_line)
    {
        ctx->print_line(ctx, buf, local);
        return;
    }

    if (local <= CCW_LOCAL) // local or remote, but not message
    {
        if (local) {
            outs(ctx->local_id);
        } else {
            outs(ANSI_COLOR(1));
            outs(ctx->remote_id);
        }
        outs(": ");
    }

    outs(buf);
    outs(ANSI_RESET "\n→");
}

static void
ccw_log_line(CCW_CTX *ctx, const char *buf, int local)
{
    if (!ctx->log) 
        return;

    if (ctx->log_line)
    {
        ctx->log_line(ctx, buf, local);
        return;
    }

    if (local <= CCW_LOCAL) // local or remote, but not message
    {
        fprintf(ctx->log, "%s: ",
                local ? ctx->local_id : ctx->remote_id);
    }
    fprintf(ctx->log, "%s\n", buf);
}

// CCW utilities

// clear content and redraw all widgets
void
ccw_reset_scr(CCW_CTX *ctx)
{
    clear();
    ctx->line = CCW_INIT_LINE;
    ctx->reset_scr = NA;
    ccw_header(ctx);
    ccw_separators(ctx);
    // following will be called again for each prompts
    ccw_footer(ctx);
    ccw_prompt(ctx);
}

// prepare screen buffer to display one line of text
void
ccw_prepare_line(CCW_CTX *ctx)
{
    move(ctx->line, 0);
    if ( ctx->line+1 < CCW_STOP_LINE)
    {
        // simply append
        ctx->line ++;
    }
    else 
    {
        // scroll screen buffer
        region_scroll_up(CCW_INIT_LINE, CCW_STOP_LINE - CCW_INIT_LINE);
        move(ctx->line-1, 0);
        // XXX after resize, we may need to scroll more than once.
    }
    clrtoeol();
}

// verify if buf (separated by space) matches partial of cmd (case insensitive)
int
ccw_partial_match(const char *buf, const char *cmd)
{
    size_t szbuf = strcspn(buf, str_space);

    assert(*cmd);
    if (!szbuf)
        return 0;

    return strncasecmp(buf, cmd, szbuf) == 0;
}

// print and log one line of text.
void 
ccw_add_line(CCW_CTX *ctx, const char *buf, int local)
{
    // only print/log local when local_echo is enabled.
    if (local != CCW_LOCAL || ctx->local_echo)
    {
        ccw_print_line(ctx, buf, local);
        if (ctx->log)
            ccw_log_line(ctx, buf, local);
    }

    if (ctx->post_input)
        ctx->post_input(ctx, buf, local);
}

// VGET callback adaptors

static int
ccw_vgetcb_peek(int key, VGET_RUNTIME *prt GCC_UNUSED, void *instance)
{
    CCW_CTX *ctx = (CCW_CTX*) instance;
    assert(ctx);

    if (ctx->peek_key &&
        ctx->peek_key(ctx, key))
    {
        return (ctx->abort_vget || ctx->abort) ? VGETCB_ABORT : VGETCB_NEXT;
    }

    // early check
    if (ctx->abort_vget || ctx->abort)
        return VGETCB_ABORT;

    // common keys
    switch (key)
    {
        case KEY_PGUP:
        case KEY_PGDN:
            if (ctx->log && ctx->log_fpath)
            {
                VREFSCR scr = vscr_save();
                add_io(0, 0);

                fflush(ctx->log);
                more(ctx->log_fpath, YEA);

                add_io(ctx->fd, 0);
                vscr_restore(scr);
            }
            return VGETCB_NEXT;

        case Ctrl('C'):
            {
                VREFSCR scr = vscr_save();
                add_io(0, 0);

                if (vans("確定要離開嗎? [y/N]: ") == 'y')
                    ctx->abort = YEA;

                add_io(ctx->fd, 0);
                vscr_restore(scr);
                // ccw_footer(ctx);
            }
            if (ctx->abort_vget || ctx->abort)
                return VGETCB_ABORT;
            return VGETCB_NEXT;
    }

    // normal data.
    return VGETCB_NONE;
}

// main processor

int
ccw_process(CCW_CTX *ctx)
{
    char inbuf[STRLEN];
    VGET_CALLBACKS vgetcb = { ccw_vgetcb_peek };

    assert( ctx->MAX_INPUT_LEN > 2 &&
            ctx->MAX_INPUT_LEN <= CCW_MAX_INPUT_LEN);

    ccw_init_session(ctx);
    ccw_reset_scr(ctx);

#ifdef DEBUG
    ccw_print_line(ctx, ANSI_COLOR(1;30)
            "◆ Powered by piaip's Common Chat Window" ANSI_RESET, 
            CCW_LOCAL_MSG);
#endif

    while (!ctx->abort)
    {
        // reset screen or print prompts (may be destroyed by short message)
        if (ctx->reset_scr)
            ccw_reset_scr(ctx);
        else {
            ccw_footer(ctx);
            ccw_prompt(ctx);
            // reset again if prompt request to redraw.
            if (ctx->reset_scr)
                ccw_reset_scr(ctx);
        }

        // get input
        ctx->abort_vget = 0;
        vgetstring(inbuf, ctx->MAX_INPUT_LEN, VGET_TRANSPARENT, "", 
                &vgetcb, ctx);

        // quick check for end flag or exit command.
        if (ctx->abort)
            break;

        // quick continue for empty input
        trim(inbuf);
        if (!*inbuf)
            continue;

        // process commands
        if (ccw_peek_cmd(ctx, inbuf, CCW_LOCAL))
            continue;

        // accept this data
        ccw_add_line(ctx, inbuf, CCW_LOCAL);
    }
    ccw_end_session(ctx);

    if (ctx->log)
        fflush(ctx->log);

    return 0;
}

/////////////////////////////////////////////////////////////////////////////
// Talk / Chat Adaptors

#if 1
#define CCW_CAP_TALK        "交談"
#define CCW_CAP_CHAT        "聊天室"
#define CCW_CAP_CHATROOM    "聊天室"
#else
#define CCW_CAP_TALK        "聊天"
#define CCW_CAP_CHAT        "談天室"
#define CCW_CAP_CHATROOM    "談天室"
#endif

int
ccw_talkchat_close_log(CCW_CTX *ctx, int force_decide, int is_chat)
{
    char ans[3];
    const char *fpath = ctx->log_fpath;

    if (!ctx->log)
        return 0;
    assert(*fpath);

    // flush
    fclose(ctx->log);
    ctx->log = NULL;
    more(fpath, NA);


    // prompt user to decide how to deal with the log
    while (1) {
        char c;
        getdata(b_lines - 1, 0, 
                force_decide ? "清除(C) 移至備忘錄(M)? [c/m]: " :
                               "清除(C) 移至備忘錄(M)? [C/m]",
                ans, sizeof(ans), LCECHO);

        // decide default answer
        c = *ans;
        if (c != 'c' && c != 'm')
        {
            if (!force_decide)
                c = 'c';
        }

        if (c == 'c')
            break;
        if (c == 'm')
        {
            char subj[STRLEN];
            if (is_chat)
                snprintf(subj, sizeof(subj), "會議記錄");
            else
                snprintf(subj, sizeof(subj), "對話記錄 (%s)", ctx->remote_id);

            if (mail_log2id(cuser.userid, subj, fpath, "[備.忘.錄]", 0, 1) < 0)
                vmsg("錯誤: 備忘錄儲存失敗。");
            break;
        }

        // force user to input correct answer...
        move(b_lines-2, 0); clrtobot();
        prints(ANSI_COLOR(0;1;3%d) "請正確輸入 c 或 m 的指令。" ANSI_RESET,
                (int)((now % 7)+1));
        if (c == 0) outs("為避免誤按所以只按 ENTER 是不行的。");
    }
    unlink(fpath);
    return 1;
}

/////////////////////////////////////////////////////////////////////////////
// Talk

#define CCW_TALK_CMD_PREFIX '/'
#define CCW_TALK_CMD_PREFIX_STR "/"
#define CCW_TALK_CMD_BYE    "bye"
#define CCW_TALK_CMD_CLEAR  "clear"
#define CCW_TALK_CMD_CLS    "cls"
#define CCW_TALK_CMD_HELP   "help"

static ssize_t
ccw_talk_send(CCW_CTX *ctx, const char *msg)
{
    // protocol: [len][msg]
    char len = strlen(msg);
    assert(len >= 0 && (int)len == strlen(msg));
    if (len < 1) return 0;

    // XXX if remote is closed (without MSG_NOSIGNAL), 
    // this may raise a SIGPIPE and cause BBS to abort...
    if (send(ctx->fd, &len, sizeof(len), MSG_NOSIGNAL) != sizeof(len))
        return -1;
    if (send(ctx->fd,  msg, len, MSG_NOSIGNAL) != len)
        return -1;
    return len;
}

static ssize_t 
ccw_talk_recv(CCW_CTX *ctx, char *buf, size_t szbuf)
{
    char len = 0;
    buf[0] = 0;
    // XXX blocking call here... (change to recv?)
    if (toread(ctx->fd, &len, sizeof(len)) != sizeof(len))
        return -1;
    if (toread(ctx->fd, buf, len)!= len)
        return -1;
    assert(len >= 0 && len < szbuf);
    buf[(size_t)len] = 0;
    return len;
}

static void
ccw_talk_end_session(CCW_CTX *ctx)
{
    // XXX note the target connection may be already closed.
    ccw_talk_send(ctx, CCW_TALK_CMD_PREFIX_STR CCW_TALK_CMD_BYE);
}

static void
ccw_talk_header(CCW_CTX *ctx)
{
    prints(ANSI_COLOR(1;37;46) " 【" CCW_CAP_TALK "】 " 
            ANSI_COLOR(45) " %-*s" ANSI_RESET "\n",
            t_columns - 12, ctx->remote_id);
}

static void
ccw_talk_footer(CCW_CTX *ctx)
{
    vs_footer(" 【" CCW_CAP_TALK "】 ", 
            " (PgUp/PgDn)回顧訊息記錄\t(Ctrl-C)離開 ");
}

static int
ccw_talk_peek_cmd(CCW_CTX *ctx, const char *buf, int local)
{
    if (buf[0] != CCW_TALK_CMD_PREFIX)
        return 0;
    buf++;

    // process remote and local commands
    if (ccw_partial_match(buf, CCW_TALK_CMD_BYE))
    {
        ctx->abort = 1;
        return 1;
    }
    // process local commands
    if (local == CCW_REMOTE)
        return 0;
    if (ccw_partial_match(buf, CCW_TALK_CMD_HELP))
    {
        ccw_print_line(ctx, "[說明] 可輸入 "
                CCW_TALK_CMD_PREFIX_STR CCW_TALK_CMD_CLEAR " 清除畫面或 "
                CCW_TALK_CMD_PREFIX_STR CCW_TALK_CMD_BYE " 離開。", 
                CCW_LOCAL_MSG);
        return 1;
    }
    if (ccw_partial_match(buf, CCW_TALK_CMD_CLEAR) ||
        ccw_partial_match(buf, CCW_TALK_CMD_CLS))
    {
        ctx->reset_scr = YEA;
        return 1;
    }
    return 0;
}

static void
ccw_talk_post_input(CCW_CTX *ctx, const char *buf, int local)
{
    if (local != CCW_LOCAL)
        return;

    // send message to server if possible.
    ctx->abort = (ccw_talk_send(ctx, buf) <= 0);
}

static int
ccw_talk_peek_key(CCW_CTX *ctx, int key)
{
    switch (key) {
        case I_OTHERDATA: // incoming
            {
                char buf[STRLEN];
                if (ccw_talk_recv(ctx, buf, sizeof(buf)) < 1)
                {
                    ctx->abort = YEA;
                    return 1;
                }
                // process commands
                if (ccw_peek_cmd(ctx, buf, CCW_REMOTE))
                    return 1;
                // received something, let's print it.
                ccw_add_line(ctx, buf, CCW_REMOTE);
            }
            return 1;
    }
    return 0;
}

int 
ccw_talk(int fd, int destuid)
{
    char fpath[PATHLEN];
    char remote_id[IDLEN+1], local_id[IDLEN+1];

    CCW_CTX ctx = {
        .fd             = fd,
        .log_fpath      = fpath,
        .local_echo     = YEA,
        .MAX_INPUT_LEN  = STRLEN - IDLEN - 5,    // 5 for ": " and more
        .remote_id      = remote_id,
        .local_id       = local_id,
        .sep_msg        = " /c 清除畫面 /b 離開 ",

        .header     = ccw_talk_header,
        .footer     = ccw_talk_footer,

        .peek_key   = ccw_talk_peek_key,
        .peek_cmd   = ccw_talk_peek_cmd,
        .post_input = ccw_talk_post_input,
        .end_session= ccw_talk_end_session,
    };

    STATINC(STAT_DOTALK);
    setutmpmode(TALK);

    // get dest user id
    assert(getuserid(destuid));
    strlcpy(remote_id, getuserid(destuid), sizeof(remote_id));
    strlcpy(local_id,  cuser.userid, sizeof(local_id));
    assert(ctx.remote_id[0]);
    assert(ctx.local_id[0]);

    // create log file
    setuserfile(fpath, "talk_XXXXXX");
    ctx.log = fdopen(mkstemp(fpath), "w");
    assert(ctx.log);
    fprintf(ctx.log, "[%s] 與 %s " CCW_CAP_TALK ":\n", Cdatelite(&now), ctx.remote_id);

    // main processor
    add_io(fd, 0);
    ccw_process(&ctx);

    // clean network resource
    add_io(0, 0);
    assert(fd == ctx.fd);
    close(fd);

    // close log file
    ccw_talkchat_close_log(&ctx, YEA, NA);

    return 0;
}

/////////////////////////////////////////////////////////////////////////////
// Chat

#define CHAT_ID_LEN     (8)
#define CHAT_ROOM_LEN   (IDLEN)
#define CHAT_TOPIC_LEN  (48)    // must < t_columns-CHAT_ROOM_LEN

typedef struct ccw_chat_ext {
    // misc
    int  newmail;
    int  old_rows, old_cols;    // for terminal resize auto detection
    // anti flood
    int  flood;
    time4_t lasttime;
    // buffer
    int  buf_remains;
    char buf[128];
    // topic
    char topic[CHAT_TOPIC_LEN+1];
} ccw_chat_ext;

static ccw_chat_ext *
ccw_chat_get_ext(CCW_CTX *ctx)
{
    assert(ctx->arg);
    return (ccw_chat_ext*)ctx->arg;
}

static void
ccw_chat_check_newmail(CCW_CTX *ctx)
{
    ccw_chat_ext *ext = ccw_chat_get_ext(ctx);
    if (ISNEWMAIL(currutmp))
    {
        if (!ext->newmail)
        {
            // no need to log this...?
            ccw_print_line(ctx, "◆ 您有未讀的新信件。", CCW_LOCAL_MSG);
            ext->newmail = 1;
        }
    }
    else if (ext->newmail)
        ext->newmail = 0;
}

static void
ccw_chat_check_term_resize(CCW_CTX *ctx)
{
    ccw_chat_ext *ext = ccw_chat_get_ext(ctx);

    // detect terminal resize
    if (ext->old_rows != t_lines ||
        ext->old_cols != t_columns)
    {
        ext->old_rows = t_lines;
        ext->old_cols = t_columns;
        ctx->reset_scr = YEA;
    }
}

static int
ccw_chat_send(CCW_CTX *ctx, const char *buf)
{
    int  len;
    char genbuf[200];

    len = snprintf(genbuf, sizeof(genbuf), "%s\n", buf);
    // XXX if remote is closed (without MSG_NOSIGNAL), 
    // this may raise a SIGPIPE and cause BBS to abort...
    return (send(ctx->fd, genbuf, len, MSG_NOSIGNAL) == len);
}

static void
ccw_chat_end_session(CCW_CTX *ctx)
{
    // XXX note the target connection may be already closed.
    ccw_chat_send(ctx, "/bye");    // protocol: bye
}

static void
ccw_chat_header(CCW_CTX *ctx)
{

    prints(ANSI_COLOR(1;37;46) " " CCW_CAP_CHAT " [%-*s] " 
            ANSI_COLOR(45) " 話題: %-*s" ANSI_RESET, 
            CHAT_ROOM_LEN, ctx->remote_id, 
            t_columns - CHAT_ROOM_LEN - 20,
            ccw_chat_get_ext(ctx)->topic);
}

static void
ccw_chat_footer(CCW_CTX *ctx)
{
    ccw_chat_check_term_resize(ctx);

    // a little weird, but this looks like the best place to do ZA here...
    if (ZA_Waiting())
    {
        // process ZA
        VREFSCR scr = vscr_save();
        add_io(0, 0);

        ZA_Enter();

        add_io(ctx->fd, 0);
        vscr_restore(scr);
    }

    // draw real footer
    vs_footer("【" CCW_CAP_CHAT "】", " (PgUp/PgDn)回顧" CCW_CAP_CHAT "記錄 "
            "(Ctrl-Z)快速切換\t(Ctrl-C)離開" CCW_CAP_CHAT);
}

static void
ccw_chat_prompt(CCW_CTX *ctx)
{
    prints("%-*s》", CHAT_ID_LEN, ctx->local_id);
}

#ifdef EXP_CHAT_HIGHLIGHTS
static int
word_match_index(const char *buf, const char *pattern, size_t szpat)
{
    const char *p = buf;

    if (!szpat) szpat = strlen(pattern);

    while ((p = strcasestr(p, pattern)) != NULL)
    {
        if (p > buf && isascii(*(p-1)) && isalnum(*(p-1)))
        {
            p++; 
            continue;
        }
        // FIXME this may skip to much, but highlights are not that important...
        p += szpat;
        if (isascii(*p) && isalnum(*p))
        {
            p++;
            continue;
        }
        return p - buf - szpat;
    }
    return -1;
}

static void
ccw_chat_print_highlights(CCW_CTX *ctx, const char *s)
{
    int m = -1, n;
    n = strlen(cuser.userid);
    if (n > 2) 
        m = word_match_index(s, cuser.userid, n);
    if (m < 0)
    {
        n = strlen(ctx->local_id);
        if (n > 2)
            m = word_match_index(s, ctx->local_id, n);
    }

    if (m >= 0)
    {
        outns(s, m);
        outs(ANSI_COLOR(1;33));
        outns(s+m, n);
        outs(ANSI_RESET);
        outs(s+m+n);
    } else {
        outs(s);
    }
}
#endif // EXP_CHAT_HIGHLIGHTS

static void
ccw_chat_print_line(CCW_CTX *ctx, const char *buf, int local)
{
#ifdef EXP_CHAT_HIGHLIGHTS
    size_t szid;

    // let's try harder to recognize the content
    if (local != CCW_REMOTE || buf[0] == ' '|| 
        strchr(buf, ESC_CHR) || !strchr(buf, ':') )
    {
        // pre-formatted or messages
        outs(buf);
    }
    else if ((szid = strlen(ctx->local_id)) &&
              strncmp(buf, ctx->local_id, szid) == 0 &&
              buf[szid] == ':')
    {
        // message from myself
        outs(ANSI_COLOR(1)); outs(ctx->local_id); outs(ANSI_RESET);
        outs(buf+szid);
    } 
    else 
    {
        // message from others
        ccw_chat_print_highlights(ctx, buf);
    }
#else  // !EXP_CHAT_HIGHLIGHTS
    outs(buf);
#endif // !EXP_CHAT_HIGHLIGHTS
    assert(local != CCW_LOCAL);
    outs(ANSI_RESET "\n→");
}

static void
ccw_chat_log_line(CCW_CTX *ctx, const char *buf, int local)
{
    assert(local != CCW_LOCAL);
    fprintf(ctx->log, "%s\n", buf);
}

static int
ccw_chat_recv(CCW_CTX *ctx)
{
    int  c, len;
    char *bptr;
    ccw_chat_ext *ext = ccw_chat_get_ext(ctx);

    // ext->buf may already contain up to ext->buf_remains bytes of data.
    // xchatd protocol: msg NUL /command NUL ...

    len = sizeof(ext->buf) - ext->buf_remains - 1;
    if ((c = recv(ctx->fd, ext->buf + ext->buf_remains, len, 0)) <= 0)
        return -1;

    c += ext->buf_remains;
    bptr = ext->buf;

    while (c > 0) {
        len = strlen(bptr) + 1;
        if (len > c && (size_t)len < (sizeof(ext->buf)/ 2) )
            break;

        if (*bptr != '/') 
        {
            // received some data, let's print it.
            if (*bptr != '>' || PERM_HIDE(currutmp))
                ccw_add_line(ctx, bptr, CCW_REMOTE);

        } else switch (bptr[1]) {

            case 'c':
                ccw_reset_scr(ctx);
                break;

            case 'n':
                strlcpy(ctx->local_id, bptr+2, CHAT_ID_LEN+1);
                DBCS_safe_trim(ctx->local_id);
                ccw_prompt(ctx);
                break;

            case 'r':
                strlcpy(ctx->remote_id, bptr+2, CHAT_ROOM_LEN+1);
                DBCS_safe_trim(ctx->remote_id);
                ccw_header(ctx);
                break;

            case 't':
                strlcpy(ext->topic, bptr+2, CHAT_TOPIC_LEN+1);
                DBCS_safe_trim(ext->topic);
                ccw_header(ctx);
                break;

            default:
                // unknown command... ignore.
#ifdef DEBUG
                assert(!"unknown xchatd response.");
#endif
                break;
        }

        c -= len;
        bptr += len;
    }

    if (c > 0) {
        memmove(ext->buf, bptr, sizeof(ext->buf)-(bptr-ext->buf));
        ext->buf_remains = len - 1;
    } else
        ext->buf_remains = 0;
    return 0;
}

#ifdef EXP_ANTIFLOOD

// prevent flooding */
static void
ccw_chat_anti_flood(CCW_CTX *ctx)
{
    ccw_chat_ext *ext = ccw_chat_get_ext(ctx);

    syncnow();
    if (now - ext->lasttime < 3 )
    {
        // 3 秒內洗半面是不行的 ((25-5)/2)
        if( ++ext->flood > 10 )
        {
            // flush all input!
            unsigned char garbage[STRLEN];
            drop_input();
            while (wait_input(1, 0))
            {
                if (num_in_buf())
                    drop_input();
                else
                    tty_read(garbage, sizeof(garbage));
            }
            drop_input();
            vmsg("請勿大量剪貼或造成洗板面的效果。");

            // log?
            sleep(2);
        }
    } else {
        ext->lasttime = now;
        ext->flood = 0;
    }
}
#endif // EXP_ANTIFLOOD

static int
ccw_chat_peek_cmd(CCW_CTX *ctx, const char *buf, int local)
{
    ccw_chat_check_newmail(ctx);
#ifdef EXP_ANTIFLOOD
    ccw_chat_anti_flood(ctx);
#endif

    if (buf[0] != '/') return 0;
    buf ++;

    if (ccw_partial_match(buf, "help"))
    {
        static const char * const hlp_op[] = {
            "[/f]lag [+-][ls]", "設定鎖定、秘密狀態",
            "[/i]nvite <id>", "邀請 <id> 加入" CCW_CAP_CHATROOM,
            "[/k]ick <id>", "將 <id> 踢出" CCW_CAP_CHATROOM,
            "[/o]p <id>", "將 Op 的權力轉移給 <id>",
            "[/t]opic <text>", "換個話題",
            "[/w]all", "廣播 (站長專用)",
            " /ban <userid>", "拒絕 <userid> 再次進入此" CCW_CAP_CHATROOM " (加入黑名單)",
            " /unban <userid>", "把 <userid> 移出黑名單",
            NULL,
        }, * const hlp[] = {
            " /help op", CCW_CAP_CHAT "管理員專用指令",
            "[//]help", "MUD-like 社交動詞",
            "[/a]ct <msg>", "做一個動作",
            "[/b]ye [msg]", "道別",
            "[/c]lear", "清除螢幕",
            "[/j]oin <room>", "建立或加入" CCW_CAP_CHATROOM,
            "[/l]ist [room]", "列出" CCW_CAP_CHATROOM "使用者",
            "[/m]sg <id> <msg>", "跟 <id> 說悄悄話",
            "[/n]ick <id>", "將暱稱換成 <id>",
            "[/p]ager", "切換呼叫器",
            "[/r]oom ", "列出一般" CCW_CAP_CHATROOM,
            "[/w]ho", "列出本" CCW_CAP_CHATROOM "使用者",
            " /whoin <room>", "列出" CCW_CAP_CHAT " <room> 的使用者",
            " /ignore <userid>", "忽略指定使用者的訊息",
            " /unignore <userid>", "停止忽略指定使用者的訊息",
            NULL,
        };

        const char * const *p = hlp;
        char msg[STRLEN];

        if (strcasestr(buf, " op"))
        {
            p = hlp_op;
            ccw_print_line(ctx, CCW_CAP_CHAT "管理員專用指令", CCW_LOCAL_MSG);
        }
        while (*p)
        {
            snprintf(msg, sizeof(msg), "  %-20s- %s", p[0], p[1]);
            ccw_print_line(ctx, msg, CCW_LOCAL_MSG);
            p += 2;
        }
        return 1;
    }
    if (ccw_partial_match(buf, "clear") ||
        ccw_partial_match(buf, "cls"))
    {
        ccw_reset_scr(ctx);
        return 1;
    }
    if (ccw_partial_match(buf, "date"))
    {
        char genbuf[STRLEN];
        syncnow();
        snprintf(genbuf, sizeof(genbuf),
                "◆ " BBSNAME "標準時間: %s", Cdate(&now));
        ccw_add_line(ctx, genbuf, CCW_LOCAL_MSG);
        return 1;
    }
    if (ccw_partial_match(buf, "pager"))
    {
        char genbuf[STRLEN];
        currutmp->pager ++;
        currutmp->pager %= PAGER_MODES;
        snprintf(genbuf, sizeof(genbuf), "◆ 您的呼叫器已設為: [%s]",
                str_pager_modes[currutmp->pager]);
        ccw_add_line(ctx, genbuf, CCW_LOCAL_MSG);
        return 1;
    }
    if (ccw_partial_match(buf, "bye"))
    {
        ctx->abort = 1;
        return 1;
    }
    return 0;
}

static void
ccw_chat_post_input(CCW_CTX *ctx, const char *buf, int local)
{
    if (local != CCW_LOCAL)
        return;

    // send message to server if possible.
    ctx->abort = (ccw_chat_send(ctx, buf) <= 0);
}

static int
ccw_chat_peek_key(CCW_CTX *ctx, int key)
{
    switch (key) {
        case I_OTHERDATA: // incoming
            if (ccw_chat_recv(ctx) == -1)
            {
                ctx->abort = YEA;
                return 1;
            }
            ccw_chat_check_newmail(ctx);
            return 1;

            // Support ZA because chat is mostly independent and secure.
        case Ctrl('Z'):
            {
                int za = 0;
                VREFCUR cur = vcur_save();
                add_io(0, 0);
                za = ZA_Select();
                ccw_footer(ctx);
                add_io(ctx->fd, 0);
                vcur_restore(cur);
                if (za)
                    ctx->abort_vget = 1;
            }
            return 1;
    }
    return 0;
}

int 
ccw_chat(int fd)
{
    char chatid[CHAT_ID_LEN+1]   = "myid",
         roomid[CHAT_ROOM_LEN+1] = "room";
    char fpath[PATHLEN];
    ccw_chat_ext ext = { 
        .old_rows = t_lines,
        .old_cols = t_columns,
    };
    CCW_CTX ctx = {
        .fd             = fd,
        .log_fpath      = fpath,
        .local_echo     = NA,
        .MAX_INPUT_LEN  = STRLEN - CHAT_ID_LEN - 3, // 3 for ": "
        .remote_id      = roomid,
        .local_id       = chatid,
        .arg            = &ext,
        .sep_msg        = " /h 查詢指令 /b 離開 ",

        .header     = ccw_chat_header,
        .footer     = ccw_chat_footer,
        .prompt     = ccw_chat_prompt,
        .print_line = ccw_chat_print_line,
        .log_line   = ccw_chat_log_line,

        .peek_key   = ccw_chat_peek_key,
        .peek_cmd   = ccw_chat_peek_cmd,
        .post_input = ccw_chat_post_input,
        .end_session= ccw_chat_end_session,
    };

    // initialize nick
    while (1) {
        const char *err = "無法使用此代號";
        char cmd[200];

        getdata(b_lines - 1, 0, "請輸入想在" CCW_CAP_CHAT "使用的暱稱: ", 
                chatid, sizeof(chatid), DOECHO);
        if(!chatid[0])
            strlcpy(chatid, cuser.userid, sizeof(chatid));

        // safe truncate
        DBCS_safe_trim(chatid);

        // login format: /! UserID ChatID password
        snprintf(cmd, sizeof(cmd), 
                "/! %s %s %s", cuser.userid, chatid, cuser.passwd);
        ccw_chat_send(&ctx, cmd);
        if (recv(ctx.fd, cmd, 3, 0) != 3) {
            close(ctx.fd);
            vmsg("系統錯誤。");
            return 0;
        }

        if (strcmp(cmd, CHAT_LOGIN_OK) == 0)
            break;
        else if (!strcmp(cmd, CHAT_LOGIN_EXISTS))
            err = "這個代號已經有人用了";
        else if (!strcmp(cmd, CHAT_LOGIN_INVALID))
            err = "這個代號是錯誤的";
        else if (!strcmp(cmd, CHAT_LOGIN_BOGUS))
            err = "請勿派遣分身進入!!";

        move(b_lines - 2, 0);
        outs(err);
        clrtoeol();
        bell();
    }

    setutmpmode(CHATING);
    currutmp->in_chat = YEA;
    strlcpy(currutmp->chatid, chatid, sizeof(currutmp->chatid));

    // generate log file
    setuserfile(fpath, "chat_XXXXXX");
    ctx.log = fdopen(mkstemp(fpath), "w");
    assert(ctx.log);
    fprintf(ctx.log, "[%s] 進入" CCW_CAP_CHAT ":\n", Cdatelite(&now));

    // main processor
    add_io(fd, 0);
    ccw_process(&ctx);

    // clean network resource
    add_io(0, 0);
    close(fd);
    currutmp->in_chat = currutmp->chatid[0] = 0;

    // close log file
    ccw_talkchat_close_log(&ctx, NA, YEA);

    return 0;
}

/////////////////////////////////////////////////////////////////////////////
// vim:sw=4:ts=8:et
