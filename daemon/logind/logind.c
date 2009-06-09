// The High Performance Login Daemon (works with tunnel mode)
// $Id: logind.c,v 1.1 2009/06/09 13:13:42 piaip Exp piaip $
//
// Create:       Hung-Te Lin <piaip@csie.ntu.edu.tw>
// Contributors: wens, kcwu
// Initial Date: 2009/06/01
//
// Copyright (C) 2009, Hung-Te Lin <piaip@csie.ntu.edu.tw>
// All rights reserved

// TODO:
// 1. cache guest's usernum and check if too many guests online
// 2. change close connection to 'wait until user hit then close' (drop?)
// 3. make original port information in margs(mbbsd)?

#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <event.h>

#define _BBS_UTIL_C_
#include "bbs.h"
#include "banip.h"
#include "logind.h"

#ifndef OPTIMIZE_SOCKET
#define OPTIMIZE_SOCKET(sock) do {} while(0)
#endif

#ifndef SOCKET_QLEN
#define SOCKET_QLEN (10)
#endif

#ifndef AUTHFAIL_SLEEP_SEC
#define AUTHFAIL_SLEEP_SEC  (15)
#endif

#ifndef OVERLOAD_SLEEP_SEC
#define OVERLOAD_SLEEP_SEC  (60)
#endif

#ifndef BAN_SLEEP_SEC
#define BAN_SLEEP_SEC       (60)
#endif

#define MAX_TEXT_SCREEN_LINES   (24)

///////////////////////////////////////////////////////////////////////
// global variables
int g_tunnel;           // tunnel for service daemon
int g_reload_data = 1;  // request to reload data

// server status
int g_overload = 0;
int g_banned   = 0;

///////////////////////////////////////////////////////////////////////
// login context, constants and states

enum {
    LOGIN_STATE_START  = 1,
    LOGIN_STATE_USERID,
    LOGIN_STATE_PASSWD,
    LOGIN_STATE_AUTH,

    LOGIN_HANDLE_WAIT = 1,
    LOGIN_HANDLE_BEEP,
    LOGIN_HANDLE_OUTC,
    LOGIN_HANDLE_BS,
    LOGIN_HANDLE_PROMPT_PASSWD,
    LOGIN_HANDLE_START_AUTH,

    AUTH_RESULT_STOP   = -2,
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
    Fnv32_t client_code;
    char userid [IDBOXLEN];
    char passwd [PASSLEN+1];
    char hostip [IPV4LEN+1];
} login_ctx;

typedef struct {
    struct bufferevent *bufev;
    struct event ev;
    TelnetCtx    telnet;
    login_ctx    ctx;
} login_conn_ctx;

void 
login_ctx_init(login_ctx *ctx)
{
    assert(ctx);
    memset(ctx, 0, sizeof(login_ctx));
    ctx->client_code = FNV1_32_INIT;
    ctx->state = LOGIN_STATE_START;
}

int 
login_ctx_retry(login_ctx *ctx)
{
    assert(ctx);
    ctx->state = LOGIN_STATE_START;
    ctx->encoding = 0;
    memset(ctx->userid, 0, sizeof(ctx->userid));
    memset(ctx->passwd, 0, sizeof(ctx->passwd));
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
        case LOGIN_STATE_START:
        case LOGIN_STATE_USERID:
            l = strlen(ctx->userid);

            if (c == KEY_ENTER)
            {
                ctx->state = LOGIN_STATE_PASSWD;
                return LOGIN_HANDLE_PROMPT_PASSWD;
            }
            if (c == KEY_BS)
            {
                if (!l)
                    return LOGIN_HANDLE_BEEP;
                ctx->userid[l-1] = 0;
                return LOGIN_HANDLE_BS;
            }

            if (!isascii(c) || !isprint(c) || 
                l+1 >= sizeof(ctx->userid))
                return LOGIN_HANDLE_BEEP;

            ctx->userid[l] = c;

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

            if (!isascii(c) || !isprint(c) || 
                l+1 >= sizeof(ctx->passwd))
                return LOGIN_HANDLE_BEEP;

            ctx->passwd[l] = c;

            return LOGIN_HANDLE_WAIT;

        default:
            break;
    }
    return LOGIN_HANDLE_BEEP;
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
_telnet_write_data_cb(void *write_arg, int fd, const void *buf, size_t nbytes)
{
    login_conn_ctx *conn = (login_conn_ctx *)write_arg;
    _buff_write(conn, buf, nbytes);
}

const static struct TelnetCallback 
telnet_callback = {
    _telnet_write_data_cb,
    _telnet_resize_term_cb,
#ifdef DETECT_CLIENT
    _telnet_update_cc_cb,
#else
    NULL,
#endif
};

///////////////////////////////////////////////////////////////////////
// Socket Option

static int 
_set_connection_opt(int sock)
{
    const int szrecv = 1024, szsend=4096;
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
_set_bind_opt(int sock)
{
    const int on = 1;

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void*)&on, sizeof(on));
    _set_connection_opt(sock);

    return 0;
}

///////////////////////////////////////////////////////////////////////
// Draw Screen

#ifdef STR_GUEST
# define MSG_GUEST "，或以[" STR_GUEST "]參觀"
#else
# define MSG_GUEST ""
#endif

#ifdef STR_REGNEW
# define MSG_REGNEW "，或以[new]註冊"
#else
# define MSG_REGNEW
#endif

#define BOTTOM_YX           "24;1"
#define LOGIN_PROMPT_MSG    ANSI_RESET "請輸入代號" MSG_GUEST MSG_REGNEW ": " ANSI_REVERSE
#define LOGIN_PROMPT_YX     "21;1"
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
#define USERID_EMPTY_MSG    ANSI_RESET "請重新輸入。"
#define USERID_EMPTY_YX     PASSWD_PROMPT_YX
#define SERVICE_FAIL_MSG    ANSI_COLOR(0;1;31) "抱歉，系統目前無法使用，請稍候再試。" ANSI_RESET "\r\n"
#define SERVICE_FAIL_YX     BOTTOM_YX
#define OVERLOAD_CPU_MSG    "系統過載, 請稍後再來...\r\n"
#define OVERLOAD_CPU_YX     BOTTOM_YX
#define OVERLOAD_USER_MSG   "由於人數過多，請您稍後再來...\r\n"
#define OVERLOAD_USER_YX    BOTTOM_YX

#define FN_WELCOME          BBSHOME "/etc/Welcome"
#define FN_GOODBYE          BBSHOME "/etc/goodbye"
#define FN_BAN              BBSHOME "/" BAN_FILE

static char *welcome_screen, *goodbye_screen, *ban_screen;

static void
load_text_screen_file(const char *filename, char **pptr)
{
    FILE *fp;
    off_t sz, wsz, psz;
    char *p, *s = NULL;
    int max_lines = MAX_TEXT_SCREEN_LINES;

    sz = dashs(filename);
    if (sz < 1)
    {
        free(*pptr);
        *pptr = NULL;
        return;
    }
    wsz = sz*2 +1; // *2 for cr+lf, extra one byte for safe strchr().

    // XXX TODO use realloc?
    assert(pptr);
    s = *pptr;
    s = realloc(s, wsz);  
    *pptr = s;
    if (!s)
        return;

    memset(s, 0, wsz);
    p = s;
    psz = wsz;

    fp = fopen(filename, "rt");
    if (!fp)
    {
        free(s);
        return;
    }
    while ( max_lines-- > 0 &&
            fgets(p, psz, fp))
    {
        psz -= strlen(p);
        p += strlen(p);
        *p ++ = '\r';
    }
    fclose(fp);
    *pptr = s;
}

static void regular_check();

static void
reload_data()
{
    regular_check();

    if (!g_reload_data)
        return;

    fprintf(stderr, "start reloading data.\r\n");
    g_reload_data = 0;
    load_text_screen_file(FN_WELCOME, &welcome_screen);
    load_text_screen_file(FN_GOODBYE, &goodbye_screen);
    load_text_screen_file(FN_BAN,     &ban_screen);
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
draw_userid_prompt(login_conn_ctx *conn)
{
    char box[IDBOXLEN];

    _mt_move_yx(conn, LOGIN_PROMPT_YX);  _mt_clrtoeol(conn);
    _buff_write(conn, LOGIN_PROMPT_MSG, sizeof(LOGIN_PROMPT_MSG)-1);
    // draw input box
    memset(box, ' ', sizeof(box));
    _buff_write (conn, box,   sizeof(box));
    memset(box, '\b',sizeof(box));
    _buff_write (conn, box,   sizeof(box));
}

static void
draw_passwd_prompt(login_conn_ctx *conn)
{
    _mt_move_yx(conn, PASSWD_PROMPT_YX); _mt_clrtoeol(conn);
    _buff_write(conn, PASSWD_PROMPT_MSG, sizeof(PASSWD_PROMPT_MSG)-1);
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
    _mt_move_yx(conn, AUTH_FAIL_YX); _mt_clrtoeol(conn);
    _buff_write(conn, AUTH_FAIL_MSG, sizeof(AUTH_FAIL_MSG)-1);
}

static void
draw_service_failure(login_conn_ctx *conn)
{
    _mt_move_yx(conn, SERVICE_FAIL_YX); _mt_clrtoeol(conn);
    _buff_write(conn, SERVICE_FAIL_MSG, sizeof(SERVICE_FAIL_MSG)-1);
}

static void
draw_overload(login_conn_ctx *conn, int type)
{
    if (type == 1)
    {
        _mt_move_yx(conn, OVERLOAD_CPU_MSG); _mt_clrtoeol(conn);
        _buff_write(conn, OVERLOAD_CPU_MSG, sizeof(OVERLOAD_CPU_MSG)-1);
    } 
    else if (type == 2)
    {
        _mt_move_yx(conn, OVERLOAD_USER_MSG); _mt_clrtoeol(conn);
        _buff_write(conn, OVERLOAD_USER_MSG, sizeof(OVERLOAD_USER_MSG)-1);
    } 
    else {
        assert(false);
        _mt_move_yx(conn, OVERLOAD_CPU_MSG); _mt_clrtoeol(conn);
        _buff_write(conn, OVERLOAD_CPU_MSG, sizeof(OVERLOAD_CPU_MSG)-1);
    }
}

///////////////////////////////////////////////////////////////////////
// BBS Logic

#define REGULAR_CHECK_DURATION (5)

static void
regular_check()
{
    // cache results
    static time_t last_check_time = 0;

    time_t now = time(0);

    if ( now - last_check_time < REGULAR_CHECK_DURATION)
        return;

    last_check_time = now;
    g_overload = 0;
    g_banned   = 0;

    if (cpuload(NULL) > MAX_CPULOAD)
    {
        g_overload = 1;
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
    }

    if (dashf(FN_BAN))
    {
        g_banned = 1;
        load_text_screen_file(FN_BAN, &ban_screen);
    }
}

static int 
check_banip(char *host)
{
    uint32_t thisip = ipstr2int(host);
    return uintbsearch(thisip, &banip[1], banip[0]) ? 1 : 0;
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

// NOTE ctx->passwd will be destroyed (must > PASSLEN+1)
// NOTE ctx->userid may be changed (must > IDLEN+1)
static int
auth_user_challenge(login_ctx *ctx)
{
    char *uid = ctx->userid,
         *passbuf = ctx->passwd;

    const char *free_uid = auth_is_free_userid(uid);

    if (free_uid)
    {
        strlcpy(ctx->userid, free_uid, sizeof(ctx->userid));
        return AUTH_RESULT_FREEID;
    }

    // reuse cuser
    memset(&cuser, 0, sizeof(cuser));
    if( initcuser(uid) < 1 || !cuser.userid[0] ||
        !checkpasswd(cuser.passwd, passbuf) )
    {
        if (cuser.userid[0])
            strcpy(uid, cuser.userid);
        return AUTH_RESULT_FAIL;
    }

    // normalize user id
    strcpy(uid, cuser.userid);
    return AUTH_RESULT_OK;
}

static int 
start_service(int fd, login_ctx *ctx)
{
    login_data ld = {0};
    int ack = 0;

    if (!g_tunnel)
        return 0;

    ld.cb = sizeof(ld);
    strlcpy(ld.userid, ctx->userid, sizeof(ld.userid));
    strlcpy(ld.hostip, ctx->hostip, sizeof(ld.hostip));
    ld.encoding = ctx->encoding;
    ld.client_code = ctx->client_code;
    ld.t_lines  = 25;
    ld.t_cols   = 80;
    if (ctx->t_lines > ld.t_lines)
        ld.t_lines = ctx->t_lines;
    if (ctx->t_cols > ld.t_cols)
        ld.t_cols = ctx->t_cols;

    fprintf(stderr, "start new service!\r\n");

    // deliver the fd to hosting service
    if (send_remote_fd(g_tunnel, fd) < 0)
        return ack;
   
    // deliver the login data to hosting servier
    if (towrite(g_tunnel, &ld, sizeof(ld)) <= 0)
        return ack;

    // wait service to response
    read(g_tunnel, &ack, sizeof(ack));
    return ack;
}

static int 
auth_start(int fd, login_conn_ctx *conn)
{
    login_ctx *ctx = &conn->ctx;
    int isfree = 1, was_valid_uid = 0;
    draw_check_passwd(conn);

    if (is_validuserid(ctx->userid))
    {
        // ctx content may be changed.
        was_valid_uid = 1;
        switch (auth_user_challenge(ctx))
        {
            case AUTH_RESULT_FAIL:
                logattempt(ctx->userid , '-', time(0), ctx->hostip);
                break;

            case AUTH_RESULT_OK:
                isfree = 0;
                logattempt(ctx->userid , ' ', time(0), ctx->hostip);
                // share FREEID case, no break here!
            case AUTH_RESULT_FREEID:
                draw_auth_success(conn, isfree);

                if (!start_service(fd, ctx))
                {
                    // too bad, we can't start service.
                    draw_service_failure(conn);
                    return AUTH_RESULT_STOP;
                }
                return AUTH_RESULT_OK;

            default:
                assert(false);
                break;
        }

    }

    // auth fail.
    _mt_bell(conn);

    // if fail, restart
    if (login_ctx_retry(ctx) >= LOGINATTEMPTS)
    {
        // end retry.
        draw_goodbye(conn);
        return AUTH_RESULT_STOP;

    }

    // prompt for retry
    if (was_valid_uid)
        draw_auth_fail(conn);
    else
        draw_empty_userid_warn(conn);

    ctx->state = LOGIN_STATE_USERID;
    draw_userid_prompt(conn);
    return AUTH_RESULT_RETRY;
}



///////////////////////////////////////////////////////////////////////
// Event callbacks

static struct event ev_sighup, ev_tunnel;

static void 
sighup_cb(int signal, short event, void *arg)
{
    fprintf(stderr, "caught sighup (request to reload)\r\n");
    g_reload_data = 1;
}

static void 
endconn_cb(int fd, short event, void *arg)
{
    login_conn_ctx *conn = (login_conn_ctx*) arg;
    fprintf(stderr, "login_conn_remove: removed connection #%d...", fd);
    event_del(&conn->ev);
    bufferevent_free(conn->bufev);
    close(fd);
    free(conn);
    fprintf(stderr, " done.\r\n");
}

static void 
login_conn_remove(login_conn_ctx *conn, int fd, int sleep_sec)
{
    if (!sleep_sec)
    {
        endconn_cb(fd, EV_TIMEOUT, (void*) conn);
    } else {
        struct timeval tv = { sleep_sec, 0};
        event_del(&conn->ev);
        event_set(&conn->ev, fd, EV_PERSIST, endconn_cb, conn);
        event_add(&conn->ev, &tv);
        fprintf(stderr, 
                "login_conn_remove: stop conn #%d in %d seconds later.\r\n", 
                fd, sleep_sec);
    }
}

static void 
client_cb(int fd, short event, void *arg)
{
    login_conn_ctx *conn = (login_conn_ctx*) arg;
    int i, len, r;
    unsigned char buf[32], ch;

    // ignore clients that timeout
    if (event & EV_TIMEOUT)
        return;

    // XXX will this happen?
    if (!(event & EV_READ))
        return;

    if ( (len = read(fd, buf, sizeof(buf) - 1)) <= 0)
    {
         if (errno == EINTR || errno == EAGAIN)
             return;

        // close connection
        login_conn_remove(conn, fd, 0);
        return;
    }

    len = telnet_process(&conn->telnet, buf, len);

    for (i = 0; i < len; i++)
    {
        int c = (unsigned char)buf[i];
        
        // quick convert
        if      (c == KEY_CR) 
            c = KEY_ENTER;
        else if (c == KEY_LF) 
            continue;   // ignore LF
        else if (c == '\b' || c == Ctrl('H') || c == 127) 
            c = KEY_BS;

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

            case LOGIN_HANDLE_OUTC:
                ch = c;
                _buff_write(conn, &ch, 1);
                break;

            case LOGIN_HANDLE_PROMPT_PASSWD:
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
                            conn->ctx.encoding = CONV_GB;
                            *uid_lastc = 0;
                            break;
                        case ',':   // UTF-8 mode
                            conn->ctx.encoding = CONV_UTF8;
                            *uid_lastc = 0;
                            break;
                    }
#endif
                    // accounts except free_auth [guest / new]
                    // require passwd.
                    if (!auth_is_free_userid(uid))
                        break;
                }

                // force changing state
                conn->ctx.state = LOGIN_STATE_AUTH;
                // XXX share start auth, no break here.
            case LOGIN_HANDLE_START_AUTH:
                if ((r = auth_start(fd, conn)) != AUTH_RESULT_RETRY)
                {
                    login_conn_remove(conn, fd,
                            (r == AUTH_RESULT_OK) ? 0 : AUTHFAIL_SLEEP_SEC);
                    return;
                }
                break;
        }
    }
}

static void 
listen_cb(int fd, short event, void *arg)
{
    int cfd;
    struct sockaddr_in xsin = {0};
    socklen_t szxsin = sizeof(xsin);
    login_conn_ctx *conn;

    if ((cfd = accept(fd, (struct sockaddr *)&xsin, &szxsin)) < 0 )
        return;

    if ((conn = malloc(sizeof(login_conn_ctx))) == NULL) {
        close(cfd);
        return;
    }
    memset(conn, 0, sizeof(login_conn_ctx));

    if ((conn->bufev = bufferevent_new(cfd, NULL, NULL, NULL, NULL)) == NULL) {
        free(conn);
        close(cfd);
        return;
    }

    reload_data();
    login_ctx_init(&conn->ctx);

    // initialize telnet protocol
    telnet_ctx_init(&conn->telnet, &telnet_callback, cfd);
    telnet_ctx_set_write_arg (&conn->telnet, (void*) conn); // use conn for buffered events
    telnet_ctx_set_resize_arg(&conn->telnet, (void*) &conn->ctx);
#ifdef DETECT_CLIENT
    telnet_ctx_set_cc_arg(&conn->telnet, (void*) &conn->ctx);
#endif
    // better send after all parameters were set
    telnet_ctx_send_init_cmds(&conn->telnet);

    // getremotename
    inet_ntop(AF_INET, &xsin.sin_addr, conn->ctx.hostip, sizeof(conn->ctx.hostip));

    // set events
    event_set(&conn->ev, cfd, EV_READ|EV_PERSIST, client_cb, conn);
    event_add(&conn->ev, NULL);

    // check ban here?  XXX can we directly use xsin.sin_addr instead of ASCII form?
    if (g_banned || check_banip(conn->ctx.hostip) )
    {
        // draw ban screen, if available. (for banip, this is empty).
        draw_text_screen(conn, ban_screen);
        login_conn_remove(conn, fd, BAN_SLEEP_SEC);
        return;
    }

    // draw banner
#ifdef INSCREEN
    _buff_write(conn, INSCREEN, sizeof(INSCREEN));
#endif

    // XXX check system load here.
    if (g_overload)
    {
        draw_overload(conn, g_overload);
        login_conn_remove(conn, fd, OVERLOAD_SLEEP_SEC);
        return;

    } else {
        draw_text_screen(conn, welcome_screen);
        draw_userid_prompt(conn);
    }
}

static void 
tunnel_cb(int fd, short event, void *arg)
{
    int cfd;
    if ((cfd = accept(fd, NULL, NULL)) < 0 )
        return;

    _set_connection_opt(cfd);

    // got new tunnel
    if (g_tunnel) 
        close(g_tunnel);
    g_tunnel = cfd;
}

///////////////////////////////////////////////////////////////////////
// Main 

#ifndef LOGIND_TUNNEL_PATH
#define LOGIND_TUNNEL_PATH BBSHOME "/run/logind.tunnel"
#endif

#ifndef FN_BINDLIST
#define FN_BINDLIST         BBSHOME "/etc/logind_ports"  // a file with list of ports to bind.
#endif

static int 
bind_port(int port)
{
    char buf[STRLEN];
    int sfd;
    struct event *pev_listen = NULL;

    snprintf(buf, sizeof(buf), "*:%d", port);

    if ( (sfd = tobindex(buf, SOCKET_QLEN, _set_bind_opt, 1)) < 0 )
    {
        fprintf(stderr, "cannot bind to port: %d. abort.\r\n", port);
        return -1;
    }
    pev_listen = malloc (sizeof(struct event));
    assert(pev_listen);

    event_set(pev_listen, sfd, EV_READ | EV_PERSIST, listen_cb, pev_listen);
    event_add(pev_listen, NULL);
    fprintf(stderr,"bound to port: %d\r\n", port);
    return 0;
}

int 
main(int argc, char *argv[])
{
    int     ch, port = 0, bound_ports = 0, tfd, as_daemon = 1;
    FILE   *fp;
    const char *tunnel_path = LOGIND_TUNNEL_PATH;
    const char *config_file = FN_BINDLIST;


    Signal(SIGPIPE, SIG_IGN);

    while ( (ch = getopt(argc, argv, "f:p:t:hD")) != -1 )
    {
        switch( ch ){
        case 'f':
            config_file = optarg;
            break;
        case 'p':
            if (optarg) port = atoi(optarg);
            break;
        case 't':
            tunnel_path = optarg;
            break;
        case 'D':
            as_daemon = 0;
            break;
        case 'h':
        default:
            fprintf(stderr, "usage: %s [-D] [-f bindlist_file] [-p port] [-t tunnel_path]\r\n", argv[0]);
            return 1;
        }
    }

    chdir(BBSHOME);
    attach_SHM();

    reload_data();

    if (as_daemon)
    {
        fprintf(stderr, "start daemonize\r\n");
        daemonize(BBSHOME "/run/logind.pid", NULL);
    }

    event_init();
    signal_set(&ev_sighup, SIGHUP, sighup_cb, &ev_sighup);
    signal_add(&ev_sighup, NULL);

    // bind ports
    if (port && bind_port(port) < 0)
    {
        fprintf(stderr, "cannot bind to port: %d. abort.\r\n", port);
        return 3;
    }
    if (port)
        bound_ports++;

    // bind from port list file
    if( NULL != (fp = fopen(config_file, "rt")) )
    {
        char buf [STRLEN];
        while (fgets(buf, sizeof(buf), fp))
        {
            port = atoi(strtok(buf, " #\r\n"));
            if (!port)
                continue;

            if (bind_port(port) < 0)
            {
                fprintf(stderr, "cannot bind to port: %d. abort.\r\n", port);
                return 3;
            }
            bound_ports++;
        }
        fclose(fp);
    }

    if (!bound_ports)
    {
        fprintf(stderr, "error: no ports to bind. abort.\r\n");
        return 4;
    }

    /* Give up root privileges: no way back from here */
    setgid(BBSGID);
    setuid(BBSUID);

    // create tunnel
    if ( (tfd = tobindex(tunnel_path, 1, _set_bind_opt, 1)) < 0)
    {
        fprintf(stderr, "cannot create tunnel: %s. abort.\r\n", tunnel_path);
        return 2;
    }
    event_set(&ev_tunnel, tfd, EV_READ | EV_PERSIST, tunnel_cb, &ev_tunnel);
    event_add(&ev_tunnel, NULL);

    fprintf(stderr, "start event dispatch.\r\n");
    event_dispatch();

    return 0;
}

// vim:et
