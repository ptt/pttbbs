/* $Id$ */
#ifndef PTTUI_THREAD_LOCK_PRIVATE_H
#define PTTUI_THREAD_LOCK_PRIVATE_H

#include "pttconst.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <errno.h>
#include <pthread.h>

#define NS_DEFAULT_SLEEP_LOCK 1000000 // 1ms
#define N_ITER_PTTUI_WRITE_LOCK 100 // write lock waits up to 100 ms
#define N_ITER_PTTUI_READ_LOCK 50 // read lock waits up to 50 ms

extern pthread_rwlock_t _PTTUI_RWLOCKS[N_PTTUI_THREAD_LOCK];

extern bool _PTTUI_WRITE_PRIORITY_LOCK[N_PTTUI_THREAD_LOCK];

#ifdef __cplusplus
}
#endif

#endif /* PTTUI_THREAD_LOCK_PRIVATE_H */
