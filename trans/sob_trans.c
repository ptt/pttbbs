/* $Id$ */
/* tool to convert sob bbs to ptt bbs, based on sob 20030803 pack */

#include "bbs.h"

typedef struct {
    char filename[32];
    char pad1[2];
    char owner[14];
    char date[6];
    char title[73];
    unsigned char filemode;
} sob_fileheader_t;

#define SOB_FILE_MARKED 0x02

int convert(const char *oldpath, char *newpath)
{
    int fd;
    char *p;
    char buf[PATHLEN];
    sob_fileheader_t whdr;
    fileheader_t fhdr;

    setadir(buf, oldpath);

    if ((fd = open(buf, O_RDONLY, 0)) == -1)
	return -1;

    while (read(fd, &whdr, sizeof(whdr)) == sizeof(whdr)) {
	snprintf(buf, sizeof(buf), "%s/%s", oldpath, whdr.filename);

	if (dashd(buf)) {
	    /* folder */
	    stampdir(newpath, &fhdr);

	    convert(buf, newpath);
	}
	else {
	    /* article */
	    stampfile(newpath, &fhdr);

	    fhdr.modified = dasht(buf);
	    if (whdr.filemode & SOB_FILE_MARKED)
		fhdr.filemode |= FILE_MARKED;

	    copy_file(buf, newpath);
	}

	p = strrchr(newpath, '/');
	*p = '\0';

	strlcpy(fhdr.title, whdr.title, sizeof(fhdr.title));
	strlcpy(fhdr.owner, whdr.owner, sizeof(fhdr.owner));
	strlcpy(fhdr.date, whdr.date, sizeof(fhdr.date));

	setadir(buf, newpath);
	append_record(buf, &fhdr, sizeof(fhdr));
    }

    close(fd);
    return 0;
}

int main(int argc, char *argv[])
{
    char buf[PATHLEN];
    char *path;
    char *board;
    int opt;
    int trans_man = 0;

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

    convert(path, buf);

    if (trans_man) {
	strlcat(buf, "/.rebuild", sizeof(buf));
	if ((opt = open(buf, O_CREAT, 0640)) > 0)
	    close(opt);
    }
    else
	touchbtotal(opt);

    return 0;
}
