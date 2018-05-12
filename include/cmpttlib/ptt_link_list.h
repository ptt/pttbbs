/* $Id$ */
#ifndef PTT_LINK_LIST_H
#define PTT_LINK_LIST_H

#include "ptterr.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

typedef struct PttLinkList {
    struct PttLinkList *next;
    union _PttLinkListVal {
        void *p;
        int d;
    } val;
} PttLinkList; 

Err destroy_ptt_link_list(PttLinkList **p);

#ifdef __cplusplus
}
#endif

#endif /* PTT_LINK_LIST_H */
