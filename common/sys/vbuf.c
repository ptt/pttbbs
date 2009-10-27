// $Id$
//
// vbuf.c
// piaip's simple virtual (ring) buffer
//
// Author: Hung-Te Lin (piaip)
// Create: Sat Oct 24 22:44:00 CST 2009
// An implementation from scratch, with the API names inspired by STL and vrb
// ---------------------------------------------------------------------------
// Copyright (c) 2009 Hung-Te Lin <piaip@csie.org>
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
// ---------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <assert.h>

// for read/write/send/recv/readv/writev
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

// #include "vbuf.h"
#include "cmsys.h"

// #define VBUFPROTO  inline
#define VBUFPROTO

// These APIs are defined as macro in vbuf.h(cmsys.h) for faster access
#if 0

VBUFPROTO int
vbuf_is_empty(VBUF *v)
{
    return v->head == v->tail;
}

VBUFPROTO size_t
vbuf_capacity(VBUF *v)
{
    return v->capacity;
}

VBUFPROTO size_t
vbuf_size(VBUF *v)
{
    return (v->tail >= v->head) ? (v->tail - v->head) :
        (v->buf_end - v->head + v->tail - v->buf);
}

VBUFPROTO size_t
vbuf_space(VBUF *v)
{
    return v->capacity - vbuf_size(v);
}

VBUFPROTO int
vbuf_peek(VBUF *v)
{
    if (vbuf_is_empty(v))
        return EOF;
    return (unsigned char)(*v->head);
}

#endif

VBUFPROTO void
vbuf_new(VBUF *v, size_t szbuf)
{
    assert(szbuf > 1);
    bzero(v, sizeof(*v)); // memset(v, 0, sizeof(*v));
    v->buf = (char*)malloc(szbuf);
    assert(v->buf);
    v->buf_end = v->buf + szbuf;
    v->capacity = szbuf-1;  // ring buffer needs one extra space
    v->head = v->tail = v->buf;
}

VBUFPROTO void
vbuf_delete(VBUF *v)
{
    free(v->buf);
    bzero(v, sizeof(*v)); // memset(v, 0, sizeof(*v));
}

VBUFPROTO void
vbuf_attach(VBUF *v, char *buf, size_t szbuf)
{
    assert(szbuf > 1);
    v->head = v->tail = v->buf = buf;
    v->buf_end = v->buf + szbuf;
    v->capacity = szbuf-1;
}

VBUFPROTO void
vbuf_detach(VBUF *v)
{
    bzero(v, sizeof(*v)); // memset(v, 0, sizeof(*v));
}

VBUFPROTO void
vbuf_clear(VBUF *v)
{
    v->head = v->tail = v->buf;
}

VBUFPROTO int
vbuf_peekat(VBUF *v, int i)
{
    const char *s = v->head + i;
    if (vbuf_is_empty(v) || i >= vbuf_capacity(v))
        return EOF;
    if (s < v->tail)
        return (unsigned char)*s;
    if (s >= v->buf_end) 
        s -= v->buf_end - v->buf;
    if (s < v->tail)
        return (unsigned char)*s;
    return EOF;
}

VBUFPROTO int
vbuf_pop(VBUF *v)
{
    int c = vbuf_peek(v);
    if (c >= 0 && ++v->head == v->buf_end)
        v->head = v->buf;
    return c;
}

VBUFPROTO void
vbuf_popn(VBUF *v, size_t n)
{
    v->head += n;
    if (v->head < v->buf_end) 
        return;

    v->head -= (v->buf_end - v->buf);
    if (v->head > v->tail)
        v->head = v->tail;
}

VBUFPROTO int
vbuf_add(VBUF *v, char c)
{
    if (vbuf_is_full(v))
        return 0;

    *v->tail++ = c;
    if (v->tail == v->buf_end)
        v->tail =  v->buf;

    return 1;
}

VBUFPROTO int  
vbuf_strchr(VBUF *v, char c)
{
    const char *s = v->head, *d = v->tail;

    if (vbuf_is_empty(v))
        return EOF;

    if (d < s) 
        d = v->buf_end;

    while (s < d)
        if (*s++ == c)
            return s - v->head -1;

    if (v->tail > v->head)
        return EOF;

    s = v->buf; d = v->tail;

    while (s < d)
        if (*s++ == c)
            return (v->buf_end - v->head) + s - v->buf -1;

    return EOF;
}

/* string operation */

VBUFPROTO char * 
vbuf_getstr (VBUF *v, char *s, size_t sz)
{
    char *sbase = s;
    assert(sz > 0);
    if (vbuf_is_empty(v))
        return NULL;

    while (sz-- > 0 && !vbuf_is_empty(v))
    {
        if ('\0' == (*s++ = vbuf_pop(v)))
            return sbase;
    }

    // need to pad NUL.
    s[ sz == 0 ? 0 : -1] = '\0';
    return sbase;
}

VBUFPROTO int 
vbuf_putstr (VBUF *v, const char *s)
{
    int len = strlen(s) + 1;
    if (vbuf_space(v) < len)
        return 0;
    vbuf_putblk(v, s, len);
    return 1;
}

VBUFPROTO static void 
vbuf_reverse(char *begin, char *end)
{
    char c;

    assert(begin <= end);
    while (begin < end)
    {
        c = *--end;
        *end = *begin;
        *begin++ = c;
    }
}

VBUFPROTO char *
vbuf_cstr  (VBUF *v)
{
    size_t sz;
    if (vbuf_is_empty(v))
        return NULL;

    // if the buffer is cstr safe, simply return
    if (v->tail > v->head)
    {
        *v->tail = 0;
        return v->head;
    }

    // wrapped ring buffer. now reverse 3 times to merge:
    // [buf head tail buf_end]
    sz = vbuf_size(v);
    vbuf_reverse(v->buf, v->tail);
    vbuf_reverse(v->head, v->buf_end);
    memmove(v->tail, v->head, v->buf_end - v->head);
    v->head = v->buf;
    v->tail = v->buf + sz;
    v->buf[sz] = 0;
    vbuf_reverse(v->head, v->tail);
    return v->buf;
}

// NOTE: VBUF_*_SZ may return a size larger than capacity, so you must check
// buffer availablity before using these macro.
#define VBUF_TAIL_SZ(v) ((v->tail >= v->head) ? v->tail - v->head : v->buf_end - v->head)
#define VBUF_HEAD_SZ(v) ((v->head >  v->tail) ? v->head - v->tail : v->buf_end - v->tail)

// get data from vbuf
VBUFPROTO int
vbuf_getblk(VBUF *v, void *p, size_t sz)
{
    size_t rw, i;
    char *s = p;
    if (!sz || vbuf_size(v) < sz)
        return 0;

    // two phase
    for (i = 0; sz && i < 2; i++)
    {
        rw = VBUF_TAIL_SZ(v);
        assert(rw >= 0 && rw <= v->capacity+1);
        if (rw > sz) rw = sz;
        memcpy(s, v->head, rw);
        v->head += rw; s += rw; sz -= rw;
        if (v->head == v->buf_end)
            v->head =  v->buf;
    }
    assert(sz == 0);
    return 1;
}

// put data into vbuf
VBUFPROTO int
vbuf_putblk(VBUF *v, const void *p, size_t sz)
{
    size_t rw, i;
    const char *s = p;
    if (!sz || vbuf_space(v) < sz)
        return 0;

    // two phase
    for (i = 0; sz && i < 2; i++)
    {
        rw = VBUF_HEAD_SZ(v);
        assert(rw >= 0 && rw <= v->capacity+1);
        if (rw > sz) rw = sz;
        memcpy(v->tail, s, rw);
        v->tail += rw; s += rw; sz -= rw;
        if (v->tail == v->buf_end)
            v->tail =  v->buf;
    }
    assert(sz == 0);
    return 1;
}

/* read/write callbacks */
// XXX warning: the return value of these callbacks are a little differnet:
// 0 means 'can continue (EAGAIN)', and -1 means EOF or error.

static ssize_t 
vbuf_rw_write(struct iovec iov[2], void *ctx)
{
    int fd = *(int*)ctx;
    ssize_t ret;
    while ( (ret = writev(fd, iov, iov[1].iov_len ? 2 : 1)) < 0 &&
            (errno == EINTR));
    if (ret <= 0)
        ret = (errno == EAGAIN) ? 0 : -1;
    return ret;
}

static ssize_t
vbuf_rw_read(struct iovec iov[2], void *ctx)
{
    int fd = *(int*)ctx;
    ssize_t ret;
    while ( (ret = readv(fd, iov, iov[1].iov_len ? 2 : 1)) < 0 &&
            (errno == EINTR));
    if (ret <= 0)
        ret = (errno == EAGAIN) ? 0 : -1;
    return ret;
}

static ssize_t 
vbuf_rw_send(struct iovec iov[2], void *ctx)
{
    int *fdflag = (int*)ctx;
    ssize_t ret;
    while ( (ret = send(fdflag[0], iov[0].iov_base, iov[0].iov_len, fdflag[1])) < 0 &&
            (errno == EINTR));
    // TODO also send for iov[1]
    if (ret <= 0)
        ret = (errno == EAGAIN) ? 0 : -1;
    return ret;
}

static ssize_t
vbuf_rw_recv(struct iovec iov[2], void *ctx)
{
    int *fdflag = (int*)ctx;
    ssize_t ret;
    while ( (ret = recv(fdflag[0], iov[0].iov_base, iov[0].iov_len, fdflag[1])) < 0 &&
            (errno == EINTR));
    // TODO also try reading from iov[1] with MSG_DONTWAIT
    if (ret <= 0)
        ret = (errno == EAGAIN) ? 0 : -1;
    return ret;
}

/* read/write herlpers */

ssize_t 
vbuf_read (VBUF *v, int fd, ssize_t sz)
{
    return vbuf_general_read(v, sz, &fd, vbuf_rw_read);
}

ssize_t 
vbuf_write(VBUF *v, int fd, ssize_t sz)
{
    return vbuf_general_write(v, sz, &fd, vbuf_rw_write);
}

ssize_t 
vbuf_recv (VBUF *v, int fd, ssize_t sz, int flags)
{
    int ctx[2] = {fd, flags};
    return vbuf_general_read(v, sz, &ctx, vbuf_rw_recv);
}

ssize_t 
vbuf_send (VBUF *v, int fd, ssize_t sz, int flags)
{
    int ctx[2] = {fd, flags};
    return vbuf_general_write(v, sz, &ctx, vbuf_rw_send);
}

/* read/write primitives */

// write from vbuf to writer
ssize_t
vbuf_general_write(VBUF *v, ssize_t sz, void *ctx,
                  ssize_t (writer)(struct iovec[2], void *ctx))
{
    ssize_t rw, copied = 0;
    int is_min = 0;
    struct iovec iov[2] = { { NULL } };

    if (sz == VBUF_RWSZ_ALL)
    {
        sz = vbuf_size(v);
    }
    else if (sz == VBUF_RWSZ_MIN)
    {
        sz = vbuf_size(v);
        is_min = 1;
    }

    if (sz < 1 || vbuf_size(v) < sz)
        return 0;
    
    do {
        rw = VBUF_TAIL_SZ(v);
        if (rw > sz) rw = sz;

        iov[0].iov_base= v->head;
        iov[0].iov_len = rw;
        iov[1].iov_base= v->buf;
        iov[1].iov_len = sz-rw;

        rw = writer(iov, ctx);
        if (rw < 0)
            return copied > 0 ? copied : -1;

        assert(rw <= sz);
        v->head += rw; sz -= rw;
        if (v->head >= v->buf_end)
            v->head -= v->buf_end - v->buf;

    } while (sz > 0 && !is_min);

    return copied;
}

// read from reader to vbuf
ssize_t 
vbuf_general_read(VBUF *v, ssize_t sz, void *ctx,
                   ssize_t (reader)(struct iovec[2], void *ctx))
{
    ssize_t rw, copied = 0;
    int is_min = 0;
    struct iovec iov[2] = { { NULL } };

    if (sz == VBUF_RWSZ_ALL)
    {
        sz = vbuf_space(v);
    }
    else if (sz == VBUF_RWSZ_MIN)
    {
        sz = vbuf_space(v);
        is_min = 1;
    }

    if (sz < 1 || vbuf_space(v) < sz)
        return 0;
    
    do {
        rw = VBUF_HEAD_SZ(v);
        if (rw > sz) rw = sz;

        iov[0].iov_base= v->tail;
        iov[0].iov_len = rw;
        iov[1].iov_base= v->buf;
        iov[1].iov_len = sz-rw;

        rw = reader(iov, ctx);
        if (rw < 0)
            return copied > 0 ? copied : -1;

        assert(rw <= sz);
        v->tail += rw; sz -= rw;
        if (v->tail >= v->buf_end)
            v->tail -= v->buf_end - v->buf;

    } while (sz > 0 && !is_min);

    return copied;
}

// testing sample

#ifdef _VBUF_TEST_MAIN
void vbuf_dbg_rpt(VBUF *v)
{
    printf("v: [ cap: %u, size: %2lu, empty: %s, ptr=(%p,h=%p,t=%p,%p)]\n", 
            (unsigned int)vbuf_capacity(v), vbuf_size(v), 
            vbuf_is_empty(v) ? "YES" : "NO", 
            v->buf, v->head, v->tail, v->buf_end);
    assert(v->buf_end == v->buf + v->capacity +1);
}

void vbuf_dbg_fragmentize(VBUF *v)
{
    v->head = v->tail = v->buf + v->capacity/2;
}

// TODO add API unit tests
int main()
{
    int i, r;
    const int szbuf = 40, szfrag = szbuf / 4 * 3;
    const char src[szbuf *2] = "hello, world";
    const char *fragstring = "1234567890ABCDEFGHIJabcdefhijk";
    const char *s;
    char dest[szbuf*2] = "";
    VBUF vbuf, *v = &vbuf;
    vbuf_new(v, szbuf);

    // check basic structure
    assert(v->buf && v->capacity == szbuf -1);
    assert(v->head == v->tail && v->tail == v->buf);
    assert(v->buf_end == v->buf + v->capacity + 1);

    // fragmentize it!
    vbuf_dbg_fragmentize(v);
    assert(vbuf_is_empty(v) && !vbuf_is_full(v));
    vbuf_putblk(v, src, szfrag);

    // check macro API
    assert(!vbuf_is_empty(v));
    assert(!vbuf_is_full(v));
    assert(vbuf_capacity(v) == szbuf-1);
    assert(vbuf_size(v) == szfrag);
    assert(vbuf_space(v) == vbuf_capacity(v) - szfrag);
    assert(vbuf_peek(v) == src[0]);

    // try filling buffer with putblk/getblk
    vbuf_dbg_fragmentize(v);
    for (i = 0; i < 10; i++)
    {
        r = vbuf_putblk(v, src, szbuf / 10);
        vbuf_dbg_rpt(v);
        assert( i == 9 ? !r : r);
        assert(!vbuf_is_full(v));
    }
    r = vbuf_putblk(v, src, vbuf_capacity(v) - vbuf_size(v));
    assert(r && vbuf_is_full(v) && !vbuf_is_empty(v));

    for (i = 0; i < 10; i++)
    {
        r = vbuf_getblk(v, dest, szbuf / 10);
        dest[szbuf/10] = 0;
        printf("%2d. [got: %s] ", i+1, dest);
        vbuf_dbg_rpt(v);
        assert(i == 9 ? !r : r);
        assert(!vbuf_is_full(v) && !vbuf_is_empty(v));
    }
    r = vbuf_getblk(v, dest, vbuf_size(v));
    assert(r && vbuf_is_empty(v) && !vbuf_is_full(v));

    // try unframgented
    vbuf_clear(v);
    r = vbuf_putblk(v, src, vbuf_capacity(v));
    assert(r && vbuf_is_full(v));
    r = vbuf_putblk(v, src, 1);
    assert(!r);
    r = vbuf_getblk(v, dest, szbuf-1);
    assert(r && vbuf_is_empty(v));
    r = vbuf_getblk(v, dest, 1);
    assert(!r && vbuf_is_empty(v));
    r = vbuf_putblk(v, src, szbuf);
    assert(!r && vbuf_is_empty(v));

    // string operation
    vbuf_clear(v);
    vbuf_putstr(v, "str test(1)");
    vbuf_putstr(v, "str test(2)");
    vbuf_putstr(v, "str test(3)");
    for (i = 0; i < 4; i++)
    {
        s = vbuf_getstr(v, dest, sizeof(dest));
        printf("put/getstr(%d): %s\n", i+1,
                s ? s : "(NULL)");
        assert(i < 3 ? s != NULL : s == NULL);
    }

    // cstr test
    vbuf_clear(v);
    vbuf_putstr(v, fragstring);
    vbuf_dbg_rpt(v);
    s = vbuf_cstr(v);
    printf("cstr test(simple): %s\n", s);
    assert(strcmp(s, fragstring)  == 0);
    vbuf_dbg_fragmentize(v);
    vbuf_putstr(v, fragstring);
    vbuf_dbg_rpt(v);
    s = vbuf_cstr(v);
    printf("cstr test(unwrap): %s\n", s);
    assert(strcmp(s, fragstring)  == 0);

    vbuf_dbg_fragmentize(v);
    vbuf_putblk(v, "*** peek test OK\n", sizeof("*** peek test OK\n"));
    while (EOF != (i = vbuf_pop(v)))
        putchar(i);

    // read/write test
    vbuf_dbg_fragmentize(v);
    printf("give me some input for finding location of 't': "); fflush(stdout);
    vbuf_read(v, 0, VBUF_RWSZ_MIN);
    printf("index of 't' = %d\n", vbuf_strchr(v, 't'));
    vbuf_dbg_rpt(v);
    printf("give me 4 chars: "); fflush(stdout);
    vbuf_read(v, 0, 4);
    vbuf_dbg_rpt(v);
    printf("\n flushing vbuf: ["); fflush(stdout);
    vbuf_write(v, 1, VBUF_RWSZ_ALL);
    printf("]\n");

    return 0;
}
#endif 

// vim:ts=8:sw=4:et
