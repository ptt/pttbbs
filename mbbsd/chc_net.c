/* $Id$ */
#include "bbs.h"
typedef struct drc_t {
    rc_t            from, to;
}               drc_t;

int
chc_recvmove(int s)
{
    drc_t           buf;

    if (read(s, &buf, sizeof(buf)) != sizeof(buf))
	return 1;
    chc_from = buf.from, chc_to = buf.to;
    return 0;
}

void
chc_sendmove(chc_act_list *list)
{
    drc_t           buf;
    chc_act_list   *p = list;

    buf.from = chc_from, buf.to = chc_to;
    
    while(p){
	if (write(p->sock, &buf, sizeof(buf)) < 0) {
	    free(p->next);
	    if (p->next->next == NULL)
		p = NULL;
	    else {
		chc_act_list *tmp = p->next->next;
		p->next = tmp;
	    }
	}
	p = p->next;
    }
}
