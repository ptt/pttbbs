#ifndef UPGRADE_COMMON_H
#define UPGRADE_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} bytebuf_t;

int buf_append(bytebuf_t *b, const void *src, size_t n);

int is_valid_utf8(const uint8_t *buf, size_t len);

/*
 * Fast read-only check for [?2026h / [?2026l (with or without leading ESC).
 */
int has_2026_seq(const uint8_t *buf, size_t len);

/*
 * Strips all ESC[?2026h, ESC[?2026l, [?2026h, [?2026l sequences from src into dst.
 * dst may alias src for in-place stripping. Returns new length.
 */
size_t strip_2026_seq(const uint8_t *src, size_t len, uint8_t *dst);

int read_file_all(const char *path, size_t expected_size,
                  uint8_t **out_buf, size_t *out_len);

int atomic_write_file(const char *path, mode_t mode,
                      const uint8_t *buf, size_t len);

typedef int (*upgrade_file_handler_fn)(const char *path, const struct stat *st);

int process_file_or_dir(const char *path, upgrade_file_handler_fn handler);

#endif /* UPGRADE_COMMON_H */
