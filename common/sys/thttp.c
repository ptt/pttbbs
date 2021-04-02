#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "cmsys.h"
#include "buffer.h"
#include "thttp.h"

static int _read(THTTP *t, void *buffer, int size) {
    return read(t->fd, buffer, size);
}

static int _write(THTTP *t, const void *buffer, int size) {
    int wrote = towrite(t->fd, buffer, size);
    if (wrote < 0)
        t->failed = 1;
    return wrote;
}

static int write_string(THTTP *t, const char *str) {
    return t->write(t, str, strlen(str));
}

static int write_int(THTTP *t, int val) {
    char conv[16];
    snprintf(conv, sizeof(conv), "%d", val);
    return write_string(t, conv);
}

static int request(THTTP *t, const char *meth, const char *uri,
                   const char *headers, ...) {
    write_string(t, meth);
    write_string(t, " ");
    write_string(t, uri);
    write_string(t, " HTTP/1.0\r\n");

    va_list va;
    va_start(va, headers);
    int i;
    for (i = 0; headers[i]; i++) {
        const char *key = va_arg(va, const char *);
        write_string(t, key);
        write_string(t, ": ");

        switch (headers[i]) {
            case 's':
            {
                const char *val = va_arg(va, const char *);
                write_string(t, val);
                break;
            }
            case 'i':
            {
                int val = va_arg(va, int);
                write_int(t, val);
                break;
            }
            default:
                /* WTF */
                va_end(va);
                return -1;
        }

        write_string(t, "\r\n");
    }
    va_end(va);

    write_string(t, "\r\n");
    return 0;
}

static int read_to_buffer(THTTP *t, BUFFER *b) {
    int total = 0;
    int last;
    while ((last = buffer_read_from_func(
                b, (int (*)(void *, void *, int))t->read, t)) > 0)
        total += last;
    if (last < 0)
        return last;
    return total;
}

static int read_response(THTTP *t) {
    BUFFER *b = &t->bresp;

    if (read_to_buffer(t, b) < 0)
        return -1;

    // to be safe
    buffer_append(b, "\0", 1);

    // read response code
    char *start = (char *)buffer_get(b, 0);
    if (sscanf(start, "%*s%d", &t->code) != 1)
        return -1;

    char *headers_start;
    char *content_start;
    if (t->code != 200) {
        // although there can be content when error,
        // we still ignore it.
        t->content_at = -1;
        t->content_length = -1;
    } else {
	headers_start = strstr(start, "\r\n");
	content_start = strstr(start, "\r\n\r\n");
	if (headers_start == NULL || content_start == NULL)
	    return -1;
	t->headers_at = headers_start + 2 - start;
        t->content_at = content_start + 4 - start;
        t->content_length = buffer_length(b) - t->content_at - 1; // added '\0'
    }

    return buffer_length(b) - 1;
}

static int thttp_connect(THTTP *t, const char *addr) {
    t->fd = toconnect3(addr, 0, t->timeout_connect);
    if (t->fd < 0)
        return -1;

    if (t->timeout_read > 0) {
        // set the io timeout
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = t->timeout_read;
        setsockopt(t->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    t->read = _read;
    t->write = _write;
    return 0;
}

static int thttp_read_response_and_finish(THTTP *t) {
    if (t->failed || read_response(t) < 0) {
        close(t->fd);
        return -1;
    }

    close(t->fd);
    return 0;
}


/* Export functions */

int thttp_init(THTTP *t) {
    memset(t, 0, sizeof(*t));
    if (buffer_init(&t->bresp, BUFFER_MIN_INCREMENT) < 0)
        return -1;
    return 0;
}

int thttp_cleanup(THTTP *t) {
    buffer_cleanup(&t->bresp);
    memset(t, 0, sizeof(*t));
    return 0;
}

void thttp_set_connect_timeout(THTTP *t, int microsecond) {
    t->timeout_connect = microsecond;
}

void thttp_set_io_timeout(THTTP *t, int microsecond) {
    t->timeout_read = microsecond;
}

int thttp_get(THTTP *t, const char *addr, const char *uri, const char *host) {
    int ret = thttp_connect(t, addr);
    if (ret < 0)
	return ret;

    ret = request(t, "GET", uri, "ssss",
		  "Accept", "text/plain",
	    	  "Host", host,
	    	  "Connection", "close",
	    	  "User-Agent", "pttbbs");
    if (ret < 0)
	t->failed = 1;

    return thttp_read_response_and_finish(t);
}

int thttp_post(THTTP *t, const char *addr, const char *uri, const char *host,
	       const char *content_type, const void *content,
	       int content_length) {
    int ret = thttp_connect(t, addr);
    if (ret < 0)
	return ret;

    ret = request(t, "POST", uri, "sssssi",
		  "Accept", "text/plain",
	    	  "Host", host,
	    	  "Connection", "close",
	    	  "User-Agent", "pttbbs",
		  "Content-Type", content_type,
		  "Content-Length", content_length);
    if (ret == 0)
	ret = t->write(t, content, content_length);
    if (ret < 0)
	t->failed = 1;

    return thttp_read_response_and_finish(t);
}

int thttp_code(THTTP *t) {
    return t->code;
}

void *thttp_get_content(THTTP *t) {
    return t->code == 200 ? buffer_get(&t->bresp, t->content_at) : "";
}

int thttp_content_length(THTTP *t) {
    return t->code == 200 ? t->content_length : 0;
}

int thttp_get_header(THTTP *t, const char *name, const char **valptr) {
    int namelen = strlen(name);
    const char *s = buffer_get(&t->bresp, t->headers_at);
    for (;;) {
	const char *e = strstr(s, "\r\n");
	if (e == NULL || s == e)
	    break;  /* invalid (NULL), or end of headers. */

	const char *v = s + namelen;
	if (v + 2 <= e &&
	    !strncasecmp(s, name, namelen) &&
	    v[0] == ':' && v[1] == ' ') {
	    *valptr = v + 2;
	    return e - *valptr;
	}

	s = e + 2;
    }
    return -1;
}
