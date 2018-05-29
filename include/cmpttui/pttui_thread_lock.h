/* $Id$ */
#ifndef PTTUI_THREAD_LOCK_H
#define PTTUI_THREAD_LOCK_H

#include "pttconst.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>

enum PttUIThreadLock {
    LOCK_PTTUI_THREAD_EXPECTED_STATE,
    LOCK_PTTUI_THREAD_BUFFER_STATE,

    N_PTTUI_THREAD_LOCK,
};

Err InitPttUIThreadLock();

Err DestroyPttUIThreadLock();

Err PttUIThreadLockWrlock(enum PttUIThreadLock thread_lock);

Err PttUIThreadLockRdlock(enum PttUIThreadLock thread_lock);

Err PttUIThreadLockUnlock(enum PttUIThreadLock thread_lock);

Err PttUIThreadLockGetLock(enum PttUIThreadLock thread_lock, pthread_rwlock_t **p_lock);

#ifdef __cplusplus
}
#endif

#endif /* PTTUI_THREAD_LOCK_H */
