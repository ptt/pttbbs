/* $Id: chc_net.c,v 1.1 2002/03/07 15:13:48 in2 Exp $ */
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include "config.h"
#include "pttstruct.h"

extern rc_t chc_from, chc_to;

typedef struct drc_t {
    rc_t from, to;
} drc_t;

int chc_recvmove(int s) {
    drc_t buf;

    if(read(s, &buf, sizeof(buf)) != sizeof(buf))
	return 1;
    chc_from = buf.from, chc_to = buf.to;
    return 0;
}

void chc_sendmove(int s) {
    drc_t buf;
    
    buf.from = chc_from, buf.to = chc_to;
    write(s, &buf, sizeof(buf));
}
