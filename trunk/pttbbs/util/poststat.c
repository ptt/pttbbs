/* $Id: poststat.c,v 1.1 2002/03/07 15:13:46 in2 Exp $ */
/* ²Î­p¤µ¤é¡B¶g¡B¤ë¡B¦~¼öªù¸ÜÃD */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#ifdef __FreeBSD__
#include <machine/param.h>
#endif

#include "config.h"
#include "pttstruct.h"

char *myfile[] =
{"day", "week", "month", "year"};
int mycount[4] =
{7, 4, 12};
int mytop[] =
{10, 50, 100, 100};
char *mytitle[] =
{"¤é¤Q", "¶g¤­¤Q", "¤ë¦Ê", "¦~«×¦Ê"};


#define HASHSIZE 1024
#define TOPCOUNT 200


struct postrec
{
    char author[13];		/* author name */
    char board[13];		/* board name */
    char title[66];		/* title name */
    time_t date;		/* last post's date */
    int number;			/* post number */
    struct postrec *next;	/* next rec */
}
*bucket[HASHSIZE];


/* 100 bytes */
struct posttop
{
    char author[13];		/* author name */
    char board[13];		/* board name */
    char title[66];		/* title name */
    time_t date;		/* last post's date */
    int number;			/* post number */
}
top[TOPCOUNT], *tp;



/*
   woju
   Cross-fs rename()
 */

int Rename(char *src, char *dst)
{

    if (rename(src, dst) == 0)
	return 0;
/*
  sprintf(cmd, "/bin/mv %s %s", src, dst);
  return system(cmd);
*/
    return 0;
}



/*-------------------------------------------------------*/
/* .BOARDS cache                                           */
/*-------------------------------------------------------*/

struct bcache_t *brdshm;
boardheader_t *bcache;
int numboards = -1;


static void
attach_err(shmkey, name)
    int shmkey;
    char *name;
{
    fprintf(stderr, "[%s error] key = %x\n", name, shmkey);
    exit(1);
}


static void *
attach_shm(shmkey, shmsize)
    int shmkey, shmsize;
{
    void *shmptr;
    int shmid;

    shmid = shmget(shmkey, shmsize, 0);
    if (shmid < 0)
    {
	shmid = shmget(shmkey, shmsize, IPC_CREAT | 0600);
	if (shmid < 0)
	    attach_err(shmkey, "shmget");
	shmptr = (void *) shmat(shmid, NULL, 0);
	if (shmptr == (void *) -1)
	    attach_err(shmkey, "shmat");
	memset(shmptr, 0, shmsize);
    }
    else
    {
	shmptr = (void *) shmat(shmid, NULL, 0);
	if (shmptr == (void *) -1)
	    attach_err(shmkey, "shmat");
    }
    return shmptr;
}



void
resolve_boards()
{
    if (brdshm == NULL)
    {
	brdshm = attach_shm(BRDSHM_KEY, sizeof(*brdshm));
	if (brdshm->touchtime == 0)
	    brdshm->touchtime = 1;
	bcache = brdshm->bcache;
    }

    while (brdshm->uptime < brdshm->touchtime)
    {
	if (brdshm->busystate)
	{
	    sleep(1);
	}
	else
	{
	    int fd;

	    brdshm->busystate = 1;

	    if ((fd = open(".BOARDS", O_RDONLY)) > 0)
	    {
		brdshm->number = read(fd, bcache, MAX_BOARD * sizeof(boardheader_t))
		    / sizeof(boardheader_t);
		close(fd);
	    }

	    /* µ¥©Ò¦³ boards ¸ê®Æ§ó·s«á¦A³]©w uptime */

	    brdshm->uptime = brdshm->touchtime;
	    brdshm->busystate = 0;
	}
    }
    numboards = brdshm->number;
}


int
ci_strcmp(s1, s2)
    register char *s1, *s2;
{
    register int c1, c2, diff;

    do
    {
	c1 = *s1++;
	c2 = *s2++;
	if (c1 >= 'A' && c1 <= 'Z')
	    c1 |= 32;
	if (c2 >= 'A' && c2 <= 'Z')
	    c2 |= 32;
	if((diff = c1 - c2))
	    return (diff);
    }
    while (c1);
    return 0;
}

int
get_record(fpath, rptr, size, id)
    char *fpath;
    char *rptr;
    int size, id;
{
    int fd;

    if ((fd = open(fpath, O_RDONLY, 0)) != -1)
    {
	if (lseek(fd, size * (id - 1), SEEK_SET) != -1)
	{
	    if (read(fd, rptr, size) == size)
	    {
		close(fd);
		return 0;
	    }
	}
	close(fd);
    }
    return -1;
}


int
getbnum(bname)
    char *bname;
{
    register int i;
    register boardheader_t *bhdr;

    resolve_boards();
    for (i = 0, bhdr = bcache; i++ < numboards; bhdr++)
	/* if (Ben_Perm(bhdr)) */
	if (!ci_strcmp(bname, bhdr->brdname))
	    return i;
    return 0;
}


int
hash(key)
    char *key;
{
    int i, value = 0;

    for (i = 0; key[i] && i < 80; i++)
	value += key[i] < 0 ? -key[i] : key[i];

    value = value % HASHSIZE;
    return value;
}


/* ---------------------------------- */
/* hash structure : array + link list */
/* ---------------------------------- */


void
search(t)
    struct posttop *t;
{
    struct postrec *p, *q, *s;
    int i, found = 0;

    i = hash(t->title);
    q = NULL;
    p = bucket[i];
    while (p && (!found))
    {
	if (!strcmp(p->title, t->title) && !strcmp(p->board, t->board))
	    found = 1;
	else
	{
	    q = p;
	    p = p->next;
	}
    }
    if (found)
    {
	p->number += t->number;
	if (p->date < t->date)	/* ¨ú¸ûªñ¤é´Á */
	    p->date = t->date;
    }
    else
    {
	s = (struct postrec *) malloc(sizeof(struct postrec));
	memcpy(s, t, sizeof(struct posttop));
	s->next = NULL;
	if (q == NULL)
	    bucket[i] = s;
	else
	    q->next = s;
    }
}


int
sort(pp, count)
    struct postrec *pp;
{
    int i, j;

    for (i = 0; i <= count; i++)
    {
	if (pp->number > top[i].number)
	{
	    if (count < TOPCOUNT - 1)
		count++;
	    for (j = count - 1; j >= i; j--)
		memcpy(&top[j + 1], &top[j], sizeof(struct posttop));

	    memcpy(&top[i], pp, sizeof(struct posttop));
	    break;
	}
    }
    return count;
}


void
load_stat(fname)
    char *fname;
{
    FILE *fp;

    if((fp = fopen(fname, "r")))
    {
	int count = fread(top, sizeof(struct posttop), TOPCOUNT, fp);
	fclose(fp);
	while (count)
	    search(&top[--count]);
    }
}


int
filter(board)
    char *board;
{
    boardheader_t bh;
    int bid;

    bid = getbnum(board);
    if (get_record(".BOARDS", &bh, sizeof(bh), bid) == -1)
	return 1;
    if (bh.brdattr & BRD_NOCOUNT)
	return 1;
/*
  if (bh.brdattr & BRD_POSTMASK)
  return 0;
  return (bh.brdattr & ~BRD_POSTMASK) ||
  >= 32;
*/
    return 0;
}


void
poststat(mytype)
    int mytype;
{
    static char *logfile = ".post";
    static char *oldfile = ".post.old";

    FILE *fp;
    char buf[40], curfile[40] = "etc/day.0", *p;
    struct postrec *pp;
    int i, j;

    if (mytype < 0)
    {
	/* --------------------------------------- */
	/* load .post and statictic processing     */
	/* --------------------------------------- */

	remove(oldfile);
	Rename(logfile, oldfile);
	if ((fp = fopen(oldfile, "r")) == NULL)
	    return;
	mytype = 0;
	load_stat(curfile);

	while (fread(top, sizeof(struct posttop), 1, fp))
	    search(top);
	fclose(fp);
    }
    else
    {
	/* ---------------------------------------------- */
	/* load previous results and statictic processing */
	/* ---------------------------------------------- */

	i = mycount[mytype];
	p = myfile[mytype];
	while (i)
	{
	    sprintf(buf, "etc/%s.%d", p, i);
	    sprintf(curfile, "etc/%s.%d", p, --i);
	    load_stat(curfile);
	    Rename(curfile, buf);
	}
	mytype++;
    }

/* ---------------------------------------------- */
/* sort top 100 issue and save results            */
/* ---------------------------------------------- */

    memset(top, 0, sizeof(top));
    for (i = j = 0; i < HASHSIZE; i++)
    {
	for (pp = bucket[i]; pp; pp = pp->next)
	{
#ifdef  DEBUG
	    printf("Title : %s, Board: %s\nPostNo : %d, Author: %s\n"
		   ,pp->title
		   ,pp->board
		   ,pp->number
		   ,pp->author);
#endif

	    j = sort(pp, j);
	}
    }

    p = myfile[mytype];
    sprintf(curfile, "etc/%s.0", p);
    if((fp = fopen(curfile, "w")))
    {
	fwrite(top, sizeof(struct posttop), j, fp);
	fclose(fp);
    }

    sprintf(curfile, "etc/%s", p);
    if((fp = fopen(curfile, "w")))
    {
	int max, cnt;

	fprintf(fp, "\t\t[1;34m-----[37m=====[41m ¥»%s¤j¼öªù¸ÜÃD [40m=====[34m-----[0m\n\n", mytitle[mytype]);

	max = mytop[mytype];
	p = buf + 4;
	for (i = cnt = 0; (cnt < max) && (i < j); i++)
	{
	    tp = &top[i];
	    if (filter(tp->board))
		continue;

	    strcpy(buf, ctime(&(tp->date)));
	    buf[20] = 0;
	    fprintf(fp,
		    "[1;31m%3d. [33m¬ÝªO : [32m%-16s[35m¡m %s¡n[36m%4d ½g[33m%16s\n"
		    "     [33m¼ÐÃD : [0;44;37m%-60.60s[40m\n"
		    ,++cnt, tp->board, p, tp->number, tp->author, tp->title);
	}
	fclose(fp);
    }

/* free statistics */

    for (i = 0; i < HASHSIZE; i++)
    {
	struct postrec *pp0;

	pp = bucket[i];
	while (pp)
	{
	    pp0 = pp;
	    pp = pp->next;
	    free(pp0);
	}

	bucket[i] = NULL;
    }
}


int main(argc, argv)
    char *argv[];
{
    time_t now;
    struct tm *ptime;

    if (argc < 2)
    {
	printf("Usage:\t%s bbshome [day]\n", argv[0]);
	return (-1);
    }
    chdir(argv[1]);

    if (argc == 3)
    {
	poststat(atoi(argv[2]));
	return (0);
    }
    time(&now);
    ptime = localtime(&now);
    if (ptime->tm_hour == 0)
    {
	if (ptime->tm_mday == 1)
	    poststat(2);

	if (ptime->tm_wday == 0)
	    poststat(1);
	poststat(0);
    }
    poststat(-1);
    return 0;
}
