#ifndef BUFFER_H
#   define BUFFER_H
#include <stdint.h>

#define BUFFER_MIN_INCREMENT (4096)

typedef struct buffer {
    int allocated;
    int length;
    uint8_t *pool;
} BUFFER;

int buffer_init(BUFFER *b, int initial_size);
int buffer_cleanup(BUFFER *b);
int buffer_append(BUFFER *b, const void *data, int size);
int buffer_grow(BUFFER *b, int desired_size);
int buffer_length(BUFFER *b);
void *buffer_get(BUFFER *b, int offset);
int buffer_read_from_func(
    BUFFER *b, int (*readfunc)(void *ctx, void *buf, int maxread),
    void *ctx);

#endif
