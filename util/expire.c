/* $Id$ */
/* 自動砍信工具程式 */

#include "bbs.h"

#define QCAST int (*)(const void *, const void *)

#define	DEF_DAYS	50
#define	DEF_MAXP        40000
#define	DEF_MINP	300

#define	EXPIRE_CONF	BBSHOME "/etc/expire.conf"
#ifdef  SAFE_ARTICLE_DELETE
char    safe_delete_only = 0;
#endif
extern  boardheader_t *bcache;
char    bpath[256];

typedef struct {
    char    bname[IDLEN + 1];	/* board ID */
    int     days;		/* expired days */
    int     maxp;		/* max post */
    int     minp;		/* min post */
} life_t;

void expire(life_t *brd)
{
    fileheader_t head;
    struct stat state;
    char lockfile[128], tmpfile[128], bakfile[128];
    char fpath[128], index[128], *fname;
    int total, bid;
    int fd, fdr, fdw, done, keep;
    int duetime, ftime;

    printf("%s\n", brd->bname);
    /* XXX: bid of cache.c's getbnum starts from 1 */
    if((bid = getbnum(brd->bname)) == 0 || 
	strcmp(brd->bname, bcache[bid-1].brdname))
     {
	 char    cmd[1024];
	 printf("no such board?: %s\n", brd->bname);
	 sprintf(cmd, "mv %s/boards/%c/%s %s/boards.error/%s",
		 BBSHOME, brd->bname[0], brd->bname, BBSHOME, brd->bname);
	 system(cmd);
        return;
     }
#ifdef	VERBOSE
    if (brd->days < 1)
    {
	printf(":Err: expire time must more than 1 day.\n");
	return;
    }
    else if (brd->maxp < 100)
    {
	printf(":Err: maxmum posts number must more than 100.\n");
	return;
    }
#endif

    sprintf(index, "%s/%s/.DIR", bpath, brd->bname);
    sprintf(lockfile, "%s.lock", index);
    if ((fd = open(lockfile, O_RDWR | O_CREAT | O_APPEND, 0644)) == -1)
	return;
    flock(fd, LOCK_EX);

    strcpy(fpath, index);
    fname = (char *) strrchr(fpath, '.');

    duetime = time(NULL) - brd->days * 24 * 60 * 60;
    done = 0;
    if ((fdr = open(index, O_RDONLY, 0)) > 0)
    {
	fstat(fdr, &state);
	total = state.st_size / sizeof(head);
	sprintf(tmpfile, "%s.new", index);
	unlink(tmpfile);
	if ((fdw = open(tmpfile, O_WRONLY | O_CREAT | O_EXCL, 0644)) > 0)
	{
	    while (read(fdr, &head, sizeof head) == sizeof head)
	    {
		done = 1;
		ftime = atoi(head.filename + 2);
		if (head.owner[0] == '-'
#ifdef SAFE_ARTICLE_DELETE
		    || strncmp(head.filename, ".delete", 7) == 0
#endif
		    )
		    keep = 0;
#ifdef SAFE_ARTICLE_DELETE
		else if( safe_delete_only )
		    keep = 1;
#endif
		else if (head.filemode & FILE_MARKED || total <= brd->minp)
		    keep = 1;
		else if (ftime < duetime || total > brd->maxp)
		    keep = 0;
		else
		    keep = 1;

		if (keep)
		{
		    if (write(fdw, (char *)&head, sizeof head) == -1)
		    {
			done = 0;
			break;
		    }
		}
		else
		{
		    strcpy(fname, head.filename);
		    unlink(fpath);
		    printf("\t%s\n", fname);
		    total--;
		}
	    }
	    close(fdw);
	}
	close(fdr);
    }

    if (done)
    {
	sprintf(bakfile, "%s.old", index);
	if (rename(index, bakfile) != -1)
         {
	    rename(tmpfile, index);
	    touchbtotal(bid);
         }
    }
    flock(fd, LOCK_UN);
    close(fd);
}

int     count;
life_t  db, table[MAX_BOARD], *key;

void toexpire(char *brdname)
{
    if( brdname[0] > ' ' && brdname[0] != '.' ){
	key = NULL;

	if( count )
	    key = (life_t *)bsearch(brdname, table, count,
				    sizeof(life_t), (QCAST)strcasecmp);
	if( key == NULL )
	    key = &db;

	strcpy(key->bname, brdname);
	expire(key);
    }
}

void visitdir(char c)
{
    DIR     *dirp;
    struct  dirent *de;

    sprintf(bpath, BBSHOME "/boards/%c", c);
    if (!(dirp = opendir(bpath))){
	printf(":Err: unable to open %s\n", bpath);
	return;
    }

    while( (de = readdir(dirp)) != NULL )
	if( de->d_name[0] != '.' )
	    toexpire(de->d_name);
    
    closedir(dirp);
}

int main(int argc, char **argv)
{
    FILE    *fin;
    int     number, i, ch;
    char    *ptr, *bname, buf[256];
    char    dirs[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
		      'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
		      'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',
		      'z', 'x', 'c', 'v', 'b', 'n', 'm',
		      'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
		      'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L',
		      'Z', 'X', 'C', 'V', 'B', 'N', 'M', NULL};

    /* default value */
    db.days = DEF_DAYS;
    db.maxp = DEF_MAXP;
    db.minp = DEF_MINP;

    while( (ch = getopt(argc, argv, "d:M:m:h"
#ifdef SAFE_ARTICLE_DELETE
"D"
#endif
			)) != -1 )
	switch( ch ){
#ifdef SAFE_ARTICLE_DELETE
	case 'D':
	    safe_delete_only = 1;
	    break;
#endif
	case 'd':
	    db.days = atoi(optarg);
	    break;
	case 'M':
	    db.maxp = atoi(optarg);
	    break;
	case 'm':
	    db.minp = atoi(optarg);
	    break;
	case 'h':
	default:
	    fprintf(stderr,
		    "usage: expire [-m minp] [-M MAXP] [-d days] [board name...]\n"
		    "deletion policy:\n"
		    "       do nothing if #articles < minp (default:%d)\n"
		    "       delete NOT MARKED articles which were post before days \n"
		    "        (default:%d) or #articles > MAXP (default:%d)\n",
		    DEF_MINP, DEF_DAYS, DEF_MAXP);
	    return 0;
	}
    argc -= optind;
    argv += optind;

/* --------------- */
/* load expire.ctl */
/* --------------- */

    count = 0;
    if( (fin = fopen(EXPIRE_CONF, "r")) ){
	while( fgets(buf, 256, fin) != NULL ){
	    if (buf[0] == '#')
		continue;

	    bname = (char *) strtok(buf, " \t\r\n");
	    if( bname && *bname ){
		ptr = (char *) strtok(NULL, " \t\r\n");
		if( ptr && (number = atoi(ptr)) > 0 ){
		    key = &(table[count++]);
		    strcpy(key->bname, bname);
		    key->days = number;
		    key->maxp = db.maxp;
		    key->minp = db.minp;

		    ptr = (char *) strtok(NULL, " \t\r\n");
		    if( ptr && (number = atoi(ptr)) > 0 ){
			key->maxp = number;
			
			ptr = (char *) strtok(NULL, " \t\r\n");
			if( ptr && (number = atoi(ptr)) > 0 ){
			    key->minp = number;
			}
		    }
		}
	    }
	}
	fclose(fin);
    }

    if( count > 1)
	qsort(table, count, sizeof(life_t), (QCAST)strcasecmp);

    attach_SHM();
    if( argc > 0 ){
	for( i = 0 ; i < argc ; ++i )
	    if( argv[i][1] == '*' )
		visitdir(argv[i][0]);
	    else{
		sprintf(bpath, BBSHOME "/boards/%c", argv[i][0]);
		toexpire(argv[i]);
	    }
    }
    else{ // visit all boards
	for( i = 0 ; dirs[i] != NULL ; ++i )
	    visitdir(dirs[i]);
    }

    return 0;
}
