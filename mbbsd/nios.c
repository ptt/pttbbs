/* $Id$ */
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include "vtkbd.h"
#include "bbs.h"

#ifdef EXP_NIOS

// nios: piaip's Network I/O Stream
//       piaip's New implementation for Input and Output System
// Author: Hung-Te Lin (piaip)
// Create: Mon Oct 26 01:56:27 CST 2009
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
// The input system has several layers:
// 1. OS socket file descriptor (read/write): network -> fd
// 2. console stream (nios.c, cin_*): fd -> buffer
// 3. (optional) Telnet Protocol (common/sys/telnet.c): buffer -> term
// 4. (optional) Encoding Conversion (convert.c): term(enc1) -> term(enc2)
// 5. Virtual Terminal Keyboard (vtkbd.c): term -> key
// 6. Virtual Key (nios.c, vkey_*) key -> preprocess and management
// --------------------------------------------------------------------------

// features
#define VKEY_USE_CIN

#ifdef DEBUG
#define CIN_DEBUG
#define VKEY_DEBUG
#endif

#ifndef DEBUG
#define CIN_PROTO  static inline
#define VKEY_PROTO inline
#else
#define CIN_PROTO
#define VKEY_PROTO
#endif

// XXX use this temporary...
int process_pager_keys(int ch);

// debug helpers
#if defined(CIN_DEBUG) || defined(VKEY_DEBUG)
#include <stdarg.h>
static void
nios_dbgf(const char *fmt, ...)
{
    char msg[256];
    time4_t now = time(0);
    const char *logfn = BBSHOME "/log/niosdbg.log";
    va_list ap;

    va_start(ap, fmt);
    snprintf (msg,  sizeof(msg), "%s %*s ", Cdate_mdHMS(&now), IDLEN, cuser.userid);
    vsnprintf(msg + strlen(msg), sizeof(msg)-strlen(msg), fmt, ap);
    va_end(ap);
    strlcat(msg, "\n", sizeof(msg));

    log_file(logfn, LOG_CREAT, msg);
}
#endif

#ifdef CIN_DEBUG
# define CINDBGLOG(...)   nios_dbgf( __VA_ARGS__)
# else 
# define CINDBGLOG(...)
#endif
#ifdef VKEY_DEBUG
# define VKEYDBGLOG(...)  nios_dbgf( __VA_ARGS__)
# else 
# define VKEYDBGLOG(...)
#endif

/////////////////////////////////////////////////////////////////////////////
// console stream input (nios:cin): fd -> buffer

// configuration
#define CIN_BUFFER_SIZE (4096)
#define CIN_DEFAULT_FD  (cin_fd)

// API prototypes

// buffer management
CIN_PROTO int  cin_is_buffer_empty(void);   // check if input buffer is empty
CIN_PROTO int  cin_is_buffer_full(void);    // check if input buffer is full
CIN_PROTO void cin_clear_buffer(void);      // drop everying in buffer
CIN_PROTO int  cin_scan_buffer(char c);     // return if buffer has given character
// fd management
CIN_PROTO int  cin_is_fd_empty(int fd);     // quick determine if a fd is empty
CIN_PROTO int  cin_poll_fds(int fd1, int fd2, int ms); // poll fd1 and fd2 for ms milliseconds
CIN_PROTO void cin_clear_fd(int fd);        // drop everying in fd
CIN_PROTO void cin_fetch_fd(int fd);        // read more data from fd to buffer
// virtual combination
CIN_PROTO int  cin_read(void);              // read one byte from cin (buffer then cin_fd)

// virtual buffer for cin
static const int cin_fd = STDIN_FILENO;
static char cin_buf[CIN_BUFFER_SIZE];
static VBUF vcin = {
    .head    = cin_buf,
    .tail    = cin_buf,
    .capacity= sizeof(cin_buf)-1,
    .buf     = cin_buf,
    .buf_end = cin_buf + sizeof(cin_buf),
}, *cin = &vcin;

#ifdef CIN_DEBUG
static    int  cin_debugging = 0;
CIN_PROTO void cin_debug_print_content();
#endif

/**
 * cin_init(): initialize cin context
 */
CIN_PROTO void
cin_init()
{
    vbuf_attach(cin, cin_buf, sizeof(cin_buf));
}

/**
 * cin_is_buffer_empty(): quick check if input buffer is empty
 */
CIN_PROTO int
cin_is_buffer_empty()
{
    return vbuf_is_empty(cin);
}

/**
 * cin_is_buffer_full(): check if input buffer is full
 */
CIN_PROTO int 
cin_is_buffer_full()
{
    return vbuf_is_full(cin);
}

/**
 * cin_scan_buffer(c): return if buffer has given character
 */
CIN_PROTO int 
cin_scan_buffer(char c)
{
    return vbuf_strchr(cin, c) >= 0;
}

/**
 * cin_is_fd_empty(fd): query if cin_fd is empty
 * @return:   1 for empty, 0 for error / data available.
 */
CIN_PROTO int
cin_is_fd_empty(int fd)
{
    CINDBGLOG("cin_is_fd_empty(%d)", fd);

#ifdef FIONREAD
// #warning nios:cin_is_fd_empty(): using ioctl(FIONREAD) method.
    // try ioctl
    int r;
    if (ioctl(fd, FIONREAD, &r) == 0)
        return r == 0;
    assert(!"sorry, your system failed for FIONREAD...");
    return 0;   // error

#elif defined(MSG_DONTWAIT) && defined(MSG_PEEK)
# warning nios:cin_is_fd_empty(): changed to recv(MSG_DONTWAIT) method.
    // try recv
    int r;
    char c = 0;
    r = recv(fd, &c, sizeof(c), MSG_PEEK | MSG_DONTWAIT);
    if (errno == EAGAIN)
        return 1;
    // XXX errno should not be EINTR...
    assert(errno != EINTR);
    return r > 0 ? 0 : 1;

#else
# warning nios:cin_is_fd_empty(): changed to polling method.
    // we can only use poll.
    return cin_poll_fds(-1, 0) == 0;

#endif
}

/**
 * cin_poll_fds(fd1, fd2, ms): query the input status of given fds.
 * @param fd1: primary fd to check
 * @param fd2: skip if <= 0
 * @param ms: 0 for non-blocking, INFTIM(-1) for infinite, otherwise timeout milliseconds
 * @return: bitmask of fds having data/error, 0 for timeout, and -1 if error.
 */
#define CIN_POLL_FDS_MASK   (POLLIN|POLLERR|POLLHUP|POLLNVAL)
// mask to check the return value of cin_poll_fds
#define CIN_POLL_CINFD  (1)
#define CIN_POLL_FD2    (2)

#define CIN_IS_VALID_FD2(fd)    ((fd) > 0)

CIN_PROTO int 
cin_poll_fds(int fd1, int fd2, int timeout)
{
    CINDBGLOG("cin_poll_fds(fd1=%d, fd2=%d, timeout=%d)", fd1, fd2, timeout);

    int r;
    struct pollfd fds[2] ={
        { .fd = fd1, .events = POLLIN, .revents = 0, },
        { .fd = fd2, .events = POLLIN, .revents = 0, },
    };

#ifdef STAT_SYSSELECT
    STATINC(STAT_SYSSELECT);
#endif
    while ( 0 > (r = poll(fds, CIN_IS_VALID_FD2(fd2) ? 2 : 1, timeout)) &&
            errno == EINTR);
    assert(r >= 0);
    if (r <= 0)
        return r;

    r = ((fds[0].revents & CIN_POLL_FDS_MASK) ? CIN_POLL_CINFD : 0) |
        ((fds[1].revents & CIN_POLL_FDS_MASK) ? CIN_POLL_FD2   : 0);
    return r;
}

/**
 * cin_clear_buffer(): drop everying in buffer
 */
CIN_PROTO void
cin_clear_buffer()
{
    CINDBGLOG("cin_clear_buffer()");
    vbuf_clear(cin);
}

/**
 * cin_clear_fd(int fd): quick discard all data in fd
 */
CIN_PROTO void
cin_clear_fd(int fd)
{
    CINDBGLOG("cin_clear_fd(%d)", fd);
    // method 1, use setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *)(&optval), sizeof(optval));
    // method 2, fetch and discard
    VBUF vgarbage, *v = &vgarbage;
    char garbage[4096]; // any magic number works here

    if (cin_is_fd_empty(fd))
        return;

    vbuf_attach(v, garbage, sizeof(garbage));
    do {
        vbuf_read(v, fd, VBUF_RWSZ_MIN);
        if (vbuf_is_empty(v)) // maybe read error
            break;
        vbuf_clear(v);
    } while (!cin_is_fd_empty(fd));
}

/**
 * cin_fetch_fd(fd): read more data from fd to buffer
 */
CIN_PROTO void  
cin_fetch_fd(int fd)
{
#ifdef STAT_SYSREADSOCKET
	STATINC(STAT_SYSREADSOCKET);
#endif
    assert(fd == cin_fd);
#if 1
    // XXX we don't know how to deal with telnetctx/convert yet...
    char buf[sizeof(cin_buf)];
    ssize_t sz;
    do {
        sz = tty_read((unsigned char*)buf, vbuf_space(cin));
        // for tty_read: sz<0 = EAGAIN
        if (sz > 0)
        {
            vbuf_putblk(cin, buf, sz);
        }
    } while (sz < 0);
#else
    // try to read data from stdin
    vbuf_read(cin, fd, VBUF_RWSZ_MIN);
#endif
#ifdef CIN_DEBUG
    cin_debug_print_content();
#endif
}

/**
 * cin_read(): read one byte from cin (buffer then fd)
 * @return: EOF if error, otherwise the first byte from cin.
 */
CIN_PROTO int
cin_read()
{
    if (!cin_is_buffer_empty())
        return vbuf_pop(cin);

    CINDBGLOG("cin_read()");

    cin_fetch_fd(cin_fd);
    if (cin_is_buffer_empty())
        return EOF;

    return vbuf_pop(cin);
}

#ifdef CIN_DEBUG
void debug_print_input_buffer(char *s, size_t len);

CIN_PROTO void
cin_debug_print_content()
{
    if (!cin_debugging || cin_is_buffer_empty())
        return;
    debug_print_input_buffer(vbuf_cstr(cin), vbuf_size(cin));
}
#endif

/////////////////////////////////////////////////////////////////////////////
// virtual key  (nios:vkey): key -> preprocess and management

// NOTE for every vkey* API, you must consider:
// 1. PEEKed character in vkctx
// 2. cin_buffer and cin_fd
// 3. vkctx.attached_fd()
// using the vkey_process() will handle all these cases. otherwise, you 
// have to take care of all thse input sources.

// vkey context
typedef struct VKEY_CTX {
    int         peek_ch;
    int         attached_fd;
    VtkbdCtx    vtkbd;
    // TelnetCtx   telnet;
} VKEY_CTX;

static VKEY_CTX vkctx = {
    .peek_ch = KEY_INCOMPLETE,
};

#define VKEY_HAS_PEEK()     (vkctx.peek_ch != KEY_INCOMPLETE)
#define VKEY_GET_PEEK()     (vkctx.peek_ch)
#define VKEY_SET_PEEK(c)    (vkctx.peek_ch = (c))
#define VKEY_RESET_PEEK()   (VKEY_SET_PEEK(KEY_INCOMPLETE))

VKEY_PROTO void
vkey_init()
{
    VKEYDBGLOG("vkey_init()");

    memset(&vkctx, 0, sizeof(vkctx));
    cin_init();
    VKEY_RESET_PEEK();
    // XXX initialize telnet, convert, ...?
}

/**
 * vkey_attach(fd): attach and replace additional fd to vkey*
 * @param fd: the file descriptor to attach
 * @return: previously attached fd
 */
VKEY_PROTO int
vkey_attach(int fd)
{
    VKEYDBGLOG("vkey_attach(%d)", fd);

    int r = vkctx.attached_fd;
    vkctx.attached_fd = fd;
    return r;
}

/**
 * vkey_detach(): detach any additional fd in vkey*
 */
VKEY_PROTO int
vkey_detach()
{
    CINDBGLOG("vkey_detach()");

    int r = vkctx.attached_fd;
    vkctx.attached_fd = 0;
    return r;
}

/**
 * vkey_is_full(): return if key buffer is full.
 */
VKEY_PROTO int 
vkey_is_full()
{
    VKEYDBGLOG("vkey_is_full()");
    return cin_is_buffer_full();
}

/**
 * vkey_process_cin(): read data from cin and process.
 * @return first byte from cin, or EOF for error
 */
static VKEY_PROTO int
vkey_process_cin()
{
    VKEYDBGLOG("vkey_process_cin()");

    int ch;

    ch = cin_read();
    if (ch  == EOF) // quick abort
        return EOF;

    // TODO process telnet protocol?
    // (it's now temporary done in cin_read -> tty_read... )

    // convert virtual terminal keys
    ch = vtkbd_process(ch, &vkctx.vtkbd);

    // process transparent keys
    switch(ch)
    {
        case KEY_ESC:
            // XXX change this to (META|key) someday...
            KEY_ESC_arg = vkctx.vtkbd.esc_arg;
            break;

            // TODO move this to somewhere else?
        case Ctrl('L'):
            ch = KEY_INCOMPLETE;
            redrawwin();
            refresh();
            // doupdate();
            break;

#ifdef DEBUG
	    case Ctrl('Q'):
            ch = KEY_INCOMPLETE;
            {
                struct rusage ru;
                getrusage(RUSAGE_SELF, &ru);
                vmsgf("sbrk: %ld KB, idrss: %d KB, isrss: %d KB",
                        (sbrk(0) - (void*)0x8048000) / 1024,
                        (int)ru.ru_idrss, (int)ru.ru_isrss);
#ifdef CIN_DEBUG
                cin_debugging = !cin_debugging;
#endif
            }
            break;
#endif
    }
    if (currutmp) ch = process_pager_keys(ch);
    return ch;
}

/**
 * vkey_process(timeout, peek): receive and block (max to timeout milliseconds) next key
 * @param timeout: 0 for non-block, INFTIM(-1) for infinite, otherwise milliseconds
 * @param peek: to keep the data in buffer
 * @return: virtual key code, or KEY_INCOMPLETE for timeout
 */
static VKEY_PROTO int
vkey_process(int timeout, int peek)
{
    VKEYDBGLOG("vkey_process(%d, %d)", timeout, peek);

    int r;

    // process lask peeked data
    if (VKEY_HAS_PEEK())
    {
        // cached
        r = VKEY_GET_PEEK();
        if (!peek)
            VKEY_RESET_PEEK();
        return r;
    }

    do {
        if (!cin_is_buffer_empty())
        {
            r = vkey_process_cin();
            continue;
        }

        // XXX should we let cin_poll select from fds of fd_empty?

        // now, try to read from fd.
        assert(1 == CIN_POLL_CINFD);    // cin_is_fd_empty() will return 1.
        if (timeout > 0 || CIN_IS_VALID_FD2(vkctx.attached_fd))
        {
            r = cin_poll_fds(CIN_DEFAULT_FD, vkctx.attached_fd, timeout);
        }
        else if (timeout == 0)
        {
            r = cin_is_fd_empty(CIN_DEFAULT_FD);
        }
        else {
            assert(timeout == INFTIM);  // let's simply read it.
            r = CIN_POLL_CINFD;
        }

        if (r == 0) // timeout
        {
            if (KEY_INCOMPLETE != I_TIMEOUT && CIN_IS_VALID_FD2(vkctx.attached_fd))
                r = I_TIMEOUT;
            else
                return KEY_INCOMPLETE; // must directly return here.
        }
        if (r <  0) // error
            r = KEY_UNKNOWN;        // or EOF?
        else if (r & CIN_POLL_FD2)  // the second fd
            r = I_OTHERDATA;
        else {
            assert(r & CIN_POLL_CINFD);
            r = vkey_process_cin();
        }
    } while (r == KEY_INCOMPLETE);

    // process peek
    if (peek && r != KEY_INCOMPLETE)
        VKEY_SET_PEEK(r);

    return r;
}

/**
 * vkey_is_ready(): determine if input buffer is complete for a key input
 * @return: 1 for something to read/error, 0 for incomplete
 */
VKEY_PROTO int
vkey_is_ready()
{
    VKEYDBGLOG("vkey_is_ready()");
    return vkey_process(0, 1) != KEY_INCOMPLETE;
}

/**
 * vkey_poll(timeout): poll for timeout milliseconds and return if key is ready
 * @param timeout: 0 for non-block, INFTIM(-1) for infinite, otherwise milliseconds
 * @return: 1 for ready, otherwise 0
 */
VKEY_PROTO int
vkey_poll(int timeout)
{
    if (timeout) refresh();
    VKEYDBGLOG("vkey_poll(%d)", timeout);
    return vkey_process(timeout, 1) != KEY_INCOMPLETE;
}

/**
 * vkey_is_typeahead(): quick check if input buffer has data arrived (maybe not ready yet)
 * @return: 0 for empty, otherwise 1
 */
VKEY_PROTO int
vkey_is_typeahead()
{
    VKEYDBGLOG("vkey_is_typeahead(): %d|%d", 
            VKEY_HAS_PEEK() || !cin_is_buffer_empty());

    return  VKEY_HAS_PEEK() || !cin_is_buffer_empty();
}

/**
 * vkey_prefetch(timeout): try to prefecth as more data as possible from fd to buffer for up to timeout milliseconds
 */
VKEY_PROTO int  
vkey_prefetch(int timeout)
{
    VKEYDBGLOG("vkey_prefetch(%d)", timeout);

    // always only prefetch cin_fd
    if (cin_poll_fds(CIN_DEFAULT_FD, -1, timeout) == 0)  // timeout
        return 0;

    // error or valid date
    cin_fetch_fd(CIN_DEFAULT_FD);
    return 1;
}

/**
 * vkey_is_prefetched(c): check if c (in raw data form) is already in prefetched buffer
 */
VKEY_PROTO int
vkey_is_prefetched(char c)
{
    // VKEYDBGLOG("vkey_is_prefetched(0x%02X)", c);

    if ((int)c == VKEY_GET_PEEK())
        return 1;

    if (cin_scan_buffer(c))
        return 1;

    return 0;
}

/**
 * vkey(): receive and block for next key
 * @return: virtual key code
 */
VKEY_PROTO int 
vkey()
{
    VKEYDBGLOG("vkey()");

    int c;
    refresh();

    while ((c = vkey_process(INFTIM, 1)) == KEY_INCOMPLETE);
    return c;
}

/**
 * vkey_purge(): clear and discard all data in current input buffer
 */
VKEY_PROTO void
vkey_purge()
{
    VKEYDBGLOG("vkey_purge()");

    // a magic number to set how much data we're going to process 
    // before a real purge (may contain telnet protocol)
    int bytes_before_purge = 32; 
    
    // let's still try to process some remaining bytes
    vkey_prefetch(0);
    while (vkey_is_ready() && bytes_before_purge-- > 0)
    {
        // we can't deal with I_OTHERDATA...
        if (vkey() == I_OTHERDATA)
            break;
    }

    // ok, now let's try our best to purge all remaining data
    cin_clear_fd(CIN_DEFAULT_FD);

    // XXX in current usage,  we don't expect vkey_purge to clean
    // remote FDs.
#if 0
    if (CIN_IS_VALID_FD2(vkctx.attached_fd))
        cin_clear_fd(vkctx.attached_fd);
#endif

    cin_clear_buffer();
    VKEY_RESET_PEEK();

    // TODO reset telnet/vtkbd/conver?
}

#endif // EXP_NIOS

// vim:ts=4:sw=4:et
