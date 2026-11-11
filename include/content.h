#ifndef INCLUDE_CONTENT_H
#define INCLUDE_CONTENT_H

#include <stddef.h>

// Content filtering: checks of what users write (spam, harassment, board
// rules), decided by content.svc. Without it (or with CONFIG_CONTENT_SVC
// disabled) nothing is filtered.
//
// Client -> service: a header line, then the title and text bytes.
//   check <kind> <userid> <ip> <board> <target> <flags> <extra> <path> <title_len> <text_len>\n
//   <title><text>
//     kind     CONTENT_KIND_* name ("comment", "wmsg", "title", "post", "mail")
//     board    current board, "-" if none
//     target   receiver of a mail or message, "-" if none
//     flags    CONTENT_F_* in hex
//     extra    kind specific: number of recipients for "mail", else 0
//     path     file to check, relative to BBSHOME ("post", "mail"), "-" if
//              none
//     title    Big5, title_len bytes (may be 0)
//     text     Big5, text_len bytes (may be 0)
//
// Service -> client: a verdict line, optionally followed by details, then the
// service closes the connection.
//   <allow|deny> <ui> <penalty> <report> <reporter> <reason>\n
//   [<details>]
//     ui       none (silently) | alert (the content alert screen)
//     penalty  none | ban (a ticket, PERM_VIOLATELAW, posted to
//              BN_VIOLATELAW, mailed, and kicked). Temporary mutes are kept
//              by the service, which denies everything until they expire.
//     report   "report": report the file (posts, mails) to BN_POLICELOG;
//              "-" not
//     reporter who detected it (shown in BN_POLICELOG), "-" for the system
//     reason   Big5, runs to the end of the line, lines separated by tabs
//              (may have ANSI colors): shown in the alert, and the reason
//              of a ban
//     details  Big5, runs to the end: shown in the alert (instead of the
//              text), and attached to a ban
//
// A check waits at most `timeout_ms` (CONTENT_SVC_TIMEOUT_MS,
// CONTENT_SVC_FILE_TIMEOUT_MS for file checks, CONTENT_SVC_PRECHECK_TIMEOUT_MS
// for prechecks) and is allowed without a verdict.

#define CONTENT_SVC_PATH               BBSHOME "/run/content.svc.sock"
#define CONTENT_SVC_TIMEOUT_MS         (5000)
#define CONTENT_SVC_FILE_TIMEOUT_MS    (30000)
#define CONTENT_SVC_PRECHECK_TIMEOUT_MS (300)
#define CONTENT_SVC_TEXT_MAX           (256 * 1024)
#define CONTENT_SVC_RESP_MAX           (8192)

// What is checked. Posts and mails are checked as saved by the editor
// (header, body and signature), so users can fix them and save again.
enum {
    CONTENT_KIND_COMMENT = 1,  // text: the comment
    CONTENT_KIND_WMSG,         // text: the message (wmsg); target
    CONTENT_KIND_TITLE,        // title: the title of a new post, before editing
    CONTENT_KIND_POST,         // path: a new or edited post; title
    CONTENT_KIND_MAIL,         // path: the mail; title; target (first); extra
};

// Flags describing the user and the board; the service decides what they mean.
#define CONTENT_F_ADMIN        (0x01)  // Has CONTENT_ADMIN_PERMS
#define CONTENT_F_PERM_BM      (0x02)  // Has PERM_BM (moderates some board)
#define CONTENT_F_BOARD_BM     (0x04)  // Moderates the current board
#define CONTENT_F_HIDDEN       (0x08)  // Board is hidden or read restricted
#define CONTENT_F_RESTRICTED   (0x10)  // Board is post restricted or no-reg post
#define CONTENT_F_FRIEND       (0x20)  // The target has the user as a friend
#define CONTENT_F_MULTI        (0x40)  // Group mail
#define CONTENT_F_REEDIT       (0x80)  // Editing an existing post
#define CONTENT_F_NEW          (0x100) // New account (is_new_account())
#define CONTENT_F_PRECHECK     (0x200) // Before writing: may the user write?

#define CONTENT_ADMIN_PERMS    (PERM_ADMIN | PERM_POLICE | PERM_POLICE_MAN)

enum {
    CONTENT_UI_NONE = 0,
    CONTENT_UI_ALERT,
};

enum {
    CONTENT_PENALTY_NONE = 0,
    CONTENT_PENALTY_BAN,
};

typedef struct {
    int  deny;
    int  ui;            // CONTENT_UI_*
    int  penalty;       // CONTENT_PENALTY_*
    int  report;
    char reporter[64];  // "" for the system
    char reason[512];
    char details[CONTENT_SVC_RESP_MAX];
} content_verdict_t;

// Asks for a verdict. NULL strings are sent as "-" (or empty for title and
// text).
// Returns 1 if a verdict was received into *v, or 0 if the service is
// unavailable (allow, and *v is cleared).
int content_filter(int kind, const char *userid, const char *ip,
                   const char *board, const char *target, unsigned flags,
                   int extra, const char *path, const char *title,
                   const char *text, size_t text_len, int timeout_ms,
                   content_verdict_t *v);

// Checks content from the current user and carries out the verdict (alert,
// report, ban). Flags of the user and the current board are filled in.
// Returns 1 if denied (the caller must not go on), else 0.
int content_check(int kind, const char *target, unsigned flags, int extra,
                  const char *path, const char *title, const char *text);

// Asks whether the current user may write `kind` (e.g. not muted), before
// they start writing. Returns 1 if denied (the alert was shown), else 0.
int content_precheck(int kind, const char *target);

// Describes what the editor is about to save, for content_check_saved(): a post
// (CONTENT_KIND_POST; CONTENT_F_REEDIT when editing an existing one) or a mail
// (CONTENT_KIND_MAIL; the first recipient, the number of recipients,
// CONTENT_F_MULTI for group mails).
// Callers set it before vedit2() and clear it (kind 0) after; edits without a
// context (notes, plans, ...) are not checked.
void content_set_context(int kind, const char *target, unsigned flags,
                         int extra);

// Checks the post or mail (as set by content_set_context()) just saved to
// `fpath`. Returns 1 if denied: the user should keep editing.
int content_check_saved(const char *fpath, const char *title);

#endif // INCLUDE_CONTENT_H
