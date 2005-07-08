/* $Id$ */
/* Vote limits were added in r2342 which uses previously unused pads.
 * This program zero outs those pads. */
#include "bbs.h"

void transform(boardheader_t *new, boardheader_t *old)
{
    memcpy(new, old, sizeof(boardheader_t));
    new->post_limit_posts = (unsigned char) 0;
    new->post_limit_logins = (unsigned char) 0;
    new->post_limit_regtime = (unsigned char) 0;
    new->vote_limit_posts = (unsigned char) 0;
    new->vote_limit_logins = (unsigned char) 0;
    new->vote_limit_regtime = (unsigned char) 0;
    memset(new->pad, 0, sizeof(new->pad));
    memset(new->pad2, 0, sizeof(new->pad2));
    memset(new->pad3, 0, sizeof(new->pad3));
}

int main(void)
{
    int fd, fdw;
    boardheader_t new, old;

    printf("You're going to zero out vote limits in your .BRD\n");
    printf("The new file will be named .BRD.trans.tmp\n");
/*
    printf("Press any key to continue\n");
    getchar();
*/

    if (chdir(BBSHOME) < 0) {
	perror("chdir");
	exit(-1);
    }

    if ((fd = open(FN_BOARD, O_RDONLY)) < 0 ||
	 (fdw = open(FN_BOARD".trans.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0 ) {
	perror("open");
	exit(-1);
    }

    while (read(fd, &old, sizeof(old)) > 0) {
	transform(&new, &old);
	write(fdw, &new, sizeof(new));
    }

    close(fd);
    close(fdw);
    return 0;
}
