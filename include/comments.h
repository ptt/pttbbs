/*
 * comments.h
 * BBS comments theme and parser subsystem
 */

#ifndef _COMMENTS_H_
#define _COMMENTS_H_

#include "bbs.h"
#include "ansi.h"

#ifndef COMMENT_LINE_LEN
#define COMMENT_LINE_LEN 64     /* chars of a comment line, without NUL */
#endif

enum {
    COMMENT_THEME_SIMPLE = 0,   /* the default */
    COMMENT_THEME_ALIGNED,
    COMMENT_THEME_CLASSIC,
    COMMENT_THEMES
};

typedef struct {
    const char *name;
    int align;                          /* pad userids to IDLEN */
    const char *mark_first;             /* before the userid, e.g. a push */
    const char *color_mark_first;
    const char *mark_more;              /* the same on further lines */
    const char *color_mark_more;
    const char *color_commenter;
    const char *color_commenter_op;
    const char *color_commenter_more;   /* NULL: blank up to the text */
    const char *color_commenter_more_op;
    const char *color_comment_text;
    const char *color_comment_text_op;
    const char *color_commenter_info;
} comment_theme_t;

/* Context passed for comment rendering */
typedef struct comment_ctx {
    const unsigned char *buf_end;       /* mf.end */
    int max_width;                      /* usually t_columns - 1 (e.g. 79) */
    int theme;                          /* bpref.comment_theme */
    int cur_is_op;                      /* state: is the line being drawn by OP */
} comment_ctx_t;

/* Theme info & preference helpers */
const comment_theme_t *comment_get_theme(int theme_idx);
const char *comment_theme_name(int theme_idx);
int comment_theme_count(void);
const char *comment_theme_menu_options(void);

/* User's preferred comment theme (persisted in cuser.comment_theme). */
int comment_pref_theme(void);
void comment_set_pref_theme(int theme_idx);

/* Renders comment header "RS[*][-|!]userid|[~]No|date|[ip]|text".
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

/* Renders the prefix of a comment body line and sets text color.
 * Checks if line_head is a comment body line ("RS[*]+userid|text"), anywhere
 * in the article; the line alone tells its OP state and userid.
 * If yes:
 *   - renders the theme's prefix (e.g. "userid: ") if *dispe == line_head
 *   - advances *dispe past the prefix up to the '|'
 *   - updates *col with the rendered prefix width
 *   - updates ctx->cur_is_op and outputs text color (OP vs non-OP)
 *   - sets *need_reset_color = 1
 *   - returns 1
 * If not a comment body line, returns 0.
 */
int comment_render_body_prefix(comment_ctx_t *ctx,
                               const unsigned char *line_head,
                               unsigned char **dispe,
                               int *col,
                               int *need_reset_color);

/* Checks if *p points to a comment reference ">>N" (where N > 0).
 * If matched, returns the length of the reference (e.g. strlen(">>12")).
 * If not matched, returns 0.
 */
int comment_match_ref(const unsigned char *p,
                      const unsigned char *end,
                      int *ref_num);

/* Returns current body text color according to theme and OP state */
const char *comment_get_body_color(const comment_ctx_t *ctx);

/* Renders a standalone line (not in a pager buffer, e.g. live mode) in the
 * new comment format: a header "RS[*][-|!]userid|..." or a body line
 * "RS[*]+userid|text". The buffer fields of ctx are not used, and each line
 * is rendered on its own.
 * Returns 1 if the line was rendered, or 0 (nothing output, state reset) if
 * it is not a new format comment line.
 */
int comment_render_line(comment_ctx_t *ctx, const char *line, int len);

#endif /* _COMMENTS_H_ */
