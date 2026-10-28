/*
 * comments.h
 * BBS comments theme and parser subsystem
 */

#ifndef _COMMENTS_H_
#define _COMMENTS_H_

#include "bbs.h"
#include "ansi.h"

enum {
    COMMENT_THEME_CLASSIC = 0,
    COMMENT_THEME_SIMPLE,
    COMMENT_THEME_BORDERED,
    COMMENT_THEMES
};

typedef struct {
    const char *name;
    const char *color_commenter;
    const char *color_commenter_op;
    const char *color_comment_text;
    const char *color_comment_text_op;
    const char *color_commenter_info;
    const char *color_border;
    const char *border_first;
    const char *border_next;
    const char *border_fill;
    const char *body_indent;
} comment_theme_t;

/* Context passed for comment rendering */
typedef struct {
    const unsigned char *buf_start;     /* mf.start */
    const unsigned char *buf_end;       /* mf.end */
    const unsigned char *comments_start;/* mf.comments_start */
    const char *op_author;              /* fh.op_author */
    int show_comment_ip;                /* mf.show_comment_ip */
    int max_width;                      /* headerw (usually t_columns - 2) */
    int theme;                          /* bpref.comment_theme */
    int cur_is_op;                      /* state: is current comment by OP */
} comment_ctx_t;

/* Theme info & preference helpers */
const comment_theme_t *comment_get_theme(int theme_idx);
const char *comment_theme_name(int theme_idx);
int comment_theme_count(void);
const char *comment_theme_menu_options(void);

/* Initialize OP state at page start if line_head is a comment body line */
void comment_init_op_state(comment_ctx_t *ctx,
                           const unsigned char *line_head);

/* Renders comment header ([N] author creation).
 * If line is recognized and rendered:
 *   - outputs formatted line to screen
 *   - sets *next_line pointing to the newline at end of line
 *   - updates ctx->cur_is_op
 *   - returns 1
 * If not recognized as comment header, returns 0.
 */
int comment_render_header(comment_ctx_t *ctx,
                          const unsigned char *line,
                          const unsigned char **next_line);

/* Renders comment body indent prefix and sets text color.
 * Checks if line_head is a comment body line (starts with "  " in comment section).
 * Uses ctx->cur_is_op for OP-specific coloring without backward scans.
 * If yes:
 *   - renders border prefix if *dispe == line_head
 *   - advances *dispe past the 2 indent spaces
 *   - updates *col with the rendered prefix width
 *   - outputs text color (OP vs non-OP)
 *   - sets *need_reset_color = 1
 *   - returns 1
 * If not a comment body line, returns 0.
 */
int comment_render_body_prefix(const comment_ctx_t *ctx,
                               const unsigned char *line_head,
                               unsigned char **dispe,
                               int *col,
                               int *need_reset_color);

#endif /* _COMMENTS_H_ */
