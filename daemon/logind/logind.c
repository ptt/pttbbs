// The High Performance Login Daemon (works with tunnel mode)
// $Id$
//
// Create:       Hung-Te Lin <piaip@csie.ntu.edu.tw>
// Contributors: wens, kcwu
// Initial Date: 2009/06/01
//
// Copyright (C) 2009, Hung-Te Lin <piaip@csie.ntu.edu.tw>
// All rights reserved

// TODO:
// 1. cache guest's usernum and check if too many guests online
// 2. [drop] change close connection to 'wait until user hit then close'
// 3. regular check text screen files instead of HUP?
// 4. start mbbsd for some parameter?

#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>
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

#ifndef IDLE_TIMEOUT_SEC
#define IDLE_TIMEOUT_SEC    (20*60)
#endif

#define MAX_TEXT_SCREEN_LINES   (24)

#ifndef MAX_FDS
#define MAX_FDS             (100000)
#endif

#define MY_SVC_NAME  "logind"
#define LOG_PREFIX  "[logind] "

///////////////////////////////////////////////////////////////////////
// global variables
int g_tunnel;           // tunnel for service daemon
int g_reload_data = 1;  // request to reload data

// server status
int g_overload = 0;
int g_banned   = 0;
int g_verbose  = 0;
int g_opened_fd= 0;

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
    LOGIN_HANDLE_REDRAW_USERID,
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
    int  icurr;         // cursor (only available in userid input mode)
    Fnv32_t client_code;
    char userid [IDBOXLEN];
    char pad0;   // for safety
    char passwd [PASSLEN+1];
    char pad1;   // for safety
    char hostip [IPV4LEN+1];
    char pad2;   // for safety
    char port   [IDLEN+1];
    char pad3;   // for safety
} login_ctx;

typedef struct {
    struct bufferevent *bufev;
    struct event ev;
    TelnetCtx    telnet;
    login_ctx    ctx;
} login_conn_ctx;

typedef struct {
    struct event ev;
    int    port;
} bind_event;

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
        case LOGIN_STATE_START:
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

                case KEY_HOME:
                    ctx->icurr = 0;
                    return LOGIN_HANDLE_REDRAW_USERID;

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
                c == ' ' ||
                l+1 >= sizeof(ctx->userid))
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
// ANSI/vt100/vt220 special keys

static int
_handle_term_keys(char **pstr, int *plen)
{
    char *str = *pstr;

    assert(plen && pstr && *pstr && *plen > 0);
    // fprintf(stderr, "handle_term: input = %d\r\n", *plen);

    // 1. check ESC
    (*plen)--; (*pstr)++;
    if (*str != ESC_CHR)
    {
        int c = (unsigned char)*str;

        switch(c)
        {
            case KEY_CR:
                return KEY_ENTER;

            case KEY_LF:
                return 0; // to ignore

            case Ctrl('A'):
                return KEY_HOME;
            case Ctrl('E'):
                return KEY_END;

            // case '\b':
            case Ctrl('H'):
            case 127:
                return KEY_BS;
        }
        return c;
    }

    // 2. check O / [
    if (!*plen)
        return KEY_ESC;
    (*plen)--; (*pstr)++; str++;
    if (*str != 'O' && *str != '[')
        return *str;
    // 3. alpha: end, digit: one more (~)
    if (!*plen)
        return *str;
    (*plen)--; (*pstr)++; str++;
    if (!isascii(*str))
        return KEY_UNKNOWN;
    if (isdigit(*str))
    {
        if (*plen)
        {
            (*plen)--; (*pstr)++;
        }
        switch(*str)
        {
            case '1':
                // fprintf(stderr, "got KEY_HOME.\r\n");
                return KEY_HOME;
            case '4':
                // fprintf(stderr, "got KEY_END.\r\n");
                return KEY_END;
            case '3':
                // fprintf(stderr, "got KEY_DEL.\r\n");
                return KEY_DEL;
            default:
                // fprintf(stderr, "got KEY_UNKNOWN.\r\n");
                return KEY_UNKNOWN;
        }
    }
    if (isalpha(*str))
    {
        switch(*str)
        {
            case 'C':
                // fprintf(stderr, "got KEY_RIGHT.\r\n");
                return KEY_RIGHT;
            case 'D':
                // fprintf(stderr, "got KEY_LEFT.\r\n");
                return KEY_LEFT;
            default:
                return KEY_UNKNOWN;
        }
    }

    // unknown
    return KEY_UNKNOWN;
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

#ifdef  LOGIND_OPENFD_IN_AYT
static void 
_telnet_send_ayt_cb(void *ayt_arg, int fd)
{
    login_conn_ctx *conn = (login_conn_ctx *)ayt_arg;
    char buf[32];

    assert(conn);
    snprintf(buf, sizeof(buf), "  %u  \r\n", g_opened_fd);
    _buff_write(conn, buf, strlen(buf));
}
#endif

const static struct TelnetCallback 
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
#define USERID_EMPTY_MSG    ANSI_RESET "請重新輸入。"
#define USERID_EMPTY_YX     PASSWD_PROMPT_YX
#define SERVICE_FAIL_MSG    ANSI_COLOR(0;1;31) "抱歉，部份系統正在維護中，請稍候再試。" ANSI_RESET "\r\n"
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

    fprintf(stderr, LOG_PREFIX "start reloading data.\r\n");
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
    if (g_verbose) fprintf(stderr, LOG_PREFIX "reset connection attribute.\r\n");
    _buff_write(conn, LOGIN_PROMPT_END, sizeof(LOGIN_PROMPT_END)-1);
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
    _mt_move_yx(conn, PASSWD_CHECK_YX); _mt_clrtoeol(conn);
    _mt_move_yx(conn, SERVICE_FAIL_YX); _mt_clrtoeol(conn);
    _buff_write(conn, SERVICE_FAIL_MSG, sizeof(SERVICE_FAIL_MSG)-1);
}

static void
draw_overload(login_conn_ctx *conn, int type)
{
    _mt_move_yx(conn, PASSWD_CHECK_YX); _mt_clrtoeol(conn);
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
    userec_t user = {0};

    if (free_uid)
    {
        strlcpy(ctx->userid, free_uid, sizeof(ctx->userid));
        return AUTH_RESULT_FREEID;
    }

    if (passwd_load_user(uid, &user) < 1 ||
        !user.userid[0] ||
        !checkpasswd(user.passwd, passbuf) )
    {
        if (user.userid[0])
            strcpy(uid, user.userid);
        return AUTH_RESULT_FAIL;
    }

    // normalize user id
    strcpy(uid, user.userid);
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
    strlcpy(ld.port,   ctx->port,   sizeof(ld.port));
    ld.encoding = ctx->encoding;
    ld.client_code = ctx->client_code;
    ld.t_lines  = 24;   // default size
    ld.t_cols   = 80;
    if (ctx->t_lines > ld.t_lines)
        ld.t_lines = ctx->t_lines;
    if (ctx->t_cols > ld.t_cols)
        ld.t_cols = ctx->t_cols;

    if (g_verbose) fprintf(stderr, LOG_PREFIX "start new service: %s@%s:%s #%d\r\n",
            ld.userid, ld.hostip, ld.port, fd);

    // XXX simulate the cache re-construction in mbbsd/login_query.
    resolve_garbage();

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
        if (g_verbose) fprintf(stderr, LOG_PREFIX "auth fail (goodbye):  %s@%s  #%d...",
                conn->ctx.userid, conn->ctx.hostip, fd);
        return AUTH_RESULT_STOP;

    }

    // prompt for retry
    if (was_valid_uid)
        draw_auth_fail(conn);
    else
        draw_empty_userid_warn(conn);

    ctx->state = LOGIN_STATE_USERID;
    draw_userid_prompt(conn, NULL, 0);
    return AUTH_RESULT_RETRY;
}



///////////////////////////////////////////////////////////////////////
// Event callbacks

static struct event ev_sighup, ev_tunnel;

static void 
sighup_cb(int signal, short event, void *arg)
{
    fprintf(stderr, LOG_PREFIX 
            "caught sighup (request to reload) with %u opening fd...\r\n",
            g_opened_fd);
    g_reload_data = 1;
}

static void 
endconn_cb(int fd, short event, void *arg)
{
    login_conn_ctx *conn = (login_conn_ctx*) arg;
    if (g_verbose) fprintf(stderr, LOG_PREFIX
            "login_conn_remove: removed connection (%s@%s) #%d...",
            conn->ctx.userid, conn->ctx.hostip, fd);
    event_del(&conn->ev);
    bufferevent_free(conn->bufev);
    close(fd);
    g_opened_fd--;
    free(conn);
    if (g_verbose) fprintf(stderr, " done.\r\n");
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
        if (g_verbose) fprintf(stderr, LOG_PREFIX
                "login_conn_remove: stop conn #%d in %d seconds later.\r\n", 
                fd, sleep_sec);
    }
}

static void 
client_cb(int fd, short event, void *arg)
{
    login_conn_ctx *conn = (login_conn_ctx*) arg;
    int len, r;
    unsigned char buf[64], ch, *s = buf;

    // for time-out, simply close connection.
    if (event & EV_TIMEOUT)
    {
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
         if (errno == EINTR || errno == EAGAIN)
             return;

        // close connection
        login_conn_remove(conn, fd, 0);
        return;
    }

    len = telnet_process(&conn->telnet, buf, len);

    while (len > 0)
    {
        int c = _handle_term_keys((char**)&s, &len);

        // for zero, ignore.
        if (!c)
            continue;

        if (c == KEY_UNKNOWN)
        {
            _mt_bell(conn);
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
                if (g_verbose) fprintf(stderr, LOG_PREFIX
                        "redraw userid: id=[%s], icurr=%d\r\n",
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
                            conn->ctx.encoding = CONV_GB;
                            *uid_lastc = 0;
                            break;
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
listen_cb(int lfd, short event, void *arg)
{
    int fd;
    struct sockaddr_in xsin = {0};
    struct timeval idle_tv = { IDLE_TIMEOUT_SEC, 0};
    socklen_t szxsin = sizeof(xsin);
    login_conn_ctx *conn;
    bind_event *pbindev = (bind_event*) arg;

    if ((fd = accept(lfd, (struct sockaddr *)&xsin, &szxsin)) < 0 )
        return;

    if ((conn = malloc(sizeof(login_conn_ctx))) == NULL) {
        close(fd);
        return;
    }
    memset(conn, 0, sizeof(login_conn_ctx));

    if ((conn->bufev = bufferevent_new(fd, NULL, NULL, NULL, NULL)) == NULL) {
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
    inet_ntop(AF_INET, &xsin.sin_addr, conn->ctx.hostip, sizeof(conn->ctx.hostip));
    snprintf(conn->ctx.port, sizeof(conn->ctx.port), "%u", pbindev->port); // ntohs(xsin.sin_port));

    if (g_verbose) fprintf(stderr, LOG_PREFIX
            "new connection: %s:%s (opened fd: %d)\r\n", 
            conn->ctx.hostip, conn->ctx.port, g_opened_fd);

    // set events
    event_set(&conn->ev, fd, EV_READ|EV_PERSIST, client_cb, conn);
    event_add(&conn->ev, &idle_tv);

    // check ban here?  XXX can we directly use xsin.sin_addr instead of ASCII form?
    if (g_banned || check_banip(conn->ctx.hostip) )
    {
        // draw ban screen, if available. (for banip, this is empty).
        draw_text_screen(conn, ban_screen);
        login_conn_remove(conn, fd, BAN_SLEEP_SEC);
        return;
    }

    // draw banner
    // XXX for systems that needs high performance, you must reduce the
    // string in INSCREEN/banner.
    // if you have your own banner, define as INSCREEN in pttbbs.conf
    // if you don't want anny benner, define NO_INSCREEN
#ifndef NO_INSCREEN
# ifndef   INSCREEN
#  define  INSCREEN "【" BBSNAME "】◎(" MYHOSTNAME ", " MYIP ") \r\n"
# endif
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
        draw_userid_prompt(conn, NULL, 0);
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

static int 
bind_port(int port)
{
    char buf[STRLEN];
    int sfd;
    bind_event *pev = NULL;

    snprintf(buf, sizeof(buf), "*:%d", port);

    fprintf(stderr, LOG_PREFIX "binding to port: %d...", port);
    if ( (sfd = tobindex(buf, SOCKET_QLEN, _set_bind_opt, 1)) < 0 )
    {
        fprintf(stderr, LOG_PREFIX "cannot bind to port: %d. abort.\r\n", port);
        return -1;
    }
    pev = malloc  (sizeof(bind_event));
    memset(pev, 0, sizeof(bind_event));
    assert(pev);

    pev->port = port;
    event_set(&pev->ev, sfd, EV_READ | EV_PERSIST, listen_cb, pev);
    event_add(&pev->ev, NULL);
    fprintf(stderr,"ok. \r\n");
    return 0;
}

static int 
parse_bindports_conf(FILE *fp, char *tunnel_path, int sz_tunnel_path)
{
    char buf [PATHLEN], vprogram[STRLEN], vport[STRLEN], vtunnel[STRLEN];
    int bound_ports = 0;
    int iport = 0;

    // format: [ vprogram port ] or [ vprogram tunnel path ]
    while (fgets(buf, sizeof(buf), fp))
    {
        if (sscanf(buf, "%s %s", vprogram, vport) != 2)
            continue;
        if (strcmp(vprogram, MY_SVC_NAME) != 0)
            continue;
        if (strcmp(vport, "tunnel") == 0)
        {
            if (sscanf(buf, "%s %s %s", vprogram, vport, vtunnel) != 3)
            {
                fprintf(stderr, LOG_PREFIX "incorrect tunnel configuration. abort.\r\n");
                exit(1);
            }
            // there can be only one tunnel, so user command line is more important.
            if (*tunnel_path)
            {
                fprintf(stderr, LOG_PREFIX
                        "warning: ignored configuration file and use %s as tunnel path.",
                        tunnel_path);
                continue;
            }
            strlcpy(tunnel_path, vtunnel, sz_tunnel_path);
            continue;
        }

        iport = atoi(vport);
        if (!iport)
        {
            fprintf(stderr, LOG_PREFIX "warning: unknown settings: %s\r\n", buf);
            continue;
        }

        if (bind_port(iport) < 0)
        {
            fprintf(stderr, LOG_PREFIX "cannot bind to port: %d. abort.\r\n", iport);
            exit(3);
        }
        bound_ports++;
    }
    return bound_ports;
}

int 
main(int argc, char *argv[])
{
    int     ch, port = 0, bound_ports = 0, tfd, as_daemon = 1;
    FILE   *fp;
    char tunnel_path[PATHLEN] = "";
    const char *config_file = FN_CONF_BINDPORTS;
    const char *log_file = NULL;


    Signal(SIGPIPE, SIG_IGN);

    while ( (ch = getopt(argc, argv, "f:p:t:l:hDv")) != -1 )
    {
        switch( ch ){
        case 'f':
            config_file = optarg;
            break;
        case 'l':
            log_file = optarg;
            break;
        case 'p':
            if (optarg) port = atoi(optarg);
            break;
        case 't':
            strlcpy(tunnel_path, optarg, sizeof(tunnel_path));
            break;
        case 'D':
            as_daemon = 0;
            break;
        case 'v':
            g_verbose++;
            break;
        case 'h':
        default:
            fprintf(stderr, "usage: %s [-v][-D] [-l log_file] [-f bindport_conf] [-p port] [-t tunnel_path]\r\n", argv[0]);
            fprintf(stderr, "\t-f: read configuration from file (default: %s)\r\n", BBSHOME "/" FN_CONF_BINDPORTS);
            fprintf(stderr, "\t-v: provide verbose messages\r\n");
            fprintf(stderr, "\t-D: do not enter daemon mode.\r\n");
            return 1;
        }
    }

    struct rlimit r = {.rlim_cur = MAX_FDS, .rlim_max = MAX_FDS};
    if (setrlimit(RLIMIT_NOFILE, &r) < 0)
    {
        fprintf(stderr, LOG_PREFIX "warning: cannot increase max fd to %u...\r\n", MAX_FDS);
    }

    chdir(BBSHOME);
    attach_SHM();

    reload_data();

    struct event_base *evb = event_init();

    // bind ports
    if (port && bind_port(port) < 0)
    {
        fprintf(stderr, LOG_PREFIX "cannot bind to port: %d. abort.\r\n", port);
        return 3;
    }
    if (port)
        bound_ports++;

    // bind from port list file
    if( NULL != (fp = fopen(config_file, "rt")) )
    {
        bound_ports += parse_bindports_conf(fp, tunnel_path, sizeof(tunnel_path));
        fclose(fp);
    }

    if (!bound_ports)
    {
        fprintf(stderr, LOG_PREFIX "error: no ports to bind. abort.\r\n");
        return 4;
    }
    if (!*tunnel_path)
    {
        fprintf(stderr, LOG_PREFIX "error: must assign one tunnel path. abort.\r\n");
        return 4;
    }

    /* Give up root privileges: no way back from here */
    setgid(BBSGID);
    setuid(BBSUID);

    // create tunnel
    fprintf(stderr, LOG_PREFIX "creating tunnel: %s...", tunnel_path);
    if ( (tfd = tobindex(tunnel_path, 1, _set_bind_opt, 1)) < 0)
    {
        fprintf(stderr, LOG_PREFIX "cannot create tunnel. abort.\r\n");
        return 2;
    }
    fprintf(stderr, "ok.\r\n");
    event_set(&ev_tunnel, tfd, EV_READ | EV_PERSIST, tunnel_cb, &ev_tunnel);
    event_add(&ev_tunnel, NULL);

    // daemonize!
    if (as_daemon)
    {
        fprintf(stderr, LOG_PREFIX "start daemonize\r\n");
        daemonize(BBSHOME "/run/logind.pid", log_file);

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

    // warning: after daemonize, the directory was changed to root (/)...
    fprintf(stderr, LOG_PREFIX "start event dispatch.\r\n");
    event_dispatch();

    return 0;
}

// vim:et
