#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cmsys.h"
#include "cmbbs.h"
#include "common.h"
#include "config.h"
#include "var.h"

void
boardheader_storage_to_mem(boardheader_t *bh, size_t count)
{
    if (!NEED_STORAGE_CONV || !bh || count == 0)
        return;
    for (size_t i = 0; i < count; i++) {
        if (bh[i].bclass[4] == ' ')
            bh[i].bclass[4] = '\0';
        storage_to_mb(bh[i].bclass, bh[i].bclass, sizeof(bh[i].bclass));
        storage_to_mb(bh[i].desc, bh[i].desc, sizeof(bh[i].desc));
        storage_to_mb(bh[i].BM, bh[i].BM, sizeof(bh[i].BM));
    }
}

void
boardheader_mem_to_storage(boardheader_t *bh)
{
    if (!NEED_STORAGE_CONV || !bh)
        return;
    mb_to_storage(bh->bclass, bh->bclass, sizeof(bh->bclass));
    mb_to_storage(bh->desc, bh->desc, sizeof(bh->desc));
    mb_to_storage(bh->BM, bh->BM, sizeof(bh->BM));
}

int
get_boardheader(boardheader_t *bh, int bid)
{
    if (!bh || bid < 1 || bid > MAX_BOARD)
        return -1;
    const boardheader_t *src = getbcache(bid);
    if (!src || !src->brdname[0])
        return 0;
    *bh = *src;
    boardheader_storage_to_mem(bh, 1);
    return 1;
}

int
get_boardheader_by_name(boardheader_t *bh, const char *brdname)
{
    if (!bh || !brdname || !brdname[0])
        return -1;
    int bid = getbnum(brdname);
    if (bid <= 0)
        return 0;
    return get_boardheader(bh, bid);
}

int
modify_boardheader(const boardheader_t *bh, int bid)
{
    if (!bh || bid < 1 || bid > MAX_BOARD)
        return -1;
    boardheader_t s_bh = *bh;
    boardheader_mem_to_storage(&s_bh);
    int ret = substitute_record(FN_BOARD, &s_bh, sizeof(boardheader_t), bid);
    if (ret == 0) {
        boardheader_t *cache_bh = getbcache(bid);
        if (cache_bh)
            *cache_bh = s_bh;
    }
    return ret;
}
