#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

#include "ipc.h"
#include "cmbbs.h"

static int wait_readable(int fd, int timeout_ms) {
    struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
    int r = poll(&pfd, 1, timeout_ms);
    return (r > 0 && (pfd.revents & POLLIN)) ? 1 : 0;
}

static const uint32_t s_test_bid = 10;
static const aidu_t   s_test_aid = 0x1730000000ULL;

static void test_client_reconnect_cb(int new_fd) {
    (void)new_fd;
    ipc_register("alice", 11111, 10);
    ipc_sub(s_test_bid, s_test_aid);
}

int main(void) {
    printf("[test_ipc] Starting C client integration test...\n");

    /* Connect client Alice */
    int fd_alice = ipc_connect();
    if (fd_alice < 0) {
        fprintf(stderr, "[test_ipc] Failed to connect to ipc.svc (is it running?)\n");
        return 1;
    }
    printf("[test_ipc] Alice connected (fd=%d)\n", fd_alice);

    int r = ipc_register("alice", 11111, 10);
    assert(r == 0);
    printf("[test_ipc] Alice registered\n");

    /* Second connection for Bob: use raw connect or toconnect */
    const char *sock_path = GET_SVC_SOCKET(ipc);
    int fd_bob = toconnect3(sock_path, 0, 50000);
    assert(fd_bob >= 0);
    printf("[test_ipc] Bob connected (fd=%d)\n", fd_bob);

    /* Bob register */
    ipc_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = IPC_MAGIC;
    hdr.version = IPC_VERSION;
    hdr.type = IPC_TYPE_REGISTER;
    hdr.seq = 1;
    hdr.len = sizeof(ipc_register_t);

    ipc_register_t reg_bob;
    memset(&reg_bob, 0, sizeof(reg_bob));
    strncpy(reg_bob.userid, "bob", sizeof(reg_bob.userid) - 1);
    reg_bob.pid = 22222;
    reg_bob.uslot = 20;

    assert(send(fd_bob, &hdr, sizeof(hdr), 0) == sizeof(hdr));
    assert(send(fd_bob, &reg_bob, sizeof(reg_bob), 0) == sizeof(reg_bob));

    /* Read Bob's ACK */
    assert(wait_readable(fd_bob, 1000));
    ipc_hdr_t ack_hdr;
    ipc_ack_t ack_payload;
    assert(ipc_read_packet(fd_bob, &ack_hdr, &ack_payload, sizeof(ack_payload)) > 0);
    assert(ack_hdr.type == IPC_TYPE_ACK);
    assert(ack_payload.code == 0);
    printf("[test_ipc] Bob registered\n");

    /* Test 1: Waterball Alice -> Bob */
    printf("[test_ipc] Test 1: Alice sends waterball to Bob...\n");
    r = ipc_wmsg_send("bob", "alice", 11111, 10, 0, "Hello Bob! This is waterball from Alice!");
    assert(r == 0);

    /* Bob receives waterball */
    assert(wait_readable(fd_bob, 1000));
    char buf[1024];
    int n = ipc_read_packet(fd_bob, &hdr, buf, sizeof(buf));
    assert(n > 0);
    assert(hdr.type == IPC_TYPE_WMSG_DELIVER);
    ipc_wmsg_t *w = (ipc_wmsg_t *)buf;
    printf("[test_ipc] Bob received wmsg from %s (pid=%d): '%s'\n", w->from_userid, w->from_pid, w->text);
    assert(strcmp(w->from_userid, "alice") == 0);
    assert(w->from_pid == 11111);
    assert(strcmp(w->text, "Hello Bob! This is waterball from Alice!") == 0);
    printf("[test_ipc] Test 1 PASS: Waterball routed successfully!\n");

    /* Test 2: Live Post PubSub */
    printf("[test_ipc] Test 2: PubSub Live post target...\n");
    uint32_t topic_bid = 1;
    aidu_t topic_aid = 0x12345678ULL;

    /* Bob subscribes to target */
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = IPC_MAGIC;
    hdr.version = IPC_VERSION;
    hdr.type = IPC_TYPE_SUB;
    hdr.seq = 2;
    hdr.len = sizeof(ipc_pubsub_t);

    ipc_pubsub_t ps_bob;
    memset(&ps_bob, 0, sizeof(ps_bob));
    ps_bob.bid = topic_bid;
    ps_bob.aid = topic_aid;

    assert(send(fd_bob, &hdr, sizeof(hdr), 0) == sizeof(hdr));
    assert(send(fd_bob, &ps_bob, sizeof(ps_bob), 0) == sizeof(ps_bob));

    assert(wait_readable(fd_bob, 1000));
    assert(ipc_read_packet(fd_bob, &ack_hdr, &ack_payload, sizeof(ack_payload)) > 0);
    assert(ack_hdr.type == IPC_TYPE_ACK);
    assert(ack_payload.code == 0);
    printf("[test_ipc] Bob subscribed to bid=%u, aid=0x%llx\n", topic_bid, (unsigned long long)topic_aid);

    /* Alice publishes comment to target */
    const char *comment = "推 user123: 這是一個真live！";
    r = ipc_pub(topic_bid, topic_aid, comment);
    assert(r == 0);
    printf("[test_ipc] Alice published comment to target\n");

    /* Bob receives live comment */
    assert(wait_readable(fd_bob, 1000));
    n = ipc_read_packet(fd_bob, &hdr, buf, sizeof(buf));
    assert(n > 0);
    assert(hdr.type == IPC_TYPE_LIVE_COMMENT);
    ipc_pubsub_t *ps_recv = (ipc_pubsub_t *)buf;
    printf("[test_ipc] Bob received LIVE_COMMENT on bid=%u, aid=0x%llx: '%s'\n", ps_recv->bid, (unsigned long long)ps_recv->aid, ps_recv->text);
    assert(ps_recv->bid == topic_bid);
    assert(ps_recv->aid == topic_aid);
    assert(strcmp(ps_recv->text, comment) == 0);
    printf("[test_ipc] Test 2 PASS: Live comment delivered via PubSub!\n");

    /* Test 3: Bob unsubs, Alice publishes, Bob should NOT receive */
    printf("[test_ipc] Test 3: Unsubscribe test...\n");
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = IPC_MAGIC;
    hdr.version = IPC_VERSION;
    hdr.type = IPC_TYPE_UNSUB;
    hdr.seq = 3;
    hdr.len = sizeof(ipc_pubsub_t);
    assert(send(fd_bob, &hdr, sizeof(hdr), 0) == sizeof(hdr));
    assert(send(fd_bob, &ps_bob, sizeof(ps_bob), 0) == sizeof(ps_bob));

    assert(wait_readable(fd_bob, 1000));
    assert(ipc_read_packet(fd_bob, &ack_hdr, &ack_payload, sizeof(ack_payload)) > 0);
    assert(ack_hdr.type == IPC_TYPE_ACK);
    assert(ack_payload.code == 0);
    printf("[test_ipc] Bob unsubscribed from bid=%u, aid=0x%llx\n", topic_bid, (unsigned long long)topic_aid);

    r = ipc_pub(topic_bid, topic_aid, "推 user456: 這句不該被收到");
    assert(r == 0);

    /* Bob should timeout and NOT receive anything */
    int readable = wait_readable(fd_bob, 200);
    assert(readable == 0);
    printf("[test_ipc] Test 3 PASS: No comment received after unsub!\n");

    /* Test 4: Talk Request via IPC (Alice pages Bob) */
    printf("[test_ipc] Test 4: Talk request via ipc.svc...\n");
    r = ipc_talk_send("bob", "alice", 11111, 10, 1 /* SIG_TALK */, 0);
    assert(r == 0);

    /* Bob receives talk request */
    assert(wait_readable(fd_bob, 1000));
    n = ipc_read_packet(fd_bob, &hdr, buf, sizeof(buf));
    assert(n > 0);
    assert(hdr.type == IPC_TYPE_TALK_REQ);
    ipc_talk_t *t_recv = (ipc_talk_t *)buf;
    printf("[test_ipc] Bob received TALK_REQ from '%s' (pid=%d, uslot=%d, sig=%d)\n",
           t_recv->from_userid, t_recv->from_pid, t_recv->from_uslot, t_recv->sig);
    assert(strcmp(t_recv->from_userid, "alice") == 0);
    assert(t_recv->from_pid == 11111);
    assert(t_recv->from_uslot == 10);
    assert(t_recv->sig == 1);
    printf("[test_ipc] Test 4 PASS: Talk request routed successfully via ipc.svc!\n");

    /* Test 5: XChat connect via ipc_chat_connect */
    printf("[test_ipc] Test 5: Connect to XChat via ipc_chat_connect()...\n");
    int fd_chat = ipc_chat_connect();
    assert(fd_chat >= 0);
    /* Handshake with xchatd backend via bridge */
    const char *chat_login = "/! PTTSYSOP guest\n";
    assert(send(fd_chat, chat_login, strlen(chat_login), 0) == (ssize_t)strlen(chat_login));
    char chat_resp[32] = {0};
    ssize_t rn = recv(fd_chat, chat_resp, sizeof(chat_resp) - 1, 0);
    assert(rn >= 3);
    assert(memcmp(chat_resp, "OK\0", 3) == 0);
    close(fd_chat);
    printf("[test_ipc] Test 5 PASS: XChat server verified via ipc_chat_connect()!\n");

    /* Test 6: Chess Session via ipc_chess_create_session and ipc_chess_join_session */
    printf("[test_ipc] Test 6: Chess session broker via ipc.svc...\n");
    int fd_chess_alice = ipc_chess_create_session("alice", "bob", 3 /* SIG_GOMO */);
    assert(fd_chess_alice >= 0);

    int fd_chess_bob = ipc_chess_join_session("bob", "alice");
    assert(fd_chess_bob >= 0);

    /* Alice should receive "READY\n" broker acknowledgement */
    assert(wait_readable(fd_chess_alice, 1000));
    char okbuf[16];
    ssize_t okn = read(fd_chess_alice, okbuf, sizeof(okbuf));
    assert(okn > 0);
    assert(strncmp(okbuf, "READY", 5) == 0);

    /* Alice writes "T" (traditional time limit mode) */
    assert(write(fd_chess_alice, "T", 1) == 1);
    assert(wait_readable(fd_chess_bob, 1000));
    char mode_ch = 0;
    assert(read(fd_chess_bob, &mode_ch, 1) == 1);
    assert(mode_ch == 'T');

    /* Bob writes "y" (accept) */
    assert(write(fd_chess_bob, "y", 1) == 1);
    assert(wait_readable(fd_chess_alice, 1000));
    char ans_ch = 0;
    assert(read(fd_chess_alice, &ans_ch, 1) == 1);
    assert(ans_ch == 'y');

    /* Test move packet exchange */
    char move_a[3] = { 7, 7, 1 };
    assert(write(fd_chess_alice, move_a, 3) == 3);
    assert(wait_readable(fd_chess_bob, 1000));
    char move_recv[3] = { 0 };
    assert(read(fd_chess_bob, move_recv, 3) == 3);
    assert(memcmp(move_a, move_recv, 3) == 0);

    close(fd_chess_alice);
    close(fd_chess_bob);
    printf("[test_ipc] Test 6a PASS: Chess normal handshake & moves verified!\n");

    /* Test 6b: Caller cancels / disconnects during handshake (Ctrl-D simulation) */
    printf("[test_ipc] Test 6b: Caller cancels during handshake (Ctrl-D)...\n");
    fd_chess_alice = ipc_chess_create_session("alice", "bob", 3);
    assert(fd_chess_alice >= 0);
    fd_chess_bob = ipc_chess_join_session("bob", "alice");
    assert(fd_chess_bob >= 0);

    /* Alice receives READY */
    assert(wait_readable(fd_chess_alice, 1000));
    okn = read(fd_chess_alice, okbuf, sizeof(okbuf));
    assert(okn > 0 && strncmp(okbuf, "READY", 5) == 0);

    /* Alice cancels by closing her socket (Ctrl-D) without sending mode */
    close(fd_chess_alice);

    /* Bob must receive EOF immediately on mode read (must not hang) */
    assert(wait_readable(fd_chess_bob, 1000));
    mode_ch = 0;
    ssize_t mn = read(fd_chess_bob, &mode_ch, 1);
    assert(mn <= 0); /* EOF received */
    close(fd_chess_bob);
    printf("[test_ipc] Test 6b PASS: Callee received EOF immediately when caller cancelled!\n");

    /* Test 6c: Caller cancels before Callee joins (Ctrl-D before JOIN) */
    printf("[test_ipc] Test 6c: Caller cancels before callee joins...\n");
    fd_chess_alice = ipc_chess_create_session("alice", "bob", 3);
    assert(fd_chess_alice >= 0);

    /* Alice cancels immediately */
    close(fd_chess_alice);

    /* Bob attempts to join */
    fd_chess_bob = ipc_chess_join_session("bob", "alice");
    assert(fd_chess_bob < 0); /* Must fail cleanly with -1 */
    printf("[test_ipc] Test 6c PASS: Callee cleanly failed when caller cancelled before join!\n");
    printf("[test_ipc] Test 6 PASS: All Chess broker scenarios verified!\n");

    printf("[test_ipc] Test 7: Auto-Reconnect & State Restoration (via callback)...\n");
    ipc_set_reconnect_callback(test_client_reconnect_cb);
    assert(ipc_sub(s_test_bid, s_test_aid) == 0);

    /* Simulate daemon crash / disconnect for Alice */
    ipc_disconnect();
    assert(ipc_is_connected() == 0);

    /* Immediately checking reconnect should be prevented by backoff timer */
    assert(ipc_check_reconnect() < 0);
    assert(ipc_is_connected() == 0);

    /* Wait for backoff timer (initial 1s + jitter) to elapse */
    sleep(2);

    /* Reconnect should now succeed and restore credentials & subscriptions! */
    int new_fd = ipc_check_reconnect();
    assert(new_fd >= 0);
    assert(ipc_is_connected() == 1);

    /* Bob publishes to that target; Alice should receive it because subscription was restored */
    ipc_hdr_t pub_hdr = {
        .magic = IPC_MAGIC,
        .version = IPC_VERSION,
        .type = IPC_TYPE_PUB,
        .seq = 99,
        .len = sizeof(ipc_pubsub_t),
    };
    ipc_pubsub_t pub_ps;
    memset(&pub_ps, 0, sizeof(pub_ps));
    pub_ps.bid = s_test_bid;
    pub_ps.aid = s_test_aid;
    STRLCPY(pub_ps.text, "自動重連訂閱復原測試");
    assert(send(fd_bob, &pub_hdr, sizeof(pub_hdr), 0) == sizeof(pub_hdr));
    assert(send(fd_bob, &pub_ps, sizeof(pub_ps), 0) == sizeof(pub_ps));

    /* Alice reads the live comment */
    assert(wait_readable(new_fd, 1000));
    char rbuf[1024];
    ipc_hdr_t rhdr;
    int rbytes = ipc_read_packet(new_fd, &rhdr, rbuf, sizeof(rbuf));
    assert(rbytes > 0);
    assert(rhdr.type == IPC_TYPE_LIVE_COMMENT);
    ipc_pubsub_t *rps = (ipc_pubsub_t *)rbuf;
    assert(rps->bid == s_test_bid);
    assert(rps->aid == s_test_aid);
    assert(strcmp(rps->text, "自動重連訂閱復原測試") == 0);
    printf("[test_ipc] Test 7 PASS: Reconnected with backoff, state and topic subscriptions restored!\n");

    /* Test 8: Query Active Connections and Service Stats */
    printf("[test_ipc] Test 8: Query active connections and service stats...\n");
    ipc_stats_t stats;
    int sr = ipc_query_stats(&stats);
    assert(sr == 0);
    printf("[test_ipc] Stats response:\n");
    printf("  active_clients:   %u\n", stats.active_clients);
    printf("  registered_users: %u\n", stats.registered_users);
    printf("  active_sessions:  %u\n", stats.active_sessions);
    printf("  active_topics:    %u\n", stats.active_topics);
    printf("  total_wmsg:       %lu\n", (unsigned long)stats.total_wmsg);
    printf("  total_talk:       %lu\n", (unsigned long)stats.total_talk);
    printf("  total_live:       %lu\n", (unsigned long)stats.total_live);
    printf("  uptime_sec:       %u\n", stats.uptime_sec);
    assert(stats.active_clients >= 2);
    assert(stats.registered_users >= 2);
    assert(stats.active_sessions >= 2);
    assert(stats.total_wmsg >= 1);
    assert(stats.total_talk >= 1);
    assert(stats.total_live >= 1);
    printf("[test_ipc] Test 8 PASS: Active connection counts and stats verified!\n");

    /* Clean up */
    close(fd_bob);
    ipc_disconnect();
    printf("[test_ipc] All C client integration tests PASSED successfully!\n");
    return 0;
}
