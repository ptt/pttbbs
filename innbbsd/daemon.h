#ifndef DAEMON_H
#define DAEMON_H

#include <stdio.h>
#include <time.h>

#ifndef ARG
#ifdef __STDC__
#define ARG(x) x
#else
#define ARG(x) ()
#endif
#endif


struct Argv_t {
    FILE           *in, *out;
    int             argc;
    char          **argv;
    char           *inputline;
    struct Daemoncmd *dc;
};

typedef struct Argv_t argv_t;

typedef struct Buffer_t {
    char           *data;
    int             used, left, lastread;
}               buffer_t;

typedef struct ClientType {
    char            hostname[1024];
    char            username[32];
    char            buffer[4096];
    int             mode;
    argv_t          Argv;
    int             fd, access, lastread, midcheck;
    buffer_t        in, out;
    int             ihavecount, ihavesize, ihaveduplicate, ihavefail;
    int             statcount, statfail;
    time_t          begin;
}               ClientType;

typedef struct Daemoncmd {
    char           *name;
    char           *usage;
    int             argc, argno, errorcode, normalcode;
    int             (*main) ARG((ClientType *));
}               daemoncmd_t;

extern void installdaemon ARG((daemoncmd_t *, int, char *));
extern ClientType *Channel;

#endif
