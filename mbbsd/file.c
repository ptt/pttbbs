/* $Id: file.c 2191 2004-09-10 00:49:47Z victor $ */

#include "bbs.h"

int file_count_line(char *file)
{
    FILE           *fp;
    int             count = 0;
    char            buf[200];

    if ((fp = fopen(file, "r"))) {
	while (fgets(buf, sizeof(buf), fp))
	    count++;
	fclose(fp);
    }
    return count;
}

int file_append_line(char *file, char *string)
{
    FILE *fp;
    if ((fp = fopen(file, "a")) == NULL)
	return -1;
    flock(fileno(fp), LOCK_EX);
    fputs(string, fp);
    flock(fileno(fp), LOCK_UN);
    fclose(fp);
    return 0;
}

#ifndef _BBS_UTIL_C_
/* Rename() is in kaede.c but not linked to util/ */
int file_delete_line(char *file, char *string, char case_sensitive)
{
    FILE           *fp, *nfp = NULL;
    char            fnew[80];
    char            genbuf[STRLEN + 1];

    sprintf(fnew, "%s.%3.3X", file, rand() & 0xFFF);
    if ((fp = fopen(file, "r")) && (nfp = fopen(fnew, "w"))) {
	int             length = strlen(string);

	while (fgets(genbuf, sizeof(genbuf), fp))
	    if ((genbuf[0] > ' ')) {
		if (((case_sensitive && strncmp(genbuf, string, length)) ||
		    (!case_sensitive && strncasecmp(genbuf, string, length))))
    		    fputs(genbuf, nfp);
	    }
	Rename(fnew, file);
    }
    if(fp)
	fclose(fp);
    if(nfp)
	fclose(nfp);
    return 0;
}
#endif

int file_exist_record(char *file, char *string)
{
    FILE           *fp;
    char            buf[STRLEN], *ptr;

    if ((fp = fopen(file, "r")) == NULL)
	return 0;

    while (fgets(buf, STRLEN, fp)) {
	if ((ptr = strtok(buf, str_space)) && !strcasecmp(ptr, string))
	    return 1;
    }
    fclose(fp);
    return 0;
}

int file_foreach_entry(char *file, int (*func)(char *, int), int info)
{
    char line[80];
    FILE *fp;

    if ((fp = fopen(file, "r")) == NULL)
	return -1;

    while (fgets(line, sizeof(line), fp)) {
	(*func)(line, info);
    }

    fclose(fp);
    return 0;
}
