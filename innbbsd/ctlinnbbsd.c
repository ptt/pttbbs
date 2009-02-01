#include "bbs.h"
#include <stdlib.h>
#include "innbbsconf.h"
#include "bbslib.h"
#include "externs.h"

extern char    *optarg;
extern int      opterr, optind;

void
usage(name)
    char           *name;
{
    fprintf(stderr, "Usage: %s [-p path] commands\n", name);
    fprintf(stderr, " where available commands:\n");
    fprintf(stderr, "  ctlinnbbsd reload   : reload datafiles for innbbsd\n");
    fprintf(stderr, "  ctlinnbbsd shutdown : shutdown innbbsd gracefully\n");
    fprintf(stderr, "  ctlinnbbsd mode     : examine mode of innbbsd\n");
    fprintf(stderr, "  ctlinnbbsd addhist <mid> path: add history\n");
    fprintf(stderr, "  ctlinnbbsd grephist <mid>: query history\n");
    fprintf(stderr, "  ctlinnbbsd verboselog on|off : verboselog on/off\n");
    fprintf(stderr, "  ctlinnbbsd hismaint : maintain history\n");
    fprintf(stderr, "  ctlinnbbsd listnodelist  : list nodelist.bbs\n");
    fprintf(stderr, "  ctlinnbbsd listnewsfeeds : list newsfeeds.bbs\n");
#ifdef GETRUSAGE
    fprintf(stderr, "  ctlinnbbsd getrusage: get resource usage\n");
#endif
#ifdef MALLOCMAP
    fprintf(stderr, "  ctlinnbbsd mallocmap: get malloc map\n");
#endif
}


char           *DefaultPath = LOCALDAEMON;
char            INNBBSbuffer[4096];

FILE           *innbbsin, *innbbsout;
int             innbbsfd;

void
ctlinnbbsd(argc, argv)
    int             argc;
    char          **argv;
{
    fgets(INNBBSbuffer, sizeof INNBBSbuffer, innbbsin);
    printf("%s", INNBBSbuffer);
    if (strcasecmp(argv[0], "shutdown") == 0 ||
	strcasecmp(argv[0], "reload") == 0 ||
	strcasecmp(argv[0], "hismaint") == 0 ||
#ifdef GETRUSAGE
	strcasecmp(argv[0], "getrusage") == 0 ||
#endif
#ifdef MALLOCMAP
	strcasecmp(argv[0], "mallocmap") == 0 ||
#endif
	strcasecmp(argv[0], "mode") == 0 ||
	strcasecmp(argv[0], "listnodelist") == 0 ||
	strcasecmp(argv[0], "listnewsfeeds") == 0
	) {
	fprintf(innbbsout, "%s\r\n", argv[0]);
	fflush(innbbsout);
	fgets(INNBBSbuffer, sizeof INNBBSbuffer, innbbsin);
	printf("%s", INNBBSbuffer);
	if (strcasecmp(argv[0], "mode") == 0
#ifdef GETRUSAGE
	    ||
	    strcasecmp(argv[0], "getrusage") == 0
	    ||
	    strcasecmp(argv[0], "mallocmap") == 0
#endif
	    ||
	    strcasecmp(argv[0], "listnodelist") == 0
	    ||
	    strcasecmp(argv[0], "listnewsfeeds") == 0
	    ) {
	    while (fgets(INNBBSbuffer, sizeof INNBBSbuffer, innbbsin) != NULL) {
		if (strcmp(INNBBSbuffer, ".\r\n") == 0) {
		    break;
		}
		printf("%s", INNBBSbuffer);
	    }
	}
    } else if (strcasecmp(argv[0], "grephist") == 0 ||
	       strcasecmp(argv[0], "verboselog") == 0) {
	if (argc < 2) {
	    usage("ctlinnbbsd");
	} else {
	    fprintf(innbbsout, "%s %s\r\n", argv[0], argv[1]);
	    fflush(innbbsout);
	    fgets(INNBBSbuffer, sizeof INNBBSbuffer, innbbsin);
	    printf("%s\n", INNBBSbuffer);
	}
    } else if (strcasecmp(argv[0], "addhist") == 0) {
	if (argc < 3) {
	    usage("ctlinnbbsd");
	} else {
	    fprintf(innbbsout, "%s %s %s\r\n", argv[0], argv[1], argv[2]);
	    fflush(innbbsout);
	    fgets(INNBBSbuffer, sizeof INNBBSbuffer, innbbsin);
	    printf("%s", INNBBSbuffer);
	}
    } else {
	fprintf(stderr, "invalid command %s\n", argv[0]);
    }
    if (strcasecmp(argv[0], "shutdown") != 0) {
	fprintf(innbbsout, "QUIT\r\n");
	fflush(innbbsout);
	fgets(INNBBSbuffer, sizeof INNBBSbuffer, innbbsin);
    }
}

void
initsocket()
{
    innbbsfd = toconnect(DefaultPath);
    if (innbbsfd < 0) {
	fprintf(stderr, "Connect to %s error. You may not run innbbsd\n", DefaultPath);
	exit(2);
    }
    if ((innbbsin = fdopen(innbbsfd, "r")) == NULL ||
	(innbbsout = fdopen(innbbsfd, "w")) == NULL) {
	fprintf(stderr, "fdopen error\n");
	exit(3);
    }
}

void
closesocket()
{
    if (innbbsin != NULL)
	fclose(innbbsin);
    if (innbbsout != NULL)
	fclose(innbbsout);
    if (innbbsfd >= 0)
	close(innbbsfd);
}

int
main(argc, argv)
    int             argc;
    char          **argv;
{
    int             c, errflag = 0;

    while ((c = getopt(argc, argv, "p:h?")) != -1)
	switch (c) {
	case 'p':
	    DefaultPath = optarg;
	    break;
	case 'h':
	case '?':
	default:
	    errflag++;
	    break;
	}
    if (errflag > 0) {
	usage(argv[0]);
	return (1);
    }
    if (argc - optind < 1) {
	usage(argv[0]);
	exit(1);
    }
    initial_bbs(NULL);
    initsocket();
    ctlinnbbsd(argc - optind, argv + optind);
    closesocket();

    return 0;
}
