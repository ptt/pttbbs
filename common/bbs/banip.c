// Ban IP Table Reader (~bbs/etc/banip.conf)
//
// Copyright (C) 2012, Hung-Te Lin <piaip@csie.ntu.edu.tw>
// All rights reserved.
// Distributed under BSD license (GPL compatible).

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "cmbbs.h"
#define BANIP_ALLOC (128)

typedef struct {
    // table allocation
    size_t sz, alloc;
    IPv4 *ar;
} IPv4List;

static int
compare_ipv4(const void *pa, const void *pb) {
    const IPv4 *a = (const IPv4*)pa, *b = (const IPv4*)pb;
    // Since IPv4 are all random number, we can't do simple a-b due to
    // underflow.
    return (*a > *b) ? 1 : (*a == *b) ? 0 : -1;
}

static void
add_banip_list(IPv4List *list, IPv4 addr) {
    if (list->sz >= list->alloc) {
        list->alloc += BANIP_ALLOC;
        list->ar = (IPv4*)realloc(list->ar, sizeof(IPv4) * list->alloc);
        assert(list->ar);
    }
    list->ar[list->sz++] = addr;
}

static void
reset_banip_list(IPv4List *list) {
    list->sz = 0;
}

static void
sort_banip_list(IPv4List *list) {
    if (!list->ar)
        return;
    qsort(list->ar, list->sz, sizeof(IPv4), compare_ipv4);
}

int
in_banip_list_addr(const BanIpList *blist, IPv4 addr) {
    const IPv4List *list = (const IPv4List*)blist;
    if (!list || !list->ar)
        return 0;
    return bsearch(&addr, list->ar, list->sz,
                   sizeof(IPv4), compare_ipv4) != NULL;
}

int
in_banip_list(const BanIpList *blist, const char *ip) {
    struct in_addr addr;
    if (blist && inet_pton(AF_INET, ip, &addr) == 1)
        return in_banip_list_addr(blist, addr.s_addr);
    return 0;
}

BanIpList*
free_banip_list(BanIpList *blist) {
    IPv4List *list = (IPv4List*) blist;
    if (!list)
        return NULL;
    free(list->ar);
    free(list);
    return NULL;
}

BanIpList*
load_banip_list(const char *filename, FILE* err) {
    // Loads banip.conf (shared by daemon/banipd).
    IPv4List *list = NULL;
    FILE *fp;
    char *p;
    char buf[PATHLEN];
    struct in_addr addr;
    
    fp = fopen(filename, "rt");
    if (!fp)
        return (BanIpList*)list;

    list = (IPv4List*)malloc(sizeof(IPv4List));
    memset(list, 0, sizeof(*list));

    reset_banip_list(list);
    while (fgets(buf, sizeof(buf), fp)) {
        // only process lines starting with digits,
        // and ignore all comments.
        p = strchr(buf, '#');
        if (p) *p = 0;
        p = buf;
        while (*p && isascii(*p) && isspace(*p))
            p++;
        if (!(*p && isascii(*p) && isdigit(*p)))
            continue;
        for (p = strtok(p, " \t\r\n"); p; p = strtok(NULL, " \t\r\n")) {
            if (!(*p && isascii(*p) && isdigit(*p)))
                continue;
            if (inet_pton(AF_INET, p, &addr) != 1) {
                if (err)
                    fprintf(err, "(banip) Invalid IP: %s\n", p);
                continue;
            }
#ifdef DEBUG
            if (err)
                fprintf(err, "(banip) Added IP: %s %u\n", p, addr.s_addr);
#endif
            add_banip_list(list, addr.s_addr);
        }
    }
    fclose(fp);
    sort_banip_list(list);
    if (err)
        fprintf(err, "(banip) Loaded %lu IPs\n", list->sz);
    return (BanIpList*)list;
}

BanIpList*
cached_banip_list(const char *basefile, const char *cachefile) {
    BanIpList *blist = NULL;
    IPv4List *list = NULL;
    char tmpfn[PATHLEN];
    FILE *fp;
    time4_t m_base = dasht(basefile), m_cache = dasht(cachefile);
    size_t sz = dashs(cachefile);

    if (m_base < 0)
        return NULL;

    if (m_cache >= m_base && sz > 0 && sz % sizeof(IPv4) == 0) {
        // valid cache, load it.
        fp = fopen(cachefile, "rb");
        if (fp) {
#ifdef DEBUG
            fprintf(stderr, "Loaded cached banip config from: %s\n",
                    cachefile);
#endif
            list = (IPv4List*) malloc (sizeof(IPv4List));
            assert(list);
            memset(list, 0, sizeof(*list));
            reset_banip_list(list);
            list->ar = (IPv4*)malloc(sz);
            list->sz = sz / sizeof(IPv4);
            list->alloc = list->sz;
            fread(list->ar, sizeof(IPv4), list->sz, fp);
            fclose(fp);
            return list;
        }
    }

    // invalid cache, rebuild it.
    blist = load_banip_list(basefile, NULL);
    list = (IPv4List*)blist;
    snprintf(tmpfn, sizeof(tmpfn), "%s.%d", cachefile, getpid());
    fp = fopen(tmpfn, "wb");
    if (fp) {
        fwrite(list->ar, sizeof(IPv4), list->sz, fp);
        fclose(fp);
        Rename(tmpfn, cachefile);
#ifdef DEBUG
        fprintf(stderr, "Updated cached banip config to: %s\n", cachefile);
#endif
    }
    return list;
}
