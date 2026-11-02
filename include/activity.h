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
//                                               value only for SEARCH_RATING (threshold)
//
// Service -> client:
//   verdict <reqid> ok
//   verdict <reqid> deny <delay_sec> <message>  message (Big5) runs to the end
//   reads <userid> <count> <delay_ms>           new posts read across sessions,
//                                               and how long to pause
//   pace <userid> <delay_ms> [deny <type>]      reply to nav: how long to pause,
//                                               and "deny": answer this type of
//                                               request with nothing
//   kick <userid> <sec>                         end this session (instead of pace)
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
    ACTIVITY_NAV_SEARCH_RATING = 4, // 'Z': search by rating; value is the threshold
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

// mbbsd: reports reading post `filename` on board `bid`. Returns the number of
// distinct posts read across sessions in the service's window, and sets
// *delay_ms to how long the caller should pause (0 without the service).
// Without the service it counts this process only.
uint32_t activity_read_post(const char *userid, time4_t firstlogin,
                            uint32_t client_code, int is_exempt,
                            int bid, const char *filename, int *delay_ms);

// mbbsd: reports navigation (`value`: the threshold for SEARCH_RATING, else 0).
// Returns -1 if the session should end, 1 if this request should be answered
// with nothing (e.g. polling), else 0; sets *delay_ms to how long the caller
// should pause first (0 without the service).
int activity_nav_track(const char *userid, time4_t firstlogin,
                       const char *hostip, uint32_t client_code,
                       int is_exempt, int nav_type, int target_bid,
                       int value, int *delay_ms);

#endif // INCLUDE_ACTIVITY_H
