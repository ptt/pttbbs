/* $Id$ */
#ifndef PTT_QUEUE_H
#define PTT_QUEUE_H

#include "ptterr.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <string.h>
#include "cmpttlib/ptt_link_list.h"

typedef struct PttQueue {
    PttLinkList *head;
    PttLinkList *tail;
    int n_queue;
} PttQueue;

Err ptt_enqueue_p(void *p, PttQueue *q);

Err destroy_ptt_queue(PttQueue *q);

#ifdef __cplusplus
}
#endif

#endif /* PTT_QUEUE_H */

