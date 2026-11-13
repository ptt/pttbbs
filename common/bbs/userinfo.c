// Descriptions of users shared by mbbsd and the utilities.
#include "bbs.h"

// The permission letters of level ('-' for those not set), in buf[33].
void
userlevel_str(uint32_t level, char buf[33])
{
    static const char letters[] = "bTCPRp#@XWBA#VSM0123456789ABCDEF";

    for (int i = 0; i < 32; i++)
        buf[i] = (level & (1U << i)) ? letters[i] : '-';
    buf[32] = '\0';
}

// The other states of user u, e.g. "[已註冊][自動轉寄][拒收外信]".
void
user_flags_str(const userec_t *u, char *buf, size_t sz)
{
    static const struct {
        uint32_t mask;
        const char *desc;
    } flags[] = {
        { UF_REJ_OUTTAMAIL, "拒收外信" },
        { UF_FAV_ADDNEW,    "新板加最愛" },
        { UF_FOREIGN,       "外藉" },
    };
    char fpath[PATHLEN];
    size_t len;

    snprintf(buf, sz, "[%s]",
             (u->userlevel & PERM_LOGINOK) ? "已註冊" : "未註冊");
    sethomefile(fpath, u->userid, FN_FORWARD);
    if (dashs(fpath) > 0)
        strlcat(buf, "[自動轉寄]", sz);
    for (size_t i = 0; i < ARRAY_SIZE(flags); i++) {
        if (!(u->uflag & flags[i].mask))
            continue;
        len = strlen(buf);
        snprintf(buf + len, sz > len ? sz - len : 0, "[%s]", flags[i].desc);
    }
}
