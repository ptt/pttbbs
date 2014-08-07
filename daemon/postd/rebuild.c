#define _UTIL_C_
#include "bbs.h"
#include "daemons.h"

// Comments format: CommentsPrefix ANSI_COLOR(33) [AUTHOR]
//                  ANSI_RESET ANSI_COLOR(33) ":" [CONTENT]
//                  ANSI_RESET [trailings]
const char *CommentsPrefix[] = {
    ANSI_COLOR(1;37) "推 ",
    ANSI_COLOR(1;31) "噓 ",
    ANSI_COLOR(1;31) "→ ", // Shared by P1 and P2 <OLDRECOMMEND>.
    NULL,
};

int CommentsExtract(const char *input,
                     char *output_owner,
                     char *output_content,
                     char *output_trailings) {
    int i, kind = -1;
    const char *prefix = NULL, *p = NULL, *p2, *p3;
    const char *pat_Prefix2 = ANSI_COLOR(33),
               *pat_PostAuthor = ANSI_RESET ANSI_COLOR(33) ":",
               *pat_PostContent = ANSI_RESET;
    for (i = 0; !p && CommentsPrefix[i]; i++) {
        prefix = CommentsPrefix[i];
        if (!str_starts_with(input, prefix))
            continue;
        p = input + strlen(prefix);
        kind = i;
    }
    if (!p) {
        // printf("error - !p\n");
        return -1;
    }
    if (!str_starts_with(p, pat_Prefix2)) {
        printf("error - !starts_with(prefix2)\n");
        return -1;
    }
    p += strlen(pat_Prefix2);
    p2 = strstr(p, pat_PostAuthor);
    if (!p || p2 <= p) {
        printf("error - !p || p2 <= p\n");
        return -1;
    }
    // author = p..p2
    if (output_owner) {
        *output_owner = 0;
        strncat(output_owner, p, p2 - p);
    }
    p = p2 + strlen(pat_PostAuthor);
    p2 = strstr(p, pat_PostContent);
    if (!p2) {
        printf("error - !p2\n");
        return -1;
    }
    // content = p..[spaces]p2
    p3 = p2 - 1;
    while (p3 > p && *p3 == ' ')
        p3--;
    if (output_content) {
        *output_content = 0;
        strncat(output_content, p, p3 - p + 1);
    }
    p = p2 + strlen(pat_PostContent);
    if (output_trailings) {
        strcpy(output_trailings, p);
    }
    return kind;
}

int IsCrossPostLog(const char *buf) {
    // format: "※ " ANSI_COLOR(1;32) "%s" ANSI_COLOR(0;32) ":轉錄至" %s
    if (!str_starts_with(buf, "※ " ANSI_COLOR(1;32)))
        return 0;
    if (!strstr(buf, ANSI_COLOR(0;32) ":轉錄至"))
        return 0;
    printf("Found XPOST!!!\n");
    return 1;
}

char *ProcessPost(const char *filename) {
    FILE *fp = fopen(filename, "rt");
    char *content;
    long offBegin, offEnd, off;
    char buf[ANSILINELEN];
    char bufOwner[ANSILINELEN],
         bufContent[ANSILINELEN],
         bufTrailing[ANSILINELEN];
    int kind;
    assert(fp);

    // first line, expecting STR_AUTHOR1 or STR_AUTHOR2.
    if (fgets(buf, sizeof(buf), fp)) {
        int skip_lines = 0;
        if (strncmp(STR_AUTHOR1, buf, strlen(STR_AUTHOR1)) == 0) {
            // local file: 3 line format. (author, subject, time)
            skip_lines = 3;
        } else if (strncmp(STR_AUTHOR2, buf, strlen(STR_AUTHOR2)) == 0) {
            // remote file: 4 line format. (author, subject, time, source)
            skip_lines = 4;
        } else {
            // unknown, sorry.
            rewind(fp);
        }
        for (; skip_lines > 0; skip_lines--) {
            fgets(buf, sizeof(buf), fp);
            if (buf[0] == '\r' || buf[0] == '\n')
                break;
        }
    }

    // Here we want to determine end of edited contents.
    // If a user has edited his file, there will be "edit" signatures.
    // Otherwise it ends with site sig.
    // So, an easy way is to parse all "valid" comments until we've reach end of
    // file and find the starting of continuous valid comments. The only
    // exceptions are system logs - ex crosspost.
    // format: "※ " ANSI_COLOR(1;32) "%s" ANSI_COLOR(0;32) ":轉錄至" %s

    offBegin = ftell(fp);
    offEnd = offBegin;
    off = offEnd;

    while (fgets(buf, sizeof(buf), fp)) {
        off += strlen(buf);
        if (IsCrossPostLog(buf) ||
            (buf[0] == ESC_CHR &&
             CommentsExtract(buf, NULL, NULL, NULL) >= 0)) {
            // do something
        } else {
            offEnd = off;
        }
    }

    // Content: offBegin to offEnd.
    fseek(fp, offBegin, SEEK_SET);
    content = malloc(offEnd - offBegin + 1);
    assert(content);
    fread(content, 1, offEnd - offBegin, fp);
    content[offEnd - offBegin] = 0;

    // Try to parse comments
    fseek(fp, offEnd, SEEK_SET);
    while (fgets(buf, sizeof(buf), fp)) {
        if (buf[0] != ESC_CHR)  // Includes IsCrossPostLog.
            continue;
        // See comments.c:FormatCommentString:
        kind = CommentsExtract(buf, bufOwner, bufContent, bufTrailing);
        assert(kind >= 0);
        chomp(bufTrailing);
        // TODO we should probably upload these comments.
        printf("K[%d], A[%s], C[%s], T[%s]\n",
               kind, bufOwner, bufContent, bufTrailing);
    }

    fclose(fp);
    return content;
}

int PostAddRecord(const char *board, const fileheader_t *fhdr, const char *fpath)
{
    int s;
    PostAddRequest req = {0};
    char *userid;
    char *contents;
    uint32_t content_length;
    char xuid[IDLEN + 1];

    req.cb = sizeof(req);
    req.operation = POSTD_REQ_ADD2;
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

    // TODO try harder to parse content, to remove header and re-construct
    // comments.

    s = toconnectex(POSTD_ADDR, 10);
    if (s < 0)
        return 1;
    if (towrite(s, &req, sizeof(req)) < 0) {
        close(s);
        return 1;
    }

    contents = ProcessPost(fpath);
    content_length = strlen(contents);
    if (towrite(s, &content_length, sizeof(content_length)) < 0 ||
        towrite(s, contents, content_length) < 0) {
        free(contents);
        close(s);
        return 1;
    }
    free(contents);
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
        if (PostAddRecord(bp->brdname, &fhdr, fpath) != 0)
            printf(" (error)");
        printf("\n");
    }

    fclose(fp);
}

int main(int argc, char **argv)
{
    int bid = 0;
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
