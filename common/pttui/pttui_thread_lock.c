#include "cmpttui/pttui_thread_lock.h"
#include "cmpttui/pttui_thread_lock_private.h"

pthread_rwlock_t _PTTUI_RWLOCKS[N_PTTUI_THREAD_LOCK] = {};
bool _PTTUI_WRITE_PRIORITY_LOCK[N_PTTUI_THREAD_LOCK] = {};

bool _IS_INIT_PTTUI_RWLOCKS[N_PTTUI_THREAD_LOCK] = {};
bool _IS_WR_PTTUI_RWLOCKS[N_PTTUI_THREAD_LOCK] = {};

Err
InitPttUIThreadLock()
{
    int ret = 0;
    for (int i = 0; i < N_PTTUI_THREAD_LOCK; i++) {
        ret = pthread_rwlock_init(&_PTTUI_RWLOCKS[i], NULL);
        if (ret) {
            return S_ERR;
        }
        _IS_INIT_PTTUI_RWLOCKS[i] = true;
    }

    return S_OK;
}

Err
DestroyPttUIThreadLock()
{

    Err error_code = S_OK;
    int ret = 0;
    for (int i = 0; i < N_PTTUI_THREAD_LOCK; i++) {
        if (!_IS_INIT_PTTUI_RWLOCKS[i]) continue;

        ret = pthread_rwlock_destroy(&_PTTUI_RWLOCKS[i]);
        if (ret) {
            error_code = S_ERR;
        }

        _IS_INIT_PTTUI_RWLOCKS[i] = false;
    }

    return error_code;
}

Err
PttUIThreadLockWrlock(enum PttUIThreadLock thread_lock)
{
    Err error_code = S_OK;

    struct timespec req = {0, NS_DEFAULT_SLEEP_LOCK};
    struct timespec rem = {};

    _IS_WR_PTTUI_RWLOCKS[thread_lock] = true;

    int ret_sleep = 0;
    int ret_lock = 0;
    int i = 0;
    for(i = 0; i < N_ITER_PTTUI_WRITE_LOCK; i++) {
        ret_lock = pthread_rwlock_trywrlock(&_PTTUI_RWLOCKS[thread_lock]);
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
    }

    if(!error_code && i == N_ITER_PTTUI_WRITE_LOCK) error_code = S_ERR_BUSY;

    _IS_WR_PTTUI_RWLOCKS[thread_lock] = false;

    return error_code;
}

Err
PttUIThreadLockRdlock(enum PttUIThreadLock thread_lock)
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

    int ret_lock = 0;
    for(i = 0; i < N_ITER_PTTUI_READ_LOCK; i++) {
        ret_lock = pthread_rwlock_tryrdlock(&_PTTUI_RWLOCKS[thread_lock]);
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
    }

    if(!error_code && i == N_ITER_PTTUI_READ_LOCK) error_code = S_ERR_BUSY;

    return error_code;
}

Err
PttUIThreadLockUnlock(enum PttUIThreadLock thread_lock)
{
    int ret = pthread_rwlock_unlock(&_PTTUI_RWLOCKS[thread_lock]);

    if (ret) return S_ERR;

    return S_OK;
}

Err
PttUIThreadLockGetLock(enum PttUIThreadLock thread_lock, pthread_rwlock_t **p_lock)
{

    *p_lock = &_PTTUI_RWLOCKS[thread_lock];

    return S_OK;
}
