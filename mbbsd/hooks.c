// Site hooks: default (weak) implementations. A site may override them,
// for example in mbbsd/local/*.c.
#include "bbs.h"

// Called when the user has logged in, right before the main menu.
GCC_WEAK void
on_user_login(void)
{
}

// Returns why the email can not be used to register, or NULL.
GCC_WEAK const char *
is_rejected_email(const char *email GCC_UNUSED)
{
    return NULL;
}

// Returns true to drop forwarding to the Internet address (the user is told
// it was sent).
GCC_WEAK bool
is_rejected_forward(const char *address GCC_UNUSED)
{
    return false;
}

// Returns false to cancel forwarding or cross-posting an article, before
// asking anything.
GCC_WEAK bool
can_forward(const char *title GCC_UNUSED)
{
    return true;
}

// Called after an article was cross-posted from board/fname to xboard/xfname.
GCC_WEAK void
on_crosspost(const char *board GCC_UNUSED, const char *fname GCC_UNUSED,
             const char *xboard GCC_UNUSED, const char *xfname GCC_UNUSED)
{
}

// Describes the system load (cpuload()) in system information.
GCC_WEAK const char *
get_loading_str(int load)
{
    return load < 5 ? "良好" : (load < 20 ? "尚可" : "過重");
}

// Called when we need a per-title(category) max pay.
GCC_WEAK int
get_max_pay_by_post_title(const char *title GCC_UNUSED)
{
    return MAX_POST_MONEY;
}

// Override for the dynamic cap.
GCC_WEAK int
calc_post_cap(int post_count, int base_cap)
{
    if (base_cap <= 0)
        return 0;
    int cap = base_cap - post_count;
    if (cap < POST_CAP_MIN)
        cap = POST_CAP_MIN;
    return cap;
}

