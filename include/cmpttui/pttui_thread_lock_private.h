/* $Id$ */
#ifndef PTTUI_THREAD_LOCK_PRIVATE_H
#define PTTUI_THREAD_LOCK_PRIVATE_H

#include "ptterr.h"
#include <mongoc.h>        // XXX hack for bool

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>
#include <pthread.h>

#define NS_DEFAULT_SLEEP_LOCK 1000000 // 1ms
#define N_ITER_PTTUI_WRITE_LOCK 10000 // write lock waits up to 10 s
#define N_ITER_PTTUI_READ_LOCK 5000 // read lock waits up to 5s

#ifdef __cplusplus
}
#endif

#endif /* PTTUI_THREAD_LOCK_PRIVATE_H */
