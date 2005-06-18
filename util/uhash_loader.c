/* $Id$ */
/* standalone uhash loader -- jochang */
#include "bbs.h"
#include "fnv_hash.h"

unsigned string_hash(unsigned char *s);
void userec_add_to_uhash(int n, userec_t *id, int onfly);
void fill_uhash(int onfly);
void load_uhash(void);

SHM_t *SHM;

int main()
{
#ifndef USE_HUGETLB
    setgid(BBSGID);
    setuid(BBSUID);
#endif
    chdir(BBSHOME);
    load_uhash();
    return 0;
}

void load_uhash(void) {
    int shmid, err;
    shmid = shmget(SHM_KEY, SHMSIZE,
#ifdef USE_HUGETLB
		   SHM_HUGETLB | 0666 |
#else
		   0600 |
#endif
		   IPC_CREAT | IPC_EXCL);
    err = errno;
    if( err == EEXIST )
	shmid = shmget(SHM_KEY, SHMSIZE,
#ifdef USE_HUGETLB
		       SHM_HUGETLB | 0666 |
#else
		       0600 |
#endif
		       );

    if( shmid < 0 ){
	perror("shmget");
	exit(1);
    }
    SHM = (void *) shmat(shmid, NULL, 0);
    if( SHM == (void *)-1 ){
	perror("shmat");
	exit(1);
    }
    if( err  != EEXIST ) {
	SHM->number=SHM->loaded = 0;
	SHM->version = SHM_VERSION;
    }

    if(SHM->version != SHM_VERSION) {
      fprintf(stderr, "Error: SHM->version(%d) != SHM_VERSION(%d)\n", SHM->version, SHM_VERSION);
      fprintf(stderr, "Please use the source code version corresponding to SHM,\n"
	 "or use ipcrm(1) command to clean share memory.\n");
      exit(1);
    }

// in case it's not assumed zero, this becomes a race... 
    if( SHM->number == 0 && SHM->loaded == 0 ){
	SHM->loaded = 0;
	fill_uhash(0);
	SHM->loaded = 1;
    }
    else{
	fill_uhash(1);	
    }
}

void checkhash(int h)
{
    int *p = &(SHM->hash_head[h]), ch, deep=0;
    while(*p != -1)
    {
       if(*p <-1 || *p >= MAX_USERS) {*p=-1; return;}
       ch = string_hash( SHM->userid[*p])%(1<<HASH_BITS);
       if(ch!=h)
       {
           printf("remove %d %d!=%d %d [%s] next:%d\n", 
		    deep, h, ch, *p, SHM->userid[*p],
		    SHM->next_in_hash[*p]);
          *p = SHM->next_in_hash[*p]; //remove from link
          // *p=-1;  Ptt: cut it?
	  //return;
       }
       else
          p = &(SHM->next_in_hash[*p]);
       deep++;
    }
}
void fill_uhash(int onfly)
{
    int fd, usernumber;
    usernumber = 0;

    for (fd = 0; fd < (1 << HASH_BITS); fd++)
      if(!onfly)
    	  SHM->hash_head[fd] = -1;
      else
	  checkhash(fd);
    
    if ((fd = open(FN_PASSWD, O_RDWR)) > 0)
    {
	struct stat stbuf;
	caddr_t fimage, mimage;

	fstat(fd, &stbuf);
	fimage = mmap(NULL, stbuf.st_size, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0);
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
	    userec_add_to_uhash(usernumber, (userec_t *)mimage, onfly);
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

    printf("total %d names %s.\n", usernumber, onfly ? "checked":"loaded");
}
unsigned string_hash(unsigned char *s)
{
    return fnv1a_32_strcase(s, FNV1_32_INIT);
}

void userec_add_to_uhash(int n, userec_t *user, int onfly)
{
    int *p, h, l=0;

    h = string_hash(user->userid)%(1<<HASH_BITS);
    
    p = &(SHM->hash_head[h]);
    if(!onfly || SHM->userid[n][0] != user->userid[0] || 
	       strncmp(SHM->userid[n], user->userid, IDLEN-1))
    {
       strcpy(SHM->userid[n], user->userid);
       SHM->money[n] = user->money;
#ifdef USE_COOLDOWN
       SHM->cooldowntime[n] = 0;
#endif
       if(onfly)
           printf("add %s\n", user->userid);
    }
    while (*p != -1)
    {
	if(onfly && *p==n )  // already in hash
	     return;
	l++;
	p = &(SHM->next_in_hash[*p]);
    }
    if(onfly)
       printf("add %d %d %d [%s] in hash\n", l, h, n, user->userid);
    SHM->next_in_hash[*p = n] = -1;
}
