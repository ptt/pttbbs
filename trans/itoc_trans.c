/* $Id$ */
/* tool to convert itoc bbs to ptt bbs */

#include "bbs.h"

typedef struct {
    time4_t chrono;
#ifdef FOR_WRETCH
    char pad[4]; // Use only for Wretch
#endif
    int xmode;
    int xid;
    char xname[32];               /* 檔案名稱 */
    char owner[80];               /* 作者 (E-mail address) */
    char nick[50];                /* 暱稱 */
    char date[9];                 /* [96/12/01] */
    char title[72];               /* 主題 (TTLEN + 1) */
    char score;
#ifdef FOR_WRETCH
    char pad2[4];
#endif
} itoc_HDR;

#define ITOC_POST_MARKED     0x00000002      /* marked */
#define ITOC_POST_BOTTOM1    0x00002000      /* 置底文章的正本 */

#define ITOC_GEM_RESTRICT    0x0800          /* 限制級精華區，須 manager 才能看 */
#define ITOC_GEM_RESERVED    0x1000          /* 限制級精華區，須 sysop 才能更改 */
#define ITOC_GEM_FOLDER      0x00010000      /* folder / article */
#define ITOC_GEM_BOARD       0x00020000      /* 看板精華區 */

static const char radix32[32] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
    'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
    'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
};

char *path;
int trans_man = 0;

int convert(char *fname, char *newpath)
{
    int fd;
    char *p;
    char buf[PATHLEN];
    itoc_HDR ihdr;
    fileheader_t fhdr;

    if (fname[0] == '.')
	snprintf(buf, sizeof(buf), "%s/%s", path, fname);
    else
	snprintf(buf, sizeof(buf), "%s/%c/%s", path, fname[7], fname);

    if ((fd = open(buf, O_RDONLY, 0)) == -1)
	return -1;

    while (read(fd, &ihdr, sizeof(ihdr)) == sizeof(ihdr)) {
	if (strcmp(ihdr.xname, "..") == 0 || strchr(ihdr.xname, '/'))
	    continue;

	if (!(ihdr.xmode & 0xffff0000)) {
	    /* article */
	    stampfile(newpath, &fhdr);

	    if (trans_man)
		strlcpy(fhdr.title, "◇ ", sizeof(fhdr.title));

	    if (ihdr.xname[0] == '@')
		snprintf(buf, sizeof(buf), "%s/@/%s", path, ihdr.xname);
	    else
		snprintf(buf, sizeof(buf), "%s/%c/%s", path, ihdr.xname[7], ihdr.xname);

	    fhdr.modified = dasht(buf);
	    if (ihdr.xmode & ITOC_POST_MARKED)
		fhdr.filemode |= FILE_MARKED;

	    copy_file(buf, newpath);
	}
	else if (ihdr.xmode & ITOC_GEM_FOLDER) {
	    /* folder */
	    if (!trans_man)
		continue;

	    stampdir(newpath, &fhdr);

	    if (trans_man)
		strlcpy(fhdr.title, "◆ ", sizeof(fhdr.title));

	    convert(ihdr.xname, newpath);
	}
	else {
	    /* ignore */
	    continue;
	}

	p = strrchr(newpath, '/');
	*p = '\0';

	if (ihdr.xmode & ITOC_GEM_RESTRICT)
	    fhdr.filemode |= FILE_BM;

	strlcat(fhdr.title, ihdr.title, sizeof(fhdr.title));
	strlcpy(fhdr.owner, ihdr.owner, sizeof(fhdr.owner));
	if (ihdr.date[0] && strlen(ihdr.date) == 8)
	    strlcpy(fhdr.date, ihdr.date + 3, sizeof(fhdr.date));
	else
	    strlcpy(fhdr.date, "     ", sizeof(fhdr.date));

	setadir(buf, newpath);
	append_record(buf, &fhdr, sizeof(fhdr));
    }

    close(fd);
    return 0;
}

int main(int argc, char *argv[])
{
    char buf[PATHLEN];
    char *board;
    int opt;

    while ((opt = getopt(argc, argv, "m")) != -1) {
	switch (opt) {
	    case 'm':
		trans_man = 1;
		break;
	    default:
		fprintf(stderr, "%s [-m] <path> <board>\n", argv[0]);
		fprintf(stderr, "  -m convert man (default is board)\n");
	}
    }

    if ((argc - optind + 1) < 2) {
	fprintf(stderr, "%s [-m] <path> <board>\n", argv[0]);
	fprintf(stderr, "  -m convert man (default is board)\n");
	return 0;
    }

    path = argv[optind];
    board = argv[optind+1];

    if (!dashd(path)) {
	fprintf(stderr, "%s is not directory\n", path);
	return 0;
    }

    attach_SHM();
    opt = getbnum(board);

    if (opt == 0) {
	fprintf(stderr, "ERR: board `%s' not found\n", board);
	return 0;
    }

    if (trans_man)
	setapath(buf, board);
    else
	setbpath(buf, board);

    if (!dashd(buf)) {
	fprintf(stderr, "%s is not directory\n", buf);
	return 0;
    }

    attach_SHM();

    convert(".DIR", buf);

    if (trans_man) {
	strlcat(buf, "/.rebuild", sizeof(buf));
	if ((opt = open(buf, O_CREAT, 0640)) > 0)
	    close(opt);
    }
    else
	touchbtotal(opt);

    return 0;
}
