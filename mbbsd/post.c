#include "bbs.h"

// A file to write the content of a new post in, aside (not in a board), to
// give to PostAddRecord(); the caller removes it afterwards.
void
post_draft_path(char *buf, size_t sz)
{
    static int seq;
    snprintf(buf, sz, "tmp/post.%d.%d", (int)getpid(), ++seq);
}

// Creates a new post in a board through bof, from the content the caller
// wrote in content_path (NULL: a record without its own content, e.g. a
// note in ALLPOST). Every post that goes into a board does: posts,
// crossposts (and their notes in ALLPOST), petitions and posts by the
// system. With fhdr->filename empty, bof names the post; fhdr gets the
// record as listed. Returns 0, or -1.
int
PostAddRecord(const char *board, fileheader_t *fhdr, const char *content_path)
{
    return CrosspostAddRecord(board, fhdr, content_path, NULL, NULL, 0);
}

int
CrosspostAddRecord(const char *board, fileheader_t *fhdr,
                   const char *content_path, const char *src_board,
                   const char *src_file, int exempt)
{
    // The list of the board itself, also from the digest.
    AUTO_BFILE(dir, board, FN_DIR);
    bof_actor_t me = mbbsd_user_actor();
    bof_create_post_t req = {
        .community = board, .bbs_dir = dir, .bbs_header = fhdr,
        .bbs_content_path = content_path,
        .bbs_crosspost_community = src_board,
        .bbs_crosspost_file = src_file,
        .bbs_crosspost_exempt = exempt,
    };
    fileheader_t created;
    bof_post_response_t res = { .bbs_header = &created };
    if (bof_create_post(&me, &req, &res) != BOF_OK)
        return -1;
    *fhdr = created;
    return 0;
}

// Shows that the file of the post is locked by someone else (e.g. being
// edited) while bof waits for it.
static void
comment_wait(int tries_left, void *ctx GCC_UNUSED)
{
    move(b_lines, 0);
    prints("==> 檔案正被它人編輯中，等待完成: %d\n", tries_left);
    doupdate();
}

// With nest.svc, the cache shows comments in its own form (a header line
// and further lines, see comments.c). Gets the lines it
// appended to path (before to after) if they are one new block of the
// user's; 0 if not, and the caller publishes its own line.
static int
live_comment_lines(const char *path, off_t before, off_t after,
                   char *buf, size_t sz)
{
    size_t idlen = strlen(cuser.userid);
    ssize_t len = after - before;
    int fd, blocks = 0;

    if (before <= 0 || len <= 1 || (size_t)len >= sz)
        return 0;
    if ((fd = open(path, O_RDONLY)) < 0)
        return 0;
    // From the start of a line, up to the end of one.
    if (pread(fd, buf, 1, before - 1) != 1 || buf[0] != '\n' ||
        pread(fd, buf, len, before) != len) {
        close(fd);
        return 0;
    }
    close(fd);
    buf[len] = '\0';
    if (buf[len - 1] != '\n')
        return 0;
    // "RS userid|" or, by the OP, "RS*userid|".
    const char *user = buf + 1 + (buf[1] == '*');
    if (buf[0] != COMMENT_LINE_MARK ||
        strncmp(user, cuser.userid, idlen) != 0 || user[idlen] != '|')
        return 0;
    // Headers; the other lines are "RS[*]<spaces>|text".
    for (const char *p = buf; *p; ) {
        const char *nl = strchr(p, '\n');
        const char *q = p + 1 + (p[1] == '*');
        if (p[0] == COMMENT_LINE_MARK && *q != ' ')
            blocks++;
        p = nl ? nl + 1 : p + strlen(p);
    }
    return blocks == 1;
}

int
CommentAddRecord(const char *board, const char *direct, fileheader_t *fhdr,
                 int ent, int type, const char *msg, const char *formatted)
{
    SCOPED_STAT(STAT_DOCOMMENT);
    bof_actor_t me = mbbsd_user_actor();
    bof_create_comment_t req = {
        .post = {
            .community = board, .bbs_dir = direct, .bbs_index = ent,
            .bbs_header = fhdr,
        },
        .content = formatted, .bbs_text = msg,
        .bbs_vote = type == RECTYPE_GOOD ? 1 : type == RECTYPE_BAD ? -1 : 0,
        .bbs_lock = IS_ENABLED(CONFIG_EDITPOST_SMARTMERGE),
        .bbs_on_wait = comment_wait,
    };
    fileheader_t updated;
    bof_comment_response_t res = { .bbs_header = &updated };
    off_t fsize;    // file size after our append: live dedupe token
    char fpath[PATHLEN];
    off_t before = 0;   // the size of the cache before the comment

    setdirpath(fpath, direct, fhdr->filename);
    if (BOF_FILES_ARE_CACHE)
        before = dashs(fpath);

    switch (bof_create_comment(&me, &req, &res)) {
    case BOF_OK:
        break;
    case BOF_ERR_COULDNT_CREATE_COMMENT:
        vmsg("錯誤: 檔案正被它人編輯中，無法寫入。");
        return -1;
    case BOF_ERR_COULDNT_UPDATE_POST:
        return -1;
    case BOF_ERR_BBS_READ_ONLY:
        vmsg("錯誤: 系統目前唯讀中，無法修改。");
        return -1;
    case BOF_ERR_BBS_POST_TOO_LARGE:
        vmsg("推文已達上限, 無法繼續推文, 請另撰文發表");
        return -1;
    default:
        vmsg("錯誤: 原檔案已被刪除。 無法寫入。");
        return -1;
    }
    *fhdr = updated;
    fsize = res.bbs_file_size;

    /* Publish live comment to IPC service (clean ASCIIZ) */
    {
        aidu_t aid = aidu_raw(fn2aidu(fhdr->filename));
        int bid = currbid;
        if (bid <= 0 && board && *board)
            bid = get_board_bid(board);
        if (bid > 0 && aid > 0) {
            char buf[IPC_TEXT_SIZE];
            if (!BOF_FILES_ARE_CACHE || res.bbs_merged ||
                !live_comment_lines(fpath, before, fsize, buf, sizeof(buf)))
                STRLCPY(buf, formatted);
            size_t len = strlen(buf);
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
                buf[--len] = '\0';
            ipc_pub((uint32_t)bid, aid, buf, fsize > 0 ? (uint64_t)fsize : 0);
        }
    }

    if (fhdr->modified != 0) {
        post_record_changed(direct, ent, fhdr);
        /* mark my self as 'read this file'. */
        brc_addlist(fhdr->filename, fhdr->modified);
    }

    return 0;
}

// Votes on a post apart from comments (Lemmy's CreatePostLike, for
// CONFIG_RATING): vote is +1, -1 or 0 to take it back. fhdr gets the new
// score. Returns 0, 1 if the vote did not change the score (the same vote
// again), -2 if the storage keeps votes only with comments (the files
// backend: bbs_storage_not_supported), or -1.
int
VotePostRecord(const char *board, const char *direct, fileheader_t *fhdr,
               int ent, int vote)
{
    bof_actor_t me = mbbsd_actor(board, false);
    bof_create_post_like_t req = {
        .post = {
            .community = board, .bbs_dir = direct, .bbs_index = ent,
            .bbs_header = fhdr,
        },
        .score = vote,
    };
    fileheader_t updated = *fhdr;
    bof_post_like_response_t res = { .bbs_header = &updated };
    bof_error_t err = bof_create_post_like(&me, &req, &res);

    if (err == BOF_ERR_BBS_STORAGE_NOT_SUPPORTED)
        return -2;
    if (err != BOF_OK)
        return -1;
    if (res.bbs_upvote_delta == 0 && res.bbs_downvote_delta == 0 && vote != 0)
        return 1;
    if (ent > 0) {
        *fhdr = updated;
    } else {
        int up = fhdr->upvote + res.bbs_upvote_delta;
        int down = fhdr->downvote + res.bbs_downvote_delta;
        fhdr->upvote = up < INT16_MIN ? INT16_MIN : up > INT16_MAX ? INT16_MAX : up;
        fhdr->downvote = down < INT16_MIN ? INT16_MIN : down > INT16_MAX ? INT16_MAX : down;
    }
    if (direct && ent > 0)
        post_record_changed(direct, ent, fhdr);
    return 0;
}
