/* $Id$ */

#include "bbs.h"

#undef  HAVE_MMAP
#define BUFSIZE 512

static void
PttLock(int fd, int start, int size, int mode)
{
    static struct flock lock_it;
    int             ret;

    lock_it.l_whence = SEEK_CUR;/* from current point */
    lock_it.l_start = start;	/* -"- */
    lock_it.l_len = size;	/* length of data */
    lock_it.l_type = mode;	/* set exclusive/write lock */
    lock_it.l_pid = 0;		/* pid not actually interesting */
    while ((ret = fcntl(fd, F_SETLKW, &lock_it)) < 0 && errno == EINTR)
      sleep(1);
}

#define safewrite       write

int
get_num_records(char *fpath, int size)
{
    struct stat     st;
    if (stat(fpath, &st) == -1)
	return 0;
    return st.st_size / size;
}

int
get_sum_records(char *fpath, int size)
{
    struct stat     st;
    long            ans = 0;
    FILE           *fp;
    fileheader_t    fhdr;
    char            buf[200], *p;

    if (!(fp = fopen(fpath, "r")))
	return -1;

    strlcpy(buf, fpath, sizeof(buf));
    p = strrchr(buf, '/');
    assert(p);
    p++;

    while (fread(&fhdr, size, 1, fp) == 1) {
	strcpy(p, fhdr.filename);
	if (stat(buf, &st) == 0 && S_ISREG(st.st_mode) && st.st_nlink == 1)
	    ans += st.st_size;
    }
    fclose(fp);
    return ans / 1024;
}

int
get_record_keep(char *fpath, void *rptr, int size, int id, int *fd)
{
    /* 和 get_record() 一樣. 不過藉由 *fd, 可使同一個檔案不要一直重複開關 */
    if (id >= 1 &&
	(*fd > 0 ||
	 ((*fd = open(fpath, O_RDONLY, 0)) > 0))){
	if (lseek(*fd, (off_t) (size * (id - 1)), SEEK_SET) != -1) {
	    if (read(*fd, rptr, size) == size) {
		return 0;
	    }
	}
    }
    return -1;
}

int
get_record(char *fpath, void *rptr, int size, int id)
{
    int             fd = -1;

    if (id >= 1 && (fd = open(fpath, O_RDONLY, 0)) != -1) {
	if (lseek(fd, (off_t) (size * (id - 1)), SEEK_SET) != -1) {
	    if (read(fd, rptr, size) == size) {
		close(fd);
		return 0;
	    }
	}
	close(fd);
    }
    return -1;
}

int
get_records(char *fpath, void *rptr, int size, int id, int number)
{
    int             fd;

    if (id < 1 || (fd = open(fpath, O_RDONLY, 0)) == -1)
	return -1;

    if (lseek(fd, (off_t) (size * (id - 1)), SEEK_SET) == -1) {
	close(fd);
	return 0;
    }
    if ((id = read(fd, rptr, size * number)) == -1) {
	close(fd);
	return -1;
    }
    close(fd);
    return id / size;
}

int
substitute_record(char *fpath, void *rptr, int size, int id)
{
    int   fd;
    int   offset=size * (id - 1);
    if (id < 1 || (fd = open(fpath, O_WRONLY | O_CREAT, 0644)) == -1)
	return -1;

    lseek(fd, (off_t) (offset), SEEK_SET);
    PttLock(fd, offset, size, F_WRLCK);
    write(fd, rptr, size);
    PttLock(fd, offset, size, F_UNLCK);
    close(fd);

    return 0;
}

int
substitute_ref_record(char *direct, fileheader_t * fhdr, int ent)
{
    fileheader_t    hdr;
    char            genbuf[256];
    int             num = 0;

    /* rocker.011018: 串接模式用reference增進效率 */
    if ((fhdr->money & FHR_REFERENCE) &&
        (num = fhdr->money & ~FHR_REFERENCE)){
        setdirpath(genbuf, direct, ".DIR");
        get_record(genbuf, &hdr, sizeof(hdr), num);
        if (strcmp(hdr.filename, fhdr->filename))
           {
            if((num = getindex(genbuf, fhdr, num))>0)
             {
               substitute_record(genbuf, fhdr, sizeof(*fhdr), num);
             }
           }
        else if(num>0)
           {
             fhdr->money = hdr.money;
             substitute_record(genbuf, fhdr, sizeof(*fhdr), num);
           }
        fhdr->money = FHR_REFERENCE | num ; // Ptt: update now!
    }
    substitute_record(direct, fhdr, sizeof(*fhdr), ent);
    return num;
}

int
getindex(char *direct, fileheader_t *fh_o, int end)
{ // Ptt: 從前面找很費力 太暴力
    int             fd=-1, begin=1, i, stamp, s; 
    fileheader_t    fh;

    i = get_num_records(direct, sizeof(fileheader_t));
    if(end>i) end = i;
    stamp = atoi(fh_o->filename+2); 
    i=(begin+end)/2;
    for(; end>begin+1; i=(begin+end)/2)
    {
        if(get_record_keep(direct, &fh, sizeof(fileheader_t), i, &fd)==-1)
              break;  
        if(!fh.filename[0]) break;
        s = atoi(fh.filename+2); 
        if (s > stamp) end = i+1;
        else if(s == stamp) 
             {
              close(fd);
              fh_o->money = fh.money; 
              return i;
             } 
        else begin = i; 
    }
    if(fd==-1) close(fd);
    return 0;
}


/* rocker.011022: 避免lock檔開啟時不正常斷線,造成永久lock */
#ifndef _BBS_UTIL_C_
static int
force_open(char *fname)
{
    int             fd;
    time_t          expire;

    expire = now - 3600;	/* lock 存在超過一個小時就是有問題! */

    if (dasht(fname) < expire)
	return -1;
    unlink(fname);
    fd = open(fname, O_WRONLY | O_TRUNC, 0644);

    return fd;
}
#endif

/* new/old/lock file processing */
typedef struct nol_t {
    char            newfn[256];
    char            oldfn[256];
    char            lockfn[256];
}               nol_t;

#ifndef _BBS_UTIL_C_
static void
nolfilename(nol_t * n, char *fpath)
{
    snprintf(n->newfn, sizeof(n->newfn), "%s.new", fpath);
    snprintf(n->oldfn, sizeof(n->oldfn), "%s.old", fpath);
    snprintf(n->lockfn, sizeof(n->lockfn), "%s.lock", fpath);
}
#endif

int
delete_records(char fpath[], int size, int id, int num)
{
   char abuf[BUFSIZE];
   int fi, fo, locksize=0, readsize=0, offset = size * (id - 1), c, d=0;
   struct stat st;


   if ((fi=open(fpath, O_RDONLY, 0)) == -1)
      return -1;

   if ((fo=open(fpath, O_WRONLY, 0)) == -1)
    {
      close(fi);
      return -1;
    }

   if(fstat(fi, &st)==-1) 
     { close(fo); close(fi); return -1;}

   locksize = st.st_size - offset;
   readsize = locksize - size*num;
   if (locksize < 0 )
     { close(fo); close(fi); return -1;}

   PttLock(fo, offset, locksize, F_WRLCK);
   if(readsize>0)
    {
     lseek(fi, offset+size, SEEK_SET);  
     lseek(fo, offset, SEEK_SET);  
     while( d<readsize && (c = read(fi, abuf, BUFSIZE))>0)
      {
        write(fo, abuf, c);
        d=d+c;
      }
    }
   close(fi);
   ftruncate(fo, st.st_size - size*num);
   PttLock(fo, offset, locksize, F_UNLCK);
   close(fo);
   return 0;

}


int delete_record(char fpath[], int size, int id)
{
  return delete_records(fpath, size, id, 1);
}


#ifndef _BBS_UTIL_C_
#ifdef SAFE_ARTICLE_DELETE
void safe_delete_range(char *fpath, int id1, int id2)
{
    int     fd, i;
    fileheader_t    fhdr;
    char            fullpath[STRLEN], *t;
    strlcpy(fullpath, fpath, sizeof(fullpath));
    t = strrchr(fullpath, '/');
    assert(t);
    t++;
    if( (fd = open(fpath, O_RDONLY)) == -1 )
	return;
    for( i = 1 ; (read(fd, &fhdr, sizeof(fileheader_t)) ==
		  sizeof(fileheader_t)) ; ++i ){
	strcpy(t, fhdr.filename);
	/* rocker.011018: add new tag delete */
	if (!((fhdr.filemode & FILE_MARKED) ||	/* 標記 */
	      (fhdr.filemode & FILE_DIGEST) ||	/* 文摘 */
	      (id1 && (i < id1 || i > id2)) ||	/* range */
	      (!id1 && Tagger(atoi(t + 2), i, TAG_NIN)))) 	/* TagList */
	    safe_article_delete(i, &fhdr, fpath);
    }
    close(fd);
}
#endif

int
delete_range(char *fpath, int id1, int id2)
{
    fileheader_t    fhdr;
    nol_t           my;
    char            fullpath[STRLEN], *t;
    int             fdr, fdw, fd;
    int             count;

    nolfilename(&my, fpath);

    if ((fd = open(my.lockfn, O_RDWR | O_CREAT | O_APPEND, 0644)) == -1)
	return -1;

    flock(fd, LOCK_EX);

    if ((fdr = open(fpath, O_RDONLY, 0)) == -1) {
	flock(fd, LOCK_UN);
	close(fd);
	return -1;
    }
    if (
	((fdw = open(my.newfn, O_WRONLY | O_CREAT | O_EXCL, 0644)) == -1) &&
	((fdw = force_open(my.newfn)) == -1)) {
	close(fdr);
	flock(fd, LOCK_UN);
	close(fd);
	return -1;
    }
    count = 1;
    strlcpy(fullpath, fpath, sizeof(fullpath));
    t = strrchr(fullpath, '/');
    assert(t);
    t++;

    while (read(fdr, &fhdr, sizeof(fileheader_t)) == sizeof(fileheader_t)) {
	strcpy(t, fhdr.filename);

	/* rocker.011018: add new tag delete */
	if (
	    (fhdr.filemode & FILE_MARKED) ||	/* 標記 */
	    (fhdr.filemode & FILE_DIGEST) ||	/* 文摘 */
	    (id1 && (count < id1 || count > id2)) ||	/* range */
	    (!id1 && Tagger(atoi(t + 2), count, TAG_NIN))) {	/* TagList */
	    if ((safewrite(fdw, &fhdr, sizeof(fileheader_t)) == -1)) {
		close(fdr);
		close(fdw);
		unlink(my.newfn);
		flock(fd, LOCK_UN);
		close(fd);
		return -1;
	    }
	} else {
	    //if (dashd(fullpath))
		unlink(fullpath);
	}
	++count;
    }
    close(fdr);
    close(fdw);
    if (Rename(fpath, my.oldfn) == -1 || Rename(my.newfn, fpath) == -1) {
	flock(fd, LOCK_UN);
	close(fd);
	return -1;
    }
    flock(fd, LOCK_UN);
    close(fd);
    return 0;
}
#endif


#ifdef SAFE_ARTICLE_DELETE
int
safe_article_delete(int ent, fileheader_t *fhdr, char *direct)
{
    fileheader_t newfhdr;
    memcpy(&newfhdr, fhdr, sizeof(fileheader_t));
    sprintf(newfhdr.title, "(本文已被刪除)");
    strcpy(newfhdr.filename, ".deleted");
    strcpy(newfhdr.owner, "-");
    substitute_record(direct, &newfhdr, sizeof(newfhdr), ent);
    return 0;
}

int
safe_article_delete_range(char *direct, int from, int to)
{
    fileheader_t newfhdr;
    int     fd;
    char    fn[128], *ptr;

    strlcpy(fn, direct, sizeof(fn));
    if( (ptr = rindex(fn, '/')) == NULL )
	return 0;

    ++ptr;
    if( (fd = open(direct, O_RDWR)) != -1 &&
	lseek(fd, sizeof(fileheader_t) * (from - 1), SEEK_SET) != -1 ){

	for( ; from <= to ; ++from ){
	    read(fd, &newfhdr, sizeof(fileheader_t));
	    if( newfhdr.filemode & (FILE_MARKED | FILE_DIGEST) )
		continue;
	    strlcpy(ptr, newfhdr.filename, sizeof(newfhdr.filename));
	    unlink(fn);

	    sprintf(newfhdr.title, "(本文已被刪除)");
	    strcpy(newfhdr.filename, ".deleted");
	    strcpy(newfhdr.owner, "-");
	    // because off_t is unsigned, we could NOT seek backward.
	    lseek(fd, sizeof(fileheader_t) * (from - 1), SEEK_SET);
	    write(fd, &newfhdr, sizeof(fileheader_t));
	}
	close(fd);
    }
    return 0;
}


#endif		

int             apply_record(char *fpath, int (*fptr) (), int size){
    char            abuf[BUFSIZE];
    FILE           *fp;

    if (!(fp = fopen(fpath, "r")))
	return -1;

    while (fread(abuf, 1, size, fp) == (size_t)size)
	if ((*fptr) (abuf) == QUIT) {
	    fclose(fp);
	    return QUIT;
	}
    fclose(fp);
    return 0;
}

/* mail / post 時，依據時間建立檔案，加上郵戳 */
int
stampfile(char *fpath, fileheader_t * fh)
{
    register char  *ip = fpath;
    time_t          dtime = COMMON_TIME;
    struct tm      *ptime;
#ifdef _BBS_UTIL_C_
    int             fp = 0;  //Ptt: don't need to check 
    // for utils, the time may be the same between several runs, by scw
#endif

    if (access(fpath, X_OK | R_OK | W_OK))
	mkdir(fpath, 0755);

    while (*(++ip));
    *ip++ = '/';
#ifdef _BBS_UTIL_C_
    do {
#endif
	sprintf(ip, "M.%d.A.%3.3X", (int)++dtime, rand() & 0xFFF);
#ifdef _BBS_UTIL_C_
	if (fp == -1 && errno != EEXIST)
	    return -1;
   } while ((fp = open(fpath, O_CREAT | O_EXCL | O_WRONLY, 0644)) == -1);
    close(fp);
#endif
    memset(fh, 0, sizeof(fileheader_t));
    strlcpy(fh->filename, ip, sizeof(fh->filename));
    ptime = localtime(&dtime);
    snprintf(fh->date, sizeof(fh->date),
	     "%2d/%02d", ptime->tm_mon + 1, ptime->tm_mday);
    return 0;
}

void
stampdir(char *fpath, fileheader_t * fh)
{
    register char  *ip = fpath;
    time_t          dtime = COMMON_TIME;
    struct tm      *ptime;

    if (access(fpath, X_OK | R_OK | W_OK))
	mkdir(fpath, 0755);

    while (*(++ip));
    *ip++ = '/';
    do {
	sprintf(ip, "D%X", (int)++dtime & 07777);
    } while (mkdir(fpath, 0755) == -1);
    memset(fh, 0, sizeof(fileheader_t));
    strlcpy(fh->filename, ip, sizeof(fh->filename));
    ptime = localtime(&dtime);
    snprintf(fh->date, sizeof(fh->date),
	     "%2d/%02d", ptime->tm_mon + 1, ptime->tm_mday);
}

void
stamplink(char *fpath, fileheader_t * fh)
{
    register char  *ip = fpath;
    time_t          dtime = COMMON_TIME;
    struct tm      *ptime;

    if (access(fpath, X_OK | R_OK | W_OK))
	mkdir(fpath, 0755);

    while (*(++ip));
    *ip++ = '/';
    do {
	sprintf(ip, "S%X", (int)++dtime);
    } while (symlink("temp", fpath) == -1);
    memset(fh, 0, sizeof(fileheader_t));
    strlcpy(fh->filename, ip, sizeof(fh->filename));
    ptime = localtime(&dtime);
    snprintf(fh->date, sizeof(fh->date),
	     "%2d/%02d", ptime->tm_mon + 1, ptime->tm_mday);
}

int
append_record(char *fpath, fileheader_t * record, int size)
{
    int             fd, fsize=0, index;
    struct stat     st;

    if ((fd = open(fpath, O_WRONLY | O_CREAT, 0644)) == -1) {
	perror("open");
	return -1;
    }
    flock(fd, LOCK_EX);

    if(fstat(fd, &st)!=-1)
       fsize = st.st_size;

    index = fsize / size;
    lseek(fd, index * size, SEEK_SET);  // avoid offset

    safewrite(fd, record, size);

    flock(fd, LOCK_UN);
    close(fd);
    return index + 1;
}

int
append_record_forward(char *fpath, fileheader_t * record, int size)
{
#if !defined(_BBS_UTIL_C_)
    int             m, n;
    if (get_num_records(fpath, sizeof(fileheader_t)) <= MAX_KEEPMAIL * 2) {
	FILE           *fp;
	char            buf[512], address[200];

	for (n = strlen(fpath) - 1; fpath[n] != '/' && n > 0; n--);
	strncpy(buf, fpath, n + 1);
	buf[n + 1] = 0;
	for (m = strlen(buf) - 2; buf[m] != '/' && m > 0; m--);
	strcat(buf, ".forward"); // XXX check buffer size
	if ((fp = fopen(buf, "r"))) {
	    fscanf(fp, "%s", address); // XXX check buffer size
	    fclose(fp);
	    if (buf[0] != 0 && buf[0] != ' ') {
		buf[n + 1] = 0;
		strcat(buf, record->filename);
		append_record(fpath, record, size);
#ifndef  USE_BSMTP
		bbs_sendmail(buf, record->title, address);
#else
		bsmtp(buf, record->title, address, 0);
#endif
		return 0;
	    }
	}
    }
#endif

    append_record(fpath, record, size);

    return 0;
}
