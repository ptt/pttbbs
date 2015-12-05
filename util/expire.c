/* 自動砍信工具程式 */

#include "bbs.h"

#define QCAST int (*)(const void *, const void *)

#define	DEF_MAXP        30000

#define	EXPIRE_CONF	BBSHOME "/etc/expire2.conf"
#ifdef  SAFE_ARTICLE_DELETE
char    safe_delete_only = 0;
#endif
extern  boardheader_t *bcache;
int     checkmode = 0;

typedef struct {
    char    bname[IDLEN + 1];	/* board ID */
    int     maxp;		/* max post */
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
    int ftime, nKeep = 0, nDelete = 0;

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
    else if( brd->maxp < 100 ){
	printf(":Err: maxmum posts number must more than 100.\n");
	return;
    }
#endif
    cleanSR(brd->bname);

    setbfile(index, brd->bname, ".DIR");
    sprintf(lockfile, "%s.lock", index);
    if ((fdlock = OpenCreate(lockfile, O_RDWR | O_APPEND)) == -1){
	perror("open lock file error");
	return;
    }
    flock(fdlock, LOCK_EX);

    strcpy(fpath, index);
    fname = (char *) strrchr(fpath, '.');

    done = 0;
    if( (fdr = open(index, O_RDONLY, 0)) >= 0 ){
	fstat(fdr, &state);
	total = state.st_size / sizeof(head);
	if( !checkmode ){
	    sprintf(tmpfile, "%s.new", index);
	    unlink(tmpfile);
	}
	// TODO use fread/fwrite to reduce system calls
	if( checkmode ||
	    (fdw = OpenCreate(tmpfile, O_WRONLY | O_EXCL)) >= 0 ){
	    done = 1;
	    while (1) {
		int ret = read(fdr, &head, sizeof(head));
		if (ret != sizeof(head)) {
		    if (ret != 0)
			done = 0;
		    break;
		}
		ftime = atoi(head.filename + 2);
                if (head.owner[0] == '-' ||
                    (!*head.filename) ||
#ifdef SAFE_ARTICLE_DELETE
		    strcmp(head.filename, FN_SAFEDEL) == 0 ||
#ifdef FN_SAFEDEL_PREFIX_LEN
		    strncmp(head.filename, FN_SAFEDEL, FN_SAFEDEL_PREFIX_LEN) == 0 ||
#endif
#endif
                    0)
		    keep = 0;
#ifdef SAFE_ARTICLE_DELETE
		else if (safe_delete_only)
		    keep = 1;
#endif
		else if (head.filemode & FILE_MARKED)
		    keep = 1;
		else if (total > brd->maxp)
		    keep = 0;
		else
		    keep = 1;

		if( keep ){
		    ++nKeep;
		    if( !checkmode &&
			write(fdw, (char *)&head, sizeof(head)) != sizeof(head) ){
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
	    if (!checkmode) {
		if (fsync(fdw) != 0) {
		    perror("fsync");
		    done = 0;
		}
		close(fdw);
	    }
	}
	close(fdr);
    }

    if( !checkmode && done ){
	sprintf(bakfile, "%s.old", index);
	if( rename(index, bakfile) == 0 ){
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
    db.maxp = DEF_MAXP;

    while( (ch = getopt(argc, argv, "M:hn"
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
	case 'M':
	    db.maxp = atoi(optarg);
	    break;
	case 'n':
	    checkmode = 1;
	    break;
	case 'h':
	default:
	    fprintf(stderr,
		    "usage: expire [-M MAXP] [board name...] [-n]\n"
		    "deletion policy:\n"
		    "       delete NOT MARKED articles if #articles > MAXP (default:%d)\n",
		    DEF_MAXP);
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
		    key->maxp = number;
		}
	    }
	}
	fclose(fin);
    } else {
        printf("Sorry, failed to load configuration file %s.\n"
               "The format has been changed, so please make sure you do\n"
               "upgraded manually.\n", EXPIRE_CONF);
        exit(1);
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
