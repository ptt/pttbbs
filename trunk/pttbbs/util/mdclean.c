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
    int     sleeptime, leavespace, clean;

    if( argc != 4 ){
	puts("usage:\tmdclean sleeptime(secs) leavespace(k) clean(%%)\n");
	return 0;
    }
    sleeptime = (atoi(argv[1]) < 1) ? 1 : atoi(argv[1]);
    leavespace= (atoi(argv[2]) <100)?100: atoi(argv[2]);
    clean     = (atoi(argv[3]) < 1) ? 1 : atoi(argv[3]);

    if( chdir(BBSHOME "/cache") < 0 )
	err(1, "chdir");

    while( 1 ){
	puts("sleeping...");
	sleep(sleeptime);
	if( (dirp = opendir(".")) == NULL )
	    err(1, "opendir");
	
	statfs(".", &sf);
	if( sf.f_bfree * sf.f_bsize / 1024 > leavespace )
	    continue;
	
	nDSs = 0;
	now = time(NULL);
	while( (dp = readdir(dirp)) != NULL ){
	    fn = dp->d_name;
	    if( fn[0] != 'e' && fn[0] != 'b' ){
		unlink(fn);
		continue;
	    }
	    if( stat(fn, &sb) < 0 )
		continue;
	    if( sb.st_atime < now - 1800 ){
		printf("atime: %s\n", fn);
		unlink(fn);
	    }
	    else if( sb.st_mtime < now - 10800 ){
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
	closedir(dirp);
	
	statfs(".", &sf);
	if( sf.f_bfree * sf.f_bsize / 1024 <= leavespace ){
	    qsort(ds, nDSs, sizeof(DS), compar);
	    nDSs = nDSs * clean / 100;
	    while( nDSs-- ){
		printf("%s\n", ds[nDSs].name);
		unlink(ds[nDSs].name);
	    }
	}
    }
    return 0;
}
