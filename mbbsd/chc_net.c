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
	return 0;
    chc_from = buf.from, chc_to = buf.to;
    return 1;
}

int
chc_sendmove(int s)
{
    drc_t           buf;

    buf.from = chc_from, buf.to = chc_to;
    if (write(s, &buf, sizeof(buf)) != sizeof(buf))
	return 0;
    return 1;
}

static void
chc_broadcast(chc_act_list *p, board_t board){
    while(p){
	if (chc_sendmove(p->sock) < 0) {
	    if (p->next->next == NULL)
		p = NULL;
	    else {
		chc_act_list *tmp = p->next->next;
		p->next = tmp;
	    }
	    free(p->next);
	}
	p = p->next;
    }
}

int
chc_broadcast_recv(chc_act_list *act_list, board_t board){
    if (!chc_recvmove(act_list->sock))
	return 0;
    chc_broadcast(act_list->next, board);
    return 1;
}

void
chc_broadcast_send(chc_act_list *act_list, board_t board){
    chc_broadcast(act_list, board);
}

