// Angel Beats! Daemon
// Make angel distribution more balanced
//
// Create: Hung-Te Lin <piaip@csie.org> 
// Mon Jun 28 22:29:43 CST 2010
//
// Copyright (C) 2010, Hung-Te Lin <piaip@csie.ntu.edu.tw>
// All rights reserved

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <limits.h>
#include <event.h>
#include "bbs.h"
#include "daemons.h"

//////////////////////////////////////////////////////////////////////////////
// configuration
#ifdef DEBUG
static int debug = 1;
static int verbose = 1;
#else
static int debug = 0;
static int verbose = 0;
#endif

// same as expire length
#ifndef ANGELBEATS_INACTIVE_TIME
#define ANGELBEATS_INACTIVE_TIME   ( 120 * DAY_SECONDS )
#endif

//////////////////////////////////////////////////////////////////////////////
// AngelInfo list operation

#define ANGEL_LIST_INIT_SIZE    (250)   // usually angels are 200~250
#define ANGEL_SUGGEST_RANGE     (10)
// By the numbers around 2010/06/30, usually online active angels are fewer
// than 50 in the evening and around 25 in day time.
// (total online angels are twice than active angels)

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
suggest_online_angel(int master_uid) {
    userinfo_t *astat = NULL;
    int candidates[ANGEL_SUGGEST_RANGE] = {0}, num_candidates = 0;
    int i;

    // 1. find appropriate angels
    // 2. select from one of them
    // 3. return uid

    for (i = 0; i < g_angel_list_size; i++) {
        AngelInfo *kanade = g_angel_list+i;
        int is_good_uid = 0;

        // skip the master himself
        if (kanade->uid == master_uid)
            continue;

        // we have to take care of multi-login sessions,
        // so it's better to reject if any of the sessions wants to reject.
        for (astat = search_ulistn(kanade->uid, 1); 
             astat && strcasecmp(astat->userid, kanade->userid) == 0; 
             astat++) {
            // ignore all dead processes
            if (astat->mode == DEBUGSLEEPING)
                continue;
            // if any sessions is not safe, ignore it.
            if (!(astat->userlevel & PERM_ANGEL) ||
                astat->angelpause) {
                is_good_uid = 0;
                break;
            }
            if (!is_good_uid)
                is_good_uid = astat->uid;
        }

        // a good candidate?
        if (is_good_uid)
            candidates[num_candidates++] = is_good_uid;
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
inc_angel_master(int uid) {
    AngelInfo *kanade = angel_list_find_by_uid(uid);
    if (!kanade)
        return 0;
    kanade->masters++;
    // TODO only sort when kanade->masters > (kanade+1)->masters
    angel_list_sort_by_masters();
    return 1;
}

int
dec_angel_master(int uid) {
    AngelInfo *kanade = angel_list_find_by_uid(uid);
    if (!kanade)
        return 0;
    if (kanade->masters == 0) {
        fprintf(stderr, "warning: trying to decrease angel master "
                "which was already zero: %d\n", uid);
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

    // skip inactive users. however, this makes the counter
    // incorrect when those kind of use goes online.
    // anyway that should not be a big change...
    if (time4(0) > u->lastlogin + ANGELBEATS_INACTIVE_TIME )
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
create_angel_report(int myuid, angel_beats_report *prpt) {
    int i;
    AngelInfo *kanade = g_angel_list;
    userinfo_t *astat = NULL;

    prpt->min_masters_of_active_angels = SHRT_MAX;
    prpt->min_masters_of_online_angels = SHRT_MAX;
    prpt->total_angels = g_angel_list_size;

    for (i = 0; i < g_angel_list_size; i++, kanade++) {
        // online?
        int is_pause = 0, is_online = 0, logins = 0;
        if (debug) printf(" - %03d. %s: ", i+1, kanade->userid);
        for (astat = search_ulistn(kanade->uid, 1);
             astat && astat->uid == kanade->uid;
             astat++) {
            if (astat->mode == DEBUGSLEEPING)
                continue;
            logins++;
            if (!(astat->userlevel & PERM_ANGEL)) {  // what now?
                if (debug) printf("[NOT ANGEL!? skip] ");
                is_online = is_pause = 0;
                break;
            }
            if (astat->angelpause) {
                if (debug) printf("[set pause (%d)] ", astat->angelpause);
                is_pause = 1;
            }
            is_online = 1;
        }
        if (debug) {
            switch(logins) {
                case 0:
                    printf("(not online)");
                    break;
                case 1:
                    printf("(online)");
                    break;
                default:
                    printf("(multi login: %d)", logins);
                    break;
            }
            printf("\n");
        }
        // update report numbers
        prpt->total_online_angels += is_online;
        if (is_online) {
            if (!is_pause) {
                prpt->total_active_angels++;
                if (prpt->max_masters_of_active_angels < kanade->masters)
                    prpt->max_masters_of_active_angels = kanade->masters;
                if (prpt->min_masters_of_active_angels > kanade->masters)
                    prpt->min_masters_of_active_angels = kanade->masters;
            }
            if (prpt->max_masters_of_online_angels < kanade->masters)
                prpt->max_masters_of_online_angels = kanade->masters;
            if (prpt->min_masters_of_online_angels > kanade->masters)
                prpt->min_masters_of_online_angels = kanade->masters;
        }
    }
    if (prpt->min_masters_of_active_angels == SHRT_MAX)
        prpt->min_masters_of_active_angels = 0;
    if (prpt->min_masters_of_online_angels == SHRT_MAX)
        prpt->min_masters_of_online_angels = 0;
    // report my information
    if (myuid > 0 && (kanade = angel_list_find_by_uid(myuid))) {
        prpt->my_active_masters = kanade->masters;
    }
    if(debug) fflush(stdout);
    return 0;
}

//////////////////////////////////////////////////////////////////////////////
// network libevent
struct timeval tv = {5, 0};
static struct event ev_listen, ev_sighup;

//////////////////////////////////////////////////////////////////////////////
// main

static void
sighup_cb(int signal, short event, void *arg) {
    time4_t clk = time(0);
    fprintf(stderr, "%s reload (HUP)\n", Cdatelite(&clk));
    init_angel_list();
}

static void
client_cb(int fd, short event, void *arg) {
    int len;
    char master_uid[IDLEN+1] = "", angel_uid[IDLEN+1] = "";
    char *uid;
    angel_beats_data data ={0};
    time4_t clk = time4(NULL);

    // ignore clients that timeout or sending invalid request
    if (event & EV_TIMEOUT)
	goto end;
    if ( (len = read(fd, &data, sizeof(data))) != sizeof(data) )
	goto end;
    if (data.cb != sizeof(data))
        goto end;

    if (debug) {
        fprintf(stderr, "%s request: op=%d, mid=%d, aid=%d\n", Cdatelite(&clk),
                data.operation, data.master_uid, data.angel_uid);
    }
    // solve user ids
    if (data.angel_uid && (uid = getuserid(data.angel_uid)))
        strlcpy(angel_uid, uid, sizeof(angel_uid));
    if (data.master_uid && (uid = getuserid(data.master_uid)))
        strlcpy(master_uid, uid, sizeof(master_uid));

    if (debug) printf("got request: %d\n", data.operation);
    switch(data.operation) {
        case ANGELBEATS_REQ_INVALID:
            fprintf(stderr, "%s got invalid request [%s/%s]\n", 
                    Cdatelite(&clk), master_uid, angel_uid);
            break;
        case ANGELBEATS_REQ_RELOAD:
            fprintf(stderr, "%s reload\n", Cdatelite(&clk));
            init_angel_list();
            break;
        case ANGELBEATS_REQ_SUGGEST_AND_LINK:
            fprintf(stderr, "%s request suggest&link from [%s], ", 
                    Cdatelite(&clk), master_uid);
            data.angel_uid = suggest_online_angel(data.master_uid);
            if (data.angel_uid > 0) {
                inc_angel_master(data.angel_uid);
                uid = getuserid(data.angel_uid);
                strlcpy(angel_uid, uid, sizeof(angel_uid));
            }
            fprintf(stderr, "result: [%s]\n", data.angel_uid > 0 ?
                    angel_uid : "<none>");
            break;
        case ANGELBEATS_REQ_REMOVE_LINK:
            fprintf(stderr, "%s request remove link by master [%s] to angel [%s]\n", 
                    Cdatelite(&clk), master_uid, angel_uid);
            dec_angel_master(data.angel_uid);
            break;
        case ANGELBEATS_REQ_REPORT:
            fprintf(stderr, "%s report by [%s]\n", Cdatelite(&clk), master_uid);
            {
                if (debug) printf("ANGELBEATS_REQ_REPORT\n");
                angel_beats_report rpt = {0};
                rpt.cb = sizeof(rpt);
                create_angel_report(data.angel_uid, &rpt);
                // write different kind of data!
                write(fd, &rpt, sizeof(rpt));
                goto end;
            }
            break;
    }
    write(fd, &data, sizeof(data));

end:
    // cleanup
    close(fd);
    free(arg);
}

static void 
listen_cb(int fd, short event, void *arg) {
    int cfd;

    if ((cfd = accept(fd, NULL, NULL)) < 0 )
	return;

    if (debug) printf("accept new connection!\n");
    struct event *ev = malloc(sizeof(struct event));

    event_set(ev, cfd, EV_READ, client_cb, ev);
    event_add(ev, &tv);
}

int 
main(int argc, char *argv[]) {
    int i;
    AngelInfo *kanade;
    int     ch, sfd, go_daemon = 1;
    const char *iface_ip = ANGELBEATS_ADDR;

    // things to do:
    // 1. if cmd='reload' or first run, read passwd and init_angel_list
    // 2. if cmd='suggest', find an online angel with least masters
    // 3. if cmd='inc_master', decrease master counter of target angel
    // 4. if cmd='dec_master', increase master counter of target angel
    // 5. if cmd='report', give verbose reports
    
    while ( (ch = getopt(argc, argv, "i:Dh")) != -1 ) {
	switch( ch ) {
	case 'i':
	    iface_ip = optarg;
	    break;

        case 'D':
            go_daemon = !go_daemon;

	case 'h':
	default:
	    fprintf(stderr, "usage: %s [-D] [-i [interface_ip]:port]\n", argv[0]);
	    return 1;
	}
    }

    srand(time(NULL));
    Signal(SIGPIPE, SIG_IGN);
    chdir(BBSHOME);
    attach_SHM();

    printf("initializing angel list...\n");
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
    printf("suggested angel=%d\n", suggest_online_angel(0));
    fflush(stdout);

    if (go_daemon)
        daemonize(BBSHOME "/run/angelbeats.pid", BBSHOME "/log/angelbeats.log");

    if ( (sfd = tobind(iface_ip)) < 0 )
	return 1;

    event_init();
    event_set(&ev_listen, sfd, EV_READ | EV_PERSIST, listen_cb, &ev_listen);
    event_add(&ev_listen, NULL);
    signal_set(&ev_sighup, SIGHUP, sighup_cb, &ev_sighup);
    signal_add(&ev_sighup, NULL);
    event_dispatch();
    
    return 0;
}

