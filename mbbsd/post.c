#include "bbs.h"

int
PostAddRecord(const char *board, const fileheader_t *fhdr, time4_t ctime GCC_UNUSED)
{
    // The list of the board itself, also from the digest.
    AUTO_BFILE(dir, board, FN_DIR);
    bof_actor_t me = { .name = cuser.userid, .from = fromhost };
    bof_create_post_t req = {
        .community = board, .bbs_dir = dir, .bbs_header = fhdr,
    };
    return bof_create_post(&me, &req, NULL) == BOF_OK ? 0 : -1;
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

int
CommentAddRecord(const char *board, const char *direct, fileheader_t *fhdr,
                 int ent, int type, const char *msg GCC_UNUSED, const char *formatted)
{
    SCOPED_STAT(STAT_DOCOMMENT);
    bof_actor_t me = { .name = cuser.userid, .from = fromhost };
    bof_create_comment_t req = {
        .post = {
            .community = board, .bbs_dir = direct, .bbs_index = ent,
            .bbs_header = fhdr,
        },
        .content = formatted,
        .bbs_vote = type == RECTYPE_GOOD ? 1 : type == RECTYPE_BAD ? -1 : 0,
        .bbs_lock = IS_ENABLED(CONFIG_EDITPOST_SMARTMERGE),
        .bbs_on_wait = comment_wait,
    };
    fileheader_t updated;
    bof_comment_response_t res = { .bbs_header = &updated };
    off_t fsize;    // file size after our append: live dedupe token

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
            char buf[IPC_TEXT_LEN];
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
