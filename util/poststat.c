/* $Id$ */
/* ²Î­p¤µ¤é¡B¶g¡B¤ë¡B¦~¼öªù¸ÜÃD */

#include "bbs.h"
#include "fnv_hash.h"

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
    time4_t date;		/* last post's date */
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
    time4_t date;		/* last post's date */
    int number;			/* post number */
}
top[TOPCOUNT], *tp;

/*
   woju
   Cross-fs rename()
 */

int Rename(const char *src, const char *dst)
{

    if (rename(src, dst) == 0)
	return 0;
/*
  sprintf(cmd, "/bin/mv %s %s", src, dst);
  return system(cmd);
*/
    return 0;
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


/* ---------------------------------- */
/* hash structure : array + link list */
/* ---------------------------------- */


void
search(t)
    struct posttop *t;
{
    struct postrec *p, *q, *s;
    int i, found = 0;

    i = fnv1a_32_str(t->title, FNV1_32_INIT) % HASHSIZE;
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

    /* XXX: bid of cache.c's getbnum starts from 1 */
    bid = getbnum(board);
    if (get_record(".BRD", &bh, sizeof(bh), bid) == -1)
	return 1;
    if (bh.brdattr & BRD_NOCOUNT)
	return 1;

    /* ªO¥D³]©w¤£¦C¤J°O¿ý */
    if (bh.brdattr & BRD_HIDE && !(bh.brdattr & BRD_BMCOUNT))
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

	    strcpy(buf, ctime4(&(tp->date)));
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
    time4_t now;
    struct tm *ptime;

    attach_SHM();
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
    now = (time4_t)time(NULL);
    ptime = localtime4(&now);
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
