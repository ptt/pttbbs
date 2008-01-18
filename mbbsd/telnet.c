/*
 * piaip's simplified implementation of TELNET protocol
 */
#ifdef DEBUG
#define TELOPTS
#define TELCMDS
#endif

#include "bbs.h"

#ifdef DETECT_CLIENT
void UpdateClientCode(unsigned char c); // see mbbsd.c
#endif

unsigned int telnet_handler(unsigned char c) ;
void	telnet_init(void);
ssize_t tty_read(unsigned char *buf, size_t max);

enum TELNET_IAC_STATES {
	IAC_NONE,
	IAC_COMMAND,
	IAC_WAIT_OPT,
	IAC_WAIT_SE,
	IAC_PROCESS_OPT,
	IAC_ERROR
};

static unsigned char iac_state = 0; /* as byte to reduce memory */

#define TELNET_IAC_MAXLEN (16)
/* We don't reply to most commands, so this maxlen can be minimal.
 * Warning: if you want to support ENV passing or other long commands,
 * remember to increase this value. Howver, some poorly implemented
 * terminals like xxMan may not follow the protocols and user will hang
 * on those terminals when IACs were sent.
 */

void
telnet_init(void)
{
    /* We are the boss. We don't respect to client.
     * It's client's responsibility to follow us.
     * Please write these codes in i-dont-care opt handlers.
     */
    const char telnet_init_cmds[] = {
	/* retrieve terminal type and throw away.
	 * why? because without this, clients enter line mode.
	 */
	IAC, DO, TELOPT_TTYPE,
	IAC, SB, TELOPT_TTYPE, TELQUAL_SEND, IAC, SE,

	/* i'm a smart term with resize ability. */
	IAC, DO, TELOPT_NAWS,

	/* i will echo. */
	IAC, WILL, TELOPT_ECHO,
	/* supress ga. */
	IAC, WILL, TELOPT_SGA,
	/* 8 bit binary. */
	IAC, WILL, TELOPT_BINARY,
	IAC, DO,   TELOPT_BINARY,
    };

    raw_connection = 1;
    write(0, telnet_init_cmds, sizeof(telnet_init_cmds));
}

/* tty_read
 * read from tty, process telnet commands if raw connection.
 * return: >0 = length, <=0 means read more, abort/eof is automatically processed.
 */
ssize_t
tty_read(unsigned char *buf, size_t max)
{
    ssize_t l = read(0, buf, max);

    if(l == 0 || (l < 0 && !(errno == EINTR || errno == EAGAIN)))
	abort_bbs(0);

    if(!raw_connection)
	return l;

    /* process buffer */
    if (l > 0) {
	unsigned char *buf2 = buf;
	size_t i = 0, i2 = 0;

	/* prescan. because IAC is rare, 
	 * this cost is worthy. */
	if (iac_state == IAC_NONE && memchr(buf, IAC, l) == NULL)
	    return l;

	/* we have to look into the buffer. */
	for (i = 0; i < l; i++, buf++)
	    if(telnet_handler(*buf) == 0)
		*(buf2++) = *buf;
	    else
		i2 ++;
	l = (i2 == l) ? -1L : l - i2;
    }
    return l;
}

#ifdef DBG_OUTRPT
extern unsigned char fakeEscape;
#endif // DBG_OUTRPT

/* input:  raw character
 * output: telnet command if c was handled, otherwise zero.
 */
unsigned int 
telnet_handler(unsigned char c) 
{
    static unsigned char iac_quote = 0; /* as byte to reduce memory */
    static unsigned char iac_opt_req = 0;

    static unsigned char iac_buf[TELNET_IAC_MAXLEN];
    static unsigned int  iac_buflen = 0;

    /* we have to quote all IACs. */
    if(c == IAC && !iac_quote) {
	iac_quote = 1;
	return NOP;
    }

#ifdef DETECT_CLIENT
    /* hash client telnet sequences */
    if(cuser.userid[0]==0) {
	if(iac_state == IAC_WAIT_SE) {
	    // skip suboption
	} else {
	    if(iac_quote)
		UpdateClientCode(IAC);
	    UpdateClientCode(c);
	}
    }
#endif

    /* a special case is the top level iac. otherwise, iac is just a quote. */
    if (iac_quote) {
	if(iac_state == IAC_NONE)
	    iac_state = IAC_COMMAND;
	if(iac_state == IAC_WAIT_SE && c == SE)
	    iac_state = IAC_PROCESS_OPT;
	iac_quote = 0;
    }

    /* now, let's process commands by state */
    switch(iac_state) {

	case IAC_NONE:
	    return 0;

	case IAC_COMMAND:
#if 0 // def DEBUG
	    {
		int cx = c; /* to make compiler happy */
		write(0, "-", 1);
		if(TELCMD_OK(cx))
		    write(0, TELCMD(c), strlen(TELCMD(c)));
		write(0, " ", 1);
	    }
#endif
	    iac_state = IAC_NONE; /* by default we restore state. */
	    switch(c) {
		case IAC:
		    // return 0;
		    // we don't want to allow IACs as input.
		    return 1;

		/* we don't want to process these. or maybe in future. */
		case BREAK:           /* break */
#ifdef DBG_OUTRPT
		    fakeEscape = !fakeEscape;
		    return NOP;
#endif

		case ABORT:           /* Abort process */
		case SUSP:            /* Suspend process */
		case AO:              /* abort output--but let prog finish */
		case IP:              /* interrupt process--permanently */
		case EOR:             /* end of record (transparent mode) */
		case DM:              /* data mark--for connect. cleaning */
		case xEOF:            /* End of file: EOF is already used... */
		    return NOP;

		case NOP:             /* nop */
		    return NOP;

		/* we should process these, but maybe in future. */
		case GA:              /* you may reverse the line */
		case EL:              /* erase the current line */
		case EC:              /* erase the current character */
		    return NOP;

		/* good */
		case AYT:             /* are you there */
		    {
			    const char *alive = "I'm still alive, loading: ";
			    char buf[STRLEN];

			    /* respond as fast as we can */
			    write(0, alive, strlen(alive));
			    cpuload(buf);
			    write(0, buf, strlen(buf));
			    write(0, "\r\n", 2);
		    }
		    return NOP;

		case DONT:            /* you are not to use option */
		case DO:              /* please, you use option */
		case WONT:            /* I won't use option */
		case WILL:            /* I will use option */
		    iac_opt_req = c;
		    iac_state = IAC_WAIT_OPT;
		    return NOP;

		case SB:              /* interpret as subnegotiation */
		    iac_state = IAC_WAIT_SE;
		    iac_buflen = 0;
		    return NOP;

		case SE:              /* end sub negotiation */
		default:
		    return NOP;
	    }
	    return 1;

	case IAC_WAIT_OPT:
#if 0 // def DEBUG
	    write(0, "-", 1);
	    if(TELOPT_OK(c))
		write(0, TELOPT(c), strlen(TELOPT(c)));
	    write(0, " ", 1);
#endif
	    iac_state = IAC_NONE;
	    /*
	     * According to RFC, there're some tricky steps to prevent loop.
	     * However because we have a poor term which does not allow 
	     * most abilities, let's be a strong boss here.
	     *
	     * Although my old imeplementation worked, it's even better to follow this:
	     * http://www.tcpipguide.com/free/t_TelnetOptionsandOptionNegotiation-3.htm
	     */
	    switch(c) {
		/* i-dont-care: i don't care about what client is. 
		 * these should be clamed in init and
		 * client must follow me. */
		case TELOPT_TTYPE:	/* termtype or line. */
		case TELOPT_NAWS:       /* resize terminal */
		case TELOPT_SGA:	/* supress GA */
		case TELOPT_ECHO:       /* echo */
		case TELOPT_BINARY:	/* we are CJK. */
		    break;

		/* i-dont-agree: i don't understand/agree these.
		 * according to RFC, saying NO stopped further
		 * requests so there'll not be endless loop. */
		case TELOPT_RCP:         /* prepare to reconnect */
		default:
		    if (iac_opt_req == WILL || iac_opt_req == DO)
		    {
			/* unknown option, reply with won't */
			unsigned char cmd[3] = { IAC, DONT, 0 };
			if(iac_opt_req == DO) cmd[1] = WONT;
			cmd[2] = c;
			write(0, cmd, sizeof(cmd));
		    }
		    break;
	    }
	    return 1;

	case IAC_WAIT_SE:
	    iac_buf[iac_buflen++] = c;
	    /* no need to convert state because previous quoting will do. */
		    
	    if(iac_buflen == TELNET_IAC_MAXLEN) {
		/* may be broken protocol?
		 * whether finished or not, break for safety 
		 * or user may be frozen.
		 */
		iac_state = IAC_NONE;
		return 0;
	    }
	    return 1;

	case IAC_PROCESS_OPT:
	    iac_state = IAC_NONE;
#if 0 // def DEBUG
	    write(0, "-", 1);
	    if(TELOPT_OK(iac_buf[0]))
		write(0, TELOPT(iac_buf[0]), strlen(TELOPT(iac_buf[0])));
	    write(0, " ", 1);
#endif
	    switch(iac_buf[0]) {

		/* resize terminal */
		case TELOPT_NAWS:
		    {
			int w = (iac_buf[1] << 8) + (iac_buf[2]);
			int h = (iac_buf[3] << 8) + (iac_buf[4]);
			term_resize(w, h);
#ifdef DETECT_CLIENT
			if(cuser.userid[0]==0) {
			    UpdateClientCode(iac_buf[0]);
			    if(w==80 && h==24)
				UpdateClientCode(1);
			    else if(w==80)
				UpdateClientCode(2);
			    else if(h==24)
				UpdateClientCode(3);
			    else
				UpdateClientCode(4);
			    UpdateClientCode(IAC);
			    UpdateClientCode(SE);
			}
#endif
		    }
		    break;

		default:
#ifdef DETECT_CLIENT
		    if(cuser.userid[0]==0) {
			int i;
			for(i=0;i<iac_buflen;i++)
			    UpdateClientCode(iac_buf[i]);
			UpdateClientCode(IAC);
			UpdateClientCode(SE);
		    }
#endif
		    break;
	    }
	    return 1;
    }
    return 1; /* never reached */
}

// vim: sw=4 
