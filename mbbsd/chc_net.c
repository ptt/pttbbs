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
chc_send_status(int sock, board_t board){
    write(sock, &board, sizeof(board));
    /////// nessesery? correct?
    write(sock, &chc_turn, sizeof(chc_turn));
}

static void
chc_broadcast(chc_act_list *p, board_t board){
    while(p){
	if (!(p->flag & CHC_ACT_BOARD))
	    chc_send_status(p->sock, board);
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

void
chc_broadcast_recv(chc_act_list *act_list, board_t board){
    chc_recvmove(act_list->sock);
    chc_broadcast(act_list->next, board);
}

void
chc_broadcast_send(chc_act_list *act_list, board_t board){
    chc_broadcast(act_list, board);
}
