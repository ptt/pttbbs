#include "cmpttlib/ptt_link_list.h"

Err
destroy_ptt_link_list(PttLinkList **link_list)
{
    PttLinkList *p = *link_list;
    PttLinkList *tmp = NULL;
    while(p != NULL) {
        tmp = p->next;
        free(p);
        p = tmp;
    }
    *link_list = NULL;

    return S_OK;
}
