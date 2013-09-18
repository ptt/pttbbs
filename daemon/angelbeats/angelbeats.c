// Angel Beats! Daemon
// Make angel distribution more balanced
//
// Create: Hung-Te Lin <piaip@csie.org> 
// Mon Jun 28 22:29:43 CST 2010
// --------------------------------------------------------------------------
// Copyright (C) 2010, Hung-Te Lin <piaip@csie.ntu.edu.tw>
// All rights reserved
// Distributed under BSD license (GPL compatible).
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// --------------------------------------------------------------------------
// TODO cache global report results.
// TODO add blame records
// TODO calculate suggest rate per hour

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <limits.h>
#include <event.h>
#include "bbs.h"
#include "daemons.h"

#define error(x...) fprintf(stderr, "ERROR: " x)
#define log(x...) fprintf(stderr, x)
#define debug(x...) { if(debug) fprintf(stderr, "DEBUG: " x); }

//////////////////////////////////////////////////////////////////////////////
// configuration
#ifdef DEBUG
static int debug = 1;
#else
static int debug = 0;
#endif

// Decide default algorithm
#if !(defined(ANGELBEATS_ASSIGN_BY_LAST_ACTIVITY) || \
      defined(ANGELBEATS_ASSIGN_BY_RANDOM))
#define ANGELBEATS_ASSIGN_BY_LAST_ACTIVITY
#endif

#ifndef ANGELBEATS_RANDOM_RANGE
#define ANGELBEATS_RANDOM_RANGE (150)
#endif

// same as expire length
#ifndef ANGELBEATS_INACTIVE_TIME
#define ANGELBEATS_INACTIVE_TIME   ( 180 * DAY_SECONDS )
#endif

// Lower priority for re-assigning masters in given period.
#ifndef ANGELBEATS_REASSIGN_PERIOD
#define ANGELBEATS_REASSIGN_PERIOD  (DAY_SECONDS)
#endif

// A period for master to send message, otherwies consider master as "dummy"
// and move angel back to schedule list.
#ifndef ANGELBEATS_ASSIGN_PROBATION_PERIOD
#define ANGELBEATS_ASSIGN_PROBATION_PERIOD (10*60)
#endif

// Merge activities in every X seconds.
#ifndef ANGELBEATS_ACTIVITY_MERGE_PERIOD
#define ANGELBEATS_ACTIVITY_MERGE_PERIOD    (15)
#endif

#ifndef ANGELBEATS_PERF_OUTPUT_FILE
#define ANGELBEATS_PERF_OUTPUT_FILE BBSHOME "/log/angel_perf.txt"
#endif

#ifndef ANGEL_STATE_FILE
#define ANGEL_STATE_FILE  BBSHOME "/log/angel_state.txt"
#endif
#define ANGEL_STATE_VERSION   (2)

//////////////////////////////////////////////////////////////////////////////
// AngelInfo list operation

#ifndef ANGEL_LIST_INIT_SIZE
#define ANGEL_LIST_INIT_SIZE    (250)   // usually angels are 200~250
#endif

typedef struct {
    int samples;
    int pause1;
    int pause2;
    // Records for that master can't find angel.
    int blamed_pause1;
    int blamed_pause2;
    int blamed_not_online;
    int blamed_other;
    time4_t last_blame;
    // Records for that angel was pinged by master.
    int ping;
    time4_t last_pinged;
    // Records for that angel has replied master's ping.
    int reply;
    time4_t last_replied;
} PerfData;

typedef struct {
    time4_t start;
    int samples;
} GlobalPerfData;

typedef struct {
    PerfData perf;
    time_t last_activity;   // last known activity from master
    time_t last_assigned;   // last time being assigned with new master
    int last_assigned_master; // uid of last new master
    int missed_assign;      // how many times this angel was not assigned.
    int uid;
    int masters;            // counter of who have this one as angel
    char userid[IDLEN+1];
} AngelInfo;

AngelInfo *g_angel_list;
size_t g_angel_list_capacity, g_angel_list_size;   // capacity and current size

struct timeval g_perf_timer_duration = { .tv_sec = ANGELBEATS_PERF_MIN_PERIOD };
struct event g_perf_timer_event;
GlobalPerfData g_perf;

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

int angel_list_comp_advanced(const void *pva, const void *pvb) {
    AngelInfo *pa = (AngelInfo*) pva, *pb = (AngelInfo*) pvb;
    time_t now = time(0),
           assign_a = pa->last_assigned, assign_b = pb->last_assigned;

    if (now - assign_a >= ANGELBEATS_ASSIGN_PROBATION_PERIOD ||
        pa->last_activity >= assign_a)
        assign_a -= assign_a % ANGELBEATS_REASSIGN_PERIOD;
    if (now - assign_b >= ANGELBEATS_ASSIGN_PROBATION_PERIOD ||
        pb->last_activity >= assign_b)
        assign_b -= assign_b % ANGELBEATS_REASSIGN_PERIOD;

    if (assign_a != assign_b)
        return assign_a > assign_b ? 1 : -1;
    if (pa->last_activity != pb->last_activity)
        return pa->last_activity > pb->last_activity ? 1 : -1;
    return pa->masters - pb->masters;
}

// search stubs

AngelInfo *
angel_list_find_by_uid(int uid) {
    // XXX TODO change this to binary search
    size_t i;
    for (i = 0; i < g_angel_list_size; i++) {
        if (g_angel_list[i].uid == uid)
            return g_angel_list + i;
    }
    return NULL;
}

AngelInfo *
angel_list_find_by_userid(const char *userid) {
    // XXX TODO change this to binary search
    size_t i;
    for (i = 0; i < g_angel_list_size; i++) {
        if (strcasecmp(g_angel_list[i].userid, userid) == 0)
            return g_angel_list + i;
    }
    return NULL;
}

// list operators

void 
angel_list_preserve(size_t newsz) {
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
angel_list_sort() {
    qsort(g_angel_list, g_angel_list_size, sizeof(AngelInfo), 
          angel_list_comp_advanced);
}

AngelInfo *
angel_list_add(const char *userid, int uid) {
    AngelInfo *kanade = angel_list_find_by_userid(userid);
    debug("adding angel: %s (%s)\n", userid, kanade ? "exist" : "new");
    if (kanade)
        return kanade;

    angel_list_preserve(g_angel_list_size+1);
    kanade = &(g_angel_list[g_angel_list_size++]);
    memset(kanade, 0, sizeof(*kanade));
    kanade->uid = uid;
    strlcpy(kanade->userid, userid, sizeof(kanade->userid));
    return kanade;
}

int
angel_list_get_index(AngelInfo *kanade) {
    int idx = (kanade - g_angel_list);
    assert(idx >= 0 && (size_t)idx < g_angel_list_size);
    return idx;
}

//////////////////////////////////////////////////////////////////////////////
// Main Operations

int get_angel_state(const AngelInfo *kanade,
                    int *p_pause,
                    int *p_logins) {
    userinfo_t *astat;
    int is_pause = 0, logins = 0;

    // we have to take care of multi-login sessions,
    // so it's better to reject if any of the sessions wants to reject.
    for (astat = search_ulistn(kanade->uid, 1); 
         astat && strcasecmp(astat->userid, kanade->userid) == 0; 
         astat++) {
        // ignore all dead processes
        if (astat->mode == DEBUGSLEEPING)
            continue;
        logins++;
        // no longer an angel!?
        if (!(astat->userlevel & PERM_ANGEL)) {
            is_pause = logins = 0;
            break;
        }
        if (astat->angelpause > is_pause)
            is_pause = astat->angelpause;
    }
    *p_pause = is_pause;
    *p_logins = logins;
    return logins > 0;
}

void
perf_angels() {
    size_t i;
    int is_pause, logins;
    time4_t clk = time4(0);
    AngelInfo *kanade = g_angel_list;

    debug("%s %s\n", Cdatelite(&clk), __func__);
    if (!g_perf.start)
        g_perf.start = clk;
    g_perf.samples++;

    for (i = 0; i < g_angel_list_size; i++, kanade++) {

        if (!get_angel_state(kanade, &is_pause, &logins))
            continue;

        kanade->perf.samples++;
        kanade->perf.pause1 += (is_pause == 1);
        kanade->perf.pause2 += (is_pause == 2);
    }
}

int 
suggest_online_angel(int master_uid) {
    size_t i;
    int is_pause, logins;
    int uid = 0;
#ifdef TRACE_ANGEL_SELECTION
    time_t clk = time(0);
    int found = 0;
#endif
    AngelInfo *kanade = g_angel_list;

#ifdef ANGELBEATS_ASSIGN_BY_RANDOM
    int random_uids[ANGELBEATS_RANDOM_RANGE];
    int crandom_uids = 0;
#endif

    for (i = 0; i < g_angel_list_size; i++, kanade++) {

        // skip the master himself
        if (kanade->uid == master_uid)
            continue;

        if (!get_angel_state(kanade, &is_pause, &logins))
            continue;

        if (is_pause)
            continue;

        if (master_uid)
            kanade->missed_assign++;
#ifdef TRACE_ANGEL_SELECTION
        log("\n %d.%*s(missed=%d,masters=%d,act=%d,assigned=%d%s) ",
            ++found, IDLEN,
            kanade->userid, kanade->missed_assign, kanade->masters,
            (int)(clk - kanade->last_activity),
            (int)(clk - kanade->last_assigned),
            ((int)(clk - kanade->last_assigned) <
             ANGELBEATS_ASSIGN_PROBATION_PERIOD &&
             kanade->last_activity < kanade->last_assigned) ?
            "[probation]" : "");
#endif

#if defined(ANGELBEATS_ASSIGN_BY_LAST_ACTIVITY)
        if (!uid)
            uid = kanade->uid;
#ifndef TRACE_ANGEL_SELECTION
        if (uid)
            break;
#endif

#elif defined(ANGELBEATS_ASSIGN_BY_RANDOM)
        random_uids[crandom_uids++] = kanade->uid;
        if (crandom_uids >= ANGELBEATS_RANDOM_RANGE)
            break;
#endif
    }
#ifdef ANGELBEATS_ASSIGN_BY_RANDOM
    if (crandom_uids > 0) {
        // Yes I didn't do srand. That's good enough.
        uid = random_uids[rand() % crandom_uids];
    }
#endif
    return uid;
}

int
inc_angel_master(int uid, int master_uid) {
    AngelInfo *kanade = angel_list_find_by_uid(uid);
    time_t now = time(0);
    if (!kanade)
        return 0;
    kanade->masters++;
    kanade->last_assigned = now;
    kanade->last_assigned_master = master_uid;
    kanade->missed_assign = 0;
    return 1;
}

int
dec_angel_master(int uid) {
    AngelInfo *kanade = angel_list_find_by_uid(uid);
    if (!kanade)
        return 0;
    if (kanade->masters == 0) {
        error("trying to decrease angel master "
              "which was already zero: %d\n", uid);
        return 0;
    }
    kanade->masters--;
    return 1;
}

int
touch_angel_activity(int uid) {
    AngelInfo *kanade = angel_list_find_by_uid(uid);
    int now = (int)time(0);

    now -= now % ANGELBEATS_ACTIVITY_MERGE_PERIOD;
    if (!kanade || kanade->last_activity == now)
        return 0;

    kanade->last_activity = now;
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
init_angel_list_callback(void *ctx GCC_UNUSED, int uidx, userec_t *u) {
    AngelInfo *kanade = NULL;
    int unum = uidx + 1;

    if (!u->userid[0])
        return 0;

    if (u->role & ROLE_ANGEL_ACTIVITY) {
        debug("skip: ROLE_ANGEL_ACTIVITY: %s\n", u->userid);
        return 0;
    }

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

        if (anum > 0 && (xuser.userlevel & PERM_ANGEL) &&
            !(xuser.role & ROLE_ANGEL_ACTIVITY))
            kanade = angel_list_add(xuser.userid, anum);
    }

    if (!kanade)
        return 0;

    // Update now (before skipping user) in case the master never called angel.
    if (u->timesetangel > kanade->last_assigned) {
        kanade->last_assigned = u->timesetangel;
        kanade->last_assigned_master = unum;
    }

    // skip inactive users. however, this makes the counter
    // incorrect when those kind of use goes online.
    // anyway that should not be a big change...
    if (time4(0) > u->timeplayangel + ANGELBEATS_INACTIVE_TIME )
        return 0;

    kanade->masters++;
    return 1;
}

int 
init_angel_list() {
    g_angel_list_size = 0;
    passwd_fast_apply(NULL, init_angel_list_callback);
    angel_list_sort();
    return 0;
}

int
create_angel_report(int myuid, angel_beats_report *prpt) {
    size_t i;
    AngelInfo *kanade = g_angel_list;
    int from_cmd = (!myuid);
#if 0
    time4_t now = time4(0);
#endif

    prpt->min_masters_of_active_angels = SHRT_MAX;
    prpt->min_masters_of_online_angels = SHRT_MAX;
    prpt->total_angels = g_angel_list_size;
    prpt->my_index = 0;
    prpt->my_active_index = 0;
    prpt->missed_assign = 0;
    prpt->inactive_days = ANGELBEATS_INACTIVE_TIME / DAY_SECONDS;

    for (i = 0; i < g_angel_list_size; i++, kanade++) {
        int is_pause, logins;
        get_angel_state(kanade, &is_pause, &logins);

        // Print state information.
        if (from_cmd) {
            log(" - %03zu. %-14s: ", i+1, kanade->userid);
            log("{samples=%d, pause1=%d, pause2=%d} "
                "(masters=%d, logins=%d, activity=%d, assigned=%d/%s)",
                kanade->perf.samples, kanade->perf.pause1,
                kanade->perf.pause2, kanade->masters, logins,
                (int)kanade->last_activity,
                (int)kanade->last_assigned,
                kanade->last_assigned_master > 0 ?
                getuserid(kanade->last_assigned_master) :
                "(unknown)");
            if (is_pause)
                log(" [PAUSE %d]", is_pause);
            log("\n");
        }

        // update report numbers
        prpt->total_online_angels += (logins > 0) ? 1 : 0;
        if (myuid > 0) {
            if (myuid == kanade->uid) {
                prpt->my_index = i + 1;
                if (!is_pause)
                    prpt->my_active_index = prpt->total_active_angels + 1;
            }
        }
        if (logins > 0) {
            if (!is_pause) {
                prpt->total_active_angels++;
                if (prpt->max_masters_of_active_angels < kanade->masters)
                    prpt->max_masters_of_active_angels = kanade->masters;
                if (prpt->min_masters_of_active_angels > kanade->masters)
                    prpt->min_masters_of_active_angels = kanade->masters;
#if 0
                if (prpt->max_inactive_time > (now - kanade->last_activity))
                    prpt->max_inactive_time = (now - kanade->last_activity);
                if (prpt->max_unassigned_time > (now - kanade->last_assigned))
                    prpt->max_unassigned_time = (now - kanade->last_assigned);
#endif
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
        prpt->last_assigned = kanade->last_assigned;
        prpt->last_assigned_master = kanade->last_assigned_master;
        prpt->missed_assign = kanade->missed_assign;
    }
    return 0;
}

int
fill_online_angel_list(angel_beats_uid_list *list) {
    static size_t i = 0;
    size_t iter = 0;
    int is_pause, logins;
    AngelInfo *kanade;

    list->angels = 0;
    for (iter = 0;
         iter < g_angel_list_size && list->angels < ANGELBEATS_UID_LIST_SIZE;
         iter++, i++) {
        i %= g_angel_list_size;
        kanade = g_angel_list + i;
        get_angel_state(kanade, &is_pause, &logins);
        if (!logins)
            continue;
        list->uids[list->angels++] = kanade->uid;
    }
    return list->angels;
}

void print_dash(FILE *fp, int len, const char *prefix) {
    if (prefix)
        fputs(prefix, fp);
    while (len-- > 0)
        fputc('-', fp);
    fputc('\n', fp);
}

void load_state_data() {
    int version = -1, i = 0;
    int activity, assigned, assigned_master;
    char uid[256];
    PerfData d;
    AngelInfo *kanade;
    FILE *fp = fopen(ANGEL_STATE_FILE, "rt");
    if (!fp)
        return;

    if (fscanf(fp, "%d\n", &version) != 1 ||
        (version != ANGEL_STATE_VERSION)) {
        error("Invalid state file (version=%d)\n", version);
        fclose(fp);
        return;
    }
    fscanf(fp, "%d %d\n", &g_perf.start, &g_perf.samples);
    while (fscanf(fp, "%s %d %d %d %d %d %d\n",
                  uid, &activity, &assigned, &assigned_master,
                  &d.samples, &d.pause1, &d.pause2) == 7) {
        i++;
        kanade = angel_list_find_by_userid(uid);
        if (!kanade)
            continue;
        kanade->last_activity = activity;
        // Update last_assigned* if the master has changed angel.
        if (!kanade->last_assigned_master) {
            kanade->last_assigned = assigned;
            kanade->last_assigned_master = assigned_master;
        } else if (assigned > kanade->last_assigned &&
                   kanade->last_assigned_master != assigned_master) {
            const char *uid1 = getuserid(kanade->last_assigned_master),
                       *uid2 = getuserid(assigned_master);
            if (!uid1) uid1 = "(unknown)";
            if (!uid2) uid2 = "(unknown)";
            log("warn: angel state data inconsistent with passwd: %s "
                "(master: [pw]%s vs [state]%s)\n",
                kanade->userid, uid1, uid2);
            kanade->last_assigned = assigned;
            kanade->last_assigned_master = assigned_master;
        }
        memcpy(&kanade->perf, &d, sizeof(d));
    }
    log("%s: got %d records.\n", __func__, i);
    fclose(fp);
}

void save_state_data() {
    const char *fname_new = ANGEL_STATE_FILE ".new",
               *fname_old = ANGEL_STATE_FILE ".old";
    FILE *fp = fopen(fname_new, "wt");
    size_t i;
    AngelInfo *kanade = g_angel_list;
    if (!fp)
        return;
    fprintf(fp, "%d\n%d %d\n", ANGEL_STATE_VERSION, g_perf.start,
            g_perf.samples);
    for (i = 0; i < g_angel_list_size; i++, kanade++) {
        fprintf(fp, "%s %d %d %d %d %d %d\n",
                kanade->userid, 
                (int)kanade->last_activity, (int)kanade->last_assigned,
                kanade->last_assigned_master,
                kanade->perf.samples, kanade->perf.pause1, kanade->perf.pause2);
    }
    fclose(fp);
    Rename(ANGEL_STATE_FILE, fname_old);
    Rename(fname_new, ANGEL_STATE_FILE);
}

void export_perf_data(FILE *fp) {
    size_t i = 0;
    time4_t clk = time4(0);
    AngelInfo *kanade = g_angel_list;
    fprintf(fp, "# Angel Performance Data (%s)\n", Cdatelite(&clk));
    fprintf(fp, "# Start: %s, Duration: %ld, Count: %d\n",
            Cdatelite(&g_perf.start), g_perf_timer_duration.tv_sec,
            g_perf.samples);
    fprintf(fp, "# No. %-*s Sample Pause1 Pause2\n# ", IDLEN, "UserID");
    print_dash(fp, 70, "# ");
    for (i = 0; i < g_angel_list_size; i++, kanade++) {
        fprintf(fp, "%4lu. %-*s %6d %6d %6d\n", i + 1, IDLEN, kanade->userid,
                kanade->perf.samples, kanade->perf.pause1, kanade->perf.pause2);
    }
    print_dash(fp, 70, "# ");
}

void reset_perf_data() {
    size_t i;
    AngelInfo *kanade = g_angel_list;
    g_perf.start = 0;
    g_perf.samples = 0;

    for (i = 0; i < g_angel_list_size; i++, kanade++) {
        memset(&kanade->perf, 0, sizeof(kanade->perf));
    }
}


//////////////////////////////////////////////////////////////////////////////
// network libevent
struct timeval tv = {5, 0};
static struct event ev_listen, ev_sighup;

//////////////////////////////////////////////////////////////////////////////
// main

static void
sighup_cb(int signal GCC_UNUSED, short event GCC_UNUSED, void *arg GCC_UNUSED) {
    time4_t clk = time(0);
    log("%s %s: reload (HUP)\n", Cdatelite(&clk), __func__);
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

    debug("%s request: op=%d, mid=%d, aid=%d\n", Cdatelite(&clk),
          data.operation, data.master_uid, data.angel_uid);

    // solve user ids
    if (data.angel_uid && (uid = getuserid(data.angel_uid)))
        strlcpy(angel_uid, uid, sizeof(angel_uid));
    if (data.master_uid && (uid = getuserid(data.master_uid)))
        strlcpy(master_uid, uid, sizeof(master_uid));

    debug("got request: %d\n", data.operation);
    switch(data.operation) {
        case ANGELBEATS_REQ_INVALID:
            error("%s invalid request [%s/%s]\n", 
                  Cdatelite(&clk), master_uid, angel_uid);
            break;
        case ANGELBEATS_REQ_RELOAD:
            log("%s reload\n", Cdatelite(&clk));
            init_angel_list();
            break;
        case ANGELBEATS_REQ_SAVE_STATE:
            log("%s save\n", Cdatelite(&clk));
            save_state_data();
            break;

        case ANGELBEATS_REQ_SUGGEST_AND_LINK:
            log("%s master [%s] request suggest&link, ", 
                Cdatelite(&clk), master_uid);
            data.angel_uid = suggest_online_angel(data.master_uid);
            if (data.angel_uid > 0) {
                inc_angel_master(data.angel_uid, data.master_uid);
                uid = getuserid(data.angel_uid);
                strlcpy(angel_uid, uid, sizeof(angel_uid));
                angel_list_sort();
            }
            log("result: [%s]\n", data.angel_uid > 0 ?  angel_uid : "<none>");
            break;
        case ANGELBEATS_REQ_REMOVE_LINK:
            log("%s master [%s] request remove link with angel [%s]\n",
                Cdatelite(&clk), master_uid, angel_uid);
            if (dec_angel_master(data.angel_uid))
                angel_list_sort();
            break;
        case ANGELBEATS_REQ_HEARTBEAT:
            log("%s master [%s] update angel activity with angel [%s]\n",
                Cdatelite(&clk), master_uid, angel_uid);
            if (touch_angel_activity(data.angel_uid))
                angel_list_sort();
            break;
        case ANGELBEATS_REQ_REG_NEW:
            log("%s admin [%s] register new angel [%s]\n",
                Cdatelite(&clk), master_uid, angel_uid);
            // Note: Angel permission may be not set yet.
            if (*angel_uid) {
                angel_list_add(angel_uid, data.angel_uid);
                angel_list_sort();
            }
            break;
        case ANGELBEATS_REQ_EXPORT_PERF:
            log("%s export_perf_data\n", Cdatelite(&clk));
            {
                FILE *fp = fopen(ANGELBEATS_PERF_OUTPUT_FILE, "at");
                if (fp) {
                    export_perf_data(fp);
                    fclose(fp);
                    reset_perf_data();
                    save_state_data();
                } else {
                    error("%s cannot output perf: %s\n",
                          Cdatelite(&clk), ANGELBEATS_PERF_OUTPUT_FILE);
                }
            }
            break;

        case ANGELBEATS_REQ_REPORT:
            log("%s angel [%s] request for report\n",
                Cdatelite(&clk), master_uid);
            {
                angel_beats_report rpt = {0};
                rpt.cb = sizeof(rpt);
                create_angel_report(data.angel_uid, &rpt);
                // write different kind of data!
                write(fd, &rpt, rpt.cb);
                goto end;
            }
            break;
        case ANGELBEATS_REQ_GET_ONLINE_LIST:
            log("%s get_online_uid_list\n", Cdatelite(&clk));
            {
                angel_beats_uid_list list = {0};
                list.cb = sizeof(list);
                list.angels = 0;
                fill_online_angel_list(&list);
                // write different kind of data!
                write(fd, &list, sizeof(list));
                goto end;
            }
            break;
        default:
            error("%s UNKNOWN REQUEST (%d)\n", Cdatelite(&clk),
                  data.operation);
            break;
    }
    write(fd, &data, sizeof(data));

end:
    // cleanup
    close(fd);
    free(arg);
}

static void 
listen_cb(int fd, short event GCC_UNUSED, void *arg GCC_UNUSED) {
    int cfd;

    if ((cfd = accept(fd, NULL, NULL)) < 0 )
	return;

    debug("accept new connection!\n");
    struct event *ev = malloc(sizeof(struct event));

    event_set(ev, cfd, EV_READ, client_cb, ev);
    event_add(ev, &tv);
}

static void
perf_timer_cb(int fd GCC_UNUSED, short event GCC_UNUSED, void *arg GCC_UNUSED) {
    // libevent needs us to do so...
    evtimer_del(&g_perf_timer_event);
    evtimer_add(&g_perf_timer_event, &g_perf_timer_duration);

    // Do perf now!
    perf_angels();

    // Save last state!
    save_state_data();
}

int 
main(int argc, char *argv[]) {
    size_t i;
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
            break;

	case 'h':
	default:
	    log("usage: %s [-D] [-i [interface_ip]:port]\n", argv[0]);
	    return 1;
	}
    }

    srand(time(NULL));
    Signal(SIGPIPE, SIG_IGN);
    chdir(BBSHOME);
    attach_SHM();

    log("initializing angel list...\n");
    init_angel_list();
    load_state_data();
    if (go_daemon)
        daemonize(BBSHOME "/run/angelbeats.pid", BBSHOME "/log/angelbeats.log");

    if ( (sfd = tobind(iface_ip)) < 0 )
	return 1;

    log("found %zd angels. (cap=%zd)\n", 
        g_angel_list_size, g_angel_list_capacity);
    kanade = g_angel_list;
    for (i = 0; i < g_angel_list_size; i++, kanade++) {
        log("%zu [%s] uid=%d, masters=%d\n", 
            i+1, 
            kanade->userid,
            kanade->uid,
            kanade->masters);
    }
    debug("suggested angel=%d\n", suggest_online_angel(0));

    event_init();
    event_set(&ev_listen, sfd, EV_READ | EV_PERSIST, listen_cb, &ev_listen);
    event_add(&ev_listen, NULL);
    signal_set(&ev_sighup, SIGHUP, sighup_cb, &ev_sighup);
    signal_add(&ev_sighup, NULL);
    evtimer_set(&g_perf_timer_event, perf_timer_cb, 0);
    evtimer_add(&g_perf_timer_event, &g_perf_timer_duration);
    event_dispatch();
    
    return 0;
}

