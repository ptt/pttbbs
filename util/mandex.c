
/* 'mandex -h' to help */

#include "bbs.h"

// Where -s saves the list of files of each board: MANDEX_LIST_DIR/B/BOARD.

#ifndef MANDEX_LIST_DIR
#define MANDEX_LIST_DIR BBSHOME "/man/list"
#endif

typedef struct {
    char bname[BRDNAME_SIZE];
    int ndir, nfile, k;
} boardinfo_t;

static const char *color[4] = {"[1;33m", "[1;32m", "[1;36m", "[1;37m"};
static const char *fn_index = ".index";
static const char *fn_new = ".index.new";
static const char *index_title = "¡· ºëµØ°Ï¥Ø¿ý¯Á¤Þ";;
int     savelist = 0;
FILE    *flist;

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

	SNPRINTF(buf, "%.*s%s%3d. %s [m\n",
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
	else {
	    if (flist && !(fhdr.filemode&(FILE_BM|FILE_HIDE)))
		fprintf(flist, "%s\n", fpath);
	    curr_brdinfo.nfile++;
	}
    }

    fclose(fp_dir);
}

// Removes the S... links to .index left by earlier runs, except keep.
static void
remove_index_links(const char *brdname, const char *keep)
{
    char apath[PATHLEN], path[PATHLEN], target[16];
    struct dirent *de;
    DIR *dir;
    ssize_t n;

    setapath(apath, brdname);
    if (!(dir = opendir(apath)))
	return;
    while ((de = readdir(dir))) {
	if (de->d_name[0] != 'S' || strcmp(de->d_name, keep) == 0)
	    continue;
	snprintf(path, sizeof(path), "%s/%s", apath, de->d_name);
	if ((n = readlink(path, target, sizeof(target) - 1)) < 0)
	    continue;
	target[n] = '\0';
	if (strcmp(target, fn_index) == 0)
	    unlink(path);
    }
    closedir(dir);
}

void
man_index(const char * brdname)
{
    char buf[PATHLEN], fpath[PATHLEN], keep[FNSIZE], *p;
    int i, index_pos = -1;
    FILE *fp_dir;
    struct stat st;
    fileheader_t fhdr;
    const boardheader_t *bptr;
    char filemode = 0;

    if ((i = getbnum(brdname)) == 0)
	return;

    bptr = getbcache(i);
    if (strcmp(brdname, bptr->brdname) != 0)
	return;

    memset(&curr_brdinfo, 0, sizeof(curr_brdinfo));
    STRLCPY(curr_brdinfo.bname, brdname);

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

    SNPRINTF(buf, "%s.new", fpath);

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
	    // Made again below; an entry naming a real file keeps it.
	    setdirpath(buf, fpath, fhdr.filename);
	    if (dashl(buf))
		unlink(buf);
	    break;
	}
	i++;
    }
    if (index_pos >= 0)
        filemode = fhdr.filemode;

    // A link, not .index itself: a leading '.' marks a deleted record, and
    // deleting the entry in the menu removes only the link.
    p = strrchr(buf, '/');
    *p = '\0';
    stamplink(buf, &fhdr);
    unlink(buf);
    symlink(fn_index, buf);
    STRLCPY(keep, fhdr.filename);
    STRLCPY(fhdr.owner, "¨C¤Ñ¦Û°Ê§ó·s");
    SNPRINTF(fhdr.title, "%s (%.1fk)", index_title, st.st_size / 1024.);
    fhdr.filemode = filemode;

    SNPRINTF(buf, "%s.new", fpath);

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

    remove_index_links(brdname, keep);
}

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
    DIR     *dirp;
    struct  dirent *de;
    char    listpath[MAXPATHLEN];
    int     i, checkrebuild = 0;
    char    *fname, fpath[PATHLEN];

    // gem.svc keeps the gem areas and makes their tables of contents on
    // demand (gem.ctl index <board>): nothing to do here.
    if (IS_ENABLED(CONFIG_GEM)) {
        fprintf(stderr, "mandex: CONFIG_GEM is on, gem.svc makes the index\n");
        return 0;
    }

    nice(10);

    while ((i = getopt(argc, argv, "sxh")) != -1) {
	switch (i) {
	    case 'x':
		checkrebuild = 1;
		break;
	    case 's':
		savelist = 1;
		break;
	    case 'h':
		printf("NAME\n"
		       "     mandex - ºëµØ°Ï¯Á¤Þµ{¦¡ (man index)\n"
		       "\n"
		       "SYNOPSIS\n"
		       "     mandex [-x] [board] ...\n"
		       "\n"
		       "DESCRIPTION\n"
		       "ºëµØ°Ï¯Á¤Þ (man index)\n\n"
		       "-x    ¥u¦³§t¦³ .rebuildªº¥Ø¿ý¤~­«»s\n"
		       /* "-v    Åã¥Ü¥þ³¡¸ô®|\n" */
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
	for (i = 0; i < argc; i++) {
	    if (savelist) {
		snprintf(listpath, sizeof(listpath), MANDEX_LIST_DIR "/%c/%s",
			 argv[i][0], argv[i]);
		if (!(flist = fopen(listpath, "w")))
		    perror(listpath);
	    }

	    man_index(argv[i]);

	    if (flist) {
		fclose(flist);
		flist = NULL;
	    }
	}

	return 0;
    }

    /* process all boards */
    for (i = 0; dirs[i] != 0; ++i) {
	SNPRINTF(fpath, "man/boards/%c", dirs[i]);
	if (!(dirp = opendir(fpath))) {
	    printf("## unable to enter [man/boards/%c]\n", dirs[i]);
	    exit(-1);
	}
    
	while ((de = readdir(dirp))) {
	    fname = de->d_name;

	    if (fname[0] == '.')
		continue;

	    if (checkrebuild) {
		setafile(fpath, fname, ".rebuild");
		if (access(fpath, 0) < 0) {
		    printf("skip no modify board %s\n", fname);
		    fflush(stdout); // in case the output is redirected...
		    continue;
		}
		unlink(fpath);
	    }

	    if (savelist) {
		snprintf(listpath, sizeof(listpath), MANDEX_LIST_DIR "/%c/%s",
			 dirs[i], fname);
		if (!(flist = fopen(listpath, "w")))
		    perror(listpath);
	    }

	    man_index(fname);

	    if (flist) {
		fclose(flist);
		flist = NULL;
	    }

	}
	closedir(dirp);
    }
    // Leftover cache of the removed gem area ranking.
    unlink(BBSHOME "/man/.rank.cache");
    return 0;
}
