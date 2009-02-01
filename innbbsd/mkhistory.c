#include <stdlib.h>
#include "his.h"
#include "externs.h"

int
main(argc, argv)
    int             argc;
    char           *argv[];
{
    if (argc < 2) {
	fprintf(stderr, "Usage: %s history-file\n", argv[0]);
	exit(1);
    }
    initial_bbs(NULL);
    mkhistory(argv[1]);
    return 0;
}
