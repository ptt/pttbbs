#include "bbs.h"
#include "fnv_hash.h"
#include "var.h"
#include "testutil.h"

static void userec_add_to_uhash(int n, userec_t *id, int onfly);
static void fill_uhash(int onfly);

void load_uhash() {
  int shmid, err;
  shmid = shmget(SHM_KEY, SHMSIZE,
#ifdef USE_HUGETLB
                 SHM_HUGETLB |
#endif
                 0600 | IPC_CREAT | IPC_EXCL);
  err = errno;
  fprintf(stderr, "load_uhash: shmid: %d err: %d EEXIST: %d\n", shmid, err, EEXIST);

  if( err == EEXIST ) {
    shmid = shmget(SHM_KEY, SHMSIZE,
#ifdef USE_HUGETLB
                   SHM_HUGETLB |
#endif
                   0600 | IPC_CREAT);
  }

  if( shmid < 0 ){
    perror("shmget");
    exit(1);
  }

  SHM = (void *) shmat(shmid, NULL, 0);
  if (SHM == (void *)-1) {
    perror("shmat");
    exit(1);
  }

  if( err  != EEXIST ) {
    SHM->number=SHM->loaded = 0;
    SHM->version = SHM_VERSION;
    SHM->size    = sizeof(SHM_t);
  }

  if(SHM->version != SHM_VERSION) {
    fprintf(stderr, "Error: SHM->version(%d) != SHM_VERSION(%d)\n", SHM->version, SHM_VERSION);
    fprintf(stderr, "Please use the source code version corresponding to SHM,\n"
            "or use ipcrm(1) command to clean share memory.\n");
    exit(1);
  }

  // in case it's not assumed zero, this becomes a race...
  fprintf(stderr, "[load_uhash]: to check onfly: SHM->number: %d SHM->loaded: %d\n", SHM->number, SHM->loaded);
  if (SHM->number == 0 && SHM->loaded == 0) {
    SHM->loaded = 0;
    fill_uhash(0);
    SHM->today_is[0] = '\0';
    SHM->loaded = 1;
  } else {
    fill_uhash(1);
  }
}

static void checkhash(int h)
{
  int *p = &(SHM->hash_head[h]), ch, deep=0;
  while(*p != -1)
  {
    if(*p <-1 || *p >= MAX_USERS) {*p=-1; return;}
    ch = StringHash( SHM->userid[*p])%(1<<HASH_BITS);
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

static void fill_uhash(int onfly)
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

  fprintf(stderr, "[fill_uhash] total %d names %s.\n", usernumber, onfly ? "checked":"loaded");
}

static void userec_add_to_uhash(int n, userec_t *user, int onfly)
{
  int *p, h, l=0;

  // uhash use userid="" to denote free slot for new register
  // However, such entries will have the same hash key.
  // So we skip most of invalid userid to prevent lots of hash collision.
  //fprintf(stderr, "[userec_add_to_uhash] start: userid: %s\n", user->userid);
  if (!is_validuserid(user->userid)) {
    // dirty hack, preserve few slot for new register
    static int count = 0;
    count++;
    if (count > 1000) {
      //fprintf(stderr, "[userec_add_to_uhash] count > 1000: userid: %s\n", user->userid);
      return;
    }
  }

  h = StringHash(user->userid)%(1<<HASH_BITS);

  p = &(SHM->hash_head[h]);
  if(!onfly || SHM->userid[n][0] != user->userid[0] ||
     strncmp(SHM->userid[n], user->userid, IDLEN-1))
  {
    strcpy(SHM->userid[n], user->userid);
    SHM->money[n] = user->money;
#ifdef USE_COOLDOWN
    SHM->cooldowntime[n] = 0;
#endif
    //fprintf(stderr, "[user_add_to_uhash] add %s\n", user->userid);
  }
  while (*p != -1)
  {
    if(onfly && *p==n ) {// already in hash
      //fprintf(stderr, "[userec_add_to_uhash] onfly and already in hash: userid: %s\n", user->userid);
      return;
    }
    l++;
    p = &(SHM->next_in_hash[*p]);
  }
  //fprintf(stderr, "[userec_add_to_uhash] add %d %d %d [%s] in hash\n", l, h, n, user->userid);
  SHM->next_in_hash[*p = n] = -1;
}
