// Site hooks (site_*): default (weak) implementations. A site may override
// them, for example in mbbsd/site/*.c.
#include "bbs.h"

// Called when the user has logged in, right before the main menu.
GCC_WEAK void
site_on_login(void)
{
}

// Returns why the email can not be used to register, or NULL.
GCC_WEAK const char *
site_get_email_reject_reason(const char *email GCC_UNUSED)
{
    return NULL;
}

// Returns false to cancel forwarding or cross-posting an article, before
// asking anything.
GCC_WEAK bool
site_can_forward(const char *title GCC_UNUSED)
{
    return true;
}

// Returns false to drop forwarding to the Internet address (the user is told
// it was sent), e.g. for forwarding services.
GCC_WEAK bool
site_can_forward_to(const char *address GCC_UNUSED)
{
    return true;
}

// Called when the user starts cross-posting an article, before asking
// anything (e.g. to show the site rules about cross-posting).
GCC_WEAK void
site_on_crosspost_prompt(void)
{
}

// Called after an article was cross-posted from board/fname to xboard/xfname.
GCC_WEAK void
site_on_crosspost(const char *board GCC_UNUSED, const char *fname GCC_UNUSED,
                  const char *xboard GCC_UNUSED, const char *xfname GCC_UNUSED)
{
}

// Describes the system load (cpuload()) in system information.
GCC_WEAK const char *
site_get_load_desc(int load)
{
    return load < 5 ? "良好" : (load < 20 ? "尚可" : "過重");
}

// Called when we need a per-title(category) max pay.
GCC_WEAK int
site_get_max_pay_by_title(const char *title GCC_UNUSED)
{
    return MAX_POST_MONEY;
}

// Adjusts the pay of a post (already capped by MAX_POST_MONEY), before the
// daily cap and site_get_max_pay_by_title().
GCC_WEAK int
site_get_post_money(int money)
{
    return money;
}

// Returns false to refuse cross-posting the article fh in board bp, after
// telling the user why.
GCC_WEAK bool
site_can_crosspost(const boardheader_t *bp GCC_UNUSED,
                   const fileheader_t *fh GCC_UNUSED)
{
    return true;
}

// Called before saving a user whose bad posts are increased (inc_badpost()).
GCC_WEAK void
site_on_inc_badpost(userec_t *u GCC_UNUSED)
{
}

// Called before a comment (msg, of type RECTYPE_*) is added to the post fh in
// board bp; who is the ID shown (e.g. an anonymous angel's nickname).
GCC_WEAK void
site_on_comment(const boardheader_t *bp GCC_UNUSED,
                const fileheader_t *fh GCC_UNUSED, int type GCC_UNUSED,
           const char *who GCC_UNUSED, const char *msg GCC_UNUSED)
{
}

// Fixes the first line of a text just loaded into the editor (data, may be
// changed in place, e.g. to remove junk sent by broken clients). Returns its
// length.
GCC_WEAK int
site_fix_edit_first_line(char *data)
{
    return strlen(data);
}

// Called after a key is typed into line lineno (0: the first) of the editor.
// Returns false to abandon editing (after telling the user why), e.g. when a
// broken client keeps sending junk.
GCC_WEAK bool
site_can_edit_input(int lineno GCC_UNUSED, const char *line GCC_UNUSED)
{
    return true;
}

// The color (ANSI) of logindays in user queries, or "".
GCC_WEAK const char *
site_get_logindays_color(int logindays GCC_UNUSED)
{
    return "";
}

// Whether it is near the maintenance of the site so time-consuming tasks should
// be disallowed.
GCC_WEAK bool
site_is_near_maintenance(void)
{
    return false;
}

// Returns whether posts on board brdname pay nothing.
GCC_WEAK bool
site_is_free_board(const char *brdname)
{
    if (strcmp(brdname, BN_TEST) == 0 || strcmp(brdname, BN_ALLPOST) == 0)
        return true;
    // DEFAULT_BOARD allows posting without special permission, so no rewards.
    return strcasecmp(brdname, DEFAULT_BOARD) == 0;
}

// Called after rows of a page of posts or mails (the count entries in page,
// the first at screen row y) are drawn, to draw anything over the page
// (psb_col_x() tells where the columns of ctx are). Called again whenever a
// row is redrawn (e.g. the cursor moved), so draw all of it every time; the
// cursor is drawn afterwards.
GCC_WEAK void
site_decorate_read_page(PSB_CTX *ctx GCC_UNUSED, int y GCC_UNUSED,
                        const fileheader_t *page GCC_UNUSED,
                        int count GCC_UNUSED)
{
}

// Returns false to refuse a search (sr_mode RS_*) for keyword in the reader,
// after telling the user why.
GCC_WEAK bool
site_can_search(const char *keyword GCC_UNUSED, int sr_mode GCC_UNUSED)
{
    return true;
}

// Returns true to stop saving comments to commentd (USE_COMMENTD).
GCC_WEAK bool
site_is_commentd_disabled(void)
{
    return false;
}

// Days to keep an account since its last login before it can be expired.
GCC_WEAK int
site_get_keep_days(const userec_t *u)
{
    return (u->userlevel & (PERM_LOGINOK | PERM_VIOLATELAW)) ?
        KEEP_DAYS_REGGED : KEEP_DAYS_UNREGGED;
}

// Override for the dynamic cap.
GCC_WEAK int
site_get_post_cap(int post_count, int base_cap)
{
    if (base_cap <= 0)
        return 0;
    int cap = base_cap - post_count;
    if (cap < POST_CAP_MIN)
        cap = POST_CAP_MIN;
    return cap;
}

