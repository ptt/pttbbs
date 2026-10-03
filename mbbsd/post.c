#include "bbs.h"

int
PostAddRecord(const char *board, const fileheader_t *fhdr, time4_t ctime GCC_UNUSED)
{
    char dir[PATHLEN];
    setbdir(dir, board);
    if (append_fileheader(dir, fhdr) == -1)
        return -1;
    return 0;
}

int
CommentAddRecord(const char *board GCC_UNUSED, const char *direct, fileheader_t *fhdr,
                 int ent, int type, const char *msg GCC_UNUSED, const char *formatted)
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

    ENDSTAT(STAT_DOCOMMENT);
    return 0;

error:
    ENDSTAT(STAT_DOCOMMENT);
    return -1;
}
