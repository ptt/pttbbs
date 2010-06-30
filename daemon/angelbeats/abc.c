// Angel Beats! Test Client
//
// Copyright (C) 2010, Hung-Te Lin <piaip@csie.ntu.edu.tw>
// All rights reserved
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bbs.h"
#include "daemons.h"

// standalone client to test angelbeats

int main(int argc, char *argv[])
{
    int fd;
    angel_beats_data req = {0};
    angel_beats_report rpt = {0};
    attach_SHM();

    if (argc < 2) {
	fprintf(stderr, "Usage: %s operation [uid]\n", argv[0]);
	return 0;
    }
    if ( (fd = toconnect(ANGELBEATS_ADDR)) < 0 ) {
	perror("toconnect");
	return 1;
    }

    // start commands
    if (strcmp(argv[1], "reload") == 0)
    {
        req.operation = ANGELBEATS_REQ_RELOAD;
    }
    else if (strcmp(argv[1], "suggest") == 0)
    {
        req.operation = ANGELBEATS_REQ_SUGGEST;
        if (argc > 2) {
            req.operation = ANGELBEATS_REQ_SUGGEST_AND_LINK;
            req.master_uid = searchuser(argv[2], NULL);
        }
    }
    else if (strcmp(argv[1], "unlink") == 0)
    {
        if (argc != 3) {
            printf("need target id.\n");
            return -1;
        }
        req.operation = ANGELBEATS_REQ_REMOVE_LINK;
        req.master_uid = 0; // anyone, just unlink
        req.angel_uid = searchuser(argv[2], NULL);
        if (!req.angel_uid) {
            printf("invalid user id: %s\n", argv[2]);
            return -1;
        }
    }
    else if (strcmp(argv[1], "report") == 0)
    {
        req.operation = ANGELBEATS_REQ_REPORT;
        if (argc > 2 && !(req.angel_uid = searchuser(argv[2], NULL))) {
            printf("invalid user id: %s\n", argv[2]);
            return -1;
        }
    }
    else
	return 0;

    req.cb = sizeof(req);
    if (towrite(fd, &req, sizeof(req)) != sizeof(req)) {
	perror("towrite");
	return 1;
    }
    if (req.operation == ANGELBEATS_REQ_REPORT) {
        if (toread(fd, &rpt, sizeof(rpt)) != sizeof(rpt)) {
            perror("toread");
            return 1;
        }
        assert(rpt.cb == sizeof(rpt));
        printf("total_angels=%d\n"
               "total_online_angels=%d\n"
               "total_active_angels=%d\n"
               "min_masters_of_online_angels=%d\n"
               "max_masters_of_online_angels=%d\n"
               "min_masters_of_active_angels=%d\n"
               "max_masters_of_active_angels=%d\n"
               "my_active_masters=%d\n",
               rpt.total_angels,
               rpt.total_online_angels,
               rpt.total_active_angels,
               rpt.min_masters_of_online_angels,
               rpt.max_masters_of_online_angels,
               rpt.min_masters_of_active_angels,
               rpt.max_masters_of_active_angels,
               rpt.my_active_masters);
    } else {
        if (toread(fd, &req, sizeof(req)) != sizeof(req)) {
            perror("toread");
            return 1;
        }
        printf("result: angel_uid=%d\n", req.angel_uid);
    }

    return 0;
}


