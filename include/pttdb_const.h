/* $Id$ */
#ifndef PTTDB_CONST_H
#define PTTDB_CONST_H

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_BUF_SIZE 8192
#define MAX_CREATE_MILLI_TIMESTAMP 9999999999999 // XXX 2286-11-20

enum LiveStatus {
    LIVE_STATUS_ALIVE,
    LIVE_STATUS_DELETED,
};

#ifdef __cplusplus
}
#endif

#endif /* PTTDB_CONST_H */
