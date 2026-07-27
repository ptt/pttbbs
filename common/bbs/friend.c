#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "cmbbs.h"
#include "cmsys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char * const friend_file[] = {
    FN_OVERRIDES,
    FN_REJECT,
    FN_ALOHA,
};

static int
delete_friend_from_file(const char *file, const char *string, int case_sensitive)
{
    FILE *fp = NULL, *nfp = NULL;
    char fnew[PATHLEN];
    char genbuf[STRLEN + 1], buf[STRLEN];
    int ret = 0;

    SNPRINTF(fnew, "%s.%3.3X", file, (unsigned int)(arc4random_uniform(0x1000)));
    if ((fp = fopen(file, "r")) && (nfp = fopen(fnew, "w"))) {
        while (fgets(genbuf, sizeof(genbuf), fp)) {
            if (genbuf[0] > ' ') {
                genbuf[sizeof(genbuf) - 1] = 0;
                sscanf(genbuf, " %s", buf);
                genbuf[sizeof(buf) - 1] = 0;
                if (((case_sensitive && strcmp(buf, string)) ||
                    (!case_sensitive && strcasecmp(buf, string))))
                    fputs(genbuf, nfp);
                else
                    ret = 1;
            }
        }
        Rename(fnew, file);
    }
    if (fp)
        fclose(fp);
    if (nfp)
        fclose(nfp);
    return ret;
}

static void
delete_user_friend(const char *uident, const char *thefriend, int type)
{
    char fn[PATHLEN];
    if (!uident || !thefriend || strcasecmp(uident, thefriend) == 0)
        return;
    if (type < 0 || type >= 3)
        return;
    sethomefile(fn, uident, friend_file[type]);
    delete_friend_from_file(fn, thefriend, 0);
}

void
friend_delete_all(const char *uident, int type)
{
    char buf[PATHLEN], line[PATHLEN];
    FILE *fp;

    if (!uident || !*uident || type < 0 || type >= 3)
        return;

    sethomefile(buf, uident, friend_file[type]);
    if ((fp = fopen(buf, "r")) == NULL)
        return;

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%s", buf) == 1 && is_validuserid(buf)) {
            delete_user_friend(buf, uident, type);
        }
    }
    fclose(fp);
}
