#define _UTIL_C_
#include "bbs.h"

/* Remove an user from any BM of existing boards */

int check(void *data, int bid, boardheader_t *bh)
{
    char changed = 0;
    char has_quote = 0;
    char *p;
    const char *userid = (const char *) data;
    char bmsrc[IDLEN * 3 + 3], bmout[IDLEN * 3 + 3] = "";
    if (!bh->brdname[0] || !bh->BM[0])
        return 0;

    if (!strcasestr(bh->BM, userid))
        return 0;

    strlcpy(bmsrc, bh->BM, sizeof(bmsrc));
    p = bmsrc;

    if (*p == '[') {
        p++;
        has_quote = 1;
    }
    p = strtok(p,"/ ]");

    while (p) {
        const char *bmid = p;
        p = strtok(NULL, "/ ]");

        if (!*bmid)
            continue;

        if (strcasecmp(bmid, userid) == 0) {
            // found match, to remove it
            changed = 1;
            continue;
        }
        if (bmout[0])
            strlcat(bmout, "/", sizeof(bmout));
        strlcat(bmout, bmid, sizeof(bmout));
    }

    if (!changed)
        return 0;

    now = time(0);
    log_filef(BBSHOME "/log/removebm.log", LOG_CREAT,
              "%s [%s] %s: %s->%s\n",
              Cdatelite(&now), userid,
              bh->brdname, bh->BM, bmout);

    if (has_quote)
        snprintf(bh->BM, sizeof(bh->BM), "[%s]", bmout);
    else
        strlcpy(bh->BM, bmout, sizeof(bh->BM));

    substitute_record(BBSHOME "/" FN_BOARD, bh, sizeof(boardheader_t), bid);
    reset_board(bid);
    return 0;
}

int main(int argc, char **argv)
{
    int i = 0;

    now = time(NULL);
    chdir(BBSHOME);
    attach_SHM();

    if (argc != 2) {
        printf("syntax: %s userid\n", argv[0]);
        return -1;
    }

    for (i = 0; i < MAX_BOARD; i++) {
	check(argv[1], i+1, &SHM->bcache[i]);
    }
    
    return 0;
}
