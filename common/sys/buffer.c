#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "buffer.h"

static int buffer__aligned_size(int desired_size);

int buffer_init(BUFFER *b, int initial_size) {
    initial_size = buffer__aligned_size(initial_size);

    memset(b, 0, sizeof(*b));
    b->pool = (uint8_t *)malloc(initial_size);
    if (!b->pool)
        return -1;
    b->allocated = initial_size;
    b->length = 0;
    return 0;
}

int buffer_cleanup(BUFFER *b) {
    free(b->pool);
    return 0;
}

/* Append data to buffer, automatically grows buffer if needed
 *   return the starting offset
 *   return -1 if failed
 */
int buffer_append(BUFFER *b, const void *data, int size) {
    if (b->length + size > b->allocated
        && buffer_grow(b, b->length + size) < 0)
        return -1;

    int start = b->length;
    memcpy(b->pool + start, data, size);
    b->length += size;
    return start;
}

/* Grows the buffer to the desired size
 * aligned to BUFFER_MIN_INCREMENT
 *   return allocated size on success
 *   return -1 on failure
 */
int buffer_grow(BUFFER *b, int desired_size) {
    if (b->allocated >= desired_size)
        return b->allocated;
    desired_size = buffer__aligned_size(desired_size);

    void *newptr = realloc(b->pool, desired_size);
    if (newptr == NULL)
        return -1;

    b->pool = newptr;
    return (b->allocated = desired_size);
}

int buffer_length(BUFFER *b) {
    return b->length;
}

void *buffer_get(BUFFER *b, int offset) {
    return (void *)(b->pool + offset);
}

/* call readfunc once, passing the remaining buffer head and
 * the remaining capacity. automatically grow the buffer once
 * if out of space.
 *   return the value readfunc returns on success
 *       or -1 on failing to grow the buffer
 */
int buffer_read_from_func(
    BUFFER *b, int (*readfunc)(void *ctx, void *buf, int maxread),
    void *ctx)
{
    if (b->length == b->allocated &&
        buffer_grow(b, b->length + BUFFER_MIN_INCREMENT) < 0)
        return -1;

    int nread = readfunc(
        ctx, b->pool + b->length, b->allocated - b->length);
    if (nread <= 0)
        return nread;
    b->length += nread;
    return nread;
}

static int buffer__aligned_size(int desired_size) {
    int aligned_size = desired_size / BUFFER_MIN_INCREMENT;
    aligned_size *= BUFFER_MIN_INCREMENT;
    if (aligned_size < desired_size)
        aligned_size += BUFFER_MIN_INCREMENT;
    return aligned_size;
}
