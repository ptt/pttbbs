/* $Id$ */
#ifndef PTTUI_THREAD_LOCK_H
#define PTTUI_THREAD_LOCK_H

#include <mongoc.h>
#include "ptterr.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>

enum PttUIThreadLock {
    LOCK_PTTUI_THREAD_EXPECTED_STATE,
    LOCK_PTTUI_THREAD_BUFFER_STATE,

    LOCK_PTTUI_EXPECTED_STATE,
    LOCK_PTTUI_BUFFER_STATE,
    LOCK_PTTUI_WR_BUFFER_INFO,
    LOCK_PTTUI_BUFFER_INFO,
    LOCK_PTTUI_FILE_INFO,
    
    N_PTTUI_THREAD_LOCK,
};

extern pthread_rwlock_t PTTUI_RWLOCKS[N_PTTUI_THREAD_LOCK];

extern bool PTTUI_WRITE_PRIORITY_LOCK[N_PTTUI_THREAD_LOCK];

Err pttui_thread_lock_init();
Err pttui_thread_lock_destroy();
Err pttui_thread_lock_wrlock(enum PttUIThreadLock thread_lock);
Err pttui_thread_lock_rdlock(enum PttUIThreadLock thread_lock);
Err pttui_thread_lock_unlock(enum PttUIThreadLock thread_lock);

Err pttui_thread_lock_get_lock(enum PttUIThreadLock thread_lock, pthread_rwlock_t **p_lock);

#ifdef __cplusplus
}
#endif

#endif /* PTTUI_THREAD_LOCK_H */
