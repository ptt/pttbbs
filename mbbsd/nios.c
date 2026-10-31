#include <sys/socket.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include "vtkbd.h"
#include "bbs.h"

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
#define CIN_PROTO  static
#define VKEY_PROTO inline
#else
#define CIN_PROTO
#define VKEY_PROTO
#endif

// Linux does not define INFTIM
#ifndef INFTIM
#define INFTIM (-1)
#endif

#ifdef FIONREAD
#define HAVE_FIONREAD   (1)
#else
#define HAVE_FIONREAD   (0)
#define FIONREAD        (-1)
#endif

// debug helpers
#if defined(CIN_DEBUG) || defined(VKEY_DEBUG)
#include <stdarg.h>

static void nios_dbgf(const char *fmt, ...) GCC_CHECK_FORMAT(1,2);
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
    STRLCAT(msg, "\n");

    log_file(logfn, msg);
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
#ifndef CIN_BUFFER_SIZE
#define CIN_BUFFER_SIZE (128)
#endif
#define CIN_DEFAULT_FD  (cin_fd)

// API prototypes

// buffer management
CIN_PROTO int  cin_is_buffer_empty(void);   // check if input buffer is empty
CIN_PROTO int  cin_is_buffer_full(void);    // check if input buffer is full
CIN_PROTO void cin_clear_buffer(void);      // drop everying in buffer
CIN_PROTO int  cin_scan_buffer(char c);     // return if buffer has given character
// fd management
CIN_PROTO int  cin_is_fd_empty(int fd);     // quick determine if a fd is empty
CIN_PROTO int  cin_poll_fds(int fd1, int fd2, int ipc_fd, int ms); // poll fds for ms milliseconds
CIN_PROTO void cin_clear_fd(int fd);        // drop everying in fd
CIN_PROTO void cin_fetch_fd(int fd);        // read more data from fd to buffer
// virtual combination
CIN_PROTO int  cin_read(void);              // read one byte from cin (buffer then cin_fd)
CIN_PROTO int  cin_is_empty(void);          // Check if the buffer & fd are both empty.

// Threshold in bytes to consider input as a burst/paste rather than a single keystroke.
// Single keystrokes are 1-6 bytes (ASCII, Big5, UTF-8, arrow keys, mouse events).
#define CIN_BURST_THRESHOLD (8)

// virtual buffer for cin
static const int cin_fd = STDIN_FILENO;
static int cin_burst = 0;
static char cin_buf[CIN_BUFFER_SIZE];
static VBUF vcin, *cin = &vcin;

#ifdef CIN_DEBUG
static    int  cin_debugging = 0;
CIN_PROTO void cin_debug_print_content();
#endif

/**
 * cin_init(): initialize cin context.
 * This may be called multiple times for re-init.
 */
CIN_PROTO void
cin_init()
{
    vbuf_attach(cin, cin_buf, sizeof(cin_buf));
    cin_burst = 0;
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

    if (HAVE_FIONREAD) {
        int r = 0;
        if (ioctl(fd, FIONREAD, &r) == 0)
            return r <= 0;
        // Error (fd closed/invalid), treat as not empty to trigger read/error
        // handling.
        return 0;
    }
    return cin_poll_fds(fd, -1, -1, 0) == 0;
}

/**
 * cin_poll_fds(fd1, fd2, ipc_fd, ms): query the input status of given fds.
 * @param fd1: primary fd to check (cin)
 * @param fd2: secondary fd (skip if <= 0)
 * @param ipc_fd: IPC service fd (skip if <= 0)
 * @param ms: 0 for non-blocking, INFTIM(-1) for infinite, otherwise timeout milliseconds
 * @return: bitmask of fds having data/error, 0 for timeout, and -1 if error.
 */
#define CIN_POLL_FDS_MASK   (POLLIN|POLLERR|POLLHUP|POLLNVAL)
// mask to check the return value of cin_poll_fds
#define CIN_POLL_CINFD  (1)
#define CIN_POLL_FD2    (2)
#define CIN_POLL_IPCFD  (4)

#define CIN_IS_VALID_FD2(fd)    ((fd) > 0)

CIN_PROTO int
cin_poll_fds(int fd1, int fd2, int ipc_fd, int timeout)
{
    CINDBGLOG("cin_poll_fds(fd1=%d, fd2=%d, ipc=%d, timeout=%d)", fd1, fd2, ipc_fd, timeout);

    struct pollfd fds[3] = {
        { .fd = fd1, .events = POLLIN, .revents = 0 },
        { .fd = fd2, .events = POLLIN, .revents = 0 },
        { .fd = ipc_fd, .events = POLLIN, .revents = 0 },
    };
    int nfds = 1;
    int idx_fd2 = -1;
    int idx_ipc = -1;

    if (CIN_IS_VALID_FD2(fd2)) {
        idx_fd2 = nfds++;
        fds[idx_fd2].fd = fd2;
    }
    if (ipc_fd > 0) {
        idx_ipc = nfds++;
        fds[idx_ipc].fd = ipc_fd;
    }

#ifdef STAT_SYSSELECT
    STATINC(STAT_SYSSELECT);
#endif
    int r;
    while ( 0 > (r = poll(fds, nfds, timeout)) &&
            errno == EINTR);
    assert(r >= 0);
    if (r <= 0)
        return r;

    int mask = 0;
    if (fds[0].revents & CIN_POLL_FDS_MASK)
        mask |= CIN_POLL_CINFD;
    if (idx_fd2 >= 0 && (fds[idx_fd2].revents & CIN_POLL_FDS_MASK))
        mask |= CIN_POLL_FD2;
    if (idx_ipc >= 0 && (fds[idx_ipc].revents & CIN_POLL_FDS_MASK))
        mask |= CIN_POLL_IPCFD;

    return mask;
}

/**
 * cin_clear_buffer(): drop everying in buffer
 */
CIN_PROTO void
cin_clear_buffer()
{
    CINDBGLOG("cin_clear_buffer()");
    vbuf_clear(cin);
    cin_burst = 0;
}

/**
 * cin_clear_fd(int fd): quick discard all data in fd
 */
CIN_PROTO void
cin_clear_fd(int fd)
{
    CINDBGLOG("cin_clear_fd(%d)", fd);
    // method 1, use setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *)(&optval),
    //                          sizeof(optval));
    // method 2, fetch and discard
    VBUF vgarbage, *v = &vgarbage;
    char garbage[PIPE_BUF]; // any magic number works here

    if (cin_is_fd_empty(fd))
        return;

    if (fd == cin_fd) {
        int max_try = 64;
        while (!cin_is_fd_empty(fd) && max_try-- > 0) {
            vbuf_from_tty(cin);
            cin_clear_buffer();
        }
        return;
    }

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
    CINDBGLOG("cin_fetch_fd(%d)", fd);

#ifdef STAT_SYSREADSOCKET
	STATINC(STAT_SYSREADSOCKET);
#endif
    assert(fd == cin_fd);

    // Ideally we want to call vbuf_read() and handle the protocols later for
    // example `cin_read()` or `vkey_process_cin()`; however currently the
    // telnet, DBCS and convert filters all need to work on a flat buffer
    // so let's try to follow their process.
    // Note we can't loop on empty input because this request may come from a
    // prefetch command.

    if (0) {
        // Try to read data from stdin (without any conversion).
        vbuf_read(cin, fd, VBUF_RWSZ_MIN);
        return;
    }

    // Legacy way to read data compatible with `io.c`.
    ssize_t sz = 0;

    // sz<=0 from vbuf_from_tty may be EAGAIN/EINTR or filtered data (e.g.
    // telnet IAC or partial UTF-8). Only loop if fd still has data available
    // so we never block on an empty socket.
    do {
        sz = vbuf_from_tty(cin);
    } while (!vbuf_is_full(cin) && sz <= 0 && !cin_is_fd_empty(fd));

    cin_burst = (vbuf_size(cin) >= CIN_BURST_THRESHOLD);

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

CIN_PROTO int
cin_is_empty()
{
    if (!cin_is_buffer_empty())
        return 0;
    if (!cin_burst)
        return 1;
    if (cin_is_fd_empty(cin_fd)) {
        cin_burst = 0;
        return 1;
    }
    return 0;
}

#ifdef CIN_DEBUG
ssize_t debug_print_input_buffer(void *s, ssize_t len);

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

static VKEY_CTX vkctx;

VKEY_CTX *
vkey_get_context(void)
{
    return &vkctx;
}

#define VKEY_HAS_PEEK()     (vkctx.peek_ch != KEY_INCOMPLETE)
#define VKEY_GET_PEEK()     (vkctx.peek_ch)
#define VKEY_SET_PEEK(c)    (vkctx.peek_ch = (c))
#define VKEY_RESET_PEEK()   (VKEY_SET_PEEK(KEY_INCOMPLETE))

/* This may be called multiple times when we do vkey_purge. */
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
 * vkey_is_full(): return if key buffer is full.
 */
VKEY_PROTO int
vkey_is_full()
{
    VKEYDBGLOG("vkey_is_full()");
    return cin_is_buffer_full();
}

/**
 * vkey_process(): process available data from cin into peek_ch.
 * @return: 1 if a complete key is decoded in peek_ch, 0 otherwise.
 */
static VKEY_PROTO int
vkey_process(void)
{
    VKEYDBGLOG("vkey_process()");

    while (!VKEY_HAS_PEEK() && !cin_is_empty()) {
        int ch = cin_read();
        if (ch == EOF)
            break;
        int r = vkey_decode(cin, ch);
        if (r != KEY_INCOMPLETE)
            VKEY_SET_PEEK(r);
    }
    return VKEY_HAS_PEEK();
}

/**
 * vkey_is_ready(): determine if input buffer is complete for a key input
 * @return: 1 for something to read/error, 0 for incomplete
 */
VKEY_PROTO int
vkey_is_ready()
{
    VKEYDBGLOG("vkey_is_ready()");
    if (!VKEY_HAS_PEEK() && cin_is_buffer_empty() && !cin_is_fd_empty(CIN_DEFAULT_FD))
        cin_fetch_fd(CIN_DEFAULT_FD);
    vkey_process();
    return VKEY_HAS_PEEK();
}

/*
 * vkey_poll_impl(timeout): vkey_poll() honoring the events wanted by the
 * current caller (vkctx.ev_want, set by vkey_ex/vkey_poll_ex).
 */
static int
vkey_poll_impl(int timeout)
{
    VKEYDBGLOG("vkey_poll(%d)", timeout);

    while (1) {
        if (vkey_process())
            return 1;
        if (vkey_has_event())
            return 1;

        vkey_sync_ipc();
        if (ipc_has_pending()) {
            if (vkey_process_ipc() > 0)
                doupdate();
            if (vkey_has_event())
                return 1;
        }

        if (timeout == 0) {
            int mask = cin_poll_fds(CIN_DEFAULT_FD, vkctx.attached_fd, vkctx.ipc_fd, 0);
            if (mask & CIN_POLL_FD2)
                return 1;
            if (mask & CIN_POLL_IPCFD) {
                if (vkey_process_ipc() > 0)
                    doupdate();
                if (vkey_has_event())
                    return 1;
            }
            return 0;
        }

        // Going to wait user input, and let's update the screen to make sure
        // every pending update is really shown to the user.
        // Note: we call doupdate() directly instead of refresh() because
        // refresh() checks vkey_is_typeahead(), which is already guaranteed
        // to be false here (!VKEY_HAS_PEEK() && cin_is_empty()).
        doupdate();

        /* Auto-reconnect is enabled only after login (ipc_init). */
        int want_reconnect = (vkctx.ipc_fd <= 0 && ipc_reconnect_enabled());
        if (want_reconnect) {
            ipc_check_reconnect();
            want_reconnect = (vkey_sync_ipc() <= 0);
        }

        int poll_timeout = timeout;
        int idle_wakeup = 0;
        if (poll_timeout == INFTIM && want_reconnect && !CIN_IS_VALID_FD2(vkctx.attached_fd)) {
            /* When disconnected from IPC service, wake up every 3s during idle
             * to check backoff timer and reconnect automatically */
            poll_timeout = 3000;
            idle_wakeup = 1;
        }

        int r = cin_poll_fds(CIN_DEFAULT_FD, vkctx.attached_fd, vkctx.ipc_fd, poll_timeout);
        if (r <= 0) {
            if (idle_wakeup) {
                /* Reconnect is retried at the top of the loop. */
                continue;
            }
            if (timeout > 0)
                syncnow();
            return 0;
        }
        if (r & CIN_POLL_CINFD) {
            cin_fetch_fd(CIN_DEFAULT_FD);
            continue;
        }
        if (r & CIN_POLL_IPCFD) {
            syncnow();
            /* Handlers consume the packets themselves (e.g. waterball shown,
             * live comment queued); only a posted event that the current
             * caller asked for interrupts the wait. */
            if (vkey_process_ipc() > 0)
                doupdate();
            if (vkey_has_event())
                return 1;
            continue;
        }
        if (r & CIN_POLL_FD2) {
            syncnow();
            return 1;
        }
    }
}

/**
 * vkey_poll(timeout): poll for timeout milliseconds and return if key is ready
 * @param timeout: 0 for non-block, INFTIM(-1) for infinite, otherwise milliseconds
 * @return: 1 for ready, otherwise 0
 */
VKEY_PROTO int
vkey_poll(int timeout)
{
    return vkey_poll_ex(timeout, 0);
}

/**
 * vkey_poll_ex(timeout, events): like vkey_poll(), but also returns 1 when
 * one of the given VKEY_EV_* events is pending (read it with vkey_ex()).
 */
VKEY_PROTO int
vkey_poll_ex(int timeout, int events)
{
    int saved = vkctx.ev_want;
    vkctx.ev_want = events;
    int r = vkey_poll_impl(timeout);
    vkctx.ev_want = saved;
    return r;
}

/**
 * vkey_is_typeahead(): quick check if input buffer has data arrived (maybe not ready yet)
 * @return: 0 for empty, otherwise 1
 */
VKEY_PROTO int
vkey_is_typeahead()
{
    VKEYDBGLOG("vkey_is_typeahead(): %d||%d",
            VKEY_HAS_PEEK(), !cin_is_empty());

    return VKEY_HAS_PEEK() || !cin_is_empty();
}

/**
 * vkey_prefetch(timeout): try to prefecth as more data as possible from fd to buffer for up to timeout milliseconds
 */
VKEY_PROTO int
vkey_prefetch(int timeout)
{
    VKEYDBGLOG("vkey_prefetch(%d)", timeout);

    // always only prefetch cin_fd
    if (cin_poll_fds(CIN_DEFAULT_FD, -1, -1, timeout) == 0)  // timeout
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
vk_byte()
{
    VKEYDBGLOG("vk_byte()");

    while (1) {
        /* impl: keep the caller's ev_want (see vkey_ex). */
        if (!vkey_poll_impl(INFTIM))
            return KEY_UNKNOWN;
        if (VKEY_HAS_PEEK()) {
            int c = VKEY_GET_PEEK();
            VKEY_RESET_PEEK();
            return c;
        }
        int ev = vkey_take_event();
        if (ev)
            return ev;
        if (CIN_IS_VALID_FD2(vkctx.attached_fd))
            return I_OTHERDATA;
    }
}

/**
 * vk_ungetc(): push back a key into peek buffer
 */
VKEY_PROTO void
vk_ungetc(int c)
{
    VKEYDBGLOG("vk_ungetc(0x%02X)", c);
    VKEY_SET_PEEK(c);
}

/**
 * vkey_purge(): clear and discard all data in current input buffer
 */
VKEY_PROTO void
vkey_purge()
{
    VKEYDBGLOG("vkey_purge()");

    // ok, now let's try our best to purge all remaining data
    // (cin_clear_fd uses vbuf_from_tty for cin_fd so telnet protocol is handled)
    cin_clear_fd(CIN_DEFAULT_FD);

    // XXX in current usage,  we don't expect vkey_purge to clean
    // remote FDs.
#if 0
    if (CIN_IS_VALID_FD2(vkctx.attached_fd))
        cin_clear_fd(vkctx.attached_fd);
#endif

    cin_clear_buffer();
    VKEY_RESET_PEEK();

    vkey_init();
}

// vim:ts=4:sw=4:et
