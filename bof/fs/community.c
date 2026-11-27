/*
 * community.c - bof (Birds Of Feather), the files backend: changing
 * communities (boards) beyond their header.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * The board header of each board is a slot of .BRD, also kept in the board
 * cache (SHM); the directories of a board are boards/<c>/<name>/ (the list
 * and the articles) and man/boards/<c>/<name>/ (the digest).
 */

#include "../bof_int.h"
#include "bof_fs.h"
#include "gem.h"

// Writes the header of slot bid into .BRD and the board cache, with its
// moderators, as reset_board() would have read it again.
static bof_error_t
write_header(int bid, const boardheader_t *bh)
{
    if (modify_boardheader(bh, bid) != 0)
        return BOF_ERR_COULDNT_UPDATE_COMMUNITY;
    buildBMcache(bid);
    return BOF_OK;
}

bof_error_t
bof_fs_edit_community(void *ctx GCC_UNUSED, const bof_actor_t *actor GCC_UNUSED,
                      const bof_edit_community_t *req)
{
    return write_header(req->bbs_id, req->bbs_header);
}

bof_error_t
bof_fs_create_community(void *ctx GCC_UNUSED,
                        const bof_actor_t *actor GCC_UNUSED,
                        const bof_create_community_t *req,
                        bof_create_community_response_t *out)
{
    boardheader_t bh;
    int bid = req->bbs_id;

    if (bid > 0) {
        bof_error_t err = write_header(bid, req->bbs_header);
        if (err == BOF_OK && out)
            out->bbs_id = bid;
        return err;
    }
    // After the last slot; the board cache is loaded again.
    bh = *req->bbs_header;
    boardheader_mem_to_storage(&bh);
    if (append_record(FN_BOARD, &bh, sizeof(bh)) == -1)
        return BOF_ERR_COULDNT_UPDATE_COMMUNITY;
    addbrd_touchcache();
    bid = num_boards();
    if (out)
        out->bbs_id = bid;
    return BOF_OK;
}

bof_error_t
bof_fs_remove_community(void *ctx GCC_UNUSED,
                        const bof_actor_t *actor GCC_UNUSED,
                        const bof_remove_community_t *req,
                        bof_remove_community_response_t *out)
{
    char bpath[PATHLEN], apath[PATHLEN], cmd[PATHLEN * 3];

    setbpath(bpath, req->community);
    setapath(apath, req->community);
    // With CONFIG_GEM the digest is in gem.svc, removed there (the copy
    // below is of the files of the board only).
    if (IS_ENABLED(CONFIG_GEM)) {
        char root[GEM_ROOT_SIZE];
        snprintf(root, sizeof(root), "board:%s", req->community);
        gem_root_delete(root);
        apath[0] = '\0';
    }
    // A copy of the files, as the BBS has always kept (the name is checked:
    // no shell characters).
    snprintf(cmd, sizeof(cmd),
             "/bin/tar zcvf tmp/board_%s.tgz %s %s >/dev/null 2>&1",
             req->community, bpath, apath);
    if (system(cmd) == -1) {
        // Removed all the same, as before.
    }
    RmTree(bpath);
    if (apath[0])
        RmTree(apath);
    if (out)
        out->bbs_posts_kept = 0;
    return BOF_OK;
}

bof_error_t
bof_fs_rename_community(void *ctx GCC_UNUSED,
                        const bof_actor_t *actor GCC_UNUSED,
                        const bof_bbs_rename_community_t *req)
{
    char src[PATHLEN], dst[PATHLEN];

    setbpath(src, req->community);
    setbpath(dst, req->new_name);
    if (!dashd(src))
        return BOF_ERR_COULDNT_FIND_COMMUNITY;
    // Only the case may change (boards/<c>/ is by the first letter).
    if (strcasecmp(req->community, req->new_name) != 0 && dashd(dst))
        return BOF_ERR_COULDNT_UPDATE_COMMUNITY;
    if (Rename(src, dst) != 0)
        return BOF_ERR_COULDNT_UPDATE_COMMUNITY;
    // The digest follows; a board may have none.
    if (IS_ENABLED(CONFIG_GEM)) {
        char from[GEM_ROOT_SIZE], to[GEM_ROOT_SIZE];
        snprintf(from, sizeof(from), "board:%s", req->community);
        snprintf(to, sizeof(to), "board:%s", req->new_name);
        gem_root_rename(from, to);
        return BOF_OK;
    }
    setapath(src, req->community);
    setapath(dst, req->new_name);
    Rename(src, dst);
    return BOF_OK;
}

// The order of records by the time in their file names.
static int
by_stamp(const void *a, const void *b)
{
    const fileheader_t *fa = a, *fb = b;
    return time4_cmp(get_fhdr_stamp_ts(fa->filename),
                     get_fhdr_stamp_ts(fb->filename));
}

bof_error_t
bof_fs_merge_communities(void *ctx GCC_UNUSED,
                         const bof_actor_t *actor GCC_UNUSED,
                         const bof_bbs_merge_communities_t *req,
                         bof_bbs_merge_communities_response_t *out)
{
    char dir_to[PATHLEN], dir_from[PATHLEN], file_to[PATHLEN];
    char file_from[PATHLEN], bak[PATHLEN];
    char *p_to, *p_from;
    int nto, nfrom, n, i, from_posts = 0, collisions = 0;
    fileheader_t *fh;

    setbfile(dir_to, req->to, FN_DIR);
    setbfile(dir_from, req->from, FN_DIR);
    {
        char bpath[PATHLEN];
        setbpath(bpath, req->to);
        if (!dashd(bpath))
            return BOF_ERR_COULDNT_FIND_COMMUNITY;
        setbpath(bpath, req->from);
        if (!dashd(bpath))
            return BOF_ERR_COULDNT_FIND_COMMUNITY;
    }
    strlcpy(file_to, dir_to, sizeof(file_to));
    strlcpy(file_from, dir_from, sizeof(file_from));
    p_to = strrchr(file_to, '/') + 1;
    p_from = strrchr(file_from, '/') + 1;

    nto = get_num_records(dir_to, sizeof(fileheader_t));
    nfrom = get_num_records(dir_from, sizeof(fileheader_t));
    if (nfrom <= 0)
        return BOF_OK;
    n = nto + nfrom;
    if (!(fh = malloc(n * sizeof(fileheader_t))))
        return BOF_ERR_UNKNOWN;
    if (nto > 0)
        get_fileheaders(dir_to, fh, 1, nto);
    get_fileheaders(dir_from, fh + nto, 1, nfrom);
    for (i = nto; i < n; i++)
        if (fh[i].filename[0])
            from_posts++;
    // Both lists, in the order of time; of two records of the same file,
    // the last is kept, with the modes of both.
    qsort(fh, n, sizeof(fileheader_t), by_stamp);
    snprintf(bak, sizeof(bak), "%s.bak", dir_to);
    Rename(dir_to, bak);
    for (i = 1; i <= n; i++) {
        if (!fh[i - 1].filename[0])
            continue;
        if (i == n || strcmp(fh[i - 1].filename, fh[i].filename) != 0) {
            fh[i - 1].upvote = fh[i - 1].downvote = 0;
            fh[i - 1].filemode |= 1;
            append_fileheader(dir_to, &fh[i - 1]);
            strlcpy(p_to, fh[i - 1].filename,
                    sizeof(file_to) - (p_to - file_to));
            if (!dashf(file_to)) {
                strlcpy(p_from, fh[i - 1].filename,
                        sizeof(file_from) - (p_from - file_from));
                Copy(file_from, file_to);
            }
        } else {
            fh[i].filemode |= fh[i - 1].filemode;
            collisions++;
        }
    }
    free(fh);
    if (out) {
        out->moved = from_posts - collisions;
        out->collisions = collisions;
    }
    return BOF_OK;
}

bof_error_t
bof_fs_rebuild_community(void *ctx GCC_UNUSED,
                         const bof_actor_t *actor GCC_UNUSED,
                         const bof_bbs_rebuild_community_t *req,
                         bof_bbs_rebuild_community_response_t *out)
{
    char bpath[PATHLEN], cmd[PATHLEN * 2];

    setbpath(bpath, req->community);
    if (!dashd(bpath))
        return BOF_ERR_COULDNT_FIND_COMMUNITY;
    // buildir reads every article: in the background.
    snprintf(cmd, sizeof(cmd), BBSHOME "/bin/buildir %s &", bpath);
    if (system(cmd) == -1)
        return BOF_ERR_COULDNT_UPDATE_COMMUNITY;
    if (out)
        out->in_background = 1;
    return BOF_OK;
}
