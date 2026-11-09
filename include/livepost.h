#ifndef INCLUDE_LIVEPOST_H
#define INCLUDE_LIVEPOST_H

/* livepost.c: live mode of the post reader (read_post in bbs.c) */

#include "comments.h"

/*
 * Live mode of a post: the reader sets the post and gives it to more_ex()
 * (more_hooks_t.live); the pager fills the rest when leaving for live mode.
 */
typedef struct live_ctx {
    uint32_t bid;           // the post (0: no live mode)
    aidu_t   aid;
    bool     start;         // go straight to live mode
    // Filled by the pager when leaving for live mode (more_leave_live):
    bool     entered;
    int      row;           // the screen row below the content of the last page
    off_t    shown;         // file bytes shown
    char     op_author[IDLEN + 1];
    comment_ctx_t cmt;      // the comment state at the end of the last page
} live_ctx_t;

void live_on_comment(uint32_t bid, aidu_t aid, const char *text, uint64_t fsize);
void live_resubscribe(void);
bool live_check_repeat(uint32_t bid, aidu_t aid);
void live_reset_repeat(void);
int  live_run(const live_ctx_t *live, const char *fpath,
              int (*comment)(void *ctx), void *ctx);

#endif
