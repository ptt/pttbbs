#include "bbs.h"

static TelnetCtx telnet_ctx;
static char		raw_connection = 0;

#ifdef DETECT_CLIENT
extern void UpdateClientCode(unsigned char c);

static void 
telnet_cb_update_client_code(void *cc_arg, unsigned char c)
{
    UpdateClientCode(c);
}
#endif

static void 
telnet_cb_resize_term(void *resize_arg, int w, int h)
{
    term_resize(w, h);
}

const static struct TelnetCallback telnet_callback = {
    telnet_cb_resize_term,
#ifdef DETECT_CLIENT
    telnet_cb_update_client_code,
#else
    NULL,
#endif
};

void
telnet_init(int do_init_cmd)
{
    int fd = 0;
    TelnetCtx *ctx = &telnet_ctx;
    raw_connection = 1;
    telnet_ctx_init(ctx, &telnet_callback, fd);
#ifdef DETECT_CLIENT
    telnet_ctx_set_cc_arg(ctx, (void*)1);
#endif
    if (do_init_cmd)
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
    telnet_ctx_set_cc_arg(ctx, NULL);
}

// vim: sw=4 
