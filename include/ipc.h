#ifndef _IPC_H_
#define _IPC_H_

#include <stdint.h>
#include <sys/types.h>
#include "pttstruct.h"

#define IPC_MAGIC           0x49504356  /* 'IPCV' */
#define IPC_VERSION         1

#define IPC_USERID_LEN      16
#define IPC_TOPIC_LEN       64
#define IPC_TEXT_LEN        128

#ifndef IPC_XCHAT_SOCK_PATH
#define IPC_XCHAT_SOCK_PATH (BBSHOME "/run/xchat.ipc.sock")
#endif

#ifndef IPC_CHESS_SOCK_PATH
#define IPC_CHESS_SOCK_PATH (BBSHOME "/run/chess.ipc.sock")
#endif

enum {
    /* 0x0001 - 0x000F: Lifecycle & Control */
    IPC_TYPE_REGISTER       = 0x0001,   /* Client -> Service (Register session) */
    IPC_TYPE_UNREGISTER     = 0x0002,   /* Client -> Service (Clean disconnect) */
    IPC_TYPE_ACK            = 0x0003,   /* Service -> Client (Command Ack/Nack) */
    IPC_TYPE_PING           = 0x0004,   /* Heartbeat ping */
    IPC_TYPE_PONG           = 0x0005,   /* Heartbeat pong */
    IPC_TYPE_STATS_REQ      = 0x0006,   /* Query service stats */
    IPC_TYPE_STATS_RESP     = 0x0007,   /* Service stats response */

    /* 0x0010 - 0x001F: Waterball (wmsg) */
    IPC_TYPE_WMSG_SEND      = 0x0010,   /* Client -> Service (Send waterball) */
    IPC_TYPE_WMSG_DELIVER   = 0x0011,   /* Service -> Target Client (Deliver waterball) */

    /* 0x0020 - 0x002F: PubSub (Live Post & Channels) */
    IPC_TYPE_SUB            = 0x0020,   /* Client -> Service (Subscribe to topic) */
    IPC_TYPE_UNSUB          = 0x0021,   /* Client -> Service (Unsubscribe from topic) */
    IPC_TYPE_PUB            = 0x0022,   /* Client -> Service (Publish to topic) */
    IPC_TYPE_LIVE_COMMENT   = 0x0023,   /* Service -> Subscribers (Deliver live comment) */

    /* 0x0030 - 0x003F: Two-Way Sessions, Talk & Chess */
    IPC_TYPE_TALK_REQ       = 0x0030,   /* Talk / Page request */
    IPC_TYPE_TALK_CANCEL    = 0x0031,   /* Talk request cancel */
    IPC_TYPE_SESSION_REQ    = 0x0030,   /* Request session (alias) */
    IPC_TYPE_SESSION_RESP   = 0x0031,   /* Accept/Reject session (alias) */
    IPC_TYPE_SESSION_DATA   = 0x0032,   /* Streaming session data */
    IPC_TYPE_SESSION_CLOSE  = 0x0033,   /* Terminate session */
    IPC_TYPE_CHESS_REQ      = 0x0034,   /* Chess invite request */
    IPC_TYPE_CHESS_RESP     = 0x0035,   /* Chess invite response */
    IPC_TYPE_CHESS_MOVE     = 0x0036,   /* Chess move */

    /* 0x0040 - 0x004F: XChat / Chat Room */
    IPC_TYPE_CHAT_MSG       = 0x0040,   /* Chat message */
    IPC_TYPE_CHAT_ROOM      = 0x0041,   /* Room join/switch/query */
    IPC_TYPE_CHAT_SYS       = 0x0042,   /* Chat system notification */

    /* 0x0050 - 0x005F: Broadcast */
    IPC_TYPE_SYS_BROADCAST  = 0x0050,   /* System-wide announcement */
};

/* Common header for all IPC messages (16 bytes) */
typedef struct {
    uint32_t magic;         /* IPC_MAGIC */
    uint16_t version;       /* IPC_VERSION */
    uint16_t type;          /* IPC_TYPE_* */
    uint32_t seq;           /* Sequence number */
    uint32_t len;           /* Payload length in bytes */
} __attribute__((packed)) ipc_hdr_t;

/* Register payload (IPC_TYPE_REGISTER) */
typedef struct {
    char     userid[IPC_USERID_LEN];
    uint32_t pid;
    int32_t  uslot;
} __attribute__((packed)) ipc_register_t;

/* ACK payload (IPC_TYPE_ACK) */
typedef struct {
    int32_t  code;          /* 0 = success, <0 = error */
    char     message[64];
} __attribute__((packed)) ipc_ack_t;

/* Waterball payload (IPC_TYPE_WMSG_SEND, IPC_TYPE_WMSG_DELIVER) */
typedef struct {
    char     from_userid[IPC_USERID_LEN];
    char     to_userid[IPC_USERID_LEN];
    uint32_t from_pid;
    int32_t  from_uslot;
    int32_t  msgmode;
    uint32_t timestamp;
    char     text[IPC_TEXT_LEN];
} __attribute__((packed)) ipc_wmsg_t;

/* PubSub payload (IPC_TYPE_SUB, IPC_TYPE_UNSUB, IPC_TYPE_PUB, IPC_TYPE_LIVE_COMMENT) */
typedef struct {
    uint32_t bid;
    uint32_t timestamp;
    aidu_t   aid;
    char     text[IPC_TEXT_LEN];
} __attribute__((packed)) ipc_pubsub_t;

/* Talk payload (IPC_TYPE_TALK_REQ) */
typedef struct {
    char     to_userid[IPC_USERID_LEN];
    char     from_userid[IPC_USERID_LEN];
    uint32_t from_pid;
    int32_t  from_uslot;
    int32_t  sig;
    int32_t  is_urgent;
    char     pad[24];
} __attribute__((packed)) ipc_talk_t;

/* Chat payload (IPC_TYPE_CHAT_MSG) */
typedef struct {
    char     room[32];
    char     userid[IPC_USERID_LEN];
    char     chatid[IPC_USERID_LEN];
    uint32_t timestamp;
    char     text[IPC_TEXT_LEN];
} __attribute__((packed)) ipc_chat_t;

/* Session payload (IPC_TYPE_SESSION_*) */
typedef struct {
    uint32_t session_id;
    char     from_userid[IPC_USERID_LEN];
    char     to_userid[IPC_USERID_LEN];
    uint16_t session_type;          /* e.g. 1=TALK, 2=CHC, 3=GOMO */
    uint16_t data_len;
    char     data[256];
} __attribute__((packed)) ipc_session_t;

/* Broadcast payload (IPC_TYPE_SYS_BROADCAST) */
typedef struct {
    char     from_userid[IPC_USERID_LEN];
    uint32_t timestamp;
    char     text[IPC_TEXT_LEN];
} __attribute__((packed)) ipc_broadcast_t;

/* Service stats payload (IPC_TYPE_STATS_RESP) */
typedef struct {
    uint32_t active_clients;    /* Current active connected clients (sockets) */
    uint32_t registered_users;  /* Unique registered userids */
    uint32_t active_sessions;   /* Registered user sessions (by pid) */
    uint32_t active_topics;     /* Active pubsub topics */
    uint32_t active_chess;      /* Active chess games */
    uint32_t active_xchat;      /* Active xchat bridge connections */
    uint64_t total_wmsg;        /* Total wmsgs routed */
    uint64_t total_talk;        /* Total talk requests routed */
    uint64_t total_live;        /* Total live comments delivered */
    uint32_t uptime_sec;        /* Service uptime in seconds */
    char     pad[24];
} __attribute__((packed)) ipc_stats_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ipc_reconnect_cb_t)(int new_fd);

int  ipc_connect(void);
void ipc_disconnect(void);
int  ipc_is_connected(void);
int  ipc_get_fd(void);
void ipc_set_reconnect_callback(ipc_reconnect_cb_t cb);
int  ipc_reconnect_enabled(void);
int  ipc_try_reconnect(void);
int  ipc_check_reconnect(void);
int  ipc_register(const char *userid, pid_t pid, int uslot);
int  ipc_has_pending(void);
int  ipc_wmsg_send(const char *to_userid, const char *from_userid,
                   pid_t from_pid, int from_uslot, int msgmode, const char *text);
int  ipc_talk_send(const char *to_userid, const char *from_userid,
                   pid_t from_pid, int from_uslot, int sig, int is_urgent);
int  ipc_sub(uint32_t bid, aidu_t aid);
int  ipc_unsub(uint32_t bid, aidu_t aid);
int  ipc_pub(uint32_t bid, aidu_t aid, const char *text);
int  ipc_broadcast(const char *from_userid, const char *text);
int  ipc_read_packet(int fd, ipc_hdr_t *hdr, void *payload_buf, size_t bufsz);
int  ipc_read_ack(int fd, uint32_t expected_seq, int timeout_ms);
int  ipc_query_stats(ipc_stats_t *stats);

int  ipc_chess_create_session(const char *caller, const char *callee, int sig);
int  ipc_chess_join_session(const char *callee, const char *caller);

#ifdef __cplusplus
}
#endif

#endif /* _IPC_H_ */
