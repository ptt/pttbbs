#include "bbs.h"

// The read records (brc) of the current user are kept by bof (see
// bof_bbs_read_marks_*() in bof.h); these give it the user and the board.

static void
brc_person(void)
{
    bof_bbs_read_marks_set_person(cuser.userid, cuser.userlevel != 0,
                              login_start_time);
}

int
brc_initialize(void)
{
    brc_person();
    return bof_bbs_read_marks_initialize();
}

void
brc_finalize(void)
{
    brc_person();
    bof_bbs_read_marks_finalize();
}

void
brc_update(void)
{
    brc_person();
    bof_bbs_read_marks_update();
}

/**
 * @return number of records in \a boardname
 */
int
brc_initial_board(const char *boardname)
{
    brc_person();
    if (strcmp(currboard, boardname) == 0) {
        bof_bbs_read_marks_initialize();
        return bof_bbs_read_marks_enter(currbid);
    }

    currbid = getbnum(boardname);
    if (currbid == 0)
        currbid = getbnum(DEFAULT_BOARD);
    assert(0 <= currbid - 1 && currbid - 1 < MAX_BOARD);
    currboard = bcache[currbid - 1].brdname;
    currbrdattr = bcache[currbid - 1].brdattr;
    return bof_bbs_read_marks_enter(currbid);
}

void
brc_toggle_all_read(int bid, int is_all_read)
{
    brc_person();
    bof_bbs_mark_community_read(bid, is_all_read);
}

void
brc_toggle_read(int bid, time4_t newtime)
{
    brc_person();
    bof_bbs_mark_community_read_until(bid, newtime);
}

void
brc_addlist(const char *fname, time4_t modified)
{
    brc_person();
    if (bof_mark_post_as_read(currbid, fname, modified) < 0)
        assert(!"brc_addlist: not on the board entered");
}

int
brc_unread_time(int bid, time4_t ftime, time4_t modified)
{
    brc_person();
    return bof_bbs_post_unread_time(bid, ftime, modified);
}

int
brc_unread(int bid, const char *fname, time4_t modified)
{
    brc_person();
    return bof_post_is_unread(bid, fname, modified);
}

int
brc_search_read(int bid, time4_t ftime, int forward, time4_t *result)
{
    brc_person();
    return bof_bbs_search_read(bid, ftime, forward, result);
}
