/* $Id$ */
/* tool to convert wretch bbs to ptt bbs */

#include "bbs.h"

struct wretch_fheader_t {
    time4_t chrono;
    char pad[4];
    int xmode;
    int xid;
    char xname[32];               /* 檔案名稱 */
    char owner[80];               /* 作者 (E-mail address) */
    char nick[50];                /* 暱稱 */
    char date[9];                 /* [96/12/01] */
    char title[73];               /* 主題 (TTLEN + 1) */
    char pad2[4];
};

#define GEM_RESTRICT    0x0800          /* 限制級精華區，須 manager 才能看 */
#define GEM_RESERVED    0x1000          /* 限制級精華區，須 sysop 才能更改 */
#define GEM_FOLDER      0x00010000      /* folder / article */
#define GEM_BOARD       0x00020000      /* 看板精華區 */

static const char radix32[32] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
    'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
    'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
};

char *path;
char *board;

int transman(char *fname, char *newpath)
{
    int fd;
    char *p;
    char buf[PATHLEN];
    struct wretch_fheader_t whdr;
    fileheader_t fhdr;

    if (fname[0] == '.')
	snprintf(buf, PATHLEN, "%s/%s", path, fname);
    else
	snprintf(buf, PATHLEN, "%s/%c/%s", path, fname[7], fname);

    if ((fd = open(buf, O_RDONLY, 0)) == -1)
	return -1;

    while (read(fd, &whdr, sizeof(whdr)) == sizeof(whdr)) {
	if (!(whdr.xmode & 0xffff0000)) {
	    /* article */
	    stampfile(newpath, &fhdr);
	    strlcpy(fhdr.title, "◇ ", sizeof(fhdr.title));

	    if (whdr.xname[0] == '@')
		snprintf(buf, sizeof(buf), "%s/@/%s", newpath, whdr.xname);
	    else
		snprintf(buf, sizeof(buf), "%s/%c/%s", newpath, whdr.xname[7], whdr.xname);

	    copy_file(buf, newpath);
	}
	else if (whdr.xmode & GEM_FOLDER) {
	    /* folder */
	    stampdir(newpath, &fhdr);
	    strlcpy(fhdr.title, "◆ ", sizeof(fhdr.title));

	    transman(whdr.xname, newpath);
	}

	p = strrchr(newpath, '/');
	*p = '\0';

	if (whdr.xmode & 0x0800)
	    fhdr.filemode |= FILE_BM;

	strncat(fhdr.title, whdr.title, sizeof(fhdr.title) - strlen(fhdr.title) - 1);
	strlcpy(fhdr.owner, whdr.owner, sizeof(fhdr.title));

	setadir(buf, newpath);
	append_record(buf, &fhdr, sizeof(fhdr));
    }

    close(fd);
    return 0;
}

int main(int argc, char *argv[])
{
    char buf[PATHLEN];

    if (argc < 2) {
	printf("%s <path> <board>\n", argv[0]);
	return 0;
    }

    path = argv[1];
    board = argv[2];

    if (!dashd(path)) {
	printf("%s is not directory\n", path);
	return 0;
    }

    setapath(buf, board);

    if (!dashd(buf)) {
	printf("%s is not directory\n", buf);
	return 0;
    }

    transman(".DIR", buf);

    return 0;
}
