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
    return (v->head >= v->tail) ? (v->head - v->tail) :
        (v->buf_end - v->tail + v->head - v->buf);
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
    return (unsigned char)(*v->tail);
}

#endif

VBUFPROTO void
vbuf_new(VBUF *v, size_t szbuf)
{
    assert(szbuf > 1);
    memset(v, 0, sizeof(*v));
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
    memset(v, 0, sizeof(*v));
}

VBUFPROTO void
vbuf_attach(VBUF *v, char *buf, size_t szbuf)
{
    assert(szbuf > 1);
    v->head = v->tail = v->buf = buf;
    v->buf_end = v->buf + v->capacity;
    v->capacity = szbuf-1;
}

VBUFPROTO void
vbuf_detach(VBUF *v)
{
    memset(v, 0, sizeof(VBUF));
}

VBUFPROTO void
vbuf_clear(VBUF *v)
{
    v->head = v->tail = v->buf;
}

VBUFPROTO int
vbuf_peekat(VBUF *v, int i)
{
    const char *s = v->tail + i;
    if (vbuf_is_empty(v) || i >= vbuf_capacity(v))
	return EOF;
    if (s < v->head)
	return (unsigned char)*s;
    if (s >= v->buf_end) 
	s -= v->buf_end - v->buf;
    if (s < v->head)
	return (unsigned char)*s;
    return EOF;
}

VBUFPROTO int
vbuf_pop(VBUF *v)
{
    int c = vbuf_peek(v);
    if (c >= 0 && ++v->tail == v->buf_end)
	v->tail = v->buf;
    return c;
}

VBUFPROTO int
vbuf_add(VBUF *v, char c)
{
    if (vbuf_is_full(v))
	return 0;
    *v->head++ = c;
    if (v->head == v->buf_end)
	v->head =  v->buf;
    return 1;
}

VBUFPROTO int  
vbuf_strchr(VBUF *v, char c)
{
    const char *s = v->tail, *d = v->head;

    if (vbuf_is_empty(v))
	return EOF;

    if (d < s) 
	d = v->buf_end;

    while (s < d)
	if (*s++ == c)
	    return s - v->tail -1;

    if (v->head > v->tail)
	return EOF;

    s = v->buf; d = v->head;

    while (s < d)
	if (*s++ == c)
	    return (v->buf_end - v->tail) + s - v->buf -1;

    return EOF;
}

// NOTE: VBUF_*_SZ may return a size larger than capacity, so you must check
// buffer availablity before using these macro.
#define VBUF_TAIL_SZ(v) ((v->head >= v->tail) ? v->head - v->tail : v->buf_end - v->tail)
#define VBUF_HEAD_SZ(v) ((v->tail >  v->head) ? v->tail - v->head : v->buf_end - v->head)

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
        memcpy(s, v->tail, rw);
        v->tail += rw; s += rw; sz -= rw;
        if (v->tail == v->buf_end)
            v->tail =  v->buf;
    }
    assert(sz == 0);
    return 1;
}

static void 
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
    if (v->head > v->tail)
    {
	*v->head = 0;
	return v->tail;
    }

    // wrapped ring buffer. now reverse 3 times to merge:
    // [buf head tail buf_end]
    sz = vbuf_size(v);
    vbuf_reverse(v->buf, v->head);
    vbuf_reverse(v->tail, v->buf_end);
    memmove(v->head, v->tail, v->buf_end - v->tail);
    v->tail = v->buf;
    v->head = v->buf + sz;
    v->buf[sz] = 0;
    vbuf_reverse(v->tail, v->head);
    return v->buf;
}

char * 
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

int 
vbuf_putstr (VBUF *v, char *s)
{
    int len = strlen(s) + 1;
    if (vbuf_space(v) < len)
	return 0;
    vbuf_putblk(v, s, len);
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
        memcpy(v->head, s, rw);
        v->head += rw; s += rw; sz -= rw;
        if (v->head == v->buf_end)
            v->head =  v->buf;
    }
    assert(sz == 0);
    return 1;
}

/* read/write callbacks */

static ssize_t 
vbuf_rw_write(struct iovec iov[2], void *ctx)
{
    int fd = *(int*)ctx;
    ssize_t ret;
    while ( (ret = writev(fd, iov, iov[1].iov_len ? 2 : 1)) < 0 &&
	    (errno == EINTR));
    if (ret < 0 && errno == EAGAIN)
	ret = 0;
    return ret;
}

static ssize_t
vbuf_rw_read(struct iovec iov[2], void *ctx)
{
    int fd = *(int*)ctx;
    ssize_t ret;
    while ( (ret = readv(fd, iov, iov[1].iov_len ? 2 : 1)) < 0 &&
	    (errno == EINTR));
    if (ret < 0 && errno == EAGAIN)
	ret = 0;
    return ret;
}

static ssize_t 
vbuf_rw_send(struct iovec iov[2], void *ctx)
{
    int *fdflag = (int*)ctx;
    ssize_t ret;
    while ( (ret = send(fdflag[0], iov[0].iov_base, iov[0].iov_len, fdflag[1])) < 0 &&
	    (errno == EINTR));
    if (ret < 0 && errno == EAGAIN)
	ret = 0;
    return ret;
}

static ssize_t
vbuf_rw_recv(struct iovec iov[2], void *ctx)
{
    int *fdflag = (int*)ctx;
    ssize_t ret;
    while ( (ret = recv(fdflag[0], iov[0].iov_base, iov[0].iov_len, fdflag[1])) < 0 &&
	    (errno == EINTR));
    if (ret < 0 && errno == EAGAIN)
	ret = 0;
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

	iov[0].iov_base= v->tail;
	iov[0].iov_len = rw;
	iov[1].iov_base= v->buf;
	iov[1].iov_len = sz-rw;

        rw = writer(iov, ctx);
        if (rw < 0)
            return copied > 0 ? copied : -1;

	assert(rw <= sz);
        v->tail += rw; sz -= rw;
        if (v->tail >= v->buf_end)
            v->tail -= v->buf_end - v->buf;

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

	iov[0].iov_base= v->head;
	iov[0].iov_len = rw;
	iov[1].iov_base= v->buf;
	iov[1].iov_len = sz-rw;

        rw = reader(iov, ctx);
        if (rw < 0)
            return copied > 0 ? copied : -1;

	assert(rw <= sz);
        v->head += rw; sz -= rw;
        if (v->head >= v->buf_end)
            v->head -= v->buf_end - v->buf;

    } while (sz > 0 && !is_min);

    return copied;
}

// testing sample

#ifdef _VBUF_TEST_MAIN
void vbuf_rpt(VBUF *v)
{
        printf("v capacity: %u, size: %lu, empty: %s, ptr=(%p,h=%p,t=%p,%p)\n", 
            (unsigned int)vbuf_capacity(v), vbuf_size(v), 
	    vbuf_is_empty(v) ? "YES" : "NO", 
	    v->buf, v->head, v->tail, v->buf_end);
}

int main()
{
    int i;
    VBUF vbuf, *v = &vbuf;

    printf("start!\n");

    vbuf_new(v, 50);
    for (i = 0; i < 10; i++)
    {
        vbuf_putblk(v, "blah", sizeof("blah"));
	vbuf_rpt(v);
    }
    for (i = 0; i < 10; i++)
    {
        char buf[64] = "";
        vbuf_getblk(v, buf, 5);
	printf("[got: %s] ", buf);
	vbuf_rpt(v);
    }

    for (i = 0; i < 10; i++)
    {
        char buf[64] = "";
        vbuf_putblk(v, "blah", sizeof("blah"));
        vbuf_getblk(v, buf, 5);
	printf("[got: %s] ", buf);
	vbuf_rpt(v);
    }

    // vbuf_clear(v);
    vbuf_rpt(v);
    printf("give me some input: "); fflush(stdout);
    vbuf_read(v, 0, VBUF_RWSZ_MIN);
    printf("index of 't' = %d\n", vbuf_strchr(v, 't'));
    vbuf_rpt(v);
    printf("give me 4 chars: "); fflush(stdout);
    vbuf_read(v, 0, 4);
    vbuf_rpt(v);
    printf("\n flushing vbuf: ["); fflush(stdout);
    vbuf_write(v, 1, VBUF_RWSZ_ALL);
    printf("]\n");
    vbuf_putblk(v, "peek test\n", sizeof("peek test\n"));
    while (EOF != (i = vbuf_pop(v)))
	putchar(i);

    vbuf_clear(v);
    vbuf_putstr(v, "string test(1)");
    vbuf_putstr(v, "string test(2)");
    vbuf_putstr(v, "string test(3)");

    for (i = 0; i < 4; i++)
    {
	char xbuf[256];
	char *s = vbuf_getstr(v, xbuf, sizeof(xbuf));
	printf("put/getstr(%d): %s\n", i+1,
		s ? s : "(NULL)");
    }
    vbuf_clear(v);
    vbuf_putstr(v, "1234567890ABCDEFGHIJK");
    vbuf_rpt(v);
    printf("cstr test(simple): %s\n", vbuf_cstr(v));
    vbuf_clear(v);
    for (i = 0; i < 4; i++) {
	char xbuf[256];
	vbuf_putstr(v, "1234567890");
	vbuf_getstr(v, xbuf, sizeof(xbuf));
    }
    vbuf_putstr(v, "1234567890ABCDEFGHIJK");
    vbuf_rpt(v);
    printf("cstr test(unwrap): %s\n", vbuf_cstr(v));

    getchar();
    return 0;
}
#endif 
