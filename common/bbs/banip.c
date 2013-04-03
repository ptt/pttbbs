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

#define BANIP_ALLOC (512)
#define BANIP_ALLOCMSG (1024)

static const char *str_banned = "YOUR ARE USING A BANNED IP.\n\r";

typedef struct {
    IPv4 addr;
    uint32_t msg_offset;
} BanRecord;

typedef struct {
    // table allocation
    size_t sz, alloc;
    size_t szmsg, allocmsg;
    char  *msg;
    BanRecord *ar;
} IPv4List;

static int
compare_ipv4(const void *pa, const void *pb) {
    const BanRecord *a = (const BanRecord*)pa, *b = (const BanRecord*)pb;
    // Since IPv4 are all random number, we can't do simple a-b due to
    // underflow.
    return (a->addr > b->addr) ? 1 : (a->addr == b->addr) ? 0 : -1;
}

static void
add_banip_list(IPv4List *list, IPv4 addr) {
    if (list->sz >= list->alloc) {
        list->alloc += BANIP_ALLOC;
        list->ar = (BanRecord*)realloc(
            list->ar, sizeof(BanRecord) * list->alloc);
        assert(list->ar);
    }
    list->ar[list->sz].msg_offset = list->szmsg;
    list->ar[list->sz++].addr = addr;
}

static void
add_banip_list_message(IPv4List *list, const char *msg) {
    int len = strlen(msg);
    char *p;
    // Add more space for '\n\r\0'
    while (list->szmsg + len + 3 >= list->allocmsg) {
#ifdef DEBUG
        fprintf(stderr, "(banip) Allocate more msg buffer: %lu->%lu\n",
                list->allocmsg, list->allocmsg + BANIP_ALLOCMSG);
#endif
        list->allocmsg += BANIP_ALLOCMSG;
        list->msg = (char*)realloc(list->msg, list->allocmsg);
        assert(list->msg);
    }
    p = list->msg + list->szmsg;
    strcpy(p, msg);

    // Remove trailing blank lines.
    while (len > 0 && isascii(p[len - 1]) && isspace(p[len - 1])) {
        p[--len] = 0;
    }
    assert(len > 0);
    p[len++] = '\n';
    p[len++] = '\r';
    p[len++] = 0;
#ifdef DEBUG
    fprintf(stderr, "(banip) Add new message: %s", p);
#endif
    list->szmsg += len;
}

static void
reset_banip_list(IPv4List *list) {
    list->sz = 0;
    list->szmsg = 0;
}

static void
sort_banip_list(IPv4List *list) {
    if (!list->ar)
        return;
    qsort(list->ar, list->sz, sizeof(BanRecord), compare_ipv4);
}

const char *
in_banip_list_addr(const BanIpList *blist, IPv4 addr) {
    const IPv4List *list = (const IPv4List*)blist;
    BanRecord r = { .addr=addr }, *p;
    if (!list || !list->ar)
        return NULL;
    p = bsearch(&r, list->ar, list->sz, sizeof(BanRecord), compare_ipv4);
    if (!p)
        return NULL;
    return (p->msg_offset < list->szmsg) ?
        list->msg + p->msg_offset : str_banned;
}

const char *
in_banip_list(const BanIpList *blist, const char *ip) {
    struct in_addr addr;
    if (blist && inet_pton(AF_INET, ip, &addr) == 1)
        return in_banip_list_addr(blist, addr.s_addr);
    return NULL;
}

BanIpList*
free_banip_list(BanIpList *blist) {
    IPv4List *list = (IPv4List*) blist;
    if (!list)
        return NULL;
    free(list->ar);
    free(list->msg);
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
    char msg[25 * ANSILINELEN];
    struct in_addr addr;
    int was_ip = 1;
    
    fp = fopen(filename, "rt");
    if (!fp)
        return (BanIpList*)list;

    list = (IPv4List*)malloc(sizeof(IPv4List));
    memset(list, 0, sizeof(*list));

    reset_banip_list(list);
    while (fgets(buf, sizeof(buf), fp)) {
        // To allow client printing message to screen directly,
        // always append \r.
        strlcat(buf, "\r", sizeof(buf));
        p = buf;
        while (*p && isascii(*p) && isspace(*p))
            p++;
        // first, remove lines with only comments.
        if (*p == '#')
            continue;

        // process IP entries, otherwise append text.
        if (*p && isascii(*p) && isdigit(*p) && inet_addr(p) != INADDR_NONE) {
            char *sharp = strchr(p, '#');
            if (sharp) *sharp = 0;
            if (!was_ip) {
                add_banip_list_message(list, msg);
                was_ip = 1;
            }
        } else {
            // For text entries, use raw input and ignore comments.
            // Also ignore all blank lines before first data.
            if (was_ip) {
                if (!*p)
                    continue;
                if (list->sz < 1) {
                    if (err)
                        fprintf(err, "(banip) WARN: Text before IP: %s", buf);
                    continue;
                }
                strlcpy(msg, buf, sizeof(msg));
            }
            else
                strlcat(msg, buf, sizeof(msg));
            was_ip = 0;
            continue;
        }

        // Parse and add IP records.
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
    if (was_ip) {
        if (err)
            fprintf(err, "(banip) WARN: Trailing IP records without text.\n");
    } else {
        add_banip_list_message(list, msg);
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

    // TODO currently we only save the ipv4 address & index (BanRecord) without
    // message body; in future we should also cache that, or throw everything
    // into SHM, or set BanRecods.msg_offset to 0.

    if (m_cache >= m_base && sz > 0 && sz % sizeof(BanRecord) == 0) {
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
            list->ar = (BanRecord*)malloc(sz);
            list->sz = sz / sizeof(BanRecord);
            list->alloc = list->sz;
            fread(list->ar, sizeof(BanRecord), list->sz, fp);
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
        fwrite(list->ar, sizeof(BanRecord), list->sz, fp);
        fclose(fp);
        Rename(tmpfn, cachefile);
#ifdef DEBUG
        fprintf(stderr, "Updated cached banip config to: %s\n", cachefile);
#endif
    }
    return list;
}
