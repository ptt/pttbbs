/* $Id$ */

/* 'mandex -h' to help */

#include "bbs.h"

typedef struct {
    char bname[40];
    int ndir, nfile, k;
} boardinfo_t;

static const char *color[4] = {"[1;33m", "[1;32m", "[1;36m", "[1;37m"};
static const char *fn_index = ".index";
static const char *fn_new = ".index.new";
static const char *index_title = "¡· ºëµØ°Ï¥Ø¿ý¯Á¤Þ";;

int sortbyname(const void *a, const void *b)
{
    return strcmp(((boardinfo_t*)b)->bname, ((boardinfo_t*)a)->bname);
}

int k_cmp(const void *x, const void *y)
{
    const boardinfo_t *b = (boardinfo_t *)x, *a = (boardinfo_t *)y;
    return ((a->k / 100 + a->ndir + a->nfile) - (b->k / 100 + b->ndir + b->nfile));
}

static FILE *fp_index;
static boardinfo_t curr_brdinfo;

/* visit the hierarchy recursively */
void
mandex(const int level, const char *num_header, char *fpath)
{
    FILE *fp_dir;
    char *fname, buf[512];
    int count;
    fileheader_t fhdr;

    if ((fp_dir = fopen(fpath, "r")) == NULL)
	return;

    fname = strrchr(fpath, '/') + 1;
    count = 0;

    while (fread(&fhdr, sizeof(fhdr), 1, fp_dir) == 1) {
	++count;

	if (!fhdr.filename[0])
	    continue;

	*fname = '\0';
	strlcat(fpath, fhdr.filename, PATHLEN);

	snprintf(buf, sizeof(buf), "%.*s%s%3d. %s [m\n",
		11 * level, num_header, color[level % 4], count, fhdr.title);
	                                                              /* Ptt */
	fputs(buf, fp_index);

	if (dashd(fpath)) {
	    curr_brdinfo.ndir++;
	    if (level < 10 && !(fhdr.filemode & (FILE_BM|FILE_HIDE))) {
		strlcat(fpath, "/" FN_DIR, PATHLEN);
		mandex(level + 1, buf, fpath);
	    }
	}
	else
	    curr_brdinfo.nfile++;
    }

    fclose(fp_dir);
}

void
man_index(const char * brdname)
{
    char buf[PATHLEN], fpath[PATHLEN], *p;
    int i, index_pos = -1;
    FILE *fp_dir;
    struct stat st;
    fileheader_t fhdr;
    const boardheader_t *bptr;

    if ((i = getbnum(brdname)) == 0)
	return;

    bptr = getbcache(i);
    if (strcmp(brdname, bptr->brdname) != 0)
	return;

    memset(&curr_brdinfo, 0, sizeof(curr_brdinfo));
    strlcpy(curr_brdinfo.bname, brdname, sizeof(curr_brdinfo.bname));

    setapath(buf, brdname);
    setadir(fpath, buf);

    setdirpath(buf, fpath, fn_new);

    if ((fp_index = fopen(buf, "w")) == NULL)
	return;

    fprintf(fp_index, "[1;32m§Ç¸¹\t\t\tºëµØ°Ï¥DÃD[m\n"
	    "[36m¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w"
	    "¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w[m\n");
    printf("%s ", fpath);
    mandex(0, "", fpath);
    fclose(fp_index);
    stat(buf, &st);
    curr_brdinfo.k = st.st_size;
    printf("(%s)[%dK] d: %d f: %d\n", buf, curr_brdinfo.k, curr_brdinfo.ndir, curr_brdinfo.nfile);
    fflush(stdout); // in case the output is redirected...

    setdirpath(fpath, buf, fn_index);
    rename(buf, fpath);
    setdirpath(fpath, buf, FN_DIR);

    sprintf(buf, "%s.new", fpath);

    if ((fp_dir = fopen(fpath, "r+")) == NULL)
	return;

    if ((fp_index = fopen(buf, "w")) == NULL) {
	fclose(fp_dir);
	return;
    }

    i = 0;
    while (fread(&fhdr, sizeof(fhdr), 1, fp_dir) == 1) {
	if (strncmp(fhdr.title, index_title, strlen(index_title)) == 0) {
	    index_pos = i;
	    setdirpath(buf, fpath, fhdr.filename);
	    unlink(buf);
	    break;
	}
	i++;
    }

    p = strrchr(buf, '/');
    *p = '\0';
    stamplink(buf, &fhdr);
    unlink(buf);
    symlink(fn_index, buf);
    strlcpy(fhdr.owner, "¨C¤Ñ¦Û°Ê§ó·s", sizeof(fhdr.owner));
    snprintf(fhdr.title, sizeof(fhdr.title), "%s (%.1fk)", index_title, st.st_size / 1024.);

    sprintf(buf, "%s.new", fpath);

    if (index_pos >= 0) {
	fseek(fp_dir, index_pos * sizeof(fhdr), SEEK_SET);
	fwrite(&fhdr, sizeof(fhdr), 1, fp_dir);
	unlink(buf);
    } else {
	fwrite(&fhdr, sizeof(fhdr), 1, fp_index);
	rewind(fp_dir);
	while (fread(&fhdr, sizeof(fhdr), 1, fp_dir) == 1)
	    fwrite(&fhdr, sizeof(fhdr), 1, fp_index);
	rename(buf, fpath);
    }

    fclose(fp_index);
    fclose(fp_dir);
}

void
output_chart(const boardinfo_t * board, const int nbrds)
{
    FILE * fp;
    int i, n, place = 0;
    boardheader_t *bptr = NULL;

    if (!(fp = fopen(BBSHOME "/etc/topboardman", "w")))
	return;

    fprintf(fp, "[1;44m±Æ¦W[47;30m        ¬Ý ªO  ¥Ø¿ý¼Æ   ÀÉ®×¼Æ"
	    "     byte¼Æ [30m  Á` ¤À     ªO   ¥D          [m\n");

    for (i = 0; i < nbrds; i++) {
	if ((n = getbnum(board[i].bname)) == 0)
	    continue;

	bptr = getbcache(n);
	if (strcmp(board[i].bname, bptr->brdname) != 0)
	    continue;

        // ÁôªO¤F§O¤H¬Ý¤£¨ìÁÙ¦³¤°»ò¦n»¡ªº¡H
	if (bptr->brdattr & (BRD_BAD | BRD_NOCOUNT | BRD_HIDE))
	    continue;

	if (board[i].ndir + board[i].nfile < 5)
	    break;

	fprintf(fp, "%3d.[33m%15s[m %5d %7d %10d   %6d [31m%-24.24s[m\n",
		++place, board[i].bname, board[i].ndir, board[i].nfile, board[i].k
		,board[i].k / 100 + board[i].nfile + board[i].ndir
		,bptr->BM);
    }
    fclose(fp);
}
static boardinfo_t board[MAX_BOARD+1];
static const char dirs[] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
	'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
	'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
	'u', 'v', 'w', 'x', 'y', 'z',
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
	'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
	'U', 'V', 'W', 'X', 'Y', 'Z', 0};

int main(int argc, char* argv[])
{
    boardinfo_t *biptr;
    int     nSorted, nb;
    DIR     *dirp;
    struct  dirent *de;
    int     i, fd, checkrebuild = 0;
    char    *fname, fpath[PATHLEN];

    nice(10);
    while ((i = getopt(argc, argv, "xh")) != -1) {
	switch (i) {
	    case 'x':
		checkrebuild = 1;
		break;
	    case 'h':
		printf("NAME\n"
		       "     mandex - ºëµØ°Ï¯Á¤Þµ{¦¡ (man index)\n"
		       "\n"
		       "SYNOPSIS\n"
		       "     mandex [-x] [-v] [board] ...\n"
		       "\n"
		       "DESCRIPTION\n"
		       "ºëµØ°Ï¯Á¤Þ (man index)\n\n"
		       "-x    ¥u¦³§t¦³ .rebuildªº¥Ø¿ý¤~­«»s\n"
		       "-v    Åã¥Ü¥þ³¡¸ô®|\n"
		       "board ¥þ³¡ªºªO (default to all)\n\n");
		return 0;
	}
    }

    argc -= optind;
    argv += optind;

    chdir(BBSHOME);

    attach_SHM();
    resolve_boards();

    /* process boards given in arguments */
    if (argc > 0) {
	for (i = 0; i < argc; i++)
	    man_index(argv[i]);

	return 0;
    }

    /* process all boards */
    if (checkrebuild &&	(fd = open("man/.rank.cache", O_RDONLY)) >= 0) {
	int dirty = 0;
	read(fd, board, sizeof(board));
	close(fd);
	qsort(board, MAX_BOARD, sizeof(boardinfo_t), sortbyname);
	for (nb = 0; board[nb].bname[0] != 0; ++nb) {
	    /* delete non-exist boards */
	    if (getbnum(board[nb].bname) == 0) {
		memset(&(board[nb]), 0, sizeof(boardinfo_t));
		dirty = 1;
	    }
	}
	/* sort again if dirty */
	if (dirty) {
	    qsort(board, MAX_BOARD, sizeof(boardinfo_t), sortbyname);
	    for (nb = 0; board[nb].bname[0] != 0; ++nb);
	}
	nSorted = nb;
    } else {
	memset(board, 0, sizeof(board));
	checkrebuild = 0;
	nSorted = nb = 0;
    }

    for (i = 0; dirs[i] != 0; ++i) {
	snprintf(fpath, sizeof(fpath), "man/boards/%c", dirs[i]);
	if (!(dirp = opendir(fpath))) {
	    printf("## unable to enter [man/boards/%c]\n", dirs[i]);
	    exit(-1);
	}
    
	while ((de = readdir(dirp))) {
	    fname = de->d_name;

	    if (fname[0] == '.')
		continue;

	    if (checkrebuild) {
		sprintf(fpath, "man/boards/%c/%s/.rebuild", dirs[i], fname);
		if (access(fpath, 0) < 0) {
		    printf("skip no modify board %s\n", fname);
		    fflush(stdout); // in case the output is redirected...
		    continue;
		}
		unlink(fpath);
	    }

	    man_index(fname);

	    if (!curr_brdinfo.k)
		continue;

	    // determine if this board was not seen in partial update queue
	    biptr = bsearch(fname, board, nSorted, sizeof(boardinfo_t), sortbyname);
	    if (!biptr)
	    {
		// give up if exceeded max entry
		if (nb >= MAX_BOARD)
		    continue;
		biptr = &board[nb++];
	    }

	    // update record
	    assert(biptr);
	    memcpy(biptr, &curr_brdinfo, sizeof(boardinfo_t));
	}
	closedir(dirp);
    }
    
    qsort(board, nb, sizeof(boardinfo_t), k_cmp);
    output_chart(board, nb);
    unlink(BBSHOME "/man/.rank.cache");

    if ((fd = open(BBSHOME "/man/.rank.cache",
		   O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0660)) >= 0) {
	write(fd, board, sizeof(board));
	close(fd);
    }

    return 0;
}
