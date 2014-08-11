#define _UTIL_C_
#include "bbs.h"
#include "daemons.h"

const char *server = POSTD_ADDR;

#define debug(args...) do { if (verbose_level > 0) printf(args); } while (0);

int verbose_level = 0;

int PostAddRecord(int s, const char *board, const fileheader_t *fhdr,
		  FILE *fp, size_t *written_data)
{
    int success = 1;
    PostAddRequest req = {0};
    char *userid;
    char xuid[IDLEN + 1];
    size_t garbage = 0;

    if (!written_data)
        written_data = &garbage;

    req.cb = sizeof(req);
    req.operation = fp ? POSTD_REQ_IMPORT_REMOTE : POSTD_REQ_IMPORT;
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
    if (success && fp) {
        uint32_t content_len;
        char *content;

	fseek(fp, 0, SEEK_END);
	content_len = ftell(fp);
	content = malloc(content_len + 1);
        assert(content && fp);
        // TODO replace by sendfile.
        fread(content, content_len, 1, fp);
        content[content_len] = 0;

        *written_data += sizeof(content_len) + content_len;
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

int64_t timestamp_ms() {
    struct timeval tv;
    uint64_t ms;
    gettimeofday(&tv, NULL);
    ms = tv.tv_sec * 1000;
    ms += tv.tv_usec / 1000;
    return ms;
}

void rebuild_board(int bid GCC_UNUSED, boardheader_t *bp, int is_remote)
{
    int s;
    char dot_dir[PATHLEN], fpath[PATHLEN];
    FILE *fp, *fp2;
    size_t output_bytes = 0;
    fileheader_t fhdr;
    printf("Rebuilding board: %s ", bp->brdname);
    debug("\n");
    int64_t ts_start, ts_sync, ts_end;

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

    ts_start = timestamp_ms();
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
	fp2 = fopen(fpath, "rt");
        if (!fp2) {
            debug(" - Non-Exist (%s).\n", fhdr.filename);
            continue;
        }

        // TODO Add .DIR sequence number.
        debug(" - Adding %s", fhdr.filename);
        if (PostAddRecord(s, bp->brdname, &fhdr, is_remote ? fp2 : NULL,
                          &output_bytes) != 0) {
            debug(" (error)\n");
            printf("Failed adding entry [%s/%s]. Abort.\n",
                   bp->brdname, fhdr.filename);
            exit(1);
        }
	fclose(fp2);
        debug("\n");
    }
    ts_sync = timestamp_ms();

    // shutdown request
    {
        int32_t num = 0;
        double data_rate;
        PostAddRequest req = {0};
        req.cb = sizeof(req);
        if (towrite(s, &req, sizeof(req)) < 0 ||
            toread(s, &num, sizeof(num)) < 0) {
            printf("Failed syncing requests. Abort.\n");
            exit(1);
        }
        ts_end = timestamp_ms();
        if (ts_end == ts_sync) ts_end++;
        data_rate = output_bytes / ((double)ts_end - ts_start) / (double) 1024;
        data_rate *= 1000;
        printf("Total %d entries (%ld bytes, %.2fKB/s),"
               " %s server, exec: %.1fs, sync: %.1fs\n",
               num, output_bytes, data_rate,
               is_remote ? "remote" : "local",
               (ts_sync - ts_start) / (double)1000,
               (ts_end - ts_sync) / (double)1000);
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
        debug("Changed server to%s: %s\n", is_remote ? " REMOTE": "", server);
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
