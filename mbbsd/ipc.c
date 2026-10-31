#include "bbs.h"
#include "ipc.h"

int
mbbsd_ipc_handler(const ipc_hdr_t *hdr, const void *payload)
{
    if (!hdr)
        return 0;

    switch (hdr->type) {
    case IPC_TYPE_WMSG_DELIVER:
        return wmsg_on_deliver((const ipc_wmsg_t *)payload);

    case IPC_TYPE_LIVE_COMMENT: {
        const ipc_pubsub_t *ps = (const ipc_pubsub_t *)payload;
        if (!ps)
            return 0;
        /* Queued by the live provider, which posts VKEY_EV_PUBSUB for the
         * live loop; see more.c. */
        more_on_live_comment(ps->bid, aidu_raw(ps->aid), ps->text, ps->fsize);
        return 0;
    }

    case IPC_TYPE_TALK_REQ:
        return talk_on_request((const ipc_talk_t *)payload);

    case IPC_TYPE_SYS_BROADCAST: {
        if (payload) {
            const ipc_broadcast_t *b = (const ipc_broadcast_t *)payload;
            outmsg(b->text);
            bell();
        }
        return 1;
    }

    default:
        return 0;
    }
}

static void
mbbsd_ipc_reconnect_cb(int new_fd GCC_UNUSED)
{
    if (currutmp && currutmp->userid[0]) {
        if (ipc_register(currutmp->userid, currutmp->pid,
                         get_utmp_slot(currutmp)) != 0) {
            /* Not registered (timeout / rejected / I/O error): an
             * unregistered link silently loses waterballs and talk requests,
             * so drop it and let the library retry with backoff. The register
             * call may already have closed the fd itself. */
            if (ipc_is_connected())
                ipc_disconnect();
            vkey_sync_ipc();
            return;
        }
    }
    vkey_sync_ipc();
    vkey_set_ipc_handler(mbbsd_ipc_handler);
    more_live_resubscribe();
}

void
ipc_init(const char *userid, pid_t pid, int uslot)
{
    if (!userid || !*userid || pid <= 0)
        return;

    ipc_set_reconnect_callback(mbbsd_ipc_reconnect_cb);

    int fd = ipc_connect();
    if (fd >= 0) {
        if (ipc_register(userid, pid, uslot) == 0) {
            vkey_attach_ipc(fd);
            vkey_set_ipc_handler(mbbsd_ipc_handler);
        } else if (ipc_is_connected()) {
            ipc_disconnect();
        }
    } else {
        ipc_disconnect();
    }
}
