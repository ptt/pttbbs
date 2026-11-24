/* 自動砍信工具程式 */

#include "bbs.h"

#define QCAST int (*)(const void *, const void *)

#define	DEF_MAXP        30000

#define	EXPIRE_CONF	BBSHOME "/etc/expire2.conf"
char    safe_delete_only = 0;
extern  boardheader_t *bcache;
int     checkmode = 0;

typedef struct {
    char    bname[BRDNAME_SIZE];	/* board ID */
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

// The posts expire() purges, by their file names (sorted).
typedef struct {
    char (*names)[FNLEN];
    int n;
} purge_set_t;

static int
cmp_name(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static int
purge_selected(const fileheader_t *fh, int index GCC_UNUSED, void *ctx)
{
    const purge_set_t *set = ctx;
    char name[FNLEN];

    // Marked (or pinned) since it was picked: kept, as when picking.
    if (fh->owner[0] != '-' && (fh->filemode & (FILE_MARKED | FILE_BOTTOM)))
        return 0;
    strlcpy(name, fh->filename, sizeof(name));
    return bsearch(name, set->names, set->n, FNLEN, cmp_name) != NULL;
}

void expire(life_t *brd)
{
    fileheader_t head;
    char index[PATHLEN], fpath[PATHLEN];
    int total, bid, keep;
    int nKeep = 0, nDelete = 0;
    purge_set_t set = { NULL, 0 };
    FILE *fp;

    printf("%s\n", brd->bname);
    /* XXX: bid of cache.c's getbnum starts from 1 */
    if( (bid = getbnum(brd->bname)) == 0 ||
	strcmp(brd->bname, bcache[bid - 1].brdname) ){
	printf("no such board?: %s\n", brd->bname);
	AUTO_STR(cmd, "mv "BBSHOME"/boards/%c/%s "BBSHOME"/boards.error/%s",
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

    // Which posts expire, then they are purged through bof (Lemmy's
    // PurgePost), which rewrites the list under its lock.
    setbfile(index, brd->bname, FN_DIR);
    if (!(fp = fopen(index, "rb")))
	return;
    fseek(fp, 0, SEEK_END);
    total = ftell(fp) / sizeof(head);
    rewind(fp);
    set.names = calloc(total > 0 ? total : 1, FNLEN);
    if (!set.names) {
	fclose(fp);
	return;
    }
    while (set.n < total && fread(&head, sizeof(head), 1, fp) == 1) {
                if (head.owner[0] == '-' ||
                    (!*head.filename) ||
		    (IS_ENABLED(CONFIG_SAFE_ARTICLE_DELETE) &&
		     strcmp(head.filename, FN_SAFEDEL) == 0) ||
                    0)
		    keep = 0;
		else if (IS_ENABLED(CONFIG_SAFE_ARTICLE_DELETE) && safe_delete_only)
		    keep = 1;
		else if (head.filemode & (FILE_MARKED | FILE_BOTTOM))
		    keep = 1;
		else if (total - nDelete > brd->maxp)
		    keep = 0;
		else
		    keep = 1;

	if (keep) {
	    ++nKeep;
	    continue;
	}
	++nDelete;
	strlcpy(set.names[set.n++], head.filename, FNLEN);
	if (checkmode && head.filename[0]) {
	    setbfile(fpath, brd->bname, head.filename);
	    callrm(fpath);
	}
    }
    fclose(fp);

    if (!checkmode && set.n > 0) {
	bof_actor_t sys = { .name = "system" };
	bof_purge_posts_t req = {
	    .community = brd->bname, .bbs_dir = index,
	    .bbs_selected = purge_selected, .bbs_ctx = &set, .reason = "expire",
	};
	int n = 0;
	qsort(set.names, set.n, FNLEN, cmp_name);
	if (bof_purge_posts(&sys, &req, &n) == BOF_OK) {
	    nDelete = n;
	    touchbtotal(bid);
	} else {
	    printf("board %s: could not purge the expired posts.\n", brd->bname);
	    nDelete = 0;
	}
    }
    free(set.names);

    printf("board %s: %d articles are kept, %d articles are deleted.\n",
	   brd->bname, nKeep, nDelete);
}

int     count;
life_t  db, table[MAX_BOARD], *key;

void toexpire(char *brdname)
{
    if (!is_valid_brdname(brdname))
	return;

    key = NULL;

    if( count )
	key = (life_t *)bsearch(brdname, table, count,
		sizeof(life_t), (QCAST)strcasecmp);
    if( key == NULL )
	key = &db;

    STRLCPY(key->bname, brdname);
    expire(key);
}

void visitdir(char c)
{
    DIR     *dirp;
    struct  dirent *de;
    AUTO_STR(bpath, BBSHOME "/boards/%c", c);

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

    while( (ch = getopt(argc, argv,
			IS_ENABLED(CONFIG_SAFE_ARTICLE_DELETE) ? "M:hnD" : "M:hn"
			)) != -1 )
	switch( ch ){
	case 'D':
	    safe_delete_only = 1;
	    break;
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
		    STRLCPY(key->bname, bname);
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
