/* $Id$ */
/* standalone uhash loader -- jochang */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#ifdef __FreeBSD__
#include <machine/param.h>
#endif

#include "config.h"
#include "pttstruct.h"
#include "common.h"

unsigned string_hash(unsigned char *s);
void add_to_uhash(int n, userec_t *id);
void fill_uhash(void);
void load_uhash(void);

SHM_t *SHM;

int main() {
    setgid(BBSGID);
    setuid(BBSUID);
    chdir(BBSHOME);
    load_uhash();
    return 0;
}

void load_uhash(void) {
    int shmid;
    shmid = shmget(SHM_KEY, sizeof(SHM_t), IPC_CREAT | 0600);
/* note we didn't use IPC_EXCL here.
   so if the loading fails,
   (like .PASSWD doesn't exist)
   we may try again later.
*/
    if (shmid < 0)
    {
	perror("shmget");
	exit(1);
    }

    SHM = (void *) shmat(shmid, NULL, 0);
    if (SHM == (void *) -1)
    {
	perror("shmat");
	exit(1);
    }

/* in case it's not assumed zero, this becomes a race... */
    SHM->loaded = 0;

    fill_uhash();

/* ok... */
    SHM->loaded = 1;
}

void fill_uhash(void)
{
    int fd, usernumber;
    usernumber = 0;

    for (fd = 0; fd < (1 << HASH_BITS); fd++)
	SHM->hash_head[fd] = -1;
    
    if ((fd = open(FN_PASSWD, O_RDONLY)) > 0)
    {
	struct stat stbuf;
	caddr_t fimage, mimage;

	fstat(fd, &stbuf);
	fimage = mmap(NULL, stbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (fimage == (char *) -1)
	{
	    perror("mmap");
	    exit(1);
	}
	close(fd);
	fd = stbuf.st_size / sizeof(userec_t);
	if (fd > MAX_USERS)
	    fd = MAX_USERS;
	
	for (mimage = fimage; usernumber < fd; mimage += sizeof(userec_t))
	{
	    add_to_uhash(usernumber, (userec_t *)mimage);
	    usernumber++;
	}
	munmap(fimage, stbuf.st_size);
    }
    else
    {
	perror("open");
	exit(1);
    }
    SHM->number = usernumber;
    printf("total %d names loaded.\n", usernumber);
}
unsigned string_hash(unsigned char *s)
{
    unsigned int v = 0;
    while (*s)
    {
	v = (v << 8) | (v >> 24);
	v ^= toupper(*s++);	/* note this is case insensitive */
    }
    return (v * 2654435769UL) >> (32 - HASH_BITS);
}

void add_to_uhash(int n, userec_t *user)
{
    int *p, h = string_hash(user->userid);
    strcpy(SHM->userid[n], user->userid);
    SHM->money[n] = user->money;
    p = &(SHM->hash_head[h]);

    while (*p != -1)
	p = &(SHM->next_in_hash[*p]);

    SHM->next_in_hash[*p = n] = -1;
}
