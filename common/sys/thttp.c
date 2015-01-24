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

    char *content_start;
    if (t->code != 200) {
        // although there can be content when error,
        // we still ignore it.
        t->content_at = -1;
        t->content_length = -1;
    } else if ((content_start = strstr(start, "\r\n\r\n")) != NULL) {
        t->content_at = content_start + 4 - start;
        t->content_length = buffer_length(b) - t->content_at - 1; // added '\0'
    } else {
        // invalid response
        return -1;
    }

    return buffer_length(b) - 1;
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

    request(t, "GET", uri, "ssss",
            "Accept", "text/plain",
            "Host", host,
            "Connection", "close",
            "User-Agent", "pttbbs");

    if (t->failed || read_response(t) < 0) {
        close(t->fd);
        return -1;
    }

    close(t->fd);
    return 0;
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
