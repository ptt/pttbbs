/* $Id$ */
/* 自動砍信工具程式 */

#include "bbs.h"

#define QCAST int (*)(const void *, const void *)

#define	DEF_DAYS	60
#define	DEF_MAXP        10000
#define	DEF_MINP	9000

#define	EXPIRE_CONF	BBSHOME "/etc/expire.conf"
#ifdef  SAFE_ARTICLE_DELETE
char    safe_delete_only = 0;
#endif
extern  boardheader_t *bcache;
int     checkmode = 0;

typedef struct {
    char    bname[IDLEN + 1];	/* board ID */
    int     days;		/* expired days */
    int     maxp;		/* max post */
    int     minp;		/* min post */
} life_t;

void callsystem(char *s)
{
    if( checkmode )
	printf("in checkmode, skip `%s`\n", s);
    else
	system(s);
}

void callrm(char *s)
{
    if( checkmode )
	printf("\tin checkmode, skip rm %s\n", s);
    else
	unlink(s);
}

void cleanSR(char *brdname)
{
    DIR     *dirp;
    char    dirf[PATHLEN], fpath[PATHLEN];
    struct  dirent  *ent;
    int     nDelete = 0;
    setbpath(dirf, brdname);
    if( (dirp = opendir(dirf)) == NULL )
	return;

    while( (ent = readdir(dirp)) != NULL )
	if( strncmp(ent->d_name, "SR.", 3) == 0 ){
	    setbfile(fpath, brdname, ent->d_name);
	    callrm(fpath);
	    ++nDelete;
	}

    closedir(dirp);
    printf("board %s: %d SRs are deleted.\n", brdname, nDelete);
}

void expire(life_t *brd)
{
    fileheader_t head;
    struct stat state;
    char lockfile[128], tmpfile[128], bakfile[128], cmd[256];
    char fpath[128], index[128], *fname;
    int total, bid;
    int fdlock, fdr, fdw = 0, done, keep;
    int duetime, ftime, nKeep = 0, nDelete = 0;

    printf("%s\n", brd->bname);
    /* XXX: bid of cache.c's getbnum starts from 1 */
    if( (bid = getbnum(brd->bname)) == 0 || 
	strcmp(brd->bname, bcache[bid - 1].brdname) ){
	printf("no such board?: %s\n", brd->bname);
	sprintf(cmd, "mv "BBSHOME"/boards/%c/%s "BBSHOME"/boards.error/%s",
		brd->bname[0], brd->bname, brd->bname);
	callsystem(cmd);
        return;
    }
#ifdef	VERBOSE
    if( brd->days < 1 ){
	printf(":Err: expire time must more than 1 day.\n");
	return;
    }
    else if( brd->maxp < 100 ){
	printf(":Err: maxmum posts number must more than 100.\n");
	return;
    }
#endif
    cleanSR(brd->bname);

    setbfile(index, brd->bname, ".DIR");
    sprintf(lockfile, "%s.lock", index);
    if ((fdlock = open(lockfile, O_RDWR | O_CREAT | O_APPEND, 0644)) == -1){
	perror("open lock file error");
	return;
    }
    flock(fdlock, LOCK_EX);

    strcpy(fpath, index);
    fname = (char *) strrchr(fpath, '.');

    duetime = (int)time(NULL) - brd->days * 24 * 60 * 60;
    done = 0;
    if( (fdr = open(index, O_RDONLY, 0)) > 0 ){
	fstat(fdr, &state);
	total = state.st_size / sizeof(head);
	if( !checkmode ){
	    sprintf(tmpfile, "%s.new", index);
	    unlink(tmpfile);
	}
	// TODO use fread/fwrite to reduce system calls
	if( checkmode ||
	    (fdw = open(tmpfile, O_WRONLY | O_CREAT | O_EXCL, 0644)) > 0 ){
	    while( read(fdr, &head, sizeof(head)) == sizeof(head) ){
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
		else if( head.filemode & FILE_MARKED || total <= brd->minp )
		    keep = 1;
		else if( ftime < duetime || total > brd->maxp )
		    keep = 0;
		else
		    keep = 1;

		if( keep ){
		    ++nKeep;
		    if( !checkmode &&
			write(fdw, (char *)&head, sizeof(head)) == -1 ){
			done = 0;
			break;
		    }
		}
		else {
		    ++nDelete;
		    if (head.filename[0] != '\0') {
			setbfile(fname, brd->bname, head.filename);
			callrm(fname);
		    }
		    total--;
		}
	    }
	    if( !checkmode )
		close(fdw);
	}
	close(fdr);
    }

    if( !checkmode && done ){
	sprintf(bakfile, "%s.old", index);
	if( rename(index, bakfile) != -1 ){
	    rename(tmpfile, index);
	    touchbtotal(bid);
	}
    }

    printf("board %s: %d articles are kept, %d articles are deleted.\n",
	   brd->bname, nKeep, nDelete);
    flock(fdlock, LOCK_UN);
    close(fdlock);
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
    char bpath[PATHLEN];

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
		      'Z', 'X', 'C', 'V', 'B', 'N', 'M', 0};

    chdir(BBSHOME);
    /* default value */
    db.days = DEF_DAYS;
    db.maxp = DEF_MAXP;
    db.minp = DEF_MINP;

    while( (ch = getopt(argc, argv, "d:M:m:hn"
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
	case 'n':
	    checkmode = 1;
	    break;
	case 'h':
	default:
	    fprintf(stderr,
		    "usage: expire [-m minp] [-M MAXP] [-d days] [board name...] [-n]\n"
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
		toexpire(argv[i]);
	    }
    }
    else{ // visit all boards
	for( i = 0 ; dirs[i] != 0 ; ++i )
	    visitdir(dirs[i]);
    }

    return 0;
}
