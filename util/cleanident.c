/* $Id$ */
#include "bbs.h"

int main(int argc, char **argv)
{
    userec_t usr;
    int     fd, i;
    if( argc == 1 || (fd = open(argv[1], O_RDWR)) < 0 ){
	fprintf(stderr, "usage: cleanident path_to_passwd\n");
	return 1;
    }
    for( i = 0 ; read(fd, &usr, sizeof(usr)) == sizeof(usr) ; ++ i ){
	memset(usr.pad0, 0, sizeof(usr.pad0));
	if( lseek(fd, i * sizeof(usr), SEEK_SET) != -1 )
	    write(fd, &usr, sizeof(usr));
    }
    printf("%d users cleaned\n", i);
    return 0;
}
