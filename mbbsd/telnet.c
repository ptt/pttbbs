#include "bbs.h"

static TelnetCtx telnet_ctx;
static char		raw_connection = 0;

#ifdef DETECT_CLIENT
extern void UpdateClientCode(unsigned char c);

void telnet_cb_update_client_code(void *ccctx, unsigned char c)
{
    UpdateClientCode(c);
}
#endif

const static struct TelnetCallback telnet_callback = {
    term_resize,
#ifdef DETECT_CLIENT
    telnet_cb_update_client_code,
#else
    NULL,
#endif
};

void
telnet_init(void)
{
    int fd = 0;
    TelnetCtx *ctx = &telnet_ctx;
    raw_connection = 1;
    telnet_ctx_init(ctx, &telnet_callback, fd);
#ifdef DETECT_CLIENT
    telnet_ctx_set_ccctx(ctx, (void*)1);
#endif
    telnet_send_init_cmds(fd);
}

/* tty_read
 * read from tty, process telnet commands if raw connection.
 * return: >0 = length, <=0 means read more, abort/eof is automatically processed.
 */
ssize_t
tty_read(unsigned char *buf, size_t max)
{
    ssize_t l = read(0, buf, max);
    TelnetCtx *ctx = &telnet_ctx;

    if(l == 0 || (l < 0 && !(errno == EINTR || errno == EAGAIN)))
	abort_bbs(0);

    if(!raw_connection)
	return l;

    l = telnet_process(ctx, buf, l);
    return l;
}

void
telnet_turnoff_client_detect(void)
{
    TelnetCtx *ctx = &telnet_ctx;
    telnet_ctx_set_ccctx(ctx, NULL);
}

// vim: sw=4 
