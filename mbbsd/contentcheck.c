// Content filtering in mbbsd: asks content.svc (common/bbs/content.c) and
// carries out its verdict. See include/content.h.

#include "bbs.h"
#include "content.h"
#include "activity.h"

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
        if (bp && (bp->brdattr & (BRD_RESTRICTEDPOST | BRD_NOREG_CAN_POST)))
            flags |= CONTENT_F_RESTRICTED;
    }
    return flags;
}

// A system alert: why, what was found, and site notes. Shown over the current
// screen, which is restored afterwards. Used by content checks and by
// activity_alert().
void
alert_screen(const char *title, const char *status, const char *reason,
             const char *shown, const char *notes)
{
    screen_backup_t old_screen;

    scr_dump(&old_screen);
    vs_hdr2(title, status);
    outs("\n");
    for (const char *p = reason ? reason : ""; *p; ) {
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
    if (notes && dashf(notes)) {
        // Site specific notes, e.g. what is not allowed and where to appeal.
        int y, x;
        getyx(&y, &x);
        if (b_lines - y - 2 > 0)
            show_file(notes, y + 1, b_lines - y - 2, SHOWFILE_ALLOW_COLOR);
    }
    vkey_purge();
    pressanykey();
    scr_restore(&old_screen);
}

// The activity alert: a denial by activity.svc (`msg`, Big5: what, why and
// until when), and the site notes (FN_ACTIVITY_ALERT).
void
activity_alert(const char *msg, const char *status)
{
    char reason[ACTIVITY_DENY_MSG_MAX + 32];

    snprintf(reason, sizeof(reason), ANSI_COLOR(1;31) "%s" ANSI_RESET, msg);
    alert_screen(" 使用限制 ", status, reason, NULL, FN_ACTIVITY_ALERT);
}

// The account is slowed down by navigation rules (on_delay of activity hooks).
void
activity_slow_alert(const char *msg)
{
    activity_alert(msg, " 限速中 ");
}

// The content alert: why, what was found, and site notes.
static void
content_alert(const content_verdict_t *v, const char *text)
{
    alert_screen(" 內容檢查 ", " 未送出", v->reason,
                 v->details[0] ? v->details : text, FN_CONTENT_ALERT);
}

// A ban: a ticket (罰單) as police give, posted with the details, mailed
// to the user, and all sessions kicked. With a ticket the user can still
// read the mail, appeal and change settings, and pays it to get back.
static void
content_ban(const content_verdict_t *v)
{
    const char *memo = v->details[0] ? v->details : NULL;
    char reason[sizeof(v->reason) + sizeof(v->details) + 8];

    pwcuViolateLaw();
    post_violatelaw2(cuser.userid, CONTENT_POLICE, v->reason, "罰單處分",
                     memo);
    if (memo)
        snprintf(reason, sizeof(reason), "%s\n %s\n", v->reason, memo);
    else
        strlcpy(reason, v->reason, sizeof(reason));
    mail_violatelaw(cuser.userid, CONTENT_POLICE, reason, "罰單處分");
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
