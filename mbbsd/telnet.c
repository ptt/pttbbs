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
    NULL,
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
	telnet_ctx_send_init_cmds(ctx);
}

#if defined(DBCSAWARE)
ssize_t 
dbcs_detect_evil_repeats(unsigned char *buf, ssize_t l)
{
    // determine DBCS repeats by evil clients (ref: io.c)
    if (l == 2)
    {
	// XXX l=2 is dangerous. hope we are not in telnet IAC state...
	// BS:	\b
	// BS2:	\x7f
	// DEL2: Ctrl('D') (KKMan3 also treats Ctrl('D') as DBCS DEL)
	if (buf[0] != buf[1])
	    return l;

	// Note: BS/DEL behavior on different clients:
	// screen/telnet:BS=0x7F, DEL=^[3~
	// PCMan2004:    BS=0x7F, DEL=^[3~
	// KKMan3:       BS=0x1b, DEL=0x7F
	// WinXP telnet: BS=0x1b, DEL=0x7F
	if (buf[0] == '\b' ||
	    buf[0] == '\x7f' ||
	    buf[0] == Ctrl('D'))
	    return l-1;
    } 
    else if (l == 6)
    {
	// RIGHT:   ESC_CHR "OC" or ESC_CHR "[C"
	// LEFT:    ESC_CHR "OD" or ESC_CHR "[D"
	if (buf[2] != 'C' && buf[2] != 'D')
	    return l;

	if ( buf[0] == ESC_CHR &&
	    (buf[1] == '[' || buf[1] == 'O') &&
	     buf[0] == buf[3] &&
	     buf[1] == buf[4] &&
	     buf[2] == buf[5])
	    return l-3;
    } 
    else if (l == 8)
    {
	// RIGHT:   ESC_CHR "[OC"
	// LEFT:    ESC_CHR "[OD"
	// DEL:	    ESC_STR "[3~" // vt220
	if (buf[2] != '3' && buf[2] != 'O')
	    return l;

	if (buf[0] != ESC_CHR ||
	    buf[1] != '[' ||
	    buf[4] != buf[0] ||
	    buf[5] != buf[1] ||
	    buf[6] != buf[2] ||
	    buf[7] != buf[3])
	    return l;

	if (buf[2] == '3' &&
	    buf[3] == '~')
	    return l-4;

	if ( buf[2] == 'O' &&
	    (buf[3] == 'C' || buf[3] == 'D') )
	    return l-4;
    }
    return l;
}
#endif

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

#if defined(DBCSAWARE)
    if (ISDBCSAWARE() && HasUserFlag(UF_DBCS_DROP_REPEAT))
	l = dbcs_detect_evil_repeats(buf, l);
#endif

    if(!raw_connection || l <= 0)
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
