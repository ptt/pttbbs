/*
 * Usage: bbsnnrp [options] nntpserver activefile -h|? (help) -v (verbose
 * protocol transactions) -c (reset active files only; don't receive
 * articles) -r remotehost(send articles to remotehost, default=local) -p
 * port|(send articles to remotehost at port, default=7777) path(send
 * articles to local at path, default=~bbs/innd/.innbbsd) -n (don't ask
 * innbbsd server and stat articles) -w seconds (wait for seconds and run
 * infinitely, default=once)  -a max_art (maximum number of articles received
 * for a group each time) -s max_stat(maximum number of articles stated for a
 * group each time) -t stdin|nntp (default=nntp)
 */

#include "bbs.h"
#include <stdlib.h>
#include "innbbsconf.h"
#include "osdep.h"
#include <sys/mman.h>
#ifndef AIX
#include <sys/fcntl.h>
#endif
#include "bbslib.h"
#include "daemon.h"
#include "nntp.h"
#include "externs.h"

#ifndef MAX_ARTS
#define MAX_ARTS 100
#endif
#ifndef MAX_STATS
#define MAX_STATS 1000
#endif

#if  defined(__linux)
#define NO_USE_MMAP
#else
#define USE_MMAP
#endif

int             Max_Arts = MAX_ARTS;
int             Max_Stats = MAX_STATS;

typedef struct NEWSRC_T {
    char           *nameptr, *lowptr, *highptr, *modeptr;
    int             namelen, lowlen, highlen;
    ULONG           low, high;
    int             mode, subscribe;
}               newsrc_t;

typedef struct NNRP_T {
    int             nnrpfd;
    int             innbbsfd;
    FILE           *nnrpin, *nnrpout;
    FILE           *innbbsin, *innbbsout;
    char            activefile[MAXPATHLEN];
    char            rcfile[MAXPATHLEN];
    newsrc_t       *newsrc;
    char           *actpointer, *actend;
    int             actsize, actfd, actdirty;
}               nnrp_t;

typedef struct XHDR_T {
    char           *header;
    ULONG           artno;
}               xhdr_t;

xhdr_t          XHDR[MAX_ARTS];
char            LockFile[1024];

#define NNRPGroupOK NNTP_GROUPOK_VAL
#define NNRPXhdrOK NNTP_HEAD_FOLLOWS_VAL
#define NNRParticleOK NNTP_ARTICLE_FOLLOWS_VAL
#define INNBBSstatOK NNTP_NOTHING_FOLLOWS_VAL
#define INNBBSihaveOK NNTP_SENDIT_VAL
#define NNRPconnectOK NNTP_POSTOK_VAL
#define NNRPstatOK NNTP_NOTHING_FOLLOWS_VAL
#define INNBBSconnectOK NNTP_POSTOK_VAL

nnrp_t          BBSNNRP;
int writerc(nnrp_t *);
int INNBBSihave(nnrp_t *, ULONG, char *);

void 
doterm(s)
    int             s;
{
    printf("bbsnnrp terminated.  Signal %d\n", s);
    writerc(&BBSNNRP);
    if (dashf(LockFile))
	unlink(LockFile);
    exit(1);
}

extern char    *optarg;
extern int      opterr, optind;

#ifndef MIN_WAIT
#define MIN_WAIT 60
#endif

int             ResetActive = 0;
int             StatHistory = 1;
int             AskLocal = 1;
int             RunOnce = 1;

int             DefaultWait = MIN_WAIT;

char           *DefaultPort = DefaultINNBBSPort;
char           *DefaultPath = LOCALDAEMON;
char           *DefaultRemoteHost;

#ifndef MAXBUFLEN
#define MAXBUFLEN 256
#endif
char            DefaultNewsgroups[MAXBUFLEN];
char            DefaultOrganization[MAXBUFLEN];
char            DefaultModerator[MAXBUFLEN];
char            DefaultTrustfrom[MAXBUFLEN];
char            DefaultTrustFrom[MAXBUFLEN];

void
usage(arg)
    char           *arg;
{
    fprintf(stderr, "Usage: %s [options] nntpserver activefile\n", arg);
    fprintf(stderr, "       -h|? (help) \n");
    fprintf(stderr, "       -v (verbose protocol transactions)\n");
    fprintf(stderr, "       -c (reset active files only; don't receive articles)\n");
    fprintf(stderr, "       -r [proto:]remotehost\n");
    fprintf(stderr, "          (send articles to remotehost, default=ihave:local)\n");
    fprintf(stderr, "       -p port|(send articles to remotehost at port, default=%s)\n", DefaultINNBBSPort);
    fprintf(stderr, "          path(send articles to local at path, default=~bbs/innd/.innbbsd)\n");
    fprintf(stderr, "       -w seconds ( > 1 wait for seconds and run infinitely, default=once)\n");
    fprintf(stderr, "       -n (don't ask innbbsd server and stat articles)\n");
    fprintf(stderr, "       -a max_art(maximum number of articles received for a group each time)\n");
    fprintf(stderr, "          default=%d\n", MAX_ARTS);
    fprintf(stderr, "       -s max_stat(maximum number of articles stated for a group each time)\n");
    fprintf(stderr, "          default=%d\n", MAX_STATS);
    fprintf(stderr, "       -t stdin|nntp (default=nntp)\n");
    fprintf(stderr, "       -g newsgroups\n");
    fprintf(stderr, "       -m moderator\n");
    fprintf(stderr, "       -o organization\n");
    fprintf(stderr, "       -f trust_user (From: trust_user)\n");
    fprintf(stderr, "       -F trust_user (From trust_user)\n");
    fprintf(stderr, " Please E-mail bug to skhuang@csie.nctu.edu.tw or\n");
    fprintf(stderr, " post to tw.bbs.admin.installbbs\n");
}

static char    *StdinInputType = "stdin";
static char    *NntpInputType = "nntp";
static char    *NntpIhaveProtocol = "ihave";
static char    *NntpPostProtocol = "post";
static char    *DefaultNntpProtocol;

int
headbegin(buffer)
    char           *buffer;
{
    if (strncmp(buffer, "Path: ", 6) == 0) {
	if (strchr(buffer + 6, '!') != NULL)
	    return 1;
    }
    if (strncmp(buffer, "From ", 5) == 0) {
	if (strchr(buffer + 5, ':') != NULL)
	    return 1;
    }
    return 0;
}

int
stdinreadnews(bbsnnrp)
    nnrp_t         *bbsnnrp;
{
    char            buffer[4096];
    char            tmpfilename[MAXPATHLEN];
    FILE           *tmpfp = NULL;
    char            mid[1024];
    int             pathagain;
    int             ngmet, submet, midmet, pathmet, orgmet, approvedmet;
    int             discard;
    char            sending_path[MAXPATHLEN];
    int             sending_path_len = 0;

    strncpy(tmpfilename, (char *)fileglue("/tmp/bbsnnrp-stdin-%d-%d", getuid(), getpid()), sizeof tmpfilename);
    fgets(buffer, sizeof buffer, bbsnnrp->innbbsin);
    verboselog("innbbsGet: %s", buffer);
    if (atoi(buffer) != INNBBSconnectOK) {
	fprintf(stderr, "INNBBS server not OK\n");
	return;
    }
    if (DefaultNntpProtocol == NntpPostProtocol) {
	fputs("MODE READER\r\n", bbsnnrp->innbbsout);
	fflush(bbsnnrp->innbbsout);
	verboselog("innbbsPut: MODE READER\n");
	fgets(buffer, sizeof buffer, bbsnnrp->innbbsin);
	verboselog("innbbsGet: %s", buffer);
    }
    if (StatHistory == 0) {
	fputs("MIDCHECK OFF\r\n", bbsnnrp->innbbsout);
	fflush(bbsnnrp->innbbsout);
	verboselog("innbbsPut: MIDCHECK OFF\n");
	fgets(buffer, sizeof buffer, bbsnnrp->innbbsin);
	verboselog("innbbsGet: %s", buffer);
    }
    tmpfp = fopen(tmpfilename, "w");
    if (tmpfp == NULL)
	return;
    *mid = '\0';
    for (;;) {
	fprintf(stderr, "Try to read from stdin ...\n");
	ngmet = 0, submet = 0, midmet = 0, pathmet = 0, orgmet = 0, approvedmet = 0;
	discard = 0;
	while (fgets(buffer, sizeof buffer, stdin) != NULL) {
	    char           *tmpptr;
	    tmpptr = strchr(buffer, '\n');
	    if (tmpptr != NULL)
		*tmpptr = '\0';
	    if (strncasecmp(buffer, "Message-ID: ", 12) == 0) {
		strncpy(mid, buffer + 12, sizeof mid);
		midmet = 1;
	    } else if (strncmp(buffer, "Subject: ", 9) == 0) {
		submet = 1;
	    } else if (strncmp(buffer, "Path: ", 6) == 0) {
		pathmet = 1;
	    } else if (strncmp(buffer, "Organization: ", 14) == 0) {
		orgmet = 1;
	    } else if (strncmp(buffer, "Approved: ", 10) == 0) {
		approvedmet = 1;
	    } else if (strncmp(buffer, "From: ", 6) == 0 && *DefaultTrustfrom) {
		if (strstr(buffer + 6, DefaultTrustfrom) == NULL) {
		    discard = 1;
		    verboselog("Discard: %s for %s", buffer, DefaultTrustfrom);
		}
	    } else if (strncmp(buffer, "From ", 5) == 0 && *DefaultTrustFrom) {
		if (strstr(buffer + 5, DefaultTrustFrom) == NULL) {
		    discard = 1;
		    verboselog("Discard: %s for %s", buffer, DefaultTrustFrom);
		}
	    } else if (strncmp(buffer, "Received: ", 10) == 0) {
		char           *rptr = buffer + 10, *rrptr;
		int             savech, len;
		if (strncmp(buffer + 10, "from ", 5) == 0) {
		    rptr += 5;
		    rrptr = strchr(rptr, '(');
		    if (rrptr != NULL)
			rptr = rrptr + 1;
		    rrptr = strchr(rptr, ' ');
		    savech = *rrptr;
		    if (rrptr != NULL)
			*rrptr = '\0';
		} else if (strncmp(buffer + 10, "(from ", 6) == 0) {
		    rptr += 6;
		    rrptr = strchr(rptr, ')');
		    savech = *rrptr;
		    if (rrptr != NULL)
			*rrptr = '\0';
		}
		len = strlen(rptr) + 1;
		if (*rptr && sending_path_len + len < sizeof(sending_path)) {
		    if (*sending_path)
			strcat(sending_path, "!");
		    strcat(sending_path, rptr);
		    sending_path_len += len;
		}
		if (rrptr != NULL)
		    *rrptr = savech;
	    }
	    if (strncmp(buffer, "Newsgroups: ", 12) == 0) {
		if (*DefaultNewsgroups) {
		    fprintf(tmpfp, "Newsgroups: %s\r\n", DefaultNewsgroups);
		} else {
		    fprintf(tmpfp, "%s\r\n", buffer);
		}
		ngmet = 1;
	    } else {
		if (buffer[0] == '\0') {
		    if (!ngmet && *DefaultNewsgroups) {
			fprintf(tmpfp, "Newsgroups: %s\r\n", DefaultNewsgroups);
		    }
		    if (!submet) {
			fprintf(tmpfp, "Subject: (no subject)\r\n");
		    }
		    if (!pathmet) {
			fprintf(tmpfp, "Path: from-mail\r\n");
		    }
		    if (!midmet) {
			static int      seed;
			time_t          now;
			time(&now);
			fprintf(tmpfp, "Message-ID: <%d@%d.%d.%d>\r\n", now, getpid(), getuid(), seed);
			sprintf(mid, "<%d@%d.%d.%d>", now, getpid(), getuid(), seed);
			seed++;
		    }
		    if (!orgmet && *DefaultOrganization) {
			fprintf(tmpfp, "Organization: %s\r\n", DefaultOrganization);
		    }
		    if (!approvedmet && *DefaultModerator) {
			fprintf(tmpfp, "Approved: %s\r\n", DefaultModerator);
		    }
		}
		if (strncmp(buffer, "From ", 5) != 0 && strncmp(buffer, "To: ", 4) != 0) {
		    if (buffer[0] == '\0') {
			if (*sending_path) {
			    fprintf(tmpfp, "X-Sending-Path: %s\r\n", sending_path);
			}
		    }
		    fprintf(tmpfp, "%s\r\n", buffer);
		}
	    }
	    if (buffer[0] == '\0')
		break;
	}
	fprintf(stderr, "Article Body begin ...\n");
	pathagain = 0;
	while (fgets(buffer, sizeof buffer, stdin) != NULL) {
	    char           *tmpptr;
	    tmpptr = strchr(buffer, '\n');
	    if (tmpptr != NULL)
		*tmpptr = '\0';
	    if (headbegin(buffer)) {
		FILE           *oldfp = bbsnnrp->nnrpin;
		pathagain = 1;
		fputs(".\r\n", tmpfp);
		fclose(tmpfp);
		fprintf(stderr, "Try to post ...\n");
		tmpfp = fopen(tmpfilename, "r");
		bbsnnrp->nnrpin = tmpfp;
		if (!discard)
		    if (INNBBSihave(bbsnnrp, -1, mid) == -1) {
			fprintf(stderr, "post failed\n");
		    }
		bbsnnrp->nnrpin = oldfp;
		fclose(tmpfp);
		*mid = '\0';
		tmpfp = fopen(tmpfilename, "w");
		fprintf(tmpfp, "%s\r\n", buffer);
		break;
	    } else {
		fprintf(tmpfp, "%s\r\n", buffer);
	    }
	}
	if (!pathagain)
	    break;
    }
    if (!pathagain && tmpfp) {
	FILE           *oldfp = bbsnnrp->nnrpin;
	fputs(".\r\n", tmpfp);
	fclose(tmpfp);
	fprintf(stderr, "Try to post ...\n");
	tmpfp = fopen(tmpfilename, "r");
	bbsnnrp->nnrpin = tmpfp;
	if (!discard)
	    if (INNBBSihave(bbsnnrp, -1, mid) == -1) {
		fprintf(stderr, "post failed\n");
	    }
	bbsnnrp->nnrpin = oldfp;
	fclose(tmpfp);
    }
    if (dashf(tmpfilename)) {
	unlink(tmpfilename);
    }
    return 0;
}

static char    *ACT_BUF, *RC_BUF;
int             ACT_COUNT;

int
initrcfiles(bbsnnrp)
    nnrp_t         *bbsnnrp;
{
    int             actfd, i, count;
    struct stat     st;
    char           *actlistptr, *ptr;

    actfd = open(bbsnnrp->activefile, O_RDWR);
    if (actfd < 0) {
	fprintf(stderr, "can't read/write %s\n", bbsnnrp->activefile);
	exit(1);
    }
    if (fstat(actfd, &st) != 0) {
	fprintf(stderr, "can't stat %s\n", bbsnnrp->activefile);
	exit(1);
    }
    bbsnnrp->actfd = actfd;
    bbsnnrp->actsize = st.st_size;
#ifdef USE_MMAP
    bbsnnrp->actpointer = mmap(0, st.st_size, PROT_WRITE | PROT_READ,
			       MAP_SHARED, actfd, 0);
    if (bbsnnrp->actpointer == (char *)-1) {
	fprintf(stderr, "mmap error \n");
	exit(1);
    }
#else
    if (bbsnnrp->actpointer == NULL) {
	bbsnnrp->actpointer = (char *)mymalloc(st.st_size);
    } else {
	bbsnnrp->actpointer = (char *)myrealloc(bbsnnrp->actpointer, st.st_size);
    }
    if (bbsnnrp->actpointer == NULL || read(actfd, bbsnnrp->actpointer, st.st_size) <= 0) {
	fprintf(stderr, "read act error \n");
	exit(1);
    }
#endif
    bbsnnrp->actend = bbsnnrp->actpointer + st.st_size;
    i = 0, count = 0;
    for (ptr = bbsnnrp->actpointer; ptr < bbsnnrp->actend && (actlistptr = (char *)strchr(ptr, '\n')) != NULL; ptr = actlistptr + 1, ACT_COUNT++) {
	if (*ptr == '\n')
	    continue;
	if (*ptr == '#')
	    continue;
	count++;
    }
    bbsnnrp->newsrc = (newsrc_t *) mymalloc(sizeof(newsrc_t) * count);
    ACT_COUNT = 0;
    for (ptr = bbsnnrp->actpointer; ptr < bbsnnrp->actend && (actlistptr = (char *)strchr(ptr, '\n')) != NULL; ptr = actlistptr + 1) {
	register newsrc_t *rcptr;
	char           *nptr;
	/**actlistptr = '\0';*/
	if (*ptr == '\n')
	    continue;
	if (*ptr == '#')
	    continue;
	rcptr = &bbsnnrp->newsrc[ACT_COUNT];
	rcptr->nameptr = NULL;
	rcptr->namelen = 0;
	rcptr->lowptr = NULL;
	rcptr->lowlen = 0;
	rcptr->highptr = NULL;
	rcptr->highlen = 0;
	rcptr->modeptr = NULL;
	rcptr->low = 0;
	rcptr->high = 0;
	rcptr->mode = 'y';
	for (nptr = ptr; *nptr && isspace(*nptr);)
	    nptr++;
	if (nptr == actlistptr)
	    continue;
	rcptr->nameptr = nptr;
	for (nptr++; *nptr && !isspace(*nptr);)
	    nptr++;
	rcptr->namelen = (int)(nptr - rcptr->nameptr);
	if (nptr == actlistptr)
	    continue;
	for (nptr++; *nptr && isspace(*nptr);)
	    nptr++;
	if (nptr == actlistptr)
	    continue;
	rcptr->highptr = nptr;
	rcptr->high = atol(nptr);
	for (nptr++; *nptr && !isspace(*nptr);)
	    nptr++;
	rcptr->highlen = (int)(nptr - rcptr->highptr);
	if (nptr == actlistptr)
	    continue;
	for (nptr++; *nptr && isspace(*nptr);)
	    nptr++;
	if (nptr == actlistptr)
	    continue;
	rcptr->lowptr = nptr;
	rcptr->low = atol(nptr);
	for (nptr++; *nptr && !isspace(*nptr);)
	    nptr++;
	rcptr->lowlen = (int)(nptr - rcptr->lowptr);
	if (nptr == actlistptr)
	    continue;
	for (nptr++; *nptr && isspace(*nptr);)
	    nptr++;
	if (nptr == actlistptr)
	    continue;
	rcptr->mode = *nptr;
	rcptr->modeptr = nptr;
	ACT_COUNT++;
    }
    return 0;
}

int
initsockets(server, bbsnnrp, type)
    char           *server;
    nnrp_t         *bbsnnrp;
    char           *type;
{
    int             nnrpfd;
    int             innbbsfd;
    if (AskLocal) {
	innbbsfd = toconnect(DefaultPath);
	if (innbbsfd < 0) {
	    fprintf(stderr, "Connect to %s error. You may not run innbbsd\n", LOCALDAEMON);
	    /*
	     * unix connect fail, may run by inetd, try to connect to local
	     * once
	     */
	    innbbsfd = inetclient("localhost", DefaultPort, "tcp");
	    if (innbbsfd < 0) {
		exit(2);
	    }
	    close(innbbsfd);
	    /* try again */
	    innbbsfd = toconnect(DefaultPath);
	    if (innbbsfd < 0) {
		exit(3);
	    }
	}
	verboselog("INNBBS connect to %s\n", DefaultPath);
    } else {
	innbbsfd = inetclient(DefaultRemoteHost, DefaultPort, "tcp");
	if (innbbsfd < 0) {
	    fprintf(stderr, "Connect to %s at %s error. Remote Server not Ready\n", DefaultRemoteHost, DefaultPort);
	    exit(2);
	}
	verboselog("INNBBS connect to %s\n", DefaultRemoteHost);
    }
    if (type == StdinInputType) {
	bbsnnrp->nnrpfd = 0;
	bbsnnrp->innbbsfd = innbbsfd;
	if ((bbsnnrp->nnrpin = fdopen(0, "r")) == NULL ||
	    (bbsnnrp->nnrpout = fdopen(1, "w")) == NULL ||
	    (bbsnnrp->innbbsin = fdopen(innbbsfd, "r")) == NULL ||
	    (bbsnnrp->innbbsout = fdopen(innbbsfd, "w")) == NULL) {
	    fprintf(stderr, "fdopen error\n");
	    exit(3);
	}
	return;
    }
    nnrpfd = inetclient(server, "nntp", "tcp");
    if (nnrpfd < 0) {
	fprintf(stderr, " connect to %s error \n", server);
	exit(2);
    }
    verboselog("NNRP connect to %s\n", server);
    bbsnnrp->nnrpfd = nnrpfd;
    bbsnnrp->innbbsfd = innbbsfd;
    if ((bbsnnrp->nnrpin = fdopen(nnrpfd, "r")) == NULL ||
	(bbsnnrp->nnrpout = fdopen(nnrpfd, "w")) == NULL ||
	(bbsnnrp->innbbsin = fdopen(innbbsfd, "r")) == NULL ||
	(bbsnnrp->innbbsout = fdopen(innbbsfd, "w")) == NULL) {
	fprintf(stderr, "fdopen error\n");
	exit(3);
    }
    return 0;
}

int
closesockets()
{
    fclose(BBSNNRP.nnrpin);
    fclose(BBSNNRP.nnrpout);
    fclose(BBSNNRP.innbbsin);
    fclose(BBSNNRP.innbbsout);
    close(BBSNNRP.nnrpfd);
    close(BBSNNRP.innbbsfd);
    return 0;
}

void
updaterc(actptr, len, value)
    char           *actptr;
    int             len;
    ULONG           value;
{
    for (actptr += len - 1; len-- > 0;) {
	*actptr-- = value % 10 + '0';
	value /= 10;
    }
}

/*
 * if old file is empty, don't need to update prevent from disk full
 */
int
myrename(old, new)
    char           *old, *new;
{
    struct stat     st;
    if (stat(old, &st) != 0)
	return -1;
    if (st.st_size <= 0)
	return -1;
    return rename(old, new);
}

void
flushrc(bbsnnrp)
    nnrp_t         *bbsnnrp;
{
    int             backfd;
    char           *bak1;
    if (bbsnnrp->actdirty == 0)
	return;
    bak1 = (char *)strdup((char *)fileglue("%s.BAK", bbsnnrp->activefile));
    if (dashf(bak1)) {
	myrename(bak1, (char *)fileglue("%s.BAK.OLD", bbsnnrp->activefile));
    }
#ifdef USE_MMAP
    if ((backfd = open((char *)fileglue("%s.BAK", bbsnnrp->activefile), O_WRONLY | O_TRUNC | O_CREAT, 0664)) < 0 || write(backfd, bbsnnrp->actpointer, bbsnnrp->actsize) < bbsnnrp->actsize)
#else
    myrename(bbsnnrp->activefile, bak1);
    if ((backfd = open(bbsnnrp->activefile, O_WRONLY | O_TRUNC | O_CREAT, 0664)) < 0 || write(backfd, bbsnnrp->actpointer, bbsnnrp->actsize) < bbsnnrp->actsize)
#endif
    {
	char            emergent[128];
	sprintf(emergent, "/tmp/bbsnnrp.%d.active", getpid());
	fprintf(stderr, "write to backup active fail. Maybe disk full\n");
	fprintf(stderr, "try to write in %s\n", emergent);
	if ((backfd = open(emergent, O_WRONLY | O_TRUNC | O_CREAT, 0644)) < 0 || write(backfd, bbsnnrp->actpointer, bbsnnrp->actsize) < bbsnnrp->actsize) {
	    fprintf(stderr, "write to %sfail.\n", emergent);
	} else {
	    close(backfd);
	}
	/* if write fail, should leave */
	/* exit(1); */
    } else {
	close(backfd);
    }
    free(bak1);
    bbsnnrp->actdirty = 0;
}

int
writerc(bbsnnrp)
    nnrp_t         *bbsnnrp;
{
    if (bbsnnrp->actpointer) {
	flushrc(bbsnnrp);
#ifdef USE_MMAP
	if (munmap(bbsnnrp->actpointer, bbsnnrp->actsize) < 0)
	    fprintf(stderr, "can't unmap\n");
	/* free(bbsnnrp->actpointer); */
	bbsnnrp->actpointer = NULL;
#endif
	if (close(bbsnnrp->actfd) < 0)
	    fprintf(stderr, "can't close actfd\n");
    }
    return 0;
}

static FILE    *Xhdrfp;
static char     NNRPbuffer[4096];
static char     INNBBSbuffer[4096];

char           *
NNRPgets(string, len, fp)
    char           *string;
    int             len;
    FILE           *fp;
{
    char           *re = fgets(string, len, fp);
    char           *ptr;
    if (re != NULL) {
	if ((ptr = (char *)strchr(string, '\r')) != NULL)
	    *ptr = '\0';
	if ((ptr = (char *)strchr(string, '\n')) != NULL)
	    *ptr = '\0';
    }
    return re;
}

int 
NNRPstat(bbsnnrp, artno, mid)
    nnrp_t         *bbsnnrp;
    ULONG           artno;
    char          **mid;
{
    char           *ptr;
    int             code;

    *mid = NULL;
    fprintf(bbsnnrp->nnrpout, "STAT %d\r\n", artno);
    fflush(bbsnnrp->nnrpout);
    verboselog("nnrpPut: STAT %d\n", artno);
    NNRPgets(NNRPbuffer, sizeof NNRPbuffer, bbsnnrp->nnrpin);
    verboselog("nnrpGet: %s\n", NNRPbuffer);

    ptr = (char *)strchr(NNRPbuffer, ' ');
    if (ptr != NULL)
	*ptr++ = '\0';
    code = atoi(NNRPbuffer);
    ptr = (char *)strchr(ptr, ' ');
    if (ptr != NULL)
	*ptr++ = '\0';
    *mid = ptr;
    ptr = (char *)strchr(ptr, ' ');
    if (ptr != NULL)
	*ptr++ = '\0';
    return code;
}

int
NNRPxhdr(pattern, bbsnnrp, i, low, high)
    char           *pattern;
    nnrp_t         *bbsnnrp;
    int             i;
    ULONG           low, high;
{
    int code;

    Xhdrfp = bbsnnrp->nnrpin;
    fprintf(bbsnnrp->nnrpout, "XHDR %s %d-%d\r\n", pattern, low, high);
#ifdef BBSNNRPDEBUG
    printf("XHDR %s %d-%d\r\n", pattern, low, high);
#endif
    fflush(bbsnnrp->nnrpout);
    verboselog("nnrpPut: XHDR %s %d-%d\n", pattern, low, high);
    NNRPgets(NNRPbuffer, sizeof NNRPbuffer, bbsnnrp->nnrpin);
    verboselog("nnrpGet: %s\n", NNRPbuffer);
    code = atoi(NNRPbuffer);
    return code;
}

int 
NNRPxhdrget(artno, mid, iscontrol)
    int            *artno;
    char          **mid;
    int             iscontrol;
{
    *mid = NULL;
    *artno = 0;
    if (NNRPgets(NNRPbuffer, sizeof NNRPbuffer, Xhdrfp) == NULL)
	return 0;
    else {
	char           *ptr, *s;
	if (strcmp(NNRPbuffer, ".") == 0)
	    return 0;
	ptr = (char *)strchr(NNRPbuffer, ' ');
	if (!ptr)
	    return 1;
	*ptr++ = '\0';
	*artno = atol(NNRPbuffer);
	if (iscontrol) {
	    ptr = (char *)strchr(s = ptr, ' ');
	    if (!ptr)
		return 1;
	    *ptr++ = '\0';
	    if (strcmp(s, "cancel") != 0)
		return 1;
	}
	*mid = ptr;
	return 1;
    }
}

int 
INNBBSstat(bbsnnrp, i, mid)
    nnrp_t         *bbsnnrp;
    int             i;
    char           *mid;
{

    fprintf(bbsnnrp->innbbsout, "STAT %s\r\n", mid);
    fflush(bbsnnrp->innbbsout);
    verboselog("innbbsPut: STAT %s\n", mid);
    NNRPgets(INNBBSbuffer, sizeof INNBBSbuffer, bbsnnrp->innbbsin);
    verboselog("innbbsGet: %s\n", INNBBSbuffer);
    return atol(INNBBSbuffer);
}

int 
INNBBSihave(bbsnnrp, artno, mid)
    nnrp_t         *bbsnnrp;
    ULONG           artno;
    char           *mid;
{
    int             code;
    int             header = 1;

    if (DefaultNntpProtocol == NntpPostProtocol) {
	fprintf(bbsnnrp->innbbsout, "POST\r\n");
	fflush(bbsnnrp->innbbsout);
	verboselog("innbbsPut: POST %s\n", mid);
    } else {
	fprintf(bbsnnrp->innbbsout, "IHAVE %s\r\n", mid);
	fflush(bbsnnrp->innbbsout);
	verboselog("innbbsPut: IHAVE %s\n", mid);
    }
    if (NNRPgets(INNBBSbuffer, sizeof INNBBSbuffer, bbsnnrp->innbbsin) == NULL) {
	return -1;
    }
    verboselog("innbbsGet: %s\n", INNBBSbuffer);
#ifdef BBSNNRPDEBUG
    printf("ihave got %s\n", INNBBSbuffer);
#endif

    if (DefaultNntpProtocol == NntpPostProtocol) {
	if ((code = atol(INNBBSbuffer)) != NNTP_START_POST_VAL) {
	    if (code == NNTP_POSTFAIL_VAL)
		return 0;
	    else
		return -1;
	}
    } else {
	if ((code = atol(INNBBSbuffer)) != INNBBSihaveOK) {
	    if (code == 435 || code == 437)
		return 0;
	    else
		return -1;
	}
    }
    if (artno != -1) {
	fprintf(bbsnnrp->nnrpout, "ARTICLE %d\r\n", artno);
	verboselog("nnrpPut: ARTICLE %d\n", artno);
#ifdef BBSNNRPDEBUG
	printf("ARTICLE %d\r\n", artno);
#endif
	fflush(bbsnnrp->nnrpout);
	if (NNRPgets(NNRPbuffer, sizeof NNRPbuffer, bbsnnrp->nnrpin) == NULL) {
	    return -1;
	}
	verboselog("nnrpGet: %s\n", NNRPbuffer);
#ifdef BBSNNRPDEBUG
	printf("article got %s\n", NNRPbuffer);
#endif
	if (atol(NNRPbuffer) != NNRParticleOK) {
	    fputs(".\r\n", bbsnnrp->innbbsout);
	    fflush(bbsnnrp->innbbsout);
	    NNRPgets(INNBBSbuffer, sizeof INNBBSbuffer, bbsnnrp->innbbsin);
	    verboselog("innbbsGet: %s\n", INNBBSbuffer);
	    return 0;
	}
    }
    header = 1;
    while (fgets(NNRPbuffer, sizeof NNRPbuffer, bbsnnrp->nnrpin) != NULL) {
	if (strcmp(NNRPbuffer, "\r\n") == 0)
	    header = 0;
	if (strcmp(NNRPbuffer, ".\r\n") == 0) {
	    verboselog("nnrpGet: .\n");
	    fputs(NNRPbuffer, bbsnnrp->innbbsout);
	    fflush(bbsnnrp->innbbsout);
	    verboselog("innbbsPut: .\n");
	    if (NNRPgets(INNBBSbuffer, sizeof INNBBSbuffer, bbsnnrp->innbbsin) == NULL)
		return -1;
	    verboselog("innbbsGet: %s\n", INNBBSbuffer);
#ifdef BBSNNRPDEBUG
	    printf("end ihave got %s\n", INNBBSbuffer);
#endif
	    code = atol(INNBBSbuffer);
	    if (DefaultNntpProtocol == NntpPostProtocol) {
		if (code == NNTP_POSTEDOK_VAL)
		    return 1;
		if (code == NNTP_POSTFAIL_VAL)
		    return 0;
	    } else {
		if (code == 235)
		    return 1;
		if (code == 437 || code == 435)
		    return 0;
	    }
	    break;
	}
	if (DefaultNntpProtocol == NntpPostProtocol &&
	header && strncasecmp(NNRPbuffer, "NNTP-Posting-Host: ", 19) == 0) {
	    fprintf(bbsnnrp->innbbsout, "X-%s", NNRPbuffer);
	} else {
	    fputs(NNRPbuffer, bbsnnrp->innbbsout);
	}
    }
    fflush(bbsnnrp->innbbsout);
    return -1;
}

int
NNRPgroup(bbsnnrp, i, low, high)
    nnrp_t         *bbsnnrp;
    int             i;
    ULONG          *low, *high;
{
    newsrc_t       *rcptr = &bbsnnrp->newsrc[i];
    int             size, code;
    ULONG           tmp;

    fprintf(bbsnnrp->nnrpout, "GROUP %-.*s\r\n",
	    rcptr->namelen, rcptr->nameptr);
    printf("GROUP %-.*s\r\n", rcptr->namelen, rcptr->nameptr);
    verboselog("nnrpPut: GROUP %-.*s\n", rcptr->namelen, rcptr->nameptr);
    fflush(bbsnnrp->nnrpout);
    NNRPgets(NNRPbuffer, sizeof NNRPbuffer, bbsnnrp->nnrpin);
    verboselog("nnrpGet: %s\n", NNRPbuffer);
    printf("%s\n", NNRPbuffer);
    sscanf(NNRPbuffer, "%d %d %ld %ld", &code, &size, low, high);
    if (*low > *high) {
	tmp = *low;
	*low = *high;
	*high = tmp;
    }
    return code;
}


void
readnews(server, bbsnnrp)
    char           *server;
    nnrp_t         *bbsnnrp;
{
    int             i;
    char            buffer[4096];
    ULONG           low, high;

    fgets(buffer, sizeof buffer, bbsnnrp->innbbsin);
    verboselog("innbbsGet: %s", buffer);
    if (atoi(buffer) != INNBBSconnectOK) {
	fprintf(stderr, "INNBBS server not OK\n");
	return;
    }
#ifdef BBSNNRPDEBUG
    printf("%s", buffer);
#endif
    fgets(buffer, sizeof buffer, bbsnnrp->nnrpin);
    verboselog("nnrpGet: %s", buffer);
    if (buffer[0] != '2') {
	/*
	 * if (atoi(buffer) != NNRPconnectOK && atoi(buffer) !=
	 * NNTP_NOPOSTOK_VAL) {
	 */
	fprintf(stderr, "NNRP server not OK\n");
	return;
    }
#ifdef BBSNNRPDEBUG
    printf("%s", buffer);
#endif
    fputs("MODE READER\r\n", bbsnnrp->nnrpout);
    fflush(bbsnnrp->nnrpout);
    verboselog("nnrpPut: MODE READER\n");
    fgets(buffer, sizeof buffer, bbsnnrp->nnrpin);
    verboselog("nnrpGet: %s", buffer);

    if (DefaultNntpProtocol == NntpPostProtocol) {
	fputs("MODE READER\r\n", bbsnnrp->innbbsout);
	fflush(bbsnnrp->innbbsout);
	verboselog("innbbsPut: MODE READER\n");
	fgets(buffer, sizeof buffer, bbsnnrp->innbbsin);
	verboselog("innbbsGet: %s", buffer);
    }
#ifdef BBSNNRPDEBUG
    printf("%s", buffer);
#endif

    if (StatHistory == 0) {
	fputs("MIDCHECK OFF\r\n", bbsnnrp->innbbsout);
	fflush(bbsnnrp->innbbsout);
	verboselog("innbbsPut: MIDCHECK OFF\n");
	fgets(buffer, sizeof buffer, bbsnnrp->innbbsin);
	verboselog("innbbsGet: %s", buffer);
    }
    bbsnnrp->actdirty = 0;
    for (i = 0; i < ACT_COUNT; i++) {
	int             code = NNRPgroup(bbsnnrp, i, &low, &high);
	newsrc_t       *rcptr = &bbsnnrp->newsrc[i];
	ULONG           artno;
	char           *mid;
	int             artcount;

#ifdef BBSNNRPDEBUG
	printf("got reply %d %ld %ld\n", code, low, high);
#endif
	artcount = 0;
	if (code == 411) {
	    FILE           *ff = fopen(BBSHOME "/innd/log/badgroup.log", "a");
	    fprintf(ff, "%s\t%-.*s\r\n", server, rcptr->namelen, rcptr->nameptr);
	    fclose(ff);
	} else if (code == NNRPGroupOK) {
	    int             xcount;
	    ULONG           maxartno = rcptr->high;
	    int             isCancelControl = (strncmp(rcptr->nameptr, "control", rcptr->namelen) == 0)
	    ||
	    (strncmp(rcptr->nameptr, "control.cancel", rcptr->namelen) == 0);

	    /* less than or equal to high, for server renumber */
	    if (rcptr->low != low) {
		bbsnnrp->actdirty = 1;
		rcptr->low = low;
		updaterc(rcptr->lowptr, rcptr->lowlen, rcptr->low);
	    }
	    if (ResetActive) {
		if (rcptr->high != high) {
		    bbsnnrp->actdirty = 1;
		    rcptr->high = high;
		    updaterc(rcptr->highptr, rcptr->highlen, rcptr->high);
		}
	    } else if (rcptr->high < high) {
		int             xhdrcode;
		ULONG           maxget = high;
		int             exception = 0;
		if (rcptr->high < low) {
		    bbsnnrp->actdirty = 1;
		    rcptr->high = low;
		    updaterc(rcptr->highptr, rcptr->highlen, low);
		}
		if (high > rcptr->high + Max_Stats) {
		    maxget = rcptr->high + Max_Stats;
		}
		if (isCancelControl)
		    xhdrcode = NNRPxhdr("Control", bbsnnrp, i, rcptr->high + 1, maxget);
		else
		    xhdrcode = NNRPxhdr("Message-ID", bbsnnrp, i, rcptr->high + 1, maxget);

		maxartno = maxget;
		if (xhdrcode == NNRPXhdrOK) {
		    while (NNRPxhdrget(&artno, &mid, isCancelControl)) {
			/* #define DEBUG */
#ifdef DEBUG
			printf("no %d id %s\n", artno, mid);
#endif
			if (artcount < Max_Arts) {
			    if (mid != NULL && !isCancelControl) {
				if (!StatHistory || INNBBSstat(bbsnnrp, i, mid) != INNBBSstatOK) {
				    printf("** %d ** %d need it %s\n", artcount, artno, mid);
				    XHDR[artcount].artno = artno;
				    XHDR[artcount].header = restrdup(XHDR[artcount].header, mid);
				    /* INNBBSihave(bbsnnrp,i,artno,mid); */
				    /* to get it */
				    artcount++;
				}
			    } else if (mid != NULL) {
				if (INNBBSstat(bbsnnrp, i, mid) == INNBBSstatOK) {
				    printf("** %d ** %d need cancel %s\n", artcount, artno, mid);
				    XHDR[artcount].artno = artno;
				    XHDR[artcount].header = restrdup(XHDR[artcount].header, mid);
				    artcount++;
				}
			    }
			    maxartno = artno;
			}
		    }
		}		/* while xhdr OK */
		exception = 0;
		for (xcount = 0; xcount < artcount; xcount++) {
		    ULONG           artno;
		    char           *mid;
		    artno = XHDR[xcount].artno;
		    mid = XHDR[xcount].header;
		    if (isCancelControl) {
			if (NNRPstat(bbsnnrp, artno, &mid) == NNRPstatOK) {
			}
		    }
		    printf("** %d ** %d i have it %s\n", xcount, artno, mid);
		    if (!ResetActive && mid != NULL)
			exception = INNBBSihave(bbsnnrp, artno, mid);
		    if (exception == -1)
			break;
		    if (rcptr->high != artno) {
			rcptr->high = artno;
			updaterc(rcptr->highptr, rcptr->highlen, rcptr->high);
		    }
		}
		if (rcptr->high != maxartno && exception != -1) {
		    bbsnnrp->actdirty = 1;
		    rcptr->high = maxartno;
		    updaterc(rcptr->highptr, rcptr->highlen, maxartno);
		}
	    }
	}
	/*
	 * flushrc(bbsnnrp);
	 */
    }
    fprintf(bbsnnrp->innbbsout, "quit\r\n");
    fprintf(bbsnnrp->nnrpout, "quit\r\n");
    fflush(bbsnnrp->innbbsout);
    fflush(bbsnnrp->nnrpout);
    fgets(NNRPbuffer, sizeof NNRPbuffer, bbsnnrp->nnrpin);
    fgets(INNBBSbuffer, sizeof INNBBSbuffer, bbsnnrp->innbbsin);

    /*
     * bbsnnrp->newsrc[0].high = 1900; updaterc(bbsnnrp->newsrc[0].highptr,
     * bbsnnrp->newsrc[0].highlen, bbsnnrp->newsrc[0].high);
     */
}

void
INNBBSDhalt()
{
}
int
main(argc, argv)
    int             argc;
    char          **argv;
{
    char           *ptr, *server, *active;
    int             c, errflag = 0;
    int             lockfd;
    char           *inputtype;

    DefaultNntpProtocol = NntpIhaveProtocol;
    *DefaultNewsgroups = '\0';
    *DefaultModerator = '\0';
    *DefaultOrganization = '\0';
    *DefaultTrustFrom = '\0';
    *DefaultTrustfrom = '\0';
    inputtype = NntpInputType;
    while ((c = getopt(argc, argv, "f:F:m:o:g:w:r:p:a:s:t:h?ncv")) != -1)
	switch (c) {
	case 'v':
	    verboseon("bbsnnrp.log");
	    break;
	case 'c':
	    ResetActive = 1;
	    break;
	case 'g':
	    strncpy(DefaultNewsgroups, optarg, sizeof DefaultNewsgroups);
	    break;
	case 'm':
	    strncpy(DefaultModerator, optarg, sizeof DefaultModerator);
	    break;
	case 'o':
	    strncpy(DefaultOrganization, optarg, sizeof DefaultOrganization);
	    break;
	case 'f':
	    strncpy(DefaultTrustfrom, optarg, sizeof DefaultTrustfrom);
	    break;
	case 'F':
	    strncpy(DefaultTrustFrom, optarg, sizeof DefaultTrustFrom);
	    break;
	case 'r':{
		char           *hostptr;
		AskLocal = 0;
		DefaultRemoteHost = optarg;
		if ((hostptr = strchr(optarg, ':')) != NULL) {
		    *hostptr = '\0';
		    DefaultRemoteHost = hostptr + 1;
		    if (strcasecmp(optarg, "post") == 0)
			DefaultNntpProtocol = NntpPostProtocol;
		    *hostptr = ':';
		}
		break;
	    }
	case 'w':
	    RunOnce = 0;
	    DefaultWait = atoi(optarg);
	    if (DefaultWait < MIN_WAIT)
		DefaultWait = MIN_WAIT;
	    break;
	case 'p':
	    if (AskLocal == 0) {
		DefaultPort = optarg;
	    } else {
		DefaultPath = optarg;
	    }
	    break;
	case 'n':
	    StatHistory = 0;
	    break;
	case 'a':
	    Max_Arts = atol(optarg);
	    if (Max_Arts < 0)
		Max_Arts = 0;
	    break;
	case 's':
	    Max_Stats = atol(optarg);
	    if (Max_Stats < 0)
		Max_Stats = 0;
	    break;
	case 't':
	    if (strcasecmp(optarg, StdinInputType) == 0) {
		inputtype = StdinInputType;
	    }
	    break;
	case 'h':
	case '?':
	default:
	    errflag++;
	}
    if (errflag > 0) {
	usage(argv[0]);
	return (1);
    }
    if (inputtype == NntpInputType && argc - optind < 2) {
	usage(argv[0]);
	exit(1);
    }
    if (inputtype == NntpInputType) {
	server = argv[optind];
	active = argv[optind + 1];
	if (dashf(active)) {
	    strncpy(BBSNNRP.activefile, active, sizeof BBSNNRP.activefile);
	} else if (strchr(active, '/') == NULL) {
	    sprintf(BBSNNRP.activefile, "%s/innd/%.*s", BBSHOME, sizeof BBSNNRP.activefile - 7 - strlen(BBSHOME), active);
	} else {
	    strncpy(BBSNNRP.activefile, active, sizeof BBSNNRP.activefile);
	}

	strncpy(LockFile, (char *)fileglue("%s.lock", active), sizeof LockFile);
	if ((lockfd = open(LockFile, O_RDONLY)) >= 0) {
	    char            buf[10];
	    int             pid;

	    if (read(lockfd, buf, sizeof buf) > 0 && (pid = atoi(buf)) > 0 && kill(pid, 0) == 0) {
		fprintf(stderr, "another process [%d] running\n", pid);
		exit(1);
	    } else {
		fprintf(stderr, "no process [%d] running, but lock file existed, unlinked\n", pid);
		unlink(LockFile);
	    }
	    close(lockfd);
	}
	if ((lockfd = open(LockFile, O_RDWR | O_CREAT | O_EXCL, 0644)) < 0) {
	    fprintf(stderr, "maybe another %s process running\n", argv[0]);
	    exit(1);
	} else {
	    char            buf[10];
	    sprintf(buf, "%-.8d\n", getpid());
	    write(lockfd, buf, strlen(buf));
	    close(lockfd);
	}
	for (;;) {
	    if (!initial_bbs(NULL)) {
		fprintf(stderr, "Initial BBS failed\n");
		exit(1);
	    }
	    initsockets(server, &BBSNNRP, inputtype);
	    ptr = (char *)strrchr(active, '/');
	    if (ptr != NULL)
		ptr++;
	    else
		ptr = active;
	    sprintf(BBSNNRP.rcfile, "%s/.newsrc.%s.%s", INNDHOME, server, ptr);
	    initrcfiles(&BBSNNRP);

	    Signal(SIGTERM, doterm);
	    Signal(SIGKILL, doterm);
	    Signal(SIGHUP, doterm);
	    Signal(SIGPIPE, doterm);

	    readnews(server, &BBSNNRP);
	    writerc(&BBSNNRP);
	    closesockets();

	    if (RunOnce)
		break;
	    sleep(DefaultWait);
	}
	unlink(LockFile);
    }
     /* NntpInputType */ 
    else {
	if (!initial_bbs(NULL)) {
	    fprintf(stderr, "Initial BBS failed\n");
	    exit(1);
	}
	initsockets(server, &BBSNNRP, inputtype);
	Signal(SIGTERM, doterm);
	Signal(SIGKILL, doterm);
	Signal(SIGHUP, doterm);
	Signal(SIGPIPE, doterm);

	stdinreadnews(&BBSNNRP);
	closesockets();
    }				/* stdin input type */
    return 0;
}
