#include "bbs.h"
#include "daemons.h"

static inline int connect_post_svc() {
    return toconnectex(POSTSVC_SOCK_PATH, 2);
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
#if IS_ENABLED(CONFIG_EDITPOST_SMARTMERGE)
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
            int legType = (type == RECTYPE_GOOD ? 1 : (type == RECTYPE_BAD ? 2 : 3));
            char hdr_buf[256];
            int hlen = snprintf(hdr_buf, sizeof(hdr_buf),
                                "COMMENT %s %s %s %u %s %ld %zu %d\n",
                                board, fhdr->filename, cuser.userid,
                                (unsigned int)cuser.firstlogin,
                                fromhost, (long)now, msg_len, legType);
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
    int len = snprintf(cmd, sizeof(cmd), "VOTE_FILE %s %s %s %u %d\n",
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
int
CrosspostRecord(const char *src_board, const char *src_file,
                const char *target_board, const char *target_file)
{
    if (!USE_POST_SVC)
        return 0;

    int s = connect_post_svc();
    if (s < 0) {
        return 1;
    }

    char cmd[256];
    int len = snprintf(cmd, sizeof(cmd), "CROSSPOST %s %s %s %s %s %u %ld\n",
                       src_board, src_file, target_board, target_file,
                       cuser.userid, (unsigned int)cuser.firstlogin, (long)now);

    if (towrite(s, cmd, len) < 0) {
        close(s);
        return 1;
    }

    char resp[64] = {0};
    toread(s, resp, sizeof(resp) - 1);
    close(s);

    return (strncmp(resp, "OK", 2) == 0) ? 0 : 1;
}

int
PostFetchOriginal(const char *board, const char *file, const char *out_path,
                  time4_t *out_modified)
{
    if (out_modified)
        *out_modified = 0;

    if (!USE_POST_SVC)
        return -1;

    int s = connect_post_svc();
    if (s < 0) {
        return -1;
    }

    char cmd[PATHLEN + 64];
    int len = snprintf(cmd, sizeof(cmd), "FETCH_POST_FILE %s %s %s\n",
                       board, file, out_path);

    if (towrite(s, cmd, len) < 0) {
        close(s);
        return -1;
    }

    char resp[128] = {0};
    if (toread(s, resp, sizeof(resp) - 1) < 0 || strncmp(resp, "OK", 2) != 0) {
        close(s);
        return -1;
    }
    close(s);

    if (out_modified) {
        long long m = 0;
        if (sscanf(resp, "OK %lld", &m) == 1) {
            *out_modified = (time4_t)m;
        }
    }
    return 0;
}

int
PostUpdateRecord(const char *board, const char *file, const char *title,
                 const char *filepath, time4_t expected_modified,
                 time4_t *out_latest_modified)
{
    if (out_latest_modified)
        *out_latest_modified = 0;

    if (!USE_POST_SVC)
        return POST_UPDATE_ERROR;

    int s = connect_post_svc();
    if (s < 0) {
        return POST_UPDATE_ERROR;
    }

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        close(s);
        return POST_UPDATE_ERROR;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *buf = (char *)malloc(sz + 1);
    if (!buf) {
        fclose(fp);
        close(s);
        return POST_UPDATE_ERROR;
    }
    if (sz > 0 && fread(buf, 1, sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        close(s);
        return POST_UPDATE_ERROR;
    }
    fclose(fp);
    buf[sz] = '\0';

    size_t title_len = strlen(title);
    char hdr[256];
    int hlen = snprintf(hdr, sizeof(hdr), "UPDATE_POST_FILE %s %s %s %lld %zu %ld\n",
                        board, file, cuser.userid, (long long)expected_modified,
                        title_len, sz);

    if (towrite(s, hdr, hlen) < 0 ||
        (title_len > 0 && towrite(s, title, title_len) < 0) ||
        (sz > 0 && towrite(s, buf, sz) < 0)) {
        free(buf);
        close(s);
        return POST_UPDATE_ERROR;
    }
    free(buf);

    char resp[128] = {0};
    toread(s, resp, sizeof(resp) - 1);
    close(s);

    if (strncmp(resp, "ERR CONFLICT", 12) == 0 || strncmp(resp, "ERR MODIFIED", 12) == 0) {
        if (out_latest_modified) {
            long long lm = 0;
            if (sscanf(resp, "%*s %*s %lld", &lm) == 1) {
                *out_latest_modified = (time4_t)lm;
            }
        }
        return POST_UPDATE_CONFLICT;
    }

    if (strncmp(resp, "OK 0", 4) == 0) {
        if (out_latest_modified) {
            long long lm = 0;
            if (sscanf(resp, "OK 0 NO_CHANGE %lld", &lm) == 1) {
                *out_latest_modified = (time4_t)lm;
            }
        }
        return POST_UPDATE_NO_CHANGE;
    } else if (strncmp(resp, "OK ", 3) == 0) {
        if (out_latest_modified) {
            long long lm = 0;
            unsigned int rev = 0;
            if (sscanf(resp, "OK %u %lld", &rev, &lm) == 2) {
                *out_latest_modified = (time4_t)lm;
            }
        }
        return POST_UPDATE_SUCCESS;
    }
    return POST_UPDATE_ERROR;
}

int
PostDeleteRecord(const char *board, const char *file, const char *deleter, const char *reason)
{
    if (!USE_POST_SVC)
        return 0;

    int s = connect_post_svc();
    if (s < 0)
        return -1;

    char cmd[PATHLEN + 128];
    int len = snprintf(cmd, sizeof(cmd), "DELETE_POST_FILE %s %s %s %s\n",
                       board, file, deleter ? deleter : "", reason ? reason : "");
    if (towrite(s, cmd, len) < 0) {
        close(s);
        return -1;
    }

    char resp[64] = {0};
    toread(s, resp, sizeof(resp) - 1);
    close(s);
    return (strncmp(resp, "OK", 2) == 0) ? 0 : -1;
}

int
PostUndeleteRecord(const char *board, const char *file)
{
    if (!USE_POST_SVC)
        return 0;

    int s = connect_post_svc();
    if (s < 0)
        return -1;

    char cmd[PATHLEN + 64];
    int len = snprintf(cmd, sizeof(cmd), "UNDELETE_POST_FILE %s %s\n", board, file);
    if (towrite(s, cmd, len) < 0) {
        close(s);
        return -1;
    }

    char resp[64] = {0};
    toread(s, resp, sizeof(resp) - 1);
    close(s);
    return (strncmp(resp, "OK", 2) == 0) ? 0 : -1;
}

int
CommentDeleteRecord(const char *board, const char *file, int seq, const char *deleter, const char *reason)
{
    if (!USE_POST_SVC)
        return 0;

    int s = connect_post_svc();
    if (s < 0)
        return -1;

    char cmd[PATHLEN + 128];
    int len = snprintf(cmd, sizeof(cmd), "DELETE_COMMENT_FILE %s %s %d %s %s\n",
                       board, file, seq, deleter ? deleter : "", reason ? reason : "");
    if (towrite(s, cmd, len) < 0) {
        close(s);
        return -1;
    }

    char resp[64] = {0};
    toread(s, resp, sizeof(resp) - 1);
    close(s);
    return (strncmp(resp, "OK", 2) == 0) ? 0 : -1;
}

int
CommentUndeleteRecord(const char *board, const char *file, int seq)
{
    if (!USE_POST_SVC)
        return 0;

    int s = connect_post_svc();
    if (s < 0)
        return -1;

    char cmd[PATHLEN + 64];
    int len = snprintf(cmd, sizeof(cmd), "UNDELETE_COMMENT_FILE %s %s %d\n", board, file, seq);
    if (towrite(s, cmd, len) < 0) {
        close(s);
        return -1;
    }

    char resp[64] = {0};
    toread(s, resp, sizeof(resp) - 1);
    close(s);
    return (strncmp(resp, "OK", 2) == 0) ? 0 : -1;
}

int
PostUpdateTitleRecord(const char *board, const char *file, const char *title, const char *editor)
{
    if (!USE_POST_SVC)
        return 0;

    int s = connect_post_svc();
    if (s < 0)
        return -1;

    size_t title_len = strlen(title);
    char hdr[256];
    int hlen = snprintf(hdr, sizeof(hdr), "UPDATE_POST_TITLE_FILE %s %s %s %zu\n",
                        board, file, editor ? editor : cuser.userid, title_len);
    if (towrite(s, hdr, hlen) < 0 || (title_len > 0 && towrite(s, title, title_len) < 0)) {
        close(s);
        return -1;
    }

    char resp[64] = {0};
    toread(s, resp, sizeof(resp) - 1);
    close(s);
    return (strncmp(resp, "OK", 2) == 0) ? 0 : -1;
}
