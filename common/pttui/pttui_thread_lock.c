#include "cmpttui/pttui_thread_lock.h"
#include "cmpttui/pttui_thread_lock_private.h"

pthread_rwlock_t PTTUI_RWLOCKS[N_PTTUI_THREAD_LOCK] = {};
bool _IS_INIT_PTTUI_RWLOCKS[N_PTTUI_THREAD_LOCK] = {};
bool _IS_WR_PTTUI_RWLOCKS[N_PTTUI_THREAD_LOCK] = {};

/**
 * @brief [brief description]
 * @details
 *
 *          XXX no need to do lock, because set only by 1 thread, and set with primitive-type (bool)
 *
 * @return [description]
 */
bool PTTUI_WRITE_PRIORITY_LOCK[N_PTTUI_THREAD_LOCK] = {};

Err
pttui_thread_lock_init()
{
    int ret = 0;
    for (int i = 0; i < N_PTTUI_THREAD_LOCK; i++) {
        ret = pthread_rwlock_init(&PTTUI_RWLOCKS[i], NULL);
        if (ret) {
            return S_ERR;
        }
        _IS_INIT_PTTUI_RWLOCKS[i] = true;
    }

    return S_OK;
}

Err
pttui_thread_lock_destroy()
{
    Err error_code = S_OK;
    int ret = 0;
    for (int i = 0; i < N_PTTUI_THREAD_LOCK; i++) {
        if (!_IS_INIT_PTTUI_RWLOCKS[i]) continue;

        ret = pthread_rwlock_destroy(&PTTUI_RWLOCKS[i]);
        if (ret) {
            error_code = S_ERR;
        }
    }

    return error_code;
}

Err
pttui_thread_lock_wrlock(enum PttUIThreadLock thread_lock)
{
    Err error_code = S_OK;

    struct timespec req = {0, NS_DEFAULT_SLEEP_LOCK};
    struct timespec rem = {};

    _IS_WR_PTTUI_RWLOCKS[thread_lock] = true;

    int ret_sleep = 0;
    int ret_lock = pthread_rwlock_trywrlock(&PTTUI_RWLOCKS[thread_lock]);

    for(int i = 0; i < N_ITER_PTTUI_WRITE_LOCK; i++) {
        if(!ret_lock) break;
        if(ret_lock != EBUSY) {
            error_code = S_ERR;
            break;
        }

        ret_sleep = nanosleep(&req, &rem);
        if(ret_sleep) {
            error_code = S_ERR;
            break;
        }

        ret_lock = pthread_rwlock_trywrlock(&PTTUI_RWLOCKS[thread_lock]);
        if(!ret_lock) break;

    }

    if(ret_lock && !error_code) error_code = S_ERR_BUSY;

    _IS_WR_PTTUI_RWLOCKS[thread_lock] = false;

    return error_code;
}

Err
pttui_thread_lock_rdlock(enum PttUIThreadLock thread_lock)
{
    Err error_code = S_OK;

    struct timespec req = {0, NS_DEFAULT_SLEEP_LOCK};
    struct timespec rem = {};

    int ret_sleep = 0;
    int i = 0; 
    for(i = 0; i < N_ITER_PTTUI_READ_LOCK; i++) {
        if(!_IS_WR_PTTUI_RWLOCKS[thread_lock]) break;

        ret_sleep = nanosleep(&req, &rem);
        if(ret_sleep) {
            error_code = S_ERR;
            break;
        }
    }
    if(!error_code && i == N_ITER_PTTUI_READ_LOCK) error_code = S_ERR_BUSY;
    
    if(error_code) return error_code;

    int ret_lock = pthread_rwlock_tryrdlock(&PTTUI_RWLOCKS[thread_lock]);

    for(i = 0; i < N_ITER_PTTUI_READ_LOCK; i++) {
        if(!ret_lock) break;
        if(ret_lock != EBUSY) {
            error_code = S_ERR;
            break;
        }

        ret_sleep = nanosleep(&req, &rem);
        if(ret_sleep) {
            error_code = S_ERR;
            break;
        }

        ret_lock = pthread_rwlock_tryrdlock(&PTTUI_RWLOCKS[thread_lock]);
        if(!ret_lock) break;

    }

    if(ret_lock && !error_code) error_code = S_ERR_BUSY;

    return error_code;
}

Err
pttui_thread_lock_unlock(enum PttUIThreadLock thread_lock)
{
    int ret = pthread_rwlock_unlock(&PTTUI_RWLOCKS[thread_lock]);

    if (ret) return S_ERR;

    return S_OK;
}

Err
pttui_thread_lock_get_lock(enum PttUIThreadLock thread_lock, pthread_rwlock_t **p_lock) {
    *p_lock = &PTTUI_RWLOCKS[thread_lock];

    return S_OK;
}