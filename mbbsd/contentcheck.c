// Content filtering in mbbsd: asks content.svc (common/bbs/content.c) and
// carries out its verdict. See include/content.h.

#include "bbs.h"
#include "content.h"

#define CONTENT_POLICE BBSMNAME " 系統警察"

static int
content_is_board_kind(int kind)
{
    return kind == CONTENT_KIND_COMMENT || kind == CONTENT_KIND_POST ||
           kind == CONTENT_KIND_TITLE;
}

static unsigned
content_collect_flags(int kind)
{
    unsigned flags = 0;

    if (HasUserPerm(CONTENT_ADMIN_PERMS))
        flags |= CONTENT_F_ADMIN;
    if (HasUserPerm(PERM_BM))
        flags |= CONTENT_F_PERM_BM;
    if (is_new_account(&cuser))
        flags |= CONTENT_F_NEW;
    if (content_is_board_kind(kind) && currbid > 0) {
        const boardheader_t *bp = getbcache(currbid);
        if (currmode & MODE_BM)
            flags |= CONTENT_F_BOARD_BM;
        if (bp && ((bp->brdattr & BRD_HIDE) ||
                   (bp->level && !(bp->brdattr & BRD_POSTMASK))))
            flags |= CONTENT_F_HIDDEN;
        if (bp && (bp->brdattr & (BRD_RESTRICTEDPOST | BRD_GUESTPOST)))
            flags |= CONTENT_F_RESTRICTED;
    }
    return flags;
}

// The content alert: why, site notes, and what was found. Shown over the
// current screen, which is restored afterwards.
static void
content_alert(const content_verdict_t *v, const char *text)
{
    const char *shown = v->details[0] ? v->details : text;
    screen_backup_t old_screen;

    scr_dump(&old_screen);
    vs_hdr2(" 內容檢查 ", " 未送出");
    outs("\n");
    for (const char *p = v->reason; *p; ) {
        int n = strcspn(p, "\n");
        prints("  " ANSI_COLOR(1) "%.*s" ANSI_RESET "\n", n, p);
        p += n;
        if (*p)
            p++;
    }
    if (shown && *shown) {
        outs("\n  有問題的內容:\n\n");
        prints(ANSI_COLOR(1;33) "%.*s" ANSI_RESET "\n", ANSILINELEN * 2, shown);
    }
    if (dashf(FN_CONTENT_ALERT)) {
        // Site specific notes, e.g. what is not allowed and where to appeal.
        int y, x;
        getyx(&y, &x);
        if (b_lines - y - 2 > 0)
            show_file(FN_CONTENT_ALERT, y + 1, b_lines - y - 2, SHOWFILE_ALLOW_COLOR);
    }
    vkey_purge();
    pressanykey();
    scr_restore(&old_screen);
}

// A ban: suspended, posted with the details, mailed to the user, and all
// sessions kicked.
static void
content_ban(const content_verdict_t *v)
{
    const char *memo = v->details[0] ? v->details : NULL;
    char reason[sizeof(v->reason) + sizeof(v->details) + 8];

    // Take PERM_BASIC as well, or they cannot tell what happened.
    pwcuBitDisableLevel(PERM_BASIC | PERM_POST);
    post_violatelaw2(cuser.userid, CONTENT_POLICE, v->reason, "停權", memo);
    if (memo)
        snprintf(reason, sizeof(reason), "%s\n %s\n", v->reason, memo);
    else
        strlcpy(reason, v->reason, sizeof(reason));
    mail_violatelaw(cuser.userid, CONTENT_POLICE, reason, "停權");
    kick_all(cuser.userid);
}

int
content_check(int kind, const char *target, unsigned flags, int extra,
              const char *path, const char *title, const char *text)
{
    content_verdict_t v;
    int timeout_ms = CONTENT_SVC_TIMEOUT_MS;
    int y = -1, x = 0, ok;

    if (!IS_ENABLED(CONFIG_CONTENT_SVC))
        return 0;

    flags |= content_collect_flags(kind);
    if (flags & CONTENT_F_PRECHECK) {
        // Only a lookup; do not keep users waiting if the service is stuck,
        // what they write is checked again when sent.
        timeout_ms = CONTENT_SVC_PRECHECK_TIMEOUT_MS;
    } else if (path && *path) {
        // May take a while (e.g. classified by a language model).
        timeout_ms = CONTENT_SVC_FILE_TIMEOUT_MS;
        // Callers keep printing where they were (e.g. do_post_article()
        // after the editor), so restore the cursor after the message.
        getyx(&y, &x);
        outmsg("檢查內容中，請稍候...");
        refresh();
    }

    ok = content_filter(kind, cuser.userid, fromhost,
                        currbid > 0 ? currboard : NULL, target, flags, extra,
                        path, title, text, text ? strlen(text) : 0,
                        timeout_ms, &v);
    if (y >= 0) {
        move(b_lines - msg_occupied, 0);
        clrtoeol();
        move(y, x);
    }
    if (!ok || !v.deny)
        return 0;

    if (v.report && path && *path)
        post_policelog_spam(kind == CONTENT_KIND_POST ? currboard : NULL,
                            path, v.reporter[0] ? v.reporter : NULL);
    if (v.ui == CONTENT_UI_ALERT)
        content_alert(&v, text);
    if (v.penalty == CONTENT_PENALTY_BAN)
        content_ban(&v);
    return 1;
}

int
content_precheck(int kind, const char *target)
{
    return content_check(kind, target, CONTENT_F_PRECHECK, 0, NULL, NULL,
                         NULL);
}

// What the editor is about to save, for content_check_saved().
static struct {
    int      kind;
    char     target[IDLEN + 1];
    unsigned flags;
    int      extra;
} content_ctx;

void
content_set_context(int kind, const char *target, unsigned flags, int extra)
{
    content_ctx.kind = kind;
    strlcpy(content_ctx.target, target ? target : "",
            sizeof(content_ctx.target));
    content_ctx.flags = flags;
    content_ctx.extra = extra;
}

int
content_check_saved(const char *fpath, const char *title)
{
    if (content_ctx.kind != CONTENT_KIND_POST &&
        content_ctx.kind != CONTENT_KIND_MAIL)
        return 0;
    return content_check(content_ctx.kind,
                         content_ctx.target[0] ? content_ctx.target : NULL,
                         content_ctx.flags, content_ctx.extra, fpath, title,
                         NULL);
}
