#define _UTIL_C_
#include "bbs.h"

/* Remove an user from any BM of existing boards */

int check(void *data, int bid, boardheader_t *bh)
{
    int changed = 0;
    const char *userid = (const char *) data;
    if (!bh->brdname[0])
        return 0;

    int target_uid = searchuser(userid, NULL);
    char old_bms[BOARD_MODERATORS_FORMAT_MAX_LEN];
    board_format_moderators(bh, old_bms, sizeof(old_bms));

    // Remove from moderators array
    int mod_idx = 0;
    for (int m = 0; m < MAX_BMs; m++) {
        if (bh->moderators[m].unum > 0) {
            int is_match = 0;
            if (target_uid > 0 && bh->moderators[m].unum == target_uid) {
                is_match = 1;
            } else {
                const char *uname = getuserid(bh->moderators[m].unum);
                if (uname && strcasecmp(uname, userid) == 0)
                    is_match = 1;
            }
            if (is_match) {
                changed = 1;
            } else {
                if (mod_idx != m)
                    bh->moderators[mod_idx] = bh->moderators[m];
                mod_idx++;
            }
        }
    }
    for (; mod_idx < MAX_BMs; mod_idx++) {
        memset(&bh->moderators[mod_idx], 0, sizeof(user_t));
    }

    if (!changed)
        return 0;

    char new_bms[BOARD_MODERATORS_FORMAT_MAX_LEN];
    board_format_moderators(bh, new_bms, sizeof(new_bms));

    now = time(0);
    log_filef(BBSHOME "/log/removebm.log",
              "[%s] %s: %s->%s\n", userid,
              bh->brdname, old_bms, new_bms);

    substitute_record(BBSHOME "/" FN_BOARD, bh, sizeof(boardheader_t), bid);
    reset_board(bid);
    return 0;
}

int main(int argc, char **argv)
{
    int i = 0;

    now = time(NULL);
    chdir(BBSHOME);
    attach_SHM();

    if (argc != 2) {
        printf("syntax: %s userid\n", argv[0]);
        return -1;
    }

    for (i = 0; i < MAX_BOARD; i++) {
	check(argv[1], i+1, &SHM->bcache[i]);
    }
    
    return 0;
}
