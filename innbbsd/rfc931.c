/*
 * rfc931_user() speaks a common subset of the RFC 931, AUTH, TAP and IDENT
 * protocols. It consults an RFC 931 etc. compatible daemon on the client
 * host to look up the remote user name. The information should not be used
 * for authentication purposes.
 * 
 * Diagnostics are reported through syslog(3).
 * 
 * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
 * 
 * Inspired by the authutil package (comp.sources.unix volume 22) by Dan
 * Bernstein (brnstnd@kramden.acf.nyu.edu).
 */

#ifndef lint
static char     sccsid[] = "@(#) rfc931.c 1.4 93/03/07 22:47:52";
#endif

#include <stdio.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <signal.h>

/* #include "log_tcp.h" */

#define	RFC931_PORT	113	/* Semi-well-known port */

#ifndef RFC931_TIMEOUT
#define	RFC931_TIMEOUT	30	/* wait for at most 30 seconds */
#endif

extern char    *strchr();
extern char    *inet_ntoa();

static jmp_buf  timebuf;

/* timeout - handle timeouts */

static void 
timeout(sig)
    int             sig;
{
    longjmp(timebuf, sig);
}

/* rfc931_name - return remote user name */

char           *
my_rfc931_name(herefd, there)
    int             herefd;
    struct sockaddr_in *there;	/* remote link information */
{
    struct sockaddr_in here;	/* local link information */
    struct sockaddr_in sin;	/* for talking to RFC931 daemon */
    int             length;
    int             s;
    unsigned        remote;
    unsigned        local;
    static char     user[256];	/* XXX */
    char            buffer[512];/* YYY */
    FILE           *fp;
    char           *cp;
    char           *result = "unknown";

    /* Find out local address and port number of stdin. */

    length = sizeof(here);
    if (getsockname(herefd, (struct sockaddr *) & here, &length) == -1) {
	syslog(LOG_ERR, "getsockname: %m");
	return (result);
    }
    /*
     * The socket that will be used for user name lookups should be bound to
     * the same local IP address as stdin. This will automagically happen on
     * hosts that have only one IP network address. When the local host has
     * more than one IP network address, we must do an explicit bind() call.
     */

    if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	return (result);

    sin = here;
    sin.sin_port = 0;
    if (bind(s, (struct sockaddr *) & sin, sizeof sin) < 0) {
	syslog(LOG_ERR, "bind: %s: %m", inet_ntoa(here.sin_addr));
	return (result);
    }
    /* Set up timer so we won't get stuck. */

    signal(SIGALRM, timeout);
    if (setjmp(timebuf)) {
	close(s);		/* not: fclose(fp) */
	return (result);
    }
    alarm(RFC931_TIMEOUT);

    /* Connect to the RFC931 daemon. */

    sin = *there;
    sin.sin_port = htons(RFC931_PORT);
    if (connect(s, (struct sockaddr *) & sin, sizeof(sin)) == -1
	|| (fp = fdopen(s, "w+")) == 0) {
	close(s);
	alarm(0);
	return (result);
    }
    /*
     * Use unbuffered I/O or we may read back our own query. setbuf() must be
     * called before doing any I/O on the stream. Thanks for the reminder,
     * Paul Kranenburg <pk@cs.few.eur.nl>!
     */

    setbuf(fp, (char *)0);

    /* Query the RFC 931 server. Would 13-byte writes ever be broken up? */

    fprintf(fp, "%u,%u\r\n", ntohs(there->sin_port), ntohs(here.sin_port));
    fflush(fp);

    /*
     * Read response from server. Use fgets()/sscanf() instead of fscanf()
     * because there is no buffer for pushback. Thanks, Chris Turbeville
     * <turbo@cse.uta.edu>.
     */

    if (fgets(buffer, sizeof(buffer), fp) != 0
	&& ferror(fp) == 0 && feof(fp) == 0
	&& sscanf(buffer, "%u , %u : USERID :%*[^:]:%255s",
		  &remote, &local, user) == 3
	&& ntohs(there->sin_port) == remote
	&& ntohs(here.sin_port) == local) {
	/* Strip trailing carriage return. */

	if (cp = strchr(user, '\r'))
	    *cp = 0;
	result = user;
    }
    alarm(0);
    fclose(fp);
    return (result);
}
