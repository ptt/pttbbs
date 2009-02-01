#include <string.h>
#include "daemon.h"
/*
 * typedef struct daemoncmd { char *cmdname; char *usage; int argc; int
 * (*main) ARG((FILE*,FILE*,int,char**,char*)); } daemoncmd_t;
 * 
 */

void deargify   ARG((char ***));
static daemoncmd_t *dcmdp = NULL;
static char    *startupmessage = NULL;
static int      startupcode = 100;
typedef int     (*F) ();

void 
installdaemon(cmds, code, startupmsg)
    daemoncmd_t    *cmds;
    int             code;
    char           *startupmsg;
{
    dcmdp = cmds;
    startupcode = code;
    startupmessage = startupmsg;
}

daemoncmd_t    *
searchcmd(cmd)
    char           *cmd;
{
    daemoncmd_t    *p;
    for (p = dcmdp; p->name != NULL; p++) {
#ifdef DEBUGCMD
	printf("searching name %s for cmd %s\n", p->name, cmd);
#endif
	if (!strncasecmp(p->name, cmd, 1024))
	    return p;
    }
    return NULL;
}

#if 0
static FILE    *DIN, *DOUT, *DIO;

int 
daemon(dfd)
    int             dfd;
{
    static char     BUF[1024];
    /* hash_init(); */
    if (dfd > 0) {
	DIO = fdopen(dfd, "rw");
	DIN = fdopen(dfd, "r");
	DOUT = fdopen(dfd, "w");
	if (DIO == NULL || DIN == NULL || DOUT == NULL) {
	    perror("fdopen");
	    return -1;
	}
    }
    if (startupmessage) {
	fprintf(DOUT, "%d %s\n", startupcode, startupmessage);
	fflush(DOUT);
    }
    while (fgets(BUF, 1024, DIN) != NULL) {
	int             i;
	int             (*Main) ();
	daemoncmd_t    *dp;
	argv_t          Argv;

	char           *p = (char *)strchr(BUF, '\r');
	if (p == NULL)
	    p = (char *)strchr(BUF, '\n');
	if (p == NULL)
	    continue;
	*p = '\0';
	if (p == BUF)
	    continue;

	Argv.argc = 0, Argv.argv = NULL, Argv.inputline = BUF;
	Argv.in = DIN, Argv.out = DOUT;
	printf("command entered: %s\n", BUF);
#ifdef DEBUGSERVER
	fprintf(DOUT, "BUF in client %s\n", BUF);
	fprintf(stdout, "BUF in server %s\n", BUF);
	fflush(DOUT);
#endif
	Argv.argc = argify(BUF, &Argv.argv);
#ifdef DEBUGSERVER
	fprintf(stdout, "argc %d argv ", Argv.argc);
	for (i = 0; i < Argv.argc; ++i)
	    fprintf(stdout, "%s ", Argv.argv[i]);
	fprintf(stdout, "\n");
#endif
	dp = searchcmd(Argv.argv[0]);
	Argv.dc = dp;
	if (dp) {
#ifdef DEBUGSERVER
	    printf("find cmd %s by %s\n", dp->name, dp->usage);
#endif
	    if (Argv.argc < dp->argc) {
		fprintf(DOUT, "%d Usage: %s\n", dp->errorcode, dp->usage);
		fflush(DOUT);
		goto cont;
	    }
	    if (dp->argno != 0 && Argv.argc > dp->argno) {
		fprintf(DOUT, "%d Usage: %s\n", dp->errorcode, dp->usage);
		fflush(DOUT);
		goto cont;
	    }
	    Main = dp->main;
	    if (Main) {
		fflush(stdout);
		(*Main) (&Argv);
	    }
	} else {
	    fprintf(DOUT, "99 command %s not available\n", Argv.argv[0]);
	    fflush(DOUT);
	}
cont:
	deargify(&Argv.argv);
    }
    /* hash_reclaim(); */
}
#endif

#define MAX_ARG 32
#define MAX_ARG_SIZE 16384

int 
argify(line, argvp)
    char           *line, ***argvp;
{
    static char    *argvbuffer[MAX_ARG + 2];
    char          **argv = argvbuffer;
    int             i;
    static char     argifybuffer[MAX_ARG_SIZE];
    char           *p;
    while (strchr("\t\n\r ", *line))
	line++;
    i = strlen(line);
    /* p=(char*) mymalloc(i+1); */
    p = argifybuffer;
    strncpy(p, line, sizeof argifybuffer);
    for (*argvp = argv, i = 0; *p && i < MAX_ARG;) {
	for (*argv++ = p; *p && !strchr("\t\r\n ", *p); p++);
	if (*p == '\0')
	    break;
	for (*p++ = '\0'; strchr("\t\r\n ", *p) && *p; p++);
    }
    *argv = NULL;
    return argv - *argvp;
}

void 
deargify(argv)
    char         ***argv;
{
    return;
    /*
     * if (*argv != NULL) { if (*argv[0] != NULL){ free(*argv[0]); argv[0] =
     * NULL; } free(*argv); argv = NULL; }
     */
}
