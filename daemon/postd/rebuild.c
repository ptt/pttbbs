#define _UTIL_C_
#include "bbs.h"
#include "daemons.h"

int PostAddRecord(const char *board, const fileheader_t *fhdr)
{
    int s;
    PostAddRequest req = {0};
    char *userid;
    char xuid[IDLEN + 1];

    req.cb = sizeof(req);
    req.operation = POSTD_REQ_IMPORT;
    strlcpy(req.key.board, board, sizeof(req.key.board));
    strlcpy(req.key.file, fhdr->filename, sizeof(req.key.file));
    memcpy(&req.header, fhdr, sizeof(req.header));

    req.extra.ctime = atoi(fhdr->filename + 2);
    // It is possible to generate req.extra.ipv4 from fhdr site sig, but we
    // probably don't really care.

    // Try harder to recognize user
    if (fhdr->filemode & FILE_ANONYMOUS) {
        // Theoretically we can look up real id, but let's don't waste time.
        userid = "-anonymous.";
    } else if (strchr(fhdr->owner, '.')) {
        // Post from some unknown user.
        userid = "-remote.";
    } else {
        // Probably a real user. Let's try it.
        userec_t urec;
        int uid = searchuser(fhdr->owner, xuid);
        if (uid > 0) {
            passwd_query(uid, &urec);
            if (strcmp(urec.userid, xuid) != 0) {
                printf(" *** oops, userid changed. ...\n");
                exit(1);
            }
            userid = xuid;
            if ((uint32_t)urec.firstlogin <= req.extra.ctime) {
                req.extra.userref = urec.firstlogin;
            } else {
                // same id, different user. damn it.
                req.extra.userref = 0;
            }
        } else {
            // does not exist. Treat like invalid user.
            userid = "-expired.";
        }
    }
    strlcpy(req.extra.userid, userid, sizeof(req.extra.userid));
    printf(" (userref: %s.%d)", req.extra.userid, req.extra.userref);

    s = toconnectex(POSTD_ADDR, 10);
    if (s < 0)
        return 1;
    if (towrite(s, &req, sizeof(req)) < 0) {
        close(s);
        return 1;
    }
    close(s);
    return 0;
}

void rebuild_board(int bid GCC_UNUSED, boardheader_t *bp)
{
    char dot_dir[PATHLEN], fpath[PATHLEN];
    FILE *fp;
    fileheader_t fhdr;
    printf("Rebuilding board: %s\n", bp->brdname);

    setbfile(dot_dir, bp->brdname, ".DIR");

    fp = fopen(dot_dir, "rb");
    if (!fp) {
        printf(" (empty directory)\n");
        return;
    }
    while (fread(&fhdr, sizeof(fhdr), 1, fp)) {
        // Skip unknown files
        if (fhdr.filename[0] != 'M' && fhdr.filename[1] != '.') {
            printf(" - Skipped (%s).\n", fhdr.filename);
            continue;
        }
        // Not sure why but some filename may contain ' '.
        if (strchr(fhdr.filename, ' ')) {
            *strchr(fhdr.filename, ' ') = 0;
        }
        setbfile(fpath, bp->brdname, fhdr.filename);
        if (dashs(fpath) < 1) {
            printf(" - Non-Exist (%s).\n", fhdr.filename);
            continue;
        }

        // TODO Add .DIR sequence number.

        printf(" - Adding %s", fhdr.filename);
        if (PostAddRecord(bp->brdname, &fhdr) != 0)
            printf(" (error)");
        printf("\n");
    }

    fclose(fp);
}

int main(int argc, char **argv)
{
    int bid = 0;
    if (argc < 2) {
        printf("usage: %s boardname ...\n", argv[0]);
        return 1;
    }

    chdir(BBSHOME);
    attach_SHM();

    for (bid = 1; bid <= MAX_BOARD; bid++) {
        int i = 0;
        boardheader_t *bp = getbcache(bid);

        if (!*bp->brdname)
            continue;

        for (i = argc; i > 1; i--) {
            if (strcasecmp(argv[i - 1], bp->brdname) == 0) {
                break;
            }
        }

        if (i > 1)
            rebuild_board(bid, bp);
    }
    return 0;
}
