#include "bbs.h"
#include <err.h>

int main(int argc, char **argv)
{
    userec_t xuser;
    int     fd, ufd;
    char    path[80], fn[80];

    chdir(BBSHOME);
    if( (fd = open(".PASSWDS", O_RDONLY)) < 0 )
	err(1, ".PASSWDS");

    while( read(fd, &xuser, sizeof(xuser)) > 0 ){
	if( strcmp(xuser.userid, "in2") != 0 )
	    continue;
	sprintf(path, "home/%c/%s", xuser.userid[0], xuser.userid);
	if( access(path, 0) < 0 ){
	    printf("user home error (%s) (%s)\n", xuser.userid, path);
	    continue;
	}
	sprintf(fn, "%s/.passwd", path);
	if( (ufd = open(fn, O_WRONLY | O_CREAT, 0600)) < 0 ){
	    perror(path);
	    continue;
	}
	write(ufd, &xuser, sizeof(xuser));
	close(ufd);
    }
    
    return 0;
}
