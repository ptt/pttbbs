#define _UTIL_C_
#include "bbs.h"
#include "daemons.h"

const char *server = POSTD_ADDR;

#define debug(args...) do { if (verbose_level > 0) printf(args); } while (0);

int verbose_level = 0;

int PostAddRecord(int s, const char *board, const fileheader_t *fhdr,
                  const char *fpath, size_t *written_data)
{
    int success = 1;
    PostAddRequest req = {0};
    char *userid;
    char xuid[IDLEN + 1];
    size_t garbage = 0;

    if (!written_data)
        written_data = &garbage;

    req.cb = sizeof(req);
    req.operation = fpath ? POSTD_REQ_IMPORT_REMOTE : POSTD_REQ_IMPORT;
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
    debug(" (userref: %s.%d)", req.extra.userid, req.extra.userref);

    *written_data += sizeof(req);
    if (success && towrite(s, &req, sizeof(req)) < 0)
        success = 0;
    if (success && fpath) {
        uint32_t content_len = dashs(fpath);
        char *content = malloc(content_len + 1);
        FILE *fp = fopen(fpath, "r");
        assert(content && fp);
        fread(content, content_len, 1, fp);
        content[content_len] = 0;
        fclose(fp);

        written_data += sizeof(content_len) + content_len;
        if (towrite(s, &content_len, sizeof(content_len)) < 0 ||
            towrite(s, content, content_len) < 0) {
            success = 0;
        }
        free(content);
        if (success)
            debug(" (content: %d)", content_len);
    }
    return !success;
}

void rebuild_board(int bid GCC_UNUSED, boardheader_t *bp, int is_remote)
{
    int s;
    char dot_dir[PATHLEN], fpath[PATHLEN];
    FILE *fp;
    size_t output_bytes = 0;
    fileheader_t fhdr;
    printf("Rebuilding board: %s\n", bp->brdname);
    time4_t start, syncpoint, end;

    setbfile(dot_dir, bp->brdname, ".DIR");
    fp = fopen(dot_dir, "rb");
    if (!fp) {
        printf(" (empty directory)\n");
        return;
    }
    s = toconnectex(server, 10);
    if (s < 0) {
        printf("failed to connect to server %s\n", server);
        exit(1);
    }

    start = time4(NULL);
    while (fread(&fhdr, sizeof(fhdr), 1, fp)) {
        // Skip unknown files
        if (fhdr.filename[0] != 'M' && fhdr.filename[1] != '.') {
            debug(" - Skipped (%s).\n", fhdr.filename);
            continue;
        }
        // Not sure why but some filename may contain ' '.
        if (strchr(fhdr.filename, ' ')) {
            *strchr(fhdr.filename, ' ') = 0;
        }
        setbfile(fpath, bp->brdname, fhdr.filename);
        if (dashs(fpath) < 1) {
            debug(" - Non-Exist (%s).\n", fhdr.filename);
            continue;
        }

        // TODO Add .DIR sequence number.
        debug(" - Adding %s", fhdr.filename);
        if (PostAddRecord(s, bp->brdname, &fhdr, is_remote ? fpath : NULL,
                          &output_bytes) != 0) {
            debug(" (error)\n");
            printf("Failed adding entry [%s/%s]. Abort.\n",
                   bp->brdname, fhdr.filename);
            exit(1);
        }
        debug("\n");
    }
    syncpoint = time4(NULL);

    // shutdown request
    {
        int32_t num = 0;
        PostAddRequest req = {0};
        req.cb = sizeof(req);
        if (towrite(s, &req, sizeof(req)) < 0 ||
            toread(s, &num, sizeof(num)) < 0) {
            printf("Failed syncing requests. Abort.\n");
            exit(1);
        }
        end = time4(NULL);
        printf(" Total %d entries (%ld bytes, %ldKb/s), Exec: %ds, Sync: %ds\n",
               num, output_bytes,
               (end > start) ? output_bytes / (end - start) / 1024 :
               output_bytes / 1024,
               syncpoint - start, end - syncpoint);
        close(s);
    }

    fclose(fp);
}

void usage_die(const char *prog) {
        printf("usage: %s [-v] [host:port<5135>] boardname ...\n", prog);
        exit(1);
}

int main(int argc, const char **argv)
{
    int bid = 0;
    const char *prog = argv[0];
    int is_remote = 0;

    while (argc > 1 && argv[1][0] == '-') {
        switch(argv[1][1]) {
        case  'v':
            verbose_level++;
            argc--, argv++;
            break;
        default:
            usage_die(prog);
            // never returns
        }
    }

    if (argc > 1 && strchr(argv[1], ':')) {
        server = argv[1];
        is_remote = (server[0] != ':');
        printf("Changed server to%s: %s\n", is_remote ? " REMOTE": "", server);
        argc--, argv++;
    }

    if (argc < 2)
        usage_die(prog);

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
            rebuild_board(bid, bp, is_remote);
    }
    return 0;
}
