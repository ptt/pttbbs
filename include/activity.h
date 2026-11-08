#ifndef INCLUDE_ACTIVITY_H
#define INCLUDE_ACTIVITY_H

#include <stddef.h>
#include <stdint.h>
#include "pttstruct.h"

// User activity hooks (common/bbs/activity.c). All decisions are made by an
// external activity service (activity.svc), which owns all logic and state;
// without it every hook allows everything.
//
// Clients keep one persistent, non-blocking unix SOCK_SEQPACKET connection
// to ACTIVITY_SVC_PATH (one ASCII message per packet, fields separated by
// single spaces). Sends never block: a message is dropped if the socket
// buffer is full. If the connection is lost, clients retry every
// ACTIVITY_SVC_RETRY_SEC seconds and treat everything in between as allowed.
//
// Client -> service:
//   fail <userid> <ip>                          wrong password
//   login <reqid> <userid> <ip> <exempt>        authenticated; expects verdict
//   read <userid> <firstlogin> <client_hex> <bid> <filename>
//                                               a post read
//   nav <userid> <firstlogin> <ip> <client_hex> <type> <bid> [<value>]
//                                               mbbsd navigation (ACTIVITY_NAV_*);
//                                               value only for SEARCH_SCORE (threshold)
//   fwd <reqid> <userid> <firstlogin> <ip> <client_hex> <bid> <filename> <kind> <address> <via>
//                                               a post (bid > 0) or mail (bid 0) sent out as
//                                               Internet mail: kind is ACTIVITY_FWD_*, address
//                                               the Internet address (or the local userid for
//                                               ACTIVITY_FWD_LOCAL), via the local mailbox
//                                               that auto-forwards (or "-"); expects a verdict
//   <firstlogin> is an opaque user token (userec_t.firstlogin as a signed
//   int32): the service only compares it to tell a recycled account, never
//   uses it as a time.
//
// Service -> client:
//   verdict <reqid> ok                          reply to login or fwd
//   verdict <reqid> deny <delay_sec> <message>  message (Big5) runs to the end;
//                                               for fwd: do not send (delay 0), and
//                                               the message (may be absent) says
//                                               why and until when
//   reads <userid> <count> <delay_ms>           reply to read: how long to pause
//                                               (count: new posts read across
//                                               sessions, for logs; not used)
//   pace <userid> <delay_ms> [deny <type> [<message>]]
//                                               reply to nav: how long to pause,
//                                               and "deny": deny this type of
//                                               request; the message (Big5, to
//                                               the end, may be absent) says why
//                                               and until when
//   ... slow <until> <message>                  at the end of reads and pace
//                                               (not with deny) while the account
//                                               is slowed down by navigation
//                                               rules: until when (Unix time) and
//                                               why (Big5, to the end); passed
//                                               to the on_delay callback once
//                                               per slowdown
//
// Denial messages are shown with activity_alert() (and FN_ACTIVITY_ALERT, the
// site notes, e.g. where to report mistakes), at most ACTIVITY_DENY_MSG_MAX.
//
// A login waits at most ACTIVITY_SVC_VERDICT_MS for its verdict and is
// accepted without one. A denied login is held for min(delay_sec,
// ACTIVITY_TARPIT_SEC) before the connection is closed.

#define ACTIVITY_SVC_PATH           BBSHOME "/run/activity.svc.sock"
#define ACTIVITY_SVC_RETRY_SEC      (5)
#define ACTIVITY_SVC_VERDICT_MS     (250)
#define ACTIVITY_SVC_MAX_TIMEOUTS   (3)     // Consecutive timeouts before reconnect
#define ACTIVITY_SVC_MSG_MAX        (4096)
#define ACTIVITY_READ_WAIT_MS       (5)     // mbbsd: wait for a read/nav reply
#define ACTIVITY_FORWARD_WAIT_MS    (50)    // mbbsd: wait for a fwd reply (rare, by hand)
#define ACTIVITY_DENY_MSG_MAX       (256)   // mbbsd: denial messages kept

// logind holds denied connections open (tarpit) for up to this long, so
// clients that reconnect as soon as they are disconnected slow down and
// users get to see the message. Capped per IP and in total to bound fds.
#define ACTIVITY_TARPIT_SEC         (180)
#define ACTIVITY_TARPIT_PER_IP      (4)
#define ACTIVITY_TARPIT_MAX         (2000)

// Users with any of these permissions are exempt from activity checks.
#define ACTIVITY_EXEMPT_PERMS (PERM_BM | PERM_ADMIN | PERM_POLICE | PERM_POLICE_MAN)

enum {
    ACTIVITY_NAV_BOARD_SWITCH  = 1,
    ACTIVITY_NAV_SEARCH_AID    = 2,
    ACTIVITY_NAV_SEARCH_SR     = 3,
    ACTIVITY_NAV_SEARCH_SCORE = 4, // 'Z': search by score; value is the threshold
};

// logind: a password was wrong.
void activity_login_failed(const char *userid, const char *hostip);

// logind: asks for a verdict on an authenticated login. Returns a request id
// (> 0) whose verdict arrives through activity_client_dispatch(), or 0 if the
// login should be accepted right away (service unavailable).
uint32_t activity_login_query(const char *userid, const char *hostip, int is_exempt);

// logind: socket to watch for readability, or -1.
int activity_client_fd(void);

// logind: reads pending replies without blocking and calls cb for each login
// verdict. The client may reconnect or close its socket inside any activity_*
// call, so re-check activity_client_fd() afterwards.
typedef void (*activity_verdict_cb)(uint32_t reqid, int deny, int delay_sec,
                                    const char *msg, void *arg);
void activity_client_dispatch(activity_verdict_cb cb, void *arg);

// logind: a verdict did not arrive in time.
void activity_client_timeout(void);

// mbbsd: tells the user that the account is slowed down by navigation rules
// (`msg`, Big5: why and until when). Called before pausing, once per slowdown
// (a slowdown extended while it lasts is the same one).
typedef void (*activity_delay_fn)(const char *msg);

// mbbsd: reports reading post `filename` on board `bid`, and pauses as the
// service asks (not at all without it). If the account is slowed down,
// `on_delay` (may be NULL) is called first.
void activity_read_post(const char *userid, int32_t user_token,
                        uint32_t client_code, int is_exempt,
                        int bid, const char *filename,
                        activity_delay_fn on_delay);

// mbbsd: reports navigation (`value`: the threshold for SEARCH_SCORE, else 0),
// and pauses as activity_read_post() does. Returns 1 if this request is denied
// (e.g. polling), else 0, and sets `msg` (if not NULL, `msgsz` bytes) to why
// it is denied ("" if not told).
int activity_nav_track(const char *userid, int32_t user_token,
                       const char *hostip, uint32_t client_code,
                       int is_exempt, int nav_type, int target_bid,
                       int value, char *msg, size_t msgsz,
                       activity_delay_fn on_delay);

// Kinds of "fwd", from the most to the least responsible sender.
enum {
    ACTIVITY_FWD_LOCAL    = 0,  // 'F' inside the BBS (counted only)
    ACTIVITY_FWD_INTERNET = 1,  // 'F' to an Internet address
    ACTIVITY_FWD_OWN      = 2,  // To one's own mailbox that auto-forwards
    ACTIVITY_FWD_OTHER    = 3,  // To another's mailbox that auto-forwards
};

// mbbsd: reports sending `filename` on board `bid` (0: a mail) out as Internet
// mail to `address` (kind ACTIVITY_FWD_*; `via`: the auto-forwarding mailbox,
// or NULL; `mtime`: when the file was last changed, to tell a changed post
// from a duplicate, or 0 if unknown). Returns 1 if it should be denied, else 0 (always 0 without the
// service or a reply in time); sets `msg` (if not NULL, `msgsz` bytes) to why
// it is denied ("" if not told).
int activity_forward(const char *userid, int32_t user_token,
                     const char *hostip, uint32_t client_code, int is_exempt,
                     int bid, const char *filename, int kind,
                     const char *address, const char *via, time_t mtime,
                     char *msg, size_t msgsz);

#endif // INCLUDE_ACTIVITY_H
