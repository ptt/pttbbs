#include "cmpttlib/ptt_queue.h"

Err
ptt_enqueue_p(void *p, PttQueue *q) {
    if(!q->tail) {
        q->tail = malloc(sizeof(PttLinkList));
        q->head = q->tail;
    }
    else {
        q->tail->next = malloc(sizeof(PttLinkList));
        q->tail = q->tail->next;
    }

    bzero(q->tail, sizeof(PttLinkList));

    q->tail->val.p = p;
    q->n_queue++;

    return S_OK;
}

Err
destroy_ptt_queue(PttQueue *q)
{
    Err error_code = destroy_ptt_link_list(&q->head);
    if(error_code) return error_code;

    q->head = NULL;
    q->tail = NULL;
    q->n_queue = 0;
    
    return S_OK;
}
