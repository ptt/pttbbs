/*
 * comments.h
 * BBS comments subsystem
 */

#ifndef _COMMENTS_H_
#define _COMMENTS_H_

#include "bbs.h"
#include "ansi.h"

/* Context passed for comment rendering */
typedef struct {
    const unsigned char *buf_start;     /* mf.start */
    const unsigned char *buf_end;       /* mf.end */
    const unsigned char *comments_start;/* mf.comments_start */
    const char *op_author;              /* fh.op_author */
    int show_comment_ip;                /* mf.show_comment_ip */
    int max_width;                      /* headerw (usually t_columns - 2) */
} comment_ctx_t;

/* Renders comment header ([N] author creation).
 * If line is recognized and rendered:
 *   - outputs formatted line to screen
 *   - sets *next_line pointing to the newline at end of line
 *   - returns 1
 * If not recognized as comment header, returns 0.
 */
int comment_render_header(const comment_ctx_t *ctx,
                          const unsigned char *line,
                          const unsigned char **next_line);

/* Renders comment body indent prefix.
 * Checks if line_head is a comment body line (starts with "  " in comment section).
 * If yes:
 *   - renders indent prefix if *dispe == line_head
 *   - advances *dispe past the 2 indent spaces
 *   - returns 1
 * If not a comment body line, returns 0.
 */
int comment_render_body_prefix(const comment_ctx_t *ctx,
                               const unsigned char *line_head,
                               unsigned char **dispe);

#endif /* _COMMENTS_H_ */
