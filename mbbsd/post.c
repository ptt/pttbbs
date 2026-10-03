#include "bbs.h"
#include "daemons.h"

static inline int connect_post_svc() {
    return toconnectex(get_postsvc_sock(), 2);
}

int
PostAddRecord(const char *board, const fileheader_t *fhdr, time4_t ctime GCC_UNUSED)
{
    char dir[PATHLEN];
    setbdir(dir, board);
    if (append_fileheader(dir, fhdr) == -1)
        return -1;

    if(!USE_POST_SVC)
        return 0;

    int s = connect_post_svc();
    if (s >= 0) {
        size_t title_len = strlen(fhdr->title);
        char hdr_buf[256];
        int hlen = snprintf(hdr_buf, sizeof(hdr_buf),
                            "POST_FILE %s %s %s %u %d %ld %zu\n",
                            board, fhdr->filename, cuser.userid,
                            (unsigned int)cuser.firstlogin,
                            fhdr->filemode, (long)ctime, title_len);
        if (towrite(s, hdr_buf, hlen) >= 0 &&
            (title_len == 0 || towrite(s, fhdr->title, title_len) >= 0)) {
            char resp[64] = {0};
            toread(s, resp, sizeof(resp) - 1);
        }
        close(s);
    }
    return 0;
}

int
CommentAddRecord(const char *board, const char *direct, fileheader_t *fhdr,
                 int ent, int type, const char *msg, const char *formatted)
{
    char path[PATHLEN];
    int update = 0;
    int fd;
    BEGINSTAT(STAT_DOCOMMENT);

    /* Lock and append, (lock may be caused other add_comment or edit_post) */
    setdirpath(path, direct, fhdr->filename);
    fd = open(path, O_APPEND | O_WRONLY);
    if (fd >= 0) {
#ifdef EDITPOST_SMARTMERGE
        int lock_retry = 5, lock_wait = 1, lock_success = 0;
        while (lock_retry-- > 0) {
            /* try several times */
            if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
                move(b_lines, 0);
                prints("==> 檔案正被它人編輯中，等待完成: %d\n", lock_retry + 1);
                doupdate();
                sleep(lock_wait);
                /* reopen the file because edit_post creates a new file. */
                close(fd);
                fd = open(path, O_APPEND | O_WRONLY);
                continue;
            }
            lock_success = 1;
            write(fd, formatted, strlen(formatted));
            flock(fd, LOCK_UN);
            break;
        }
        close(fd);
        if (!lock_success) {
            vmsg("錯誤: 檔案正被它人編輯中，無法寫入。");
            goto error;
        }
#else
        write(fd, formatted, strlen(formatted));
        close(fd);
#endif
    } else {
        vmsg((errno == EROFS) ? "錯誤: 系統目前唯讀中，無法修改。" :
             "錯誤: 原檔案已被刪除。 無法寫入。");
        goto error;
    }

    if (type == RECTYPE_GOOD && fhdr->recommend < MAX_RECOMMENDS)
        update = 1;
    else if (type == RECTYPE_BAD && fhdr->recommend > -MAX_RECOMMENDS)
        update = -1;
    fhdr->recommend += update;

    /* since we want to do 'modification'... */
    fhdr->modified = dasht(path);

    if (fhdr->modified != 0) {
        if (modify_dir_lite(direct, ent, fhdr->filename,
                            fhdr->modified, NULL, NULL, NULL, update, NULL, 0, 0) < 0)
            goto error;
        /* mark my self as 'read this file'. */
        brc_addlist(fhdr->filename, fhdr->modified);
    }

    if (USE_POST_SVC && msg != NULL) {
        int s = connect_post_svc();
        if (s >= 0) {
            size_t msg_len = strlen(msg);
            char hdr_buf[256];
            int hlen = snprintf(hdr_buf, sizeof(hdr_buf),
                                "COMMENT %s %s %s %u %s %ld %zu\n",
                                board, fhdr->filename, cuser.userid,
                                (unsigned int)cuser.firstlogin,
                                fromhost, (long)now, msg_len);
            if (towrite(s, hdr_buf, hlen) >= 0 &&
                (msg_len == 0 || towrite(s, msg, msg_len) >= 0)) {
                char resp[64] = {0};
                toread(s, resp, sizeof(resp) - 1);
            }
            close(s);
        }
    }

    ENDSTAT(STAT_DOCOMMENT);
    return 0;

error:
    ENDSTAT(STAT_DOCOMMENT);
    return -1;
}

int
VotePostRecord(const char *board, const char *file, int vote)
{
    if (!USE_POST_SVC)
        return 0;

    int s = connect_post_svc();
    if (s < 0) {
        return 1;
    }

    char cmd[256];
    int len = snprintf(cmd, sizeof(cmd), "VOTE %s %s %s %u %d\n",
                       board, file, cuser.userid,
                       (unsigned int)cuser.firstlogin, vote);

    if (towrite(s, cmd, len) < 0) {
        close(s);
        return 1;
    }

    char resp[64] = {0};
    toread(s, resp, sizeof(resp) - 1);
    close(s);

    return (strncmp(resp, "OK", 2) == 0) ? 0 : 1;
}
