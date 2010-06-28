// Angel Beats Daemon
// Make angel distribution more balanced
//
// Create: Hung-Te Lin <piaip@csie.org> 
// Mon Jun 28 22:29:43 CST 2010
//
// Copyright (C) 2010, Hung-Te Lin <piaip@csie.ntu.edu.tw>
// All rights reserved

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "bbs.h"
#include "daemons.h"

//////////////////////////////////////////////////////////////////////////////
// configuration
static int debug = 1;
static int verbose = 1;

//////////////////////////////////////////////////////////////////////////////
// AngelInfo list operation

#define ANGEL_LIST_INIT_SIZE    (250)   // usually angels are 200~250
#define ANGEL_SUGGEST_RANGE     (20)    // 10% of all the angels

typedef struct {
    int uid;
    int masters;            // counter of who have this one as angel
    char userid[IDLEN+1];
} AngelInfo;

AngelInfo *g_angel_list;
size_t g_angel_list_capacity, g_angel_list_size;   // capacity and current size

// quick sort stubs

int angel_list_comp_uid(const void *pva, const void *pvb) {
    AngelInfo *pa = (AngelInfo*) pva, *pb = (AngelInfo*) pvb;
    return pa->uid - pb->uid;
}

int angel_list_comp_userid(const void *pva, const void *pvb) {
    AngelInfo *pa = (AngelInfo*) pva, *pb = (AngelInfo*) pvb;
    return strcasecmp(pa->userid, pb->userid);
}

int angel_list_comp_masters(const void *pva, const void *pvb) {
    AngelInfo *pa = (AngelInfo*) pva, *pb = (AngelInfo*) pvb;
    return pa->masters - pb->masters;
}

// search stubs

AngelInfo *
angel_list_find_by_uid(int uid) {
    // XXX TODO change this to binary search
    int i;
    for (i = 0; i < g_angel_list_size; i++) {
        if (g_angel_list[i].uid == uid)
            return g_angel_list+i;
    }
    return NULL;
}

AngelInfo *
angel_list_find_by_userid(const char *userid) {
    // XXX TODO change this to binary search
    int i;
    for (i = 0; i < g_angel_list_size; i++) {
        if (strcasecmp(g_angel_list[i].userid, userid) == 0)
            return g_angel_list+i;
    }
    return NULL;
}

// list operators

void 
angel_list_preserve(int newsz) {
    if (newsz < g_angel_list_capacity)
        return;

    assert(newsz >= g_angel_list_size);

    while (g_angel_list_capacity < newsz)
        g_angel_list_capacity += ANGEL_LIST_INIT_SIZE;
    g_angel_list = (AngelInfo*) realloc (
            g_angel_list, g_angel_list_capacity * sizeof(AngelInfo));
    assert(g_angel_list);
}

void 
angel_list_sort_by_masters() {
    qsort(g_angel_list, g_angel_list_size, sizeof(AngelInfo), 
          angel_list_comp_masters);
}

AngelInfo *
angel_list_add(const char *userid, int uid) {
    AngelInfo *kanade = angel_list_find_by_userid(userid);
    // printf("adding angel: %s (%s)\n", userid, kanade ? "exist" : "new");
    if (kanade)
        return kanade;

    angel_list_preserve(g_angel_list_size+1);
    kanade = &(g_angel_list[g_angel_list_size++]);
    memset(kanade, 0, sizeof(*kanade));
    kanade->uid = uid;
    strlcpy(kanade->userid, userid, sizeof(kanade->userid));
    return kanade;
}

//////////////////////////////////////////////////////////////////////////////
// Main Operations

int 
suggest_online_angel() {
    userinfo_t *astat = NULL;
    int candidates[ANGEL_SUGGEST_RANGE] = {0}, num_candidates = 0;
    int i;

    // 1. find appropriate angels
    // 2. select from one of them
    // 3. return uid

    for (i = 0; i < g_angel_list_size; i++) {
        AngelInfo *kanade = g_angel_list+i;

        // XXX or search_ulist_pid ?
        astat = search_ulist_userid(kanade->userid);

        for (; astat && 
               strcasecmp(astat->userid, kanade->userid) == 0; astat++) {
            // ignore invalid entries
            if (!(astat->userlevel & PERM_ANGEL))
                continue;   // XXX maybe we can quick abort?
            if (astat->angelpause || astat->mode == DEBUGSLEEPING)
                continue;
            // a good candidate
            candidates[num_candidates++] = astat->uid;
            break;
        }
        // terminate when too many candidates
        if (num_candidates >= ANGEL_SUGGEST_RANGE)
            break;
    }
    if (!num_candidates)
        return 0;

    // verbose report
    if (debug || verbose) {
        printf("suggest candidates: ");
        for (i = 0; i < num_candidates; i++) {
            printf("%d, ", candidates[i]);
        }
        printf("\n");
    }

    return candidates[rand() % num_candidates];
}

int
inc_angel_master(const char *userid) {
    AngelInfo *kanade = angel_list_find_by_userid(userid);
    if (!kanade)
        return 0;
    kanade->masters++;
    // TODO only sort when kanade->masters > (kanade+1)->masters
    angel_list_sort_by_masters();
    return 1;
}

int
dec_angel_master(const char *userid) {
    AngelInfo *kanade = angel_list_find_by_userid(userid);
    if (!kanade)
        return 0;
    if (kanade->masters == 0) {
        fprintf(stderr, "warning: trying to decrease angel master "
                "which was already zero: %s\n", userid);
        return 0;
    }
    kanade->masters--;
    // TODO only sort when kanade->masters < (kanade+-)->masters
    angel_list_sort_by_masters();
    return 1;
}

static int
_getuser(const char *userid, userec_t *xuser) {
    int uid;

    if ((uid = searchuser(userid, NULL))) {
	passwd_query(uid, xuser);
    }
    return uid;
}

int 
init_angel_list_callback(void *ctx, int uidx, userec_t *u) {
    AngelInfo *kanade = NULL;
    int unum = uidx + 1;

    if (!u->userid[0])
        return 0;

    // add entry if I'm an angel.
    if (u->userlevel & PERM_ANGEL)
        angel_list_add(u->userid, unum);

    // add reference if I have an angel.
    if (!u->myangel[0])
        return 0;
    kanade = angel_list_find_by_userid(u->myangel);
    if (!kanade) {
        // valid angel?
        userec_t xuser;
        int anum = _getuser(u->myangel, &xuser);

        if (anum > 0 && (xuser.userlevel & PERM_ANGEL))
            kanade = angel_list_add(xuser.userid, anum);
    }

    // found an angel?
    if (kanade) {
        // printf(" * %s -> angel = %s\n", u->userid, xuser.userid);
        kanade->masters++;
    }
    return 1;
}

int 
init_angel_list() {
    g_angel_list_size = 0;
    passwd_apply(NULL, init_angel_list_callback);
    angel_list_sort_by_masters();
    return 0;
}

int 
main(int argc, char *argv[]) {
    int i;
    AngelInfo *kanade;

    // things to do:
    // 1. if cmd='reload' or first run, read passwd and init_angel_list
    // 2. if cmd='suggest', find an online angel with least masters
    // 3. if cmd='inc_master', decrease master counter of target angel
    // 4. if cmd='dec_master', increase master counter of target angel
    // 5. if cmd='report', give verbose reports

    srand(time(NULL));
    Signal(SIGPIPE, SIG_IGN);
    chdir(BBSHOME);
    attach_SHM();

    init_angel_list();
    printf("found %zd angels. (cap=%zd)\n", 
            g_angel_list_size, g_angel_list_capacity);
    kanade = g_angel_list;
    for (i = 0; i < g_angel_list_size; i++, kanade++) {
        printf("%d [%s] uid=%d, masters=%d\n", 
                i+1, 
                kanade->userid,
                kanade->uid,
                kanade->masters);
    }
    printf("suggested angel=%d\n", suggest_online_angel());
    return 0;
}

