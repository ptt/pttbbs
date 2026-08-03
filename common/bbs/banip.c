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
    uint32_t start_ip; // host byte order
    uint32_t end_ip;   // host byte order
    uint32_t msg_offset;
} BanRecord;

typedef struct {
    // table allocation
    size_t sz, alloc;
    size_t szmsg, allocmsg;
    char  *msg;
    BanRecord *ar;
} IPv4List;

// Parses an IP string which may be a single IP, CIDR subnet, IP range, or wildcard.
// Supported formats:
// - Single IP: "140.112.1.2"
// - CIDR: "12.12.12.0/24"
// - Range: "10.0.0.1-10.0.0.50" or "10.0.0.1-50"
// - Wildcards: "12.12.12.*", "12.12.*.*", "12.*.*.*"
// Returns 1 on success, 0 on invalid format.
static int
parse_ip_or_range(const char *s, uint32_t *pstart, uint32_t *pend) {
    char buf[128];
    STRLCPY(buf, s);

    // 1. Check CIDR notation '/'
    char *slash = strchr(buf, '/');
    if (slash) {
        *slash = '\0';
        int prefix = atoi(slash + 1);
        if (prefix < 0 || prefix > 32)
            return 0;
        struct in_addr in;
        if (inet_pton(AF_INET, buf, &in) != 1)
            return 0;
        uint32_t ip = ntohl(in.s_addr);
        uint32_t mask = (prefix == 0) ? 0 : (prefix == 32) ? 0xFFFFFFFFU : (0xFFFFFFFFU << (32 - prefix));
        *pstart = ip & mask;
        *pend = *pstart | (~mask);
        return 1;
    }

    // 2. Check Range notation '-'
    char *dash = strchr(buf, '-');
    if (dash) {
        *dash = '\0';
        char *s_start = buf;
        char *s_end = dash + 1;
        struct in_addr in_start, in_end;
        if (inet_pton(AF_INET, s_start, &in_start) != 1)
            return 0;
        uint32_t ip1 = ntohl(in_start.s_addr);
        uint32_t ip2 = 0;

        if (strchr(s_end, '.')) {
            if (inet_pton(AF_INET, s_end, &in_end) != 1)
                return 0;
            ip2 = ntohl(in_end.s_addr);
        } else {
            int val = atoi(s_end);
            if (val < 0 || val > 255)
                return 0;
            ip2 = (ip1 & 0xFFFFFF00U) | (uint32_t)val;
        }

        if (ip1 > ip2) {
            *pstart = ip2;
            *pend = ip1;
        } else {
            *pstart = ip1;
            *pend = ip2;
        }
        return 1;
    }

    // 3. Check Wildcard '*'
    if (strchr(buf, '*')) {
        char s_min[128], s_max[128];
        int i = 0, j_min = 0, j_max = 0;

        for (i = 0; buf[i]; i++) {
            if (buf[i] == '*') {
                s_min[j_min++] = '0';
                s_max[j_max++] = '2';
                s_max[j_max++] = '5';
                s_max[j_max++] = '5';
            } else {
                s_min[j_min++] = buf[i];
                s_max[j_max++] = buf[i];
            }
        }
        s_min[j_min] = '\0';
        s_max[j_max] = '\0';

        struct in_addr in_min, in_max;
        if (inet_pton(AF_INET, s_min, &in_min) != 1 ||
            inet_pton(AF_INET, s_max, &in_max) != 1) {
            return 0;
        }
        *pstart = ntohl(in_min.s_addr);
        *pend = ntohl(in_max.s_addr);
        if (*pstart > *pend) {
            uint32_t tmp = *pstart;
            *pstart = *pend;
            *pend = tmp;
        }
        return 1;
    }

    // 4. Single IPv4 Address
    struct in_addr in;
    if (inet_pton(AF_INET, buf, &in) == 1) {
        *pstart = *pend = ntohl(in.s_addr);
        return 1;
    }

    return 0;
}

static int
compare_banrecord(const void *pa, const void *pb) {
    const BanRecord *a = (const BanRecord*)pa, *b = (const BanRecord*)pb;
    if (a->start_ip < b->start_ip) return -1;
    if (a->start_ip > b->start_ip) return 1;
    if (a->end_ip < b->end_ip) return -1;
    if (a->end_ip > b->end_ip) return 1;
    return 0;
}

static void
add_banip_list_range(IPv4List *list, uint32_t start_ip, uint32_t end_ip) {
    if (list->sz >= list->alloc) {
        list->alloc += BANIP_ALLOC;
        list->ar = (BanRecord*)realloc(
            list->ar, sizeof(BanRecord) * list->alloc);
        assert(list->ar);
    }
    list->ar[list->sz].msg_offset = list->szmsg;
    list->ar[list->sz].start_ip = start_ip;
    list->ar[list->sz].end_ip = end_ip;
    list->sz++;
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
    if (!list || !list->ar || list->sz == 0)
        return;
    qsort(list->ar, list->sz, sizeof(BanRecord), compare_banrecord);

    // Merge adjacent / overlapping intervals with same message
    size_t out = 0;
    for (size_t i = 1; i < list->sz; i++) {
        BanRecord *prev = &list->ar[out];
        BanRecord *curr = &list->ar[i];

        if (curr->start_ip <= prev->end_ip + 1 && curr->start_ip >= prev->start_ip) {
            if (curr->msg_offset == prev->msg_offset) {
                if (curr->end_ip > prev->end_ip) {
                    prev->end_ip = curr->end_ip;
                }
                continue;
            }
        }
        out++;
        list->ar[out] = *curr;
    }
    list->sz = out + 1;
}

static const BanRecord *
search_banrecord(const BanRecord *ar, size_t sz, uint32_t ip) {
    if (!ar || sz == 0)
        return NULL;

    size_t low = 0;
    size_t high = sz - 1;
    size_t best_candidate = (size_t)-1;

    while (low <= high) {
        size_t mid = low + (high - low) / 2;
        if (ar[mid].start_ip <= ip) {
            best_candidate = mid;
            if (ip <= ar[mid].end_ip) {
                return &ar[mid];
            }
            low = mid + 1;
        } else {
            if (mid == 0) break;
            high = mid - 1;
        }
    }

    if (best_candidate != (size_t)-1) {
        for (ssize_t i = (ssize_t)best_candidate; i >= 0; i--) {
            if (ip >= ar[i].start_ip && ip <= ar[i].end_ip) {
                return &ar[i];
            }
        }
    }

    return NULL;
}

const char *
in_banip_list_addr(const BanIpList *blist, IPv4 addr) {
    const IPv4List *list = (const IPv4List*)blist;
    if (!list || !list->ar || list->sz == 0)
        return NULL;
    uint32_t ip = ntohl(addr);
    const BanRecord *p = search_banrecord(list->ar, list->sz, ip);
    if (!p)
        return NULL;
    return (p->msg_offset < list->szmsg && list->msg) ?
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
    int was_ip = 1;

    fp = fopen(filename, "rt");
    if (!fp)
        return (BanIpList*)list;

    list = (IPv4List*)malloc(sizeof(IPv4List));
    memset(list, 0, sizeof(*list));
    msg[0] = '\0';

    reset_banip_list(list);
    while (fgets(buf, sizeof(buf), fp)) {
        STRLCAT(buf, "\r");
        p = buf;
        while (*p && isascii(*p) && isspace(*p))
            p++;

        // 1. Any line starting with '#' is ALWAYS ignored as a comment
        if (*p == '#')
            continue;

        // 2. Test if line (tokens before any inline '#') is a valid IP/Range line
        char line_copy[PATHLEN];
        STRLCPY(line_copy, p);
        char *sharp = strchr(line_copy, '#');
        if (sharp) *sharp = '\0';

        int is_ip_line = 1;
        int token_count = 0;
        char *tok_ptr = line_copy;
        char *tok;

        while ((tok = strtok(tok_ptr, " \t\r\n")) != NULL) {
            tok_ptr = NULL;
            token_count++;
            uint32_t dummy_start, dummy_end;
            if (!parse_ip_or_range(tok, &dummy_start, &dummy_end)) {
                is_ip_line = 0;
                break;
            }
        }
        if (token_count == 0) is_ip_line = 0;

        if (is_ip_line) {
            // It's an IP line!
            if (!was_ip) {
                add_banip_list_message(list, msg);
                msg[0] = '\0';
                was_ip = 1;
            }

            // Parse and add IP/Range records
            STRLCPY(line_copy, p);
            sharp = strchr(line_copy, '#');
            if (sharp) *sharp = '\0';

            for (tok = strtok(line_copy, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n")) {
                uint32_t start_ip, end_ip;
                if (!parse_ip_or_range(tok, &start_ip, &end_ip))
                    continue;
                add_banip_list_range(list, start_ip, end_ip);
            }
        } else {
            // It's NOT an IP line AND does not start with '#'!
            if (list->sz < 1) {
                if (!*p) continue;
                if (err) fprintf(err, "(banip) WARN: Text before IP: %s", buf);
                continue;
            }

            if (was_ip) {
                // IP Parsing Phase: ignore blank lines immediately following IP lines
                if (!*p)
                    continue;
                // First non-blank message line!
                STRLCPY(msg, buf);
                was_ip = 0;
            } else {
                // MESSAGE Content Phase: preserve blank lines!
                STRLCAT(msg, buf);
            }
        }
    }

    if (!was_ip) {
        add_banip_list_message(list, msg);
    } else {
        if (err && list->sz > 0)
            fprintf(err, "(banip) WARN: Trailing IP records without text.\n");
    }

    fclose(fp);
    sort_banip_list(list);
    if (err)
        fprintf(err, "(banip) Loaded %lu IP ranges\n", list->sz);
    return (BanIpList*)list;
}

BanIpList*
cached_banip_list(const char *basefile, const char *cachefile) {
    BanIpList *blist = NULL;
    IPv4List *list = NULL;
    char tmpfn[PATHLEN];
    FILE *fp;
    time4_t m_base = dasht(basefile);

    if (time4_is_invalid(m_base))
        return NULL;

    time4_t m_cache = dasht(cachefile);
    size_t sz = dashs(cachefile);

    if (m_cache >= m_base && sz >= sizeof(size_t) * 2) {
        fp = fopen(cachefile, "rb");
        if (fp) {
            size_t rsz = 0, rszmsg = 0;
            if (fread(&rsz, sizeof(size_t), 1, fp) == 1 &&
                fread(&rszmsg, sizeof(size_t), 1, fp) == 1 &&
                sz == sizeof(size_t) * 2 + rsz * sizeof(BanRecord) + rszmsg) {
#ifdef DEBUG
                fprintf(stderr, "Loaded cached banip config from: %s\n", cachefile);
#endif
                list = (IPv4List*) malloc(sizeof(IPv4List));
                assert(list);
                memset(list, 0, sizeof(*list));
                list->sz = rsz;
                list->alloc = rsz;
                list->szmsg = rszmsg;
                list->allocmsg = rszmsg;
                if (rsz > 0) {
                    list->ar = (BanRecord*)malloc(rsz * sizeof(BanRecord));
                    assert(list->ar);
                    fread(list->ar, sizeof(BanRecord), rsz, fp);
                }
                if (rszmsg > 0) {
                    list->msg = (char*)malloc(rszmsg);
                    assert(list->msg);
                    fread(list->msg, 1, rszmsg, fp);
                }
                fclose(fp);
                return (BanIpList*)list;
            }
            fclose(fp);
        }
    }

    blist = load_banip_list(basefile, NULL);
    list = (IPv4List*)blist;
    if (!list)
        return NULL;

    SNPRINTF(tmpfn, "%s.%d", cachefile, getpid());
    fp = fopen(tmpfn, "wb");
    if (fp) {
        fwrite(&list->sz, sizeof(size_t), 1, fp);
        fwrite(&list->szmsg, sizeof(size_t), 1, fp);
        if (list->sz > 0 && list->ar) {
            fwrite(list->ar, sizeof(BanRecord), list->sz, fp);
        }
        if (list->szmsg > 0 && list->msg) {
            fwrite(list->msg, 1, list->szmsg, fp);
        }
        fclose(fp);
        Rename(tmpfn, cachefile);
#ifdef DEBUG
        fprintf(stderr, "Updated cached banip config to: %s\n", cachefile);
#endif
    }
    return (BanIpList*)list;
}
