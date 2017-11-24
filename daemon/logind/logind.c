// The High Performance Login Daemon (works with tunnel mode)
// $Id$
//
// Create:       Hung-Te Lin <piaip@csie.ntu.edu.tw>
// Contributors: wens, kcwu
// Initial Date: 2009/06/01
// --------------------------------------------------------------------------
// Copyright (C) 2009, Hung-Te Lin <piaip@csie.ntu.edu.tw>
// All rights reserved
// Distributed under BSD license (GPL compatible).
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// --------------------------------------------------------------------------

// TODO:
// 1. [done] cache guest's usernum and check if too many guests online
// 2. [drop] change close connection to 'wait until user hit then close'
// 3. [done] regular check text screen files instead of HUP?
// 4. [done] re-start mbbsd if pipe broken?
// 5. [drop] clean mbbsd pid log files?
// 6. [done] handle non-block i/o
// 7. [done] asynchronous tunnel handshake
// 8. simplify async ack queue to ordered sequence
// 9. force telnet_init_cmd to complete
// 10.prioritized logattempt (and let connection to wait logattempt to end)

#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <event.h>

#ifndef FIONWRITE
#include <linux/sockios.h>
#define FIONWRITE SIOCOUTQ
#endif

// XXX should we need to keep this definition?
#define _BBS_UTIL_C_

#include "bbs.h"
#include "daemons.h"

#ifndef LOGIND_REGULAR_CHECK_DURATION
#define LOGIND_REGULAR_CHECK_DURATION   (30)
#endif 

#ifndef LOGIND_MAX_FDS
#define LOGIND_MAX_FDS      (100000)
#endif

#ifndef LOGIND_ACKQUEUE_BOUND
#define LOGIND_ACKQUEUE_BOUND   (255)
#endif 

#ifndef LOGIND_TUNNEL_BUFFER_BOUND
#define LOGIND_TUNNEL_BUFFER_BOUND  (32768)
#endif

#define MY_EVENT_PRIORITY_NUMBERS   (4)
#define EVTPRIORITY_NORM    (MY_EVENT_PRIORITY_NUMBERS/2)
#define EVTPRIORITY_ACK     (EVTPRIORITY_NORM-1)

// some systems has hard limit of this to 128.
#ifndef LOGIND_SOCKET_QLEN
#define LOGIND_SOCKET_QLEN  (100)
#endif

#ifndef AUTHFAIL_SLEEP_SEC
#define AUTHFAIL_SLEEP_SEC  (15)
#endif

#ifndef OVERLOAD_SLEEP_SEC
#define OVERLOAD_SLEEP_SEC  (60)
#endif

#ifndef ACK_TIMEOUT_SEC
#define ACK_TIMEOUT_SEC     (5*60)
#endif

#ifndef BAN_SLEEP_SEC
#define BAN_SLEEP_SEC       (60)
#endif

#ifndef IDLE_TIMEOUT_SEC
#define IDLE_TIMEOUT_SEC    (20*60)
#endif

#ifndef MAX_TEXT_SCREEN_LINES
#define MAX_TEXT_SCREEN_LINES   (24)
#endif

#ifndef TUNNEL_BUFFER_SIZE
#define TUNNEL_BUFFER_SIZE  (131072)
#endif

#ifndef OPTIMIZE_SOCKET
#define OPTIMIZE_SOCKET(sock) do {} while(0)
#endif

// to prevent flood trying services...
#ifndef LOGIND_MAX_RETRY_SERVICE
#define LOGIND_MAX_RETRY_SERVICE   (15)
#endif

#ifndef LOGIND_INITIAL_ENCODING
#define LOGIND_INITIAL_ENCODING (0)
#endif

// local definiions
#define MY_SVC_NAME  "logind"
#define LOG_PREFIX  "[logind] "

// #define ENABLE_DEBUG_IO
#define DEBUG_IO_LIMIT (8192)

#ifndef LOGIND_DEFAULT_PID_PATH
#define LOGIND_DEFAULT_PID_PATH (BBSHOME "/run/logind.pid")
#endif

///////////////////////////////////////////////////////////////////////
// global variables
int g_tunnel;           // tunnel for service daemon
int g_logattempt_pipe;  // pipe to log attempts
int g_tunnel_send_buffer_size;  // buffer window size of tunnel.

// server status
int g_overload = 0;
int g_banned   = 0;
int g_opened_fd= 0;
int g_nonblock = 1;
int g_async_ack= 1;
int g_async_logattempt = 1;

// debug and reporting
int g_verbose  = 0;
int g_report_timeout = 0;
char g_logfile_path[PATHLEN];
char g_pidfile_path[PATHLEN];

// retry service
char g_retry_cmd[PATHLEN];
int  g_retry_times;

// cache data
int g_reload_data = 1;  // request to reload data
time4_t g_welcome_mtime;
int g_guest_usernum  = 0;  // numeric uid of guest account
int g_guest_too_many = 0;  // 1 if exceed MAX_GUEST

// banned ip
BanIpList *g_banip;
time4_t g_banip_mtime = -1;

// options
int g_reuseport = 0;

enum {
    VERBOSE_ERROR,
    VERBOSE_INFO,
    VERBOSE_DEBUG
};

///////////////////////////////////////////////////////////////////////
// login context, constants and states

enum {
    LOGIN_CONN_STATE_CONNDATA = 1,
    LOGIN_CONN_STATE_TERMINAL,

    LOGIN_STATE_INIT  = 1,
    LOGIN_STATE_USERID,
    LOGIN_STATE_PASSWD,
    LOGIN_STATE_AUTH,
    LOGIN_STATE_WAITACK,

    LOGIN_HANDLE_WAIT = 1,
    LOGIN_HANDLE_BEEP,
    LOGIN_HANDLE_OUTC,
    LOGIN_HANDLE_REDRAW_USERID,
    LOGIN_HANDLE_BS,
    LOGIN_HANDLE_PROMPT_PASSWD,
    LOGIN_HANDLE_START_AUTH,

    AUTH_RESULT_FAIL_INSECURE = -4,
    AUTH_RESULT_STOP   = -3,
    AUTH_RESULT_FREEID_TOOMANY = -2,
    AUTH_RESULT_FREEID = -1,
    AUTH_RESULT_FAIL   = 0,
    AUTH_RESULT_RETRY  = AUTH_RESULT_FAIL,
    AUTH_RESULT_OK     = 1,
};

#ifdef  CONVERT
#define IDBOXLEN    (IDLEN+2)   // one extra char for encoding
#else
#define IDBOXLEN    (IDLEN+1)
#endif

typedef struct {
    int  state;
    int  retry;
    int  encoding;
    int  t_lines;
    int  t_cols;
    int  icurr;         // cursor (only available in userid input mode)
    Fnv32_t client_code;
    int  is_secure_connection;
    char userid [IDBOXLEN];
    char pad0;   // for safety
    char passwd [PASSLEN+1];
    char pad1;   // for safety
    char hostip [IPV4LEN+1];
    char pad2;   // for safety
    char port   [IDLEN+1];
    char pad3;   // for safety
    // buffer for extra connection data
    conn_data cdata;
    int       cdata_len;
    // remote address
    struct sockaddr_in addr;
    socklen_t addr_len;
} login_ctx;

typedef struct {
    unsigned int cb;
    time4_t      enter;
    struct bufferevent *bufev;
    struct event ev;
    TelnetCtx    telnet;
    VtkbdCtx     vtkbd;
    login_ctx    ctx;
    int          state;
} login_conn_ctx;

typedef struct {
    size_t  cb;
    time4_t logtime;
    char    userid[IDLEN+1];
    char    hostip[IPV4LEN+1];
} logattempt_ctx;

enum {
    BIND_EVENT_WILL_PASS_CONNDATA = 1 << 0,
};

typedef struct {
    struct event ev;
    char   bind_name[PATHLEN];
    int    flags;
} bind_event;

void 
login_ctx_init(login_ctx *ctx)
{
    assert(ctx);
    memset(ctx, 0, sizeof(login_ctx));
    ctx->state = LOGIN_STATE_INIT;
    ctx->client_code = FNV1_32_INIT;
    ctx->encoding = LOGIND_INITIAL_ENCODING;
}

static int
login_ctx_has_conn_data(login_ctx *ctx)
{
    return ctx->cdata_len == sizeof(ctx->cdata) ? 1 : 0;
}

int 
login_ctx_retry(login_ctx *ctx)
{
    assert(ctx);
    ctx->state = LOGIN_STATE_USERID;
    ctx->encoding = login_ctx_has_conn_data(ctx) ? ctx->cdata.encoding
                                                 : LOGIND_INITIAL_ENCODING;
    memset(ctx->userid, 0, sizeof(ctx->userid));
    memset(ctx->passwd, 0, sizeof(ctx->passwd));
    ctx->icurr    = 0;
    // do not touch hostip, client code, t_*
    ctx->retry ++;
    return ctx->retry;
}

int 
login_ctx_handle(login_ctx *ctx, int c)
{
    int l;

    assert(ctx);
    switch(ctx->state)
    {
        case LOGIN_STATE_INIT:
        case LOGIN_STATE_USERID:
            l = strlen(ctx->userid);

            switch(c)
            {
                case KEY_ENTER:
                    ctx->state = LOGIN_STATE_PASSWD;
                    return LOGIN_HANDLE_PROMPT_PASSWD;

                case KEY_BS:
                    if (!l || !ctx->icurr)
                        return LOGIN_HANDLE_BEEP;
                    if (ctx->userid[ctx->icurr])
                    {
                        ctx->icurr--;
                        memmove(ctx->userid + ctx->icurr,
                                ctx->userid + ctx->icurr+1,
                                l - ctx->icurr);
                        return LOGIN_HANDLE_REDRAW_USERID;
                    }
                    // simple BS
                    ctx->icurr--;
                    ctx->userid[l-1] = 0;
                    return LOGIN_HANDLE_BS;

                case Ctrl('D'):
                case KEY_DEL:
                    if (!l || !ctx->userid[ctx->icurr])
                        return LOGIN_HANDLE_BEEP;
                    memmove(ctx->userid + ctx->icurr,
                            ctx->userid + ctx->icurr+1,
                            l - ctx->icurr);
                    return LOGIN_HANDLE_REDRAW_USERID;

                case Ctrl('B'):
                case KEY_LEFT:
                    if (ctx->icurr)
                        ctx->icurr--;
                    return LOGIN_HANDLE_REDRAW_USERID;

                case Ctrl('F'):
                case KEY_RIGHT:
                    if (ctx->userid[ctx->icurr])
                        ctx->icurr ++;
                    return LOGIN_HANDLE_REDRAW_USERID;

                case Ctrl('A'):
                case KEY_HOME:
                    ctx->icurr = 0;
                    return LOGIN_HANDLE_REDRAW_USERID;

                case Ctrl('E'):
                case KEY_END:
                    ctx->icurr = l;
                    return LOGIN_HANDLE_REDRAW_USERID;

                case Ctrl('K'):
                    if (!l || !ctx->userid[ctx->icurr])
                        return LOGIN_HANDLE_BEEP;
                    memset( ctx->userid + ctx->icurr, 0,
                            l - ctx->icurr +1);
                    return LOGIN_HANDLE_REDRAW_USERID;
            }

            // default: insert characters
            if (!isascii(c) || !isprint(c) || 
                c == ' ' || (size_t)l + 1 >= sizeof(ctx->userid))
                return LOGIN_HANDLE_BEEP;

            memmove(ctx->userid + ctx->icurr + 1,
                    ctx->userid + ctx->icurr,
                    l - ctx->icurr +1);
            ctx->userid[ctx->icurr++] = c;

            if (ctx->icurr != l+1)
                return LOGIN_HANDLE_REDRAW_USERID;

            return LOGIN_HANDLE_OUTC;

        case LOGIN_STATE_PASSWD:
            l = strlen(ctx->passwd);

            if (c == KEY_ENTER)
            {
                // no matter what, apply the passwd
                ctx->state = LOGIN_STATE_AUTH;
                return LOGIN_HANDLE_START_AUTH;
            }
            if (c == KEY_BS)
            {
                if (!l)
                    return LOGIN_HANDLE_BEEP;
                ctx->passwd[l-1] = 0;
                return LOGIN_HANDLE_WAIT;
            }

            // XXX check VGET_PASSWD = VGET_NOECHO|VGET_ASCIIONLY
            if ( (!isascii(c) || !isprint(c)) || 
                (size_t)l + 1 >= sizeof(ctx->passwd))
                return LOGIN_HANDLE_BEEP;

            ctx->passwd[l] = c;

            return LOGIN_HANDLE_WAIT;

        default:
            break;
    }
    return LOGIN_HANDLE_BEEP;
}

///////////////////////////////////////////////////////////////////////
// Mini Queue

#ifndef ACK_QUEUE_DEFAULT_CAPACITY
#define ACK_QUEUE_DEFAULT_CAPACITY  (128)
#endif

struct login_ack_queue
{
    login_conn_ctx **queue;
    size_t           size;
    size_t           reuse;
    size_t           capacity;
};

static struct login_ack_queue g_ack_queue;

static void
ackq_gc()
{
    // reset queue to zero if already empty.
    if (g_ack_queue.reuse == g_ack_queue.size)
        g_ack_queue.reuse =  g_ack_queue.size = 0;
}

static ssize_t
ackq_size()
{
    return g_ack_queue.size - g_ack_queue.reuse;
}

static void
ackq_add(login_conn_ctx *ctx)
{
    assert(ctx->cb == sizeof(login_conn_ctx));
    if (g_ack_queue.reuse)
    {
        // there's some space in the queue, let's use it.
        size_t i;
        for (i = 0; i < g_ack_queue.size; i++)
        {
            if (g_ack_queue.queue[i])
                continue;

            g_ack_queue.queue[i] = ctx;
            g_ack_queue.reuse--;
            ackq_gc();
            return;
        }
        assert(!"corrupted ack queue");
        // may cause leak here, since queue is corrupted.
        return;
    }

    if (++g_ack_queue.size > g_ack_queue.capacity)
    {
        time4_t tnow = time(NULL);
        g_ack_queue.capacity *= 2;
        if (g_ack_queue.capacity < ACK_QUEUE_DEFAULT_CAPACITY)
            g_ack_queue.capacity = ACK_QUEUE_DEFAULT_CAPACITY;

        fprintf(stderr, LOG_PREFIX "%s: resize ack queue to: %u (%u in use)\n", 
                Cdate(&tnow),
                (unsigned int)g_ack_queue.capacity, (unsigned int)g_ack_queue.size);

        g_ack_queue.queue = (login_conn_ctx**) realloc (g_ack_queue.queue, 
                sizeof(login_conn_ctx*) * g_ack_queue.capacity);
        assert(g_ack_queue.queue);
    }
    g_ack_queue.queue[g_ack_queue.size-1] = ctx;
    ackq_gc();
}

static int
ackq_del(login_conn_ctx *conn)
{
    size_t i;

    // XXX in current implementation, the conn pointer may be
    // destroyed before getting acks, so don't check its validness.
    // assert(conn && conn->cb == sizeof(login_conn_ctx));
    for (i = 0; i < g_ack_queue.size; i++)
    {
        if (g_ack_queue.queue[i] != conn)
            continue;

        // found the target
        g_ack_queue.queue[i] = NULL;

        if (i+1 == g_ack_queue.size)
            g_ack_queue.size--;
        else
            g_ack_queue.reuse++;

        ackq_gc();
        return 1;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////
// I/O

static ssize_t 
_buff_write(login_conn_ctx *conn, const void *buf, size_t nbytes)
{
    return bufferevent_write(conn->bufev, buf, nbytes);
}

///////////////////////////////////////////////////////////////////////
// Mini Terminal

static void 
_mt_bell(login_conn_ctx *conn)
{
    static const char b = Ctrl('G');
    _buff_write(conn, &b, 1);
}

static void 
_mt_bs(login_conn_ctx *conn)
{
    static const char cmd[] = "\b \b";
    _buff_write(conn, cmd, sizeof(cmd)-1);
}

static void 
_mt_home(login_conn_ctx *conn)
{
    static const char cmd[] = ESC_STR "[H";
    _buff_write(conn, cmd, sizeof(cmd)-1);
}

static void 
_mt_clrtoeol(login_conn_ctx *conn)
{
    static const char cmd[] = ESC_STR "[K";
    _buff_write(conn, cmd, sizeof(cmd)-1);
}

static void 
_mt_clear(login_conn_ctx *conn)
{
    static const char cmd[] = ESC_STR "[2J";
    _mt_home(conn);
    _buff_write(conn, cmd, sizeof(cmd)-1);
}

static void 
_mt_move_yx(login_conn_ctx *conn, const char *mcmd)
{
    static const char cmd1[] = ESC_STR "[",
                      cmd2[] = "H";
    _buff_write(conn, cmd1, sizeof(cmd1)-1);
    _buff_write(conn, mcmd, strlen(mcmd));
    _buff_write(conn, cmd2, sizeof(cmd2)-1);
}

///////////////////////////////////////////////////////////////////////
// Telnet Protocol

static void 
_telnet_resize_term_cb(void *resize_arg, int w, int h)
{
    login_ctx *ctx = (login_ctx*) resize_arg;
    assert(ctx);
    ctx->t_lines = h;
    ctx->t_cols  = w;
}

static void 
_telnet_update_cc_cb(void *cc_arg, unsigned char c)
{
    login_ctx *ctx = (login_ctx*) cc_arg;
    assert(ctx);
    // fprintf(stderr, "update cc: %08lX", (unsigned long)ctx->client_code);
    FNV1A_CHAR(c, ctx->client_code);
    // fprintf(stderr, "-> %08lX\r\n", (unsigned long)ctx->client_code);
}

static void 
_telnet_write_data_cb(void *write_arg, int fd GCC_UNUSED,
                      const void *buf, size_t nbytes)
{
    login_conn_ctx *conn = (login_conn_ctx *)write_arg;
    _buff_write(conn, buf, nbytes);
}

#ifdef  LOGIND_OPENFD_IN_AYT
static void 
_telnet_send_ayt_cb(void *ayt_arg, int fd GCC_UNUSED)
{
    login_conn_ctx *conn = (login_conn_ctx *)ayt_arg;
    char buf[64];

    assert(conn);
    if (!g_async_ack)
    {
        snprintf(buf, sizeof(buf), "  (#%d)fd:%u  \r\n", 
                g_retry_times, g_opened_fd);
    }
    else
    {
        snprintf(buf, sizeof(buf), "  (#%d)fd:%u,ack:%u(-%u)  \r\n", 
                g_retry_times, g_opened_fd, 
                (unsigned int)g_ack_queue.size, 
                (unsigned int)g_ack_queue.reuse );
    }
    _buff_write(conn, buf, strlen(buf));
}
#endif

static const struct TelnetCallback 
telnet_callback = {
    _telnet_write_data_cb,
    _telnet_resize_term_cb,

#ifdef DETECT_CLIENT
    _telnet_update_cc_cb,
#else
    NULL,
#endif

#ifdef LOGIND_OPENFD_IN_AYT
    _telnet_send_ayt_cb,
#else
    NULL,
#endif
};

///////////////////////////////////////////////////////////////////////
// Socket Option

static void
_enable_nonblock(int sock)
{
    // XXX note: NONBLOCK is not always inherited (eg, not on Linux).
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);
}

static void
_disable_nonblock(int sock)
{
    // XXX note: NONBLOCK is not always inherited (eg, not on Linux).
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) & (~O_NONBLOCK) );
}

static int 
_set_connection_opt(int sock)
{
    const int szrecv = 1024, szsend = 4096;
    const struct linger lin = {0};

    // keep alive: server will check target connection. (around 2hr)
    const int on = 1;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (void*)&on, sizeof(on));
   
    // fast close
    setsockopt(sock, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
    // adjust transmission window
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (void*)&szrecv, sizeof(szrecv));
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (void*)&szsend, sizeof(szsend));
    OPTIMIZE_SOCKET(sock);

    return 0;
}

static int
_set_tunnel_opt(int sock)
{
    // Some systems like linux, buffer of domain sockets are fixed values; for
    // systems like FreeBSD, you can really set the value.
    socklen_t len = 0;
    int szrecv = TUNNEL_BUFFER_SIZE, szsend = TUNNEL_BUFFER_SIZE;

    // adjust tunnel transmission window
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &szrecv, sizeof(szrecv)) < 0 ||
        setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &szsend, sizeof(szsend)) < 0)
    {
        fprintf(stderr, LOG_PREFIX "WARNING: "
                "set_tunnel_opt: failed to increase buffer to (%u,%u)\n",
                szsend, szrecv);
    }

    len = sizeof(szsend);
    getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &szsend, &len);
    len = sizeof(szrecv);
    getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &szrecv, &len);

    g_tunnel_send_buffer_size = szsend;
    fprintf(stderr, LOG_PREFIX "tunnel buffer window: send/recv = %d / %d\n",
            szsend, szrecv);

    return 0;
}

static int
_set_bind_opt(int sock)
{
    const int on = 1;

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void*)&on, sizeof(on));
    _set_connection_opt(sock);

    return 0;
}

static int
_set_bind_ip_port_opt(int sock)
{
#ifdef SO_REUSEPORT
    const int on = 1;

    if (g_reuseport)
        setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (void*)&on, sizeof(on));
#endif

    _set_bind_opt(sock);

    return 0;
}

#ifdef ENABLE_DEBUG_IO
void
DEBUG_IO(int fd, const char *msg) {
    int nread = 0, nwrite = 0, sndbuf = 0, rcvbuf = 0, len;
    time4_t now;

    ioctl(fd, FIONREAD, &nread);
    ioctl(fd, FIONWRITE, &nwrite);
    len = sizeof(sndbuf);
    getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, &len);
    len = sizeof(rcvbuf);
    getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, &len);

#ifdef DEBUG_IO_LIMIT
    if (nread < DEBUG_IO_LIMIT && nwrite < DEBUG_IO_LIMIT)
        return;
#else
    if (g_verbose < VERBOSE_DEBUG) 
        return;
#endif
    now = time(NULL);
    fprintf(stderr, LOG_PREFIX "%s: %s fd(%d): FION READ:%d/%d WRITE:%d/%d\n",
            Cdate(&now), msg, fd, nread, rcvbuf, nwrite, sndbuf);
}
#else
#define DEBUG_IO(fd, msg) {}
#endif

///////////////////////////////////////////////////////////////////////
// Draw Screen

#ifndef  INSCREEN
# define INSCREEN ANSI_RESET "\r\n【" BBSNAME "】◎(" MYHOSTNAME ", " MYIP ") \r\n"
#endif

#ifdef   STR_GUEST
# define MSG_GUEST "，或以 " STR_GUEST " 參觀"
#else
# define MSG_GUEST ""
#endif

#ifdef   STR_REGNEW
# define MSG_REGNEW "，或以 new 註冊"
#else
# define MSG_REGNEW
#endif

#define BOTTOM_YX           "24;1"
#define LOGIN_PROMPT_MSG    ANSI_RESET "請輸入代號" MSG_GUEST MSG_REGNEW ": " ANSI_REVERSE
#define LOGIN_PROMPT_YX     "21;1"
#define LOGIN_PROMPT_END    ANSI_RESET
#define PASSWD_PROMPT_MSG   ANSI_RESET MSG_PASSWD
#define PASSWD_PROMPT_YX    "22;1"
#define PASSWD_CHECK_MSG    ANSI_RESET "正在檢查帳號與密碼..."
#define PASSWD_CHECK_YX     PASSWD_PROMPT_YX
#define AUTH_SUCCESS_MSG    ANSI_RESET "密碼正確！ 開始登入系統...\r\n"
#define AUTH_SUCCESS_YX     PASSWD_PROMPT_YX
#define FREEAUTH_SUCCESS_MSG    ANSI_RESET "開始登入系統...\r\n"
#define FREEAUTH_SUCCESS_YX AUTH_SUCCESS_YX
#define AUTH_FAIL_MSG       ANSI_RESET ERR_PASSWD
#define AUTH_FAIL_YX        PASSWD_PROMPT_YX
#define REJECT_INSECURE_MSG ANSI_RESET "抱歉，此帳號已設定為只能使用安全連線(如ssh)登入。"
#define REJECT_INSECURE_YX  PASSWD_PROMPT_YX
#define USERID_EMPTY_MSG    ANSI_RESET "請重新輸入。"
#define USERID_EMPTY_YX     PASSWD_PROMPT_YX
#define SERVICE_FAIL_MSG    ANSI_COLOR(0;1;31) "抱歉，部份系統正在維護中，請稍候再試。 " ANSI_RESET
#define SERVICE_FAIL_YX     BOTTOM_YX
#define OVERLOAD_CPU_MSG    ANSI_RESET " 系統過載, 請稍後再來... "
#define OVERLOAD_CPU_YX     BOTTOM_YX
#define OVERLOAD_USER_MSG   ANSI_RESET " 由於人數過多，請您稍後再來... "
#define OVERLOAD_USER_YX    BOTTOM_YX

#define NO_SUCH_ENCODING_MSG ANSI_RESET " Sorry, GB encoding is NOT supported anymore. Please use UTF-8."
#define NO_SUCH_ENCODING_YX PASSWD_PROMPT_YX

#define REJECT_FREE_UID_MSG ANSI_RESET " 抱歉，此帳號或服務已達上限。 "
#define REJECT_FREE_UID_YX  BOTTOM_YX

#ifdef  STR_GUEST
#define TOO_MANY_GUEST_MSG  ANSI_RESET " 抱歉，目前已有太多 " STR_GUEST " 在站上。 "
#define TOO_MANY_GUEST_YX   BOTTOM_YX
#endif

#define FN_WELCOME          BBSHOME "/etc/Welcome"
#define FN_GOODBYE          BBSHOME "/etc/goodbye"
#define FN_BANIP            BBSHOME "/etc/banip.scr"
#define FN_BAN              BBSHOME "/" BAN_FILE

static char *welcome_screen, *goodbye_screen, *ban_screen, *banip_screen;

static void
load_text_screen_file(const char *filename, char **pptr)
{
    FILE *fp = NULL;
    off_t sz, wsz=0, psz;
    char *p, *s = NULL;
    int max_lines = MAX_TEXT_SCREEN_LINES;

    sz = dashs(filename);
    if (sz > 1)
    {
        wsz = sz*2 +1; // *2 for cr+lf, extra one byte for safe strchr().
        fp = fopen(filename, "rt");
    }

    // check valid file
    assert(pptr);
    s = *pptr;
    if (!fp)
    {
        if (s) free(s);
        *pptr = NULL;
        return;
    }

    // check memory buffer
    s = realloc(*pptr, wsz);  
    if (!s)
    {
        fclose(fp);
        return;
    }
    *pptr = s;

    // prepare buffer
    memset(s, 0, wsz);
    p = s;
    psz = wsz;

    while ( max_lines-- > 0 &&
            fgets(p, psz, fp))
    {
        size_t l = strlen(p);
        psz -= l;
        p   += l;
        if (l > 0 && *(p-1) == '\n')
        {
            // convert \n to \r\n
            *(p-1)= '\r';
            *p ++ = '\n';

            // Remove \n from last line (breaks full screen)
            if (max_lines == 0) {
                *(p-2) = '\0';
                p     -= 2;
                psz   += 2;
            }
        }
    }
    fclose(fp);
}

static void regular_check();

static void
reload_data()
{
    regular_check();

    if (!g_reload_data)
        return;

    fprintf(stderr, LOG_PREFIX "start reloading data.\n");
    g_reload_data = 0;
    g_welcome_mtime = dasht(FN_WELCOME);
    load_text_screen_file(FN_WELCOME, &welcome_screen);
    load_text_screen_file(FN_GOODBYE, &goodbye_screen);
    load_text_screen_file(FN_BANIP,   &banip_screen);
    load_text_screen_file(FN_BAN,     &ban_screen);

    // Loading banip table is slow - only reload if the file is really modified.
    if (dasht(FN_CONF_BANIP) != g_banip_mtime) {
        fprintf(stderr, LOG_PREFIX "reload banip table: %s.\n", FN_CONF_BANIP);
        g_banip_mtime = dasht(FN_CONF_BANIP);
        g_banip = free_banip_list(g_banip);
        g_banip = load_banip_list(FN_CONF_BANIP, stderr);
    }
}

static void
draw_text_screen(login_conn_ctx *conn, const char *scr)
{
    const char *ps, *pe;
    char buf[64];
    time4_t now;

    _mt_clear(conn);
    if (!scr || !*scr)
        return;

    // draw the screen from text file
    // XXX Because the text file may contain a very small subset of escape sequence
    // *t[Cdate] and *u[SHM->UTMPnumber], we implement a tiny version of 
    // expand_esc_star here.

    ps = pe = scr;
    while(pe && *pe)
    {
        // find a safe range between (ps, pe) to print
        pe = strchr(pe, ESC_CHR);
        if (!pe)
        {
            // no more escapes, print all.
            _buff_write(conn, ps, strlen(ps));
            break;
        }

        // let's look ahead
        pe++;

        // if not esc_star, search for next.
        if (*pe != '*')
            continue;

        // flush previous data
        _buff_write(conn, ps, pe - ps - 1);

        buf[0] = 0; pe++;

        // expand the star
        switch(*pe)
        {
            case 't':   // current time
                // strcpy(buf, "[date]");
                now = time(0);
                strlcpy(buf, Cdate(&now), sizeof(buf));
                break;

            case 'u':   // current online users
                // strcpy(buf, "[SHM->UTMPnumber]");
                snprintf(buf, sizeof(buf), "%d", SHM->UTMPnumber);
                break;
        }

        if(buf[0])
            _buff_write(conn, buf, strlen(buf));
        pe ++;
        ps = pe;
    }
}

static void
draw_goodbye(login_conn_ctx *conn)
{
    draw_text_screen(conn, goodbye_screen);
}

static void 
draw_userid_prompt(login_conn_ctx *conn, const char *uid, int icurr)
{
    char box[IDBOXLEN];

    _mt_move_yx(conn, LOGIN_PROMPT_YX);  _mt_clrtoeol(conn);
    _buff_write(conn, LOGIN_PROMPT_MSG, sizeof(LOGIN_PROMPT_MSG)-1);
    // draw input box
    memset(box, ' ', sizeof(box));
    if (uid) memcpy(box, uid, strlen(uid));
    _buff_write (conn, box,   sizeof(box));
    memset(box, '\b',sizeof(box));
    _buff_write (conn, box,   sizeof(box)-icurr);
}

static void
draw_userid_prompt_end(login_conn_ctx *conn)
{
    if (g_verbose > VERBOSE_DEBUG) 
        fprintf(stderr, LOG_PREFIX "reset connection attribute.\r\n");
    _buff_write(conn, LOGIN_PROMPT_END, sizeof(LOGIN_PROMPT_END)-1);
}

static void
draw_passwd_prompt(login_conn_ctx *conn)
{
    STATINC(STAT_LOGIND_PASSWDPROMPT);
    _mt_move_yx(conn, PASSWD_PROMPT_YX); _mt_clrtoeol(conn);
    _buff_write(conn, PASSWD_PROMPT_MSG, sizeof(PASSWD_PROMPT_MSG)-1);
}

static void
draw_reject_insecure_connection_msg(login_conn_ctx *conn)
{
    _mt_move_yx(conn, REJECT_INSECURE_YX); _mt_clrtoeol(conn);
    _buff_write(conn, REJECT_INSECURE_MSG, sizeof(REJECT_INSECURE_MSG)-1);
}

static void
draw_empty_userid_warn(login_conn_ctx *conn)
{
    _mt_move_yx(conn, USERID_EMPTY_YX); _mt_clrtoeol(conn);
    _buff_write(conn, USERID_EMPTY_MSG, sizeof(USERID_EMPTY_MSG)-1);
}

static void 
draw_check_passwd(login_conn_ctx *conn)
{
    _mt_move_yx(conn, PASSWD_CHECK_YX); _mt_clrtoeol(conn);
    _buff_write(conn, PASSWD_CHECK_MSG, sizeof(PASSWD_CHECK_MSG)-1);
}

static void
draw_auth_success(login_conn_ctx *conn, int free)
{
    if (free)
    {
        _mt_move_yx(conn, FREEAUTH_SUCCESS_YX); _mt_clrtoeol(conn);
        _buff_write(conn, FREEAUTH_SUCCESS_MSG, sizeof(FREEAUTH_SUCCESS_MSG)-1);
    } else {
        _mt_move_yx(conn, AUTH_SUCCESS_YX); _mt_clrtoeol(conn);
        _buff_write(conn, AUTH_SUCCESS_MSG, sizeof(AUTH_SUCCESS_MSG)-1);
    }
}

static void
draw_auth_fail(login_conn_ctx *conn)
{
    STATINC(STAT_LOGIND_AUTHFAIL);
    _mt_move_yx(conn, AUTH_FAIL_YX); _mt_clrtoeol(conn);
    _buff_write(conn, AUTH_FAIL_MSG, sizeof(AUTH_FAIL_MSG)-1);
}

static void
draw_service_failure(login_conn_ctx *conn)
{
    STATINC(STAT_LOGIND_SERVFAIL);
    _mt_move_yx(conn, PASSWD_CHECK_YX); _mt_clrtoeol(conn);
    _mt_move_yx(conn, SERVICE_FAIL_YX); _mt_clrtoeol(conn);
    _buff_write(conn, SERVICE_FAIL_MSG, sizeof(SERVICE_FAIL_MSG)-1);
}

static void
draw_overload(login_conn_ctx *conn, int type)
{
    STATINC(STAT_LOGIND_OVERLOAD);
    // XXX currently overload is displayed immediately after
    // banner/INSCREEN, so an enter is enough.
    _buff_write(conn, "\r\n", 2);
    // _mt_move_yx(conn, PASSWD_CHECK_YX); _mt_clrtoeol(conn);
    if (type == 1)
    {
        // _mt_move_yx(conn, OVERLOAD_CPU_YX); _mt_clrtoeol(conn);
        _buff_write(conn, OVERLOAD_CPU_MSG, sizeof(OVERLOAD_CPU_MSG)-1);
    } 
    else if (type == 2)
    {
        // _mt_move_yx(conn, OVERLOAD_USER_YX); _mt_clrtoeol(conn);
        _buff_write(conn, OVERLOAD_USER_MSG, sizeof(OVERLOAD_USER_MSG)-1);
    } 
    else {
        assert(!"unknown overload type");
        // _mt_move_yx(conn, OVERLOAD_CPU_YX); _mt_clrtoeol(conn);
        _buff_write(conn, OVERLOAD_CPU_MSG, sizeof(OVERLOAD_CPU_MSG)-1);
    }
}

static void
draw_reject_free_userid(login_conn_ctx *conn, const char *freeid)
{
    _mt_move_yx(conn, PASSWD_CHECK_YX); _mt_clrtoeol(conn);
#ifdef STR_GUEST
    if (strcasecmp(freeid, STR_GUEST) == 0)
    {
        _mt_move_yx(conn, TOO_MANY_GUEST_YX); _mt_clrtoeol(conn);
        _buff_write(conn, TOO_MANY_GUEST_MSG, sizeof(TOO_MANY_GUEST_MSG)-1);
        return;
    }
#endif
    _mt_move_yx(conn, REJECT_FREE_UID_YX); _mt_clrtoeol(conn);
    _buff_write(conn, REJECT_FREE_UID_MSG, sizeof(REJECT_FREE_UID_MSG)-1);

}

static void
draw_no_such_encoding(login_conn_ctx *conn)
{
    _mt_move_yx(conn, NO_SUCH_ENCODING_YX); _mt_clrtoeol(conn);
    _buff_write(conn, NO_SUCH_ENCODING_MSG, sizeof(NO_SUCH_ENCODING_MSG)-1);
}

///////////////////////////////////////////////////////////////////////
// BBS Logic

static void
regular_check()
{
    // cache results
    static time_t last_check_time = 0;
    time4_t now = time(0);

    if ( now - last_check_time < LOGIND_REGULAR_CHECK_DURATION)
        return;

    int was_overload = g_overload;
    last_check_time = now;
    g_overload = 0;
    g_banned   = 0;

#ifndef LOGIND_DONT_CHECK_FREE_USERID
    g_guest_too_many = 0;
    g_guest_usernum  = 0;
#endif

    if (cpuload(NULL) > MAX_CPULOAD)
    {
        g_overload = 1;
        fprintf(stderr, LOG_PREFIX "%s: system overload (cpu)\n", Cdate(&now));
    }
    else if (SHM->UTMPnumber >= MAX_ACTIVE
#ifdef DYMAX_ACTIVE
            || (SHM->GV2.e.dymaxactive > 2000 &&
                SHM->UTMPnumber >= SHM->GV2.e.dymaxactive)
#endif
        )
    {
        ++SHM->GV2.e.toomanyusers;
        g_overload = 2;
        fprintf(stderr, LOG_PREFIX "%s: system overload (too many users)\n",
                Cdate(&now));
    }
    else if (was_overload)
    {
        fprintf(stderr, LOG_PREFIX "%s: system leaves overload state.\n",
                Cdate(&now));
    }

    if (dashf(FN_BAN))
    {
        g_banned = 1;
        load_text_screen_file(FN_BAN, &ban_screen);
    }

    // check welcome screen
    if (g_verbose > VERBOSE_INFO) 
        fprintf(stderr, LOG_PREFIX "check welcome screen.\n");
    if (dasht(FN_WELCOME) != g_welcome_mtime)
    {
        g_reload_data = 1;
        if (g_verbose > VERBOSE_INFO)
            fprintf(stderr, LOG_PREFIX 
                    "modified. must update welcome screen ...\n");
    }
}

static int
is_tunnel_available() {
    // for each client we need to send two kinds of data:
    //  send_remote_fd: struct msghdr + char[CMSG_SPACE(sizeof(int))];
    //  towrite: sizeof(login_data)
    // However, it's hard to estimate how much sendmsg() needs.

    if (ackq_size() > LOGIND_ACKQUEUE_BOUND)
        return 0;

#ifdef LOGIND_TUNNEL_BUFFER_BOUND
    {
        int nwrite = 0, sndbuf = g_tunnel_send_buffer_size;

        ioctl(g_tunnel, FIONWRITE, &nwrite);
        if (sndbuf >= nwrite && (sndbuf - nwrite) < LOGIND_TUNNEL_BUFFER_BOUND)
        {
            time4_t now = time(NULL);
            fprintf(stderr, LOG_PREFIX "%s: tunnel buffer is full (%d/%d)\n",
                    Cdate(&now), nwrite, sndbuf);
            return 0;
        }
    }
#endif

    return 1;
}

static const char *
auth_is_free_userid(const char *userid)
{
#if defined(STR_GUEST) && !defined(NO_GUEST_ACCOUNT_REG)
    if (strcasecmp(userid, STR_GUEST) == 0)
        return STR_GUEST;
#endif

#ifdef STR_REGNEW 
    if (strcasecmp(userid, STR_REGNEW) == 0)
        return STR_REGNEW;
#endif

    return NULL;
}
static int
auth_check_free_userid_allowance(const char *userid)
{
#ifdef LOGIND_DONT_CHECK_FREE_USERID
    // XXX experimental to disable free id checking
    return 1;
#endif

#ifdef STR_REGNEW
    // accept all 'new' command.
    if (strcasecmp(userid, STR_REGNEW) == 0)
        return 1;
#endif

#ifdef STR_GUEST
    if (strcasecmp(userid, STR_GUEST) == 0)
    {
#  ifndef MAX_GUEST
        g_guest_too_many = 0;
#  else
        // if already too many guest, fast reject until next regular check.
        // XXX TODO also cache if guest is not too many?
        if (g_guest_too_many)
            return 0;

        // now, load guest account information.
        if (!g_guest_usernum)
        {
            if (g_verbose > VERBOSE_INFO) 
                fprintf(stderr, LOG_PREFIX " reload guest information\n");

            // reload guest information
            g_guest_usernum = searchuser(STR_GUEST, NULL);

            if (g_guest_usernum < 1 || g_guest_usernum > MAX_USERS)
                g_guest_usernum = 0;

            // if guest is not created, it's administrator's problem...
            assert(g_guest_usernum);
        }

        // update the 'too many' status.
        g_guest_too_many = 
            (!g_guest_usernum || (search_ulistn(g_guest_usernum, MAX_GUEST) != NULL));

        if (g_verbose > VERBOSE_INFO) fprintf(stderr, LOG_PREFIX 
                " guests are %s\n", g_guest_too_many ? "TOO MANY" : "ok.");

#  endif // MAX_GUEST
        return g_guest_too_many ? 0 : 1;
    }
#endif // STR_GUEST

    // shall never reach here.
    assert(!"unknown free userid");
    return 0;
}

// Check if the userid specified in ctx can continue to login.
static int
auth_check_userid(login_ctx *ctx)
{
    // NOTE: for security reasons, only report error when a secure
    // connection is required. Otherwise, silently let user continue.
    if (is_validuserid(ctx->userid))
    {
        userec_t user = {0};

        if (!ctx->is_secure_connection &&
            passwd_load_user(ctx->userid, &user) >= 1 &&
            user.userid[0] &&
            passwd_require_secure_connection(&user))
        {
            return AUTH_RESULT_FAIL_INSECURE;
        }
    }
    return AUTH_RESULT_OK;
}

// NOTE ctx->passwd will be destroyed (must > PASSLEN+1)
// NOTE ctx->userid may be changed (must > IDLEN+1)
static int
auth_user_challenge(login_ctx *ctx)
{
    char *uid = ctx->userid,
         *passbuf = ctx->passwd;
    const char *free_uid = auth_is_free_userid(uid);
    userec_t user = {0};

    if (free_uid)
    {
        strlcpy(ctx->userid, free_uid, sizeof(ctx->userid));
        return AUTH_RESULT_FREEID;
    }

    if (passwd_load_user(uid, &user) < 1 ||
        !user.userid[0])
    {
        if (user.userid[0])
            strcpy(uid, user.userid);
        return AUTH_RESULT_FAIL;
    }

    // normalize user id
    strcpy(uid, user.userid);

    if (!ctx->is_secure_connection &&
        passwd_require_secure_connection(&user))
    {
        return AUTH_RESULT_FAIL_INSECURE;
    }

    if (!checkpasswd(user.passwd, passbuf))
    {
        return AUTH_RESULT_FAIL;
    }

    return AUTH_RESULT_OK;
}

static void
retry_service()
{
    // empty g_tunnel means the service is not started or waiting retry.
    if (!g_tunnel)
        return ;

    g_tunnel = 0;

    // now, see if we can retry for it.
    if (!*g_retry_cmd)
        return;

    if (g_retry_times >= LOGIND_MAX_RETRY_SERVICE)
    {
        fprintf(stderr, LOG_PREFIX 
                "retry too many times (>%d), stop and wait manually maintainance.\n",
                LOGIND_MAX_RETRY_SERVICE);
        return;
    }

    g_retry_times++;
    fprintf(stderr, LOG_PREFIX "#%d retry to start service: %s\n", 
            g_retry_times, g_retry_cmd);
    system(g_retry_cmd);
}

static int
login_conn_end_ack(login_conn_ctx *conn, void *ack, int fd);

static int 
start_service(int fd, login_conn_ctx *conn)
{
    login_data ld = {0};
    int ack = 0;
    login_ctx *ctx;

    if (!g_tunnel)
        return 0;

    assert(conn);
    ctx = &conn->ctx;

    ld.cb  = sizeof(ld);
    ld.ack = (void*)conn;

    strlcpy(ld.userid, ctx->userid, sizeof(ld.userid));
    strlcpy(ld.hostip, ctx->hostip, sizeof(ld.hostip));
    strlcpy(ld.port,   ctx->port,   sizeof(ld.port));
    ld.encoding = ctx->encoding;
    ld.client_code = ctx->client_code;
    ld.t_lines  = 24;   // default size
    ld.t_cols   = 80;
    if (ctx->t_lines > ld.t_lines)
        ld.t_lines = ctx->t_lines;
    if (ctx->t_cols > ld.t_cols)
        ld.t_cols = ctx->t_cols;
    if (login_ctx_has_conn_data(ctx))
        ld.flags = ctx->cdata.flags;

    if (g_verbose > VERBOSE_INFO) 
        fprintf(stderr, LOG_PREFIX "start new service: %s@%s:%s #%d\n",
                ld.userid, ld.hostip, ld.port, fd);

    // XXX simulate the cache re-construction in mbbsd/login_query.
    resolve_garbage();

    // since mbbsd may be running in blocking mode, let's re-configure fd.
    _disable_nonblock(fd);

    // deliver the fd to hosting service
    if (send_remote_fd(g_tunnel, fd) < 0)
    {
        if (g_verbose > VERBOSE_ERROR) fprintf(stderr, LOG_PREFIX
                "failed in send_remote_fd\n");
        return ack;
    }
   
    // deliver the login data to hosting servier
    DEBUG_IO(g_tunnel, "before start_service:towrite(g_tunnel)");
    if (towrite(g_tunnel, &ld, sizeof(ld)) < (int)sizeof(ld))
    {
        if (g_verbose > VERBOSE_ERROR) fprintf(stderr, LOG_PREFIX
                "failed in towrite(login_data)\n");
        return ack;
    }
    DEBUG_IO(g_tunnel, "after  start_service:towrite(g_tunnel)");

    // to prevent buffer full, we set priority here to force all ackes processed
    // (otherwise tunnel daemon may try to send act and 
    event_priority_set(&conn->ev, EVTPRIORITY_ACK);


    // wait (or async) service to response
    if (!login_conn_end_ack(conn, ld.ack, fd))
    {
        if (g_verbose > VERBOSE_ERROR) fprintf(stderr, LOG_PREFIX
                "failed in logind_conn_end_ack\n");
        return ack;
    }
    return 1;
}

static void
logattempt2(const char *userid, char c, time4_t logtime, const char *hostip)
{
    logattempt_ctx ctx = {0};

    while (g_async_logattempt)
    {
        assert(g_logattempt_pipe);
        ctx.cb = sizeof(ctx);
        ctx.logtime = logtime;
        strlcpy(ctx.userid, userid, sizeof(ctx.userid));
        strlcpy(ctx.hostip, hostip, sizeof(ctx.hostip));

        DEBUG_IO(g_logattempt_pipe, "before logattempt2::towrite()");
        if (towrite(g_logattempt_pipe, &ctx, sizeof(ctx)) == sizeof(ctx))
            return;
        DEBUG_IO(g_logattempt_pipe, "after  logattempt2::towrite()");

        // failed ... back to internal.
        fprintf(stderr, LOG_PREFIX 
                "error: cannot use logattempt daemon, change to internal.\n");
        close(g_logattempt_pipe);
        g_async_logattempt= 0;
        g_logattempt_pipe = 0;
        break;
    }
    logattempt(userid, c, logtime, hostip);
}

typedef void (*draw_prompt_func)(login_conn_ctx *);

// Shared part for auth_start and auth_precheck_userid
static int
auth_fail(int fd, login_conn_ctx *conn, draw_prompt_func draw_prompt)
{
    _mt_bell(conn);

    // if fail, restart
    if (login_ctx_retry(&conn->ctx) >= LOGINATTEMPTS)
    {
        // end retry.
        draw_goodbye(conn);
        if (g_verbose > VERBOSE_INFO) 
            fprintf(stderr, LOG_PREFIX "auth fail (goodbye):  %s@%s  #%d...",
                    conn->ctx.userid, conn->ctx.hostip, fd);
        return AUTH_RESULT_STOP;

    }

    // prompt for retry
    if (draw_prompt)
        draw_prompt(conn);

    conn->ctx.state = LOGIN_STATE_USERID;
    draw_userid_prompt(conn, NULL, 0);
    return AUTH_RESULT_RETRY;
}

static int
auth_precheck_userid(int fd, login_conn_ctx *conn)
{
    switch (auth_check_userid(&conn->ctx))
    {
        case AUTH_RESULT_OK:
            return AUTH_RESULT_OK;

        case AUTH_RESULT_FAIL_INSECURE:
            return auth_fail(fd, conn, draw_reject_insecure_connection_msg);

        default:
            break;
    }

    return auth_fail(fd, conn, draw_empty_userid_warn);
}

static int 
auth_start(int fd, login_conn_ctx *conn)
{
    login_ctx *ctx = &conn->ctx;
    int isfree = 0;
    draw_prompt_func prompt;

    draw_check_passwd(conn);

    if (is_validuserid(ctx->userid))
    {
        // ctx content may be changed.
        prompt = draw_auth_fail;
        switch (auth_user_challenge(ctx))
        {
            case AUTH_RESULT_FAIL:
                // logattempt(ctx->userid , '-', time(0), ctx->hostip);
                logattempt2(ctx->userid , '-', time(0), ctx->hostip);
                break;

            case AUTH_RESULT_FAIL_INSECURE:
                // failure due to user setting for forcing secure connection
                // will not be logged.
                prompt = draw_reject_insecure_connection_msg;
                break;

            case AUTH_RESULT_FREEID:
                isfree = 1;
                // share FREEID case, no break here!
            case AUTH_RESULT_OK:
                if (!isfree)
                {
                    // do nothing. logattempt for auth-ok users is
                    // now done in mbbsd.
                }
                else if (!auth_check_free_userid_allowance(ctx->userid))
                {
                    // XXX since the only case of free
                    draw_reject_free_userid(conn, ctx->userid);
                    return AUTH_RESULT_STOP;
                }

                // consider system as overloaded if tunnel is not available.
                if (g_overload || !is_tunnel_available())
                {
                    // set overload again to reject all incoming connections
                    g_overload = 1;
                    draw_overload(conn, 1);
                    return AUTH_RESULT_STOP;
                }

                draw_auth_success(conn, isfree);
                if (!start_service(fd, conn))
                {
                    // too bad, we can't start service.
                    retry_service();
                    draw_service_failure(conn);
                    return AUTH_RESULT_STOP;
                }
                STATINC(STAT_LOGIND_SERVSTART);
                return AUTH_RESULT_OK;

            default:
                assert(!"unknown auth state.");
                break;
        }
    }
    else
        prompt = draw_empty_userid_warn;

    return auth_fail(fd, conn, prompt);
}

///////////////////////////////////////////////////////////////////////
// Event callbacks

static struct event ev_sighup, ev_tunnel, ev_ack;

static void 
sighup_cb(int signal GCC_UNUSED, short event GCC_UNUSED, void *arg GCC_UNUSED)
{
    fprintf(stderr, LOG_PREFIX 
            "caught sighup (request to reload) with %u opening fd...\n",
            g_opened_fd);
    g_reload_data = 1;
}

static void
stop_g_tunnel()
{
    if (!g_tunnel)
        return;

    close(g_tunnel);
    if (g_async_ack) event_del(&ev_ack);
    g_tunnel = 0;
}

static void
stop_tunnel(int tunnel_fd)
{
    if (!tunnel_fd)
        return;
    if (tunnel_fd == g_tunnel)
        stop_g_tunnel();
    else
        close(tunnel_fd);
}

static void 
endconn_cb(int fd, short event GCC_UNUSED, void *arg)
{
    login_conn_ctx *conn = (login_conn_ctx*) arg;
    if (g_verbose > VERBOSE_INFO) fprintf(stderr, LOG_PREFIX
            "login_conn_remove: removed connection (%s@%s) #%d...",
            conn->ctx.userid, conn->ctx.hostip, fd);

    // remove from ack queue
    if (conn->ctx.state == LOGIN_STATE_WAITACK)
    {
        // it should be already inside.
        ackq_del(conn);
    }

    event_del(&conn->ev);
    bufferevent_free(conn->bufev);
    close(fd);
    g_opened_fd--;
    free(conn);
    if (g_verbose > VERBOSE_INFO) fprintf(stderr, " done.\n");
}

static void
endconn_cb_buffer(struct bufferevent * evb GCC_UNUSED, short event GCC_UNUSED,
                  void *arg)
{
    login_conn_ctx *conn = (login_conn_ctx*) arg;

    // "event" for bufferevent and normal event are different
    endconn_cb(EVENT_FD(&conn->ev), 0, arg);
}

static void 
login_conn_remove(login_conn_ctx *conn, int fd, int sleep_sec)
{
    assert(conn->cb == sizeof(login_conn_ctx));
    if (!sleep_sec)
    {
        endconn_cb(fd, EV_TIMEOUT, (void*) conn);
    } else {
        struct timeval tv = { sleep_sec, 0};
        event_del(&conn->ev);
        event_set(&conn->ev, fd, 0, endconn_cb, conn);
        event_add(&conn->ev, &tv);
        if (g_verbose > VERBOSE_INFO)
            fprintf(stderr, LOG_PREFIX
                    "login_conn_remove: stop conn #%d in %d seconds later.\n", 
                    fd, sleep_sec);
    }
}

static void *
get_tunnel_ack(int tunnel)
{
    void *arg = NULL;

#ifdef VERIFY_BLOCKING_TUNNEL
    // fprintf(stderr, LOG_PREFIX "get_tunnel_ack: tunnel %s .\n", 
    // ( fcntl(tunnel, F_GETFL) & O_NONBLOCK ) ? "nonblock" : "blocking");
    if (fcntl(tunnel, F_GETFL) & (O_NONBLOCK))
    {
        fprintf(stderr, LOG_PREFIX
                "get_tunnel_ack: warning: tunnel is nonblocking.\n");
        stop_tunnel(tunnel);
        return NULL;
    }
#endif

    DEBUG_IO(tunnel, "before get_tunnel_ack:toread(tunnel)");
    if (toread(tunnel, &arg, sizeof(arg)) < (int)sizeof(arg) ||
        !arg)
    {
        // sorry... broken, let's shutdown the tunnel.
        if (g_verbose > VERBOSE_ERROR)
            fprintf(stderr, LOG_PREFIX
                    "get_tunnel_ack: tunnel (%d) is broken with arg %p.\n", 
                    tunnel, arg);

        stop_tunnel(tunnel);
        return NULL;
    }
    DEBUG_IO(tunnel, "after  get_tunnel_ack:toread(tunnel)");

    return arg;

}

static void
ack_cb(int tunnel, short event, void *arg)
{
    login_conn_ctx *conn = NULL;

    assert(tunnel);
    if (!(event & EV_READ))
    {
        // not read event (closed? timeout?)
        if (g_verbose > VERBOSE_ERROR) fprintf(stderr, LOG_PREFIX 
                "warning: invalid ack event at tunnel %d.\n", tunnel);
        stop_tunnel(tunnel);
        return;
    }

    assert(sizeof(arg) == sizeof(conn));
    conn = (login_conn_ctx*) get_tunnel_ack(tunnel);
    if (!conn)
    {
        if (g_verbose > VERBOSE_ERROR) fprintf(stderr, LOG_PREFIX 
                "warning: invalid ack at tunnel %d.\n", tunnel);
        return;
    }

    // some connections may be removed (for example, socket close) before being acked.
    // XXX FIXME if someone created a new connection before ack comes and re-used
    // the memory location of previous destroyed one, we'd have problem here.
    if (!ackq_del(conn))
    {
        if  (g_verbose > VERBOSE_ERROR) fprintf(stderr, LOG_PREFIX 
                "drop abandoned ack connection: %p.\n", conn);
        return;
    }

    // check connection
    if (conn->cb != sizeof(login_conn_ctx))
    {
        fprintf(stderr, LOG_PREFIX 
                "warning: received invalid ack from tunnel. abort/reset tunnel?\n");
        // assert(conn && conn->cb == sizeof(login_conn_ctx));
        return;
    }

    // reset the state to prevent processing ackq again
    conn->ctx.state = LOGIN_STATE_AUTH;
    // this event is still in queue.
    login_conn_remove(conn, conn->telnet.fd, 0);
}


static int
login_conn_end_ack(login_conn_ctx *conn, void *ack, int fd)
{
    // fprintf(stderr, LOG_PREFIX "login_conn_end_ack: enter.\n");

    if (g_async_ack)
    {
        // simply wait for ack_cb to complete
        // fprintf(stderr, LOG_PREFIX "login_conn_end_ack: async mode.\n");

        // mark as queued for waiting ack
        conn->ctx.state = LOGIN_STATE_WAITACK;
        ackq_add(conn);

        // set a safe timeout
        login_conn_remove(conn, fd, ACK_TIMEOUT_SEC);

    } else {
        // wait service to complete
        void *rack = NULL;

        // fprintf(stderr, LOG_PREFIX "login_conn_end_ack: sync mode.\n");
        if (!g_tunnel)
            return 0;

        rack = get_tunnel_ack(g_tunnel);
        if (!rack)
            return 0;

        if (rack != ack)
        {
            // critical error!
            fprintf(stderr, LOG_PREFIX 
                    "login_conn_end_ack: failed in ack value (%p != %p).\n",
                    rack, ack);

            stop_g_tunnel();
            return 0;
        }

        // safe to close.
        login_conn_remove(conn, fd, 0);
    }
    return 1;
}

static int
login_conn_handle_conndata(login_conn_ctx *conn, int fd, unsigned char *buf, int len);
static int
login_conn_handle_terminal(login_conn_ctx *conn, int fd, unsigned char *buf, int len);

static void 
client_cb(int fd, short event, void *arg)
{
    login_conn_ctx *conn = (login_conn_ctx*) arg;
    int len;
    unsigned char buf[64];

    // for time-out, simply close connection.
    if (event & EV_TIMEOUT)
    {
        // report timeout conection information --
        if (g_verbose || g_report_timeout)
        {
            time4_t tnow = time(NULL);
            strlcpy((char*)buf, Cdate(&tnow), sizeof(buf));

            fprintf(stderr, LOG_PREFIX
                    "timeout: %-16s [%s -> %s : %-4ds] %08X %s%s\n",
                     conn->ctx.hostip,
                     Cdate(&conn->enter),
                     buf,
                     tnow - conn->enter,
                    (unsigned int)conn->ctx.client_code,
                    (conn->ctx.state == LOGIN_STATE_INIT) ? "(*dummy*) " : "",
                     conn->ctx.userid
                   );
        }
        // end report ---------------------------

        endconn_cb(fd, EV_TIMEOUT, (void*) conn);
        return;
    }

    // XXX will this happen?
    if (!(event & EV_READ))
    {
        assert(event & EV_READ);
        return;
    }

    if ( (len = read(fd, buf, sizeof(buf))) <= 0)
    {
        // case to continue:
        if ((len < 0) && (errno == EINTR || errno == EAGAIN))
            return;

        // len == 0: EOF
        // len <  0: any other error.

        // close connection
        login_conn_remove(conn, fd, 0);
        return;
    }

    while (len > 0)
    {
        int consumed;
        switch (conn->state)
        {
            case LOGIN_CONN_STATE_CONNDATA:
                consumed = login_conn_handle_conndata(conn, fd, buf, len);
                break;

            case LOGIN_CONN_STATE_TERMINAL:
                consumed = login_conn_handle_terminal(conn, fd, buf, len);
                break;

            default:
                consumed = -1;
                login_conn_remove(conn, fd, 0);
                assert(!"login conn is in bad state");
                break;
        }
        if (consumed < 0 || consumed == len)
            return;
        memmove(buf, buf + consumed, len - consumed);
        len -= consumed;
    }
}

static int
login_ctx_activate(login_conn_ctx *conn, int fd);

static int
login_conn_activate(login_conn_ctx *conn, int fd)
{
    switch (conn->state)
    {
        case LOGIN_CONN_STATE_CONNDATA:
            return 0;

        case LOGIN_CONN_STATE_TERMINAL:
            return login_ctx_activate(conn, fd);

        default:
            login_conn_remove(conn, fd, 0);
            assert(!"login conn is in bad state");
            return -1;
    }
}

static int
login_conn_handle_conndata(login_conn_ctx *conn, int fd, unsigned char *buf, int len)
{
    assert(conn->state == LOGIN_CONN_STATE_CONNDATA);

    login_ctx *ctx = &conn->ctx;
    uint8_t *p = (uint8_t *) &ctx->cdata;
    int to_copy = sizeof(ctx->cdata) - ctx->cdata_len;
    if (to_copy > len)
        to_copy = len;
    memcpy(p + ctx->cdata_len, buf, to_copy);
    conn->ctx.cdata_len += to_copy;

    if (g_verbose >= VERBOSE_DEBUG)
    {
        fprintf(stderr, LOG_PREFIX "conn_data recv %d/%d\n",
                ctx->cdata_len, (int) sizeof(ctx->cdata));
    }

    assert((unsigned long) ctx->cdata_len <= sizeof(ctx->cdata));

    if (ctx->cdata_len == sizeof(ctx->cdata))
    {
        login_ctx *ctx = &conn->ctx;

        if (ctx->cdata.cb != sizeof(ctx->cdata) || ctx->cdata.raddr_len != 4)
        {
            login_conn_remove(conn, fd, 0);
            return -1;
        }

        ctx->encoding = ctx->cdata.encoding;
        inet_ntop(AF_INET, ctx->cdata.raddr, ctx->hostip, sizeof(ctx->hostip));
        snprintf(ctx->port, sizeof(ctx->port), "%u", ctx->cdata.lport);
        ctx->is_secure_connection = (ctx->cdata.flags & CONN_FLAG_SECURE);

        if (g_verbose >= VERBOSE_DEBUG)
        {
            fprintf(stderr, LOG_PREFIX "conn activate: "
                    "encoding=%d hostip=%s rport=%u lport=%u flags=%08x\n",
                    ctx->cdata.encoding, ctx->hostip, ctx->cdata.rport,
                    ctx->cdata.lport, ctx->cdata.flags);
        }

        conn->state = LOGIN_CONN_STATE_TERMINAL;
        if (login_ctx_activate(conn, fd) < 0)
            return -1;
    }

    return to_copy;
}

static int
login_conn_handle_terminal(login_conn_ctx *conn, int fd, unsigned char *buf, int len)
{
    assert(conn->state == LOGIN_CONN_STATE_TERMINAL);

    int consumed = len;
    unsigned char ch;
    char *s = (char*)buf;

    len = telnet_process(&conn->telnet, buf, len);

    while (len-- > 0)
    {
        int c = vtkbd_process((unsigned char)*s++, &conn->vtkbd);

        if (c == KEY_INCOMPLETE)
            continue;

        if (c == KEY_UNKNOWN)
        {
            // XXX for stupid clients always doing anti-idle, 
            // user will get beeps and have no idea what happened...
            // _mt_bell(conn);
            continue;
        }

        // deal with context
        switch ( login_ctx_handle(&conn->ctx, c) )
        {
            case LOGIN_HANDLE_WAIT:
                break;

            case LOGIN_HANDLE_BEEP:
                _mt_bell(conn);
                break;

            case LOGIN_HANDLE_BS:
                _mt_bs(conn);
                break;

            case LOGIN_HANDLE_REDRAW_USERID:
                if (g_verbose > VERBOSE_DEBUG) fprintf(stderr, LOG_PREFIX
                        "redraw userid: id=[%s], icurr=%d\n",
                        conn->ctx.userid, conn->ctx.icurr);
                draw_userid_prompt(conn, conn->ctx.userid, conn->ctx.icurr);
                break;

            case LOGIN_HANDLE_OUTC:
                ch = c;
                _buff_write(conn, &ch, 1);
                break;

            case LOGIN_HANDLE_PROMPT_PASSWD:
                // because prompt would reverse attribute, reset here.
                draw_userid_prompt_end(conn);

#ifdef DETECT_CLIENT
                // stop detection
                conn->telnet.cc_arg = NULL;
#endif
                if (conn->ctx.userid[0])
                {
                    char *uid = conn->ctx.userid;
                    char *uid_lastc = uid + strlen(uid)-1;

                    draw_passwd_prompt(conn);
#ifdef CONVERT
                    // convert encoding if required
                    switch(*uid_lastc)
                    {
                        case '.':   // GB mode
                            draw_no_such_encoding(conn);
                            login_conn_remove(conn, fd, AUTHFAIL_SLEEP_SEC);
                            return -1;
                        case ',':   // UTF-8 mode
                            conn->ctx.encoding = CONV_UTF8;
                            *uid_lastc = 0;
                            break;
                    }
                    // force to eliminate the extra field.
                    // (backward behavior compatible)
                    uid[IDLEN] = 0;
#endif
                    // accounts except free_auth [guest / new]
                    // require passwd.
                    if (!auth_is_free_userid(uid))
                    {
                        int r = auth_precheck_userid(fd, conn);
                        if (r != AUTH_RESULT_RETRY && r != AUTH_RESULT_OK)
                        {
                            login_conn_remove(conn, fd, AUTHFAIL_SLEEP_SEC);
                            return -1;
                        }

                        // leave the switch case, user will be prompt for
                        // password.
                        break;
                    }
                }

                // force changing state
                conn->ctx.state = LOGIN_STATE_AUTH;
                // XXX share start auth, no break here.
            case LOGIN_HANDLE_START_AUTH:
            {
                int r = auth_start(fd, conn);
                if (r != AUTH_RESULT_RETRY)
                {
                    // for AUTH_RESULT_OK, the connection is handled in
                    // login_conn_end_ack.
                    if (r != AUTH_RESULT_OK)
                        login_conn_remove(conn, fd, AUTHFAIL_SLEEP_SEC);
                    return -1;
                }
                break;
            }
        }
    }
    return consumed;
}

static void 
listen_cb(int lfd, short event GCC_UNUSED, void *arg)
{
    int fd;
    struct sockaddr_in xsin = {0};
    struct timeval idle_tv = { IDLE_TIMEOUT_SEC, 0};
    socklen_t szxsin = sizeof(xsin);
    login_conn_ctx *conn;
    bind_event *pbindev = (bind_event*) arg;

    while ( (fd = accept(lfd, (struct sockaddr *)&xsin, &szxsin)) >= 0 ) {
        STATINC(STAT_LOGIND_NEWCONN);

        // XXX note: NONBLOCK is not always inherited (eg, not on Linux).
        // So we have to set blocking mode for client again here.
        if (g_nonblock) _enable_nonblock(fd);

#ifndef LOGIND_NO_INSCREEN
        // fast draw banner (don't use buffered i/o - this banner is not really important.)
# ifdef INSCREEN
        write(fd, INSCREEN, sizeof(INSCREEN));
# endif // INSCREEN
#endif // !LOGIND_NO_INSCREEN

        if ((conn = malloc(sizeof(login_conn_ctx))) == NULL) {
            close(fd);
            return;
        }
        memset(conn, 0, sizeof(login_conn_ctx));
        conn->cb = sizeof(login_conn_ctx);
        conn->enter = (time4_t) time(NULL);
        conn->state = (pbindev->flags & BIND_EVENT_WILL_PASS_CONNDATA) ?
            LOGIN_CONN_STATE_CONNDATA : LOGIN_CONN_STATE_TERMINAL;

        if ((conn->bufev = bufferevent_new(fd, NULL, NULL, endconn_cb_buffer, conn)) == NULL) {
            free(conn);
            close(fd);
            return;
        }

        g_opened_fd ++;
        reload_data();
        login_ctx_init(&conn->ctx);

        // initialize telnet protocol
        telnet_ctx_init(&conn->telnet, &telnet_callback, fd);
        telnet_ctx_set_write_arg (&conn->telnet, (void*) conn); // use conn for buffered events
        telnet_ctx_set_resize_arg(&conn->telnet, (void*) &conn->ctx);
#ifdef DETECT_CLIENT
        telnet_ctx_set_cc_arg(&conn->telnet, (void*) &conn->ctx);
#endif
#ifdef LOGIND_OPENFD_IN_AYT
        telnet_ctx_set_ayt_arg(&conn->telnet, (void*) conn); // use conn for buffered events
#endif
        // better send after all parameters were set
        telnet_ctx_send_init_cmds(&conn->telnet);

        // get remote ip & local port info
        conn->ctx.addr_len = szxsin;
        memcpy(&conn->ctx.addr, &xsin, szxsin);
        inet_ntop(AF_INET, &xsin.sin_addr, conn->ctx.hostip, sizeof(conn->ctx.hostip));
        strlcpy(conn->ctx.port, pbindev->bind_name, sizeof(conn->ctx.port));

        if (g_verbose > VERBOSE_INFO) fprintf(stderr, LOG_PREFIX
                "new connection: fd=#%d %s:%s (opened fd: %d)\n", 
                fd, conn->ctx.hostip, conn->ctx.port, g_opened_fd);

        // set events
        event_set(&conn->ev, fd, EV_READ|EV_PERSIST, client_cb, conn);
        event_add(&conn->ev, &idle_tv);

        if (login_conn_activate(conn, fd) < 0)
            return;

        // in blocking mode, we cannot wait accept to return error.
        if (!g_nonblock)
            break;
    }
}

static int
login_ctx_activate(login_conn_ctx *conn, int fd)
{
    const char *banmsg;

    if (g_banned) {
        STATINC(STAT_LOGIND_BANNED);
        draw_text_screen (conn, ban_screen);
        login_conn_remove(conn, fd, BAN_SLEEP_SEC);
        return -1;
    }

    if ((banmsg = in_banip_list_addr(g_banip, conn->ctx.addr.sin_addr.s_addr)))
    {
        STATINC(STAT_LOGIND_BANNED);
        draw_text_screen (conn, *banmsg ? banmsg : banip_screen);
        login_conn_remove(conn, fd, BAN_SLEEP_SEC);
        return -1;
    }

    // XXX check system load here.
    if (g_overload)
    {
        draw_overload    (conn, g_overload);
        login_conn_remove(conn, fd, OVERLOAD_SLEEP_SEC);
        return -1;

    } else {
        draw_text_screen  (conn, welcome_screen);
        draw_userid_prompt(conn, NULL, 0);
    }

    return 0;
}

static void 
tunnel_cb(int fd, short event GCC_UNUSED, void *arg GCC_UNUSED)
{
    int cfd;
    if ((cfd = accept(fd, NULL, NULL)) < 0 )
        return;

    // got new tunnel
    fprintf(stderr, LOG_PREFIX "new tunnel established.\n");
    _set_connection_opt(cfd);
    _set_tunnel_opt(cfd);

    stop_g_tunnel();
    g_tunnel = cfd;

    if (g_async_ack)
    {
        event_set(&ev_ack, g_tunnel, EV_READ | EV_PERSIST, ack_cb, NULL);
        event_add(&ev_ack, NULL);
    }
}

///////////////////////////////////////////////////////////////////////
// Main 

static int
logattempt_daemon()
{
    int pipe_fds[2];
    int pid;
    logattempt_ctx ctx = {0};

    fprintf(stderr, LOG_PREFIX "forking logattempt daemon...\n");
    if (pipe(pipe_fds) < 0)
    {
        perror("pipe");
        return -1;
    }

    pid = fork();
    if (pid < 0)
    {
        perror("fork");
        return -1;
    }

    if (pid != 0)
    {
        g_logattempt_pipe = pipe_fds[1];
        close(pipe_fds[0]);
        return 0;
    }

    // child here.
    g_logattempt_pipe = pipe_fds[0];
    close(pipe_fds[1]);
    setproctitle(MY_SVC_NAME " [logattempts]");

    // TODO change to batched processing
    DEBUG_IO(g_logattempt_pipe,
             "before logattempt_daemon:toread(g_logattempt_pipe)");
    while (toread(g_logattempt_pipe, &ctx, sizeof(ctx)) == sizeof(ctx))
    {
        DEBUG_IO(g_logattempt_pipe,
                 "after  logattempt_daemon:toread(g_logattempt_pipe)");
        if (ctx.cb != sizeof(ctx))
        {
            fprintf(stderr, LOG_PREFIX "broken pipe. abort.\n");
            break;
        }

        logattempt(ctx.userid, '-', ctx.logtime, ctx.hostip);
    }

    exit(0);
}

static int
bind_generic(const char *name, const char *addr, int flags, int (*_setsock)(int sock))
{
    int sfd;
    bind_event *pev = NULL;

    fprintf(stderr, LOG_PREFIX "binding to: %s...", addr);
    if ( (sfd = tobindex(addr, LOGIND_SOCKET_QLEN, _setsock, 1)) < 0 )
    {
        fprintf(stderr, LOG_PREFIX "cannot bind to: %s. abort.\n", addr);
        return -1;
    }

    // only set non-blocking to out ports (not for tunnel!)
    if (g_nonblock)
        _enable_nonblock(sfd);

    pev = malloc  (sizeof(bind_event));
    memset(pev, 0, sizeof(bind_event));
    assert(pev);

    strlcpy(pev->bind_name, name, sizeof(pev->bind_name));
    pev->flags = flags;
    event_set(&pev->ev, sfd, EV_READ | EV_PERSIST, listen_cb, pev);
    event_add(&pev->ev, NULL);
    fprintf(stderr,"ok. \n");
    return 0;
}

static int 
bind_ip_port(const char *ip, int port)
{
    char name[STRLEN], addr[STRLEN];
    snprintf(name, sizeof(name), "%d", port);
    snprintf(addr, sizeof(addr), "%s:%d", ip, port);
    return bind_generic(name, addr, 0, _set_bind_ip_port_opt);
}

static int
bind_port(int port)
{
    return bind_ip_port("*", port);
}

static int
bind_unix(const char *path, unsigned int mode)
{
    if (bind_generic("0", path, BIND_EVENT_WILL_PASS_CONNDATA, _set_bind_opt) < 0)
        return -1;
    if (chown(path, BBSUID, BBSGID) < 0)
        fprintf(stderr, LOG_PREFIX "warning: chown: %s: %s\n",
                path, strerror(errno));
    if (chmod(path, mode) < 0)
        fprintf(stderr, LOG_PREFIX "warning: chmod %o %s: %s\n",
                mode, path, strerror(errno));
    return 0;
}

static int 
parse_bindports_conf(FILE *fp, 
        char *tunnel_path, int sz_tunnel_path,
        char *tclient_cmd, int sz_tclient_cmd
        )
{
    char buf [STRLEN*3], vprogram[STRLEN], vport[STRLEN], vtunnel[STRLEN];
    int bound_ports = 0;
    int iport = 0;

    // format: [ vprogram port ] or [ vprogram tunnel path ]
    while (fgets(buf, sizeof(buf), fp))
    {
        if (sscanf(buf, "%s%s", vprogram, vport) != 2)
            continue;
        if (strcmp(vprogram, MY_SVC_NAME) != 0)
            continue;

        if (strcmp(vport, "client") == 0)
        {
            // syntax: client command-line$
            if (*tclient_cmd)
            {
                fprintf(stderr, LOG_PREFIX
                        "warning: ignored configuration file due to specified client command: %s\n",
                        tclient_cmd);
                continue;
            }
            if (sscanf(buf, "%*s%*s%[^\n]", vtunnel) != 1 || !*vtunnel)
            {
                fprintf(stderr, LOG_PREFIX "incorrect tunnel client configuration. abort.\n");
                exit(1);
            }
            if (g_verbose) fprintf(stderr, "client: %s\n", vtunnel);
            strlcpy(tclient_cmd, vtunnel, sz_tclient_cmd);
            continue;
        }
        else if (strcmp(vport, "client_retry") == 0)
        {
            // syntax: client_retry command-line$
            if (*g_retry_cmd)
            {
                fprintf(stderr, LOG_PREFIX
                        "warning: ignored configuration file due to specified retry command: %s\n",
                        g_retry_cmd);
                continue;
            }
            if (sscanf(buf, "%*s%*s%[^\n]", vtunnel) != 1 || !*vtunnel)
            {
                fprintf(stderr, LOG_PREFIX "incorrect retry client configuration. abort.\n");
                exit(1);
            }
            if (g_verbose) fprintf(stderr, "client_retry: %s\n", vtunnel);
            strlcpy(g_retry_cmd, vtunnel, sizeof(g_retry_cmd));
            continue;
        }
        else if (strcmp(vport, "logfile") == 0)
        {
            // syntax: logfile file$
            if (*g_logfile_path)
            {
                fprintf(stderr, LOG_PREFIX
                        "warning: ignored configuration file due to specified log: %s\n",
                        g_logfile_path);
                continue;
            }
            if (sscanf(buf, "%*s%*s%s", vtunnel) != 1 || !*vtunnel)
            {
                fprintf(stderr, LOG_PREFIX "incorrect tunnel configuration. abort.\n");
                exit(1);
            }
            if (g_verbose) fprintf(stderr, "log_file: %s\n", vtunnel);
            strlcpy(g_logfile_path, vtunnel, sizeof(g_logfile_path));
            continue;
        }
        else if (strcmp(vport, "pidfile") == 0)
        {
            if (*g_pidfile_path)
            {
                fprintf(stderr, LOG_PREFIX
                        "warning: ignored configuration file due to specified pidfile: %s\n",
                        g_pidfile_path);
                continue;
            }
            if (sscanf(buf, "%*s%*s%s", vtunnel) != 1 || !*vtunnel)
            {
                fprintf(stderr, LOG_PREFIX "incorrect pidfile configuration. abort.\n");
                exit(1);
            }
            if (g_verbose) fprintf(stderr, "pidfile: %s\n", vtunnel);
            strlcpy(g_pidfile_path, vtunnel, sizeof(g_pidfile_path));
            continue;
        }
        else if (strcmp(vport, "tunnel") == 0)
        {
            if (*tunnel_path)
            {
                fprintf(stderr, LOG_PREFIX
                        "warning: ignored configuration file due to specified tunnel: %s\n",
                        tunnel_path);
                continue;
            }
            if (sscanf(buf, "%*s%*s%s", vtunnel) != 1 || !*vtunnel)
            {
                fprintf(stderr, LOG_PREFIX "incorrect tunnel configuration. abort.\n");
                exit(1);
            }
            if (g_verbose) fprintf(stderr, "tunnel: %s\n", vtunnel);
            strlcpy(tunnel_path, vtunnel, sz_tunnel_path);
            continue;
        }
        else if (strcmp(vport, "unix") == 0)
        {
            // syntax: unix mode path
            unsigned int mode;
            char vpath[PATHLEN];
            if (sscanf(buf, "%*s%*s%o%s", &mode, vpath) != 2 || !*vpath)
            {
                fprintf(stderr, LOG_PREFIX
                        "cannot parse unix socket to bind, line: %s", buf);
                exit(1);
            }
            if (bind_unix(vpath, mode) < 0)
            {
                fprintf(stderr, LOG_PREFIX
                        "cannot bind to unix socket: %s. abort.\n", vpath);
                exit(1);
            }
            bound_ports++;
            continue;
        }
        else if (strcmp(vport, "tcp") == 0)
        {
            // syntax: tcp address port
            char vip[STRLEN];
            if (sscanf(buf, "%*s%*s%s%s", vip, vport) != 2)
            {
                fprintf(stderr, LOG_PREFIX
                        "cannot parse address and port to bind, line: %s",
                        buf);
                exit(1);
            }
            iport = atoi(vport);
            if (!iport)
            {
                fprintf(stderr, LOG_PREFIX
                        "cannot parse port to bind: %s\n", buf);
                exit(1);
            }
            if (bind_ip_port(vip, iport) < 0)
            {
                fprintf(stderr, LOG_PREFIX "cannot bind to: %s:%d. abort.\n",
                        vip, iport);
                exit(1);
            }
            bound_ports++;
            continue;
        }
        else if (strcmp(vport, "option") == 0)
        {
            // syntax: option flagname
            char flagname[STRLEN];
            if (sscanf(buf, "%*s%*s%s", flagname) != 1)
            {
                fprintf(stderr, LOG_PREFIX
                        "cannot parse option flag name, line: %s", buf);
                exit(1);
            }
            if (strcmp(flagname, "reuseport") == 0)
            {
                g_reuseport = 1;
#ifndef SO_REUSEPORT
                fprintf(stderr, LOG_PREFIX
                        "warning: system doesn't support SO_REUSEPORT; "
                        "it will have no effect; line: %s", buf);
#endif
            }
            else
            {
                fprintf(stderr, LOG_PREFIX
                        "unknown flag name: \"%s\", line: %s", flagname, buf);
                exit(1);
            }
            continue;
        }

        iport = atoi(vport);
        if (!iport)
        {
            fprintf(stderr, LOG_PREFIX "warning: unknown settings: %s\n", buf);
            continue;
        }

        if (bind_port(iport) < 0)
        {
            fprintf(stderr, LOG_PREFIX "cannot bind to port: %d. abort.\n", iport);
            exit(3);
        }
        bound_ports++;
    }
    return bound_ports;
}

int 
main(int argc, char *argv[], char *envp[])
{
    int     ch, port = 0, bound_ports = 0, tfd, as_daemon = 1;
    FILE   *fp;
    char tunnel_path[PATHLEN] = "", tclient_cmd[PATHLEN] = "";
    char unix_path[PATHLEN] = "";
    const char *config_file = FN_CONF_BINDPORTS;
    struct event_base *evb;
    struct rlimit r = {.rlim_cur = LOGIND_MAX_FDS, .rlim_max = LOGIND_MAX_FDS};

    Signal(SIGPIPE, SIG_IGN);
    initsetproctitle(argc, argv, envp);

    while ( (ch = getopt(argc, argv, "f:p:t:l:r:P:u:c:hvTDdBbAaMm")) != -1 )
    {
        switch( ch ){
        case 'f':
            config_file = optarg;
            break;

        case 'l':
            strlcpy(g_logfile_path, optarg, sizeof(g_logfile_path));
            break;

        case 'p':
            if (optarg) port = atoi(optarg);
            break;

        case 't':
            strlcpy(tunnel_path, optarg, sizeof(tunnel_path));
            break;

        case 'r':
            strlcpy(g_retry_cmd, optarg, sizeof(g_retry_cmd));
            break;

        case 'P':
            strlcpy(g_pidfile_path, optarg, sizeof(g_pidfile_path));
            break;

        case 'u':
            strlcpy(unix_path, optarg, sizeof(unix_path));
            break;

        case 'c':
            strlcpy(tclient_cmd, optarg, sizeof(tclient_cmd));
            break;

        case 'd':
            as_daemon = 1;
            break;

        case 'D':
            as_daemon = 0;
            break;

        case 'a':
            g_async_ack = 1;
            break;

        case 'A':
            g_async_ack = 0;
            break;

        case 'b':
            g_nonblock = 1;
            break;

        case 'B':
            g_nonblock = 0;
            break;

        case 'm':
            g_async_logattempt = 1;
            break;
            
        case 'M':
            g_async_logattempt = 0;
            break;

        case 'v':
            g_verbose++;
            break;

        case 'T':
            g_report_timeout = 1;
            break;

        case 'h':
        default:
            fprintf(stderr,
                    "usage: %s [-vTmMaAbBdD] [-l log_file] [-f conf] [-p port] [-t tunnel] [-P pidfile] [-c client_command]\n", argv[0]);
            fprintf(stderr, 
                    "\t-v:    provide verbose messages\n"
                    "\t-T:    provide timeout connection info\n"
                    "\t-d/-D: do/not enter daemon mode (default: %s)\n"
                    "\t-a/-A: do/not use asynchronous service ack (deafult: %s)\n"
                    "\t-b/-B: do/not use non-blocking socket mode (default: %s)\n"
                    "\t-m/-M: do/not use asynchronous logattempts (default: %s)\n"
                    "\t-f: read configuration from file (default: %s)\n"
                    "\t-P: pid file (default: %s)\n",
                    as_daemon   ? "true" : "false",
                    g_async_ack ? "true" : "false",
                    g_nonblock  ? "true" : "false",
                    g_async_logattempt  ? "true" : "false",
                    BBSHOME "/" FN_CONF_BINDPORTS,
                    LOGIND_DEFAULT_PID_PATH);
            fprintf(stderr, 
                    "\t-l: log meesages into log_file\n"
                    "\t-p: bind (listen) to specific port\n"
                    "\t-u: bind unix socket for incoming connections (with conn data passing enabled)\n"
                    "\t-t: create tunnel in given path\n"
                    "\t-c: spawn a (tunnel) client after initialization\n"
                    "\t-r: the command to retry spawning clients\n"
                   );
            return 1;
        }
    }

    if (setrlimit(RLIMIT_NOFILE, &r) < 0)
    {
        fprintf(stderr, LOG_PREFIX "warning: cannot increase max fd to %u...\n", LOGIND_MAX_FDS);
    }

    chdir(BBSHOME);
    attach_SHM();

    if (g_async_logattempt && logattempt_daemon() < 0)
    {
        fprintf(stderr, LOG_PREFIX "error: cannot fork to handle logattempts.\n");
        return 5;
    }

    reload_data();
    evb = event_init();
    event_base_priority_init(evb, MY_EVENT_PRIORITY_NUMBERS);

    // bind ports
    if (port && bind_port(port) < 0)
    {
        fprintf(stderr, LOG_PREFIX "cannot bind to port: %d. abort.\n", port);
        return 3;
    }
    if (port)
        bound_ports++;

    // bind from port list file
    if( NULL != (fp = fopen(config_file, "rt")) )
    {
        bound_ports += parse_bindports_conf(fp, 
                tunnel_path, sizeof(tunnel_path),
                tclient_cmd, sizeof(tclient_cmd));
        fclose(fp);
    }

    // bind unix socket
    if (*unix_path)
    {
        if (bind_unix(unix_path, 0600) < 0)
        {
            fprintf(stderr, LOG_PREFIX
                    "cannot bind to unix socket: %s. abort.\n", unix_path);
            return 3;
        }
        bound_ports++;
    }

    if (!bound_ports)
    {
        fprintf(stderr, LOG_PREFIX "error: no ports to bind. abort.\n");
        return 4;
    }
    if (!*tunnel_path)
    {
        fprintf(stderr, LOG_PREFIX "error: must assign one tunnel path. abort.\n");
        return 4;
    }

    if (!*g_pidfile_path)
    {
        strlcpy(g_pidfile_path, LOGIND_DEFAULT_PID_PATH, sizeof(g_pidfile_path));
    }

    /* Give up root privileges: no way back from here */
    setgid(BBSGID);
    setuid(BBSUID);

    // create tunnel
    fprintf(stderr, LOG_PREFIX "creating tunnel: %s...", tunnel_path);
    if ( (tfd = tobindex(tunnel_path, 1, _set_bind_opt, 1)) < 0)
    {
        fprintf(stderr, LOG_PREFIX "cannot create tunnel. abort.\n");
        return 2;
    }
    fprintf(stderr, "ok.\n");
    event_set(&ev_tunnel, tfd, EV_READ | EV_PERSIST, tunnel_cb, &ev_tunnel);
    event_add(&ev_tunnel, NULL);

    // daemonize!
    if (as_daemon)
    {
        char *logfile_path = NULL;
        if (g_logfile_path[0]) logfile_path = g_logfile_path;
        fprintf(stderr, LOG_PREFIX "start daemonize\n");
        daemonize(g_pidfile_path, logfile_path);

        // because many of the libraries used in this daemon (for example,
        // passwd / logging / ...) all assume cwd=BBSHOME,
        // let's workaround them.
        chdir(BBSHOME);
    }

    // Some event notification mechanisms don't work across forks (e.g. kqueue)
    event_reinit(evb);

    // SIGHUP handler is reset in daemonize()
    signal_set(&ev_sighup, SIGHUP, sighup_cb, &ev_sighup);
    signal_add(&ev_sighup, NULL);

    // spawn tunnel client if specified.
    if (*tclient_cmd)
    {
        int r;
        fprintf(stderr, LOG_PREFIX "invoking client...\n");
        // XXX this should NOT be a blocking call.
        r = system(tclient_cmd);
        if (g_verbose)
            fprintf(stderr, LOG_PREFIX "client return value = %d\n", r);
    }

    // warning: after daemonize, the directory was changed to root (/)...
    {
        time4_t tnow = time(0);
        fprintf(stderr, LOG_PREFIX "%s: start event dispatch.\n",
                Cdate(&tnow));
    }
    event_dispatch();

    return 0;
}

// vim:et
