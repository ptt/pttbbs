#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/mount.h>

#define  MAXDSs   1048576
#define  LEAVE    10
#define  CLEAN    10
typedef struct {
    time_t  atime;
    char    name[60];
} DS;

DS      ds[MAXDSs];
int     nDSs;

int compar(const void *a, const void *b)
{
    return ((DS *)a)->atime - ((DS *)b)->atime;
}

int main(int argc, char **argv)
{
    DIR     *dirp;
    struct  dirent *dp;
    struct  stat   sb;
    struct  statfs sf;
    char    *fn;
    time_t  now;
    if( chdir(BBSHOME "/cache") < 0 )
	err(1, "chdir");
    
    if( (dirp = opendir(".")) == NULL )
	err(1, "opendir");

    statfs(".", &sf);
    if( sf.f_bfree * 100 / sf.f_blocks > LEAVE )
	return 0;

    now = time(NULL);
    while( (dp = readdir(dirp)) != NULL ){
	fn = dp->d_name;
	if( fn[0] != 'e' && fn[0] != 'b' )
	    continue;
	if( stat(fn, &sb) < 0 )
	    continue;
	if( sb.st_atime < now - 1800 ){
	    printf("atime: %s\n", fn);
	    unlink(fn);
	}
	else if( sb.st_mtime < now - 7200 ){
	    printf("mtime: %s\n", fn);
	    unlink(fn);
	}
	else{
	    if( nDSs != MAXDSs ){
		strcpy(ds[nDSs].name, fn);
		ds[nDSs].atime = sb.st_atime;
		++nDSs;
	    }
	}
    }
    
    statfs(".", &sf);
    if( sf.f_bfree * 100 / sf.f_blocks <= LEAVE ){
	qsort(ds, nDSs, sizeof(DS), compar);
	nDSs = nDSs * CLEAN / 100;
	while( nDSs-- ){
	    printf("%s\n", ds[nDSs].name);
	    unlink(ds[nDSs].name);
	}
    }
    return 0;
}
