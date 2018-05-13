#include "cmpttui/pttui_thread.h"
#include "cmpttui/pttui_thread_private.h"

enum PttUIThreadState PTTUI_THREAD_EXPECTED_STATE = PTTUI_THREAD_STATE_START;
enum PttUIThreadState PTTUI_THREAD_BUFFER_STATE = PTTUI_THREAD_STATE_START;

pthread_t PTTUI_THREAD_BUFFER;

Err
init_pttui_thread()
{
    Err error_code = S_OK;

    int ret = pthread_create(&PTTUI_THREAD_BUFFER, NULL, pttui_thread_buffer, NULL);
    if(ret) error_code = S_ERR;

    return error_code;
}

Err
destroy_pttui_thread()
{
    Err error_code = S_OK;

    int ret = pthread_cancel(PTTUI_THREAD_BUFFER);
    if(ret) error_code = S_ERR;

    return error_code;
}

void *
pttui_thread_buffer(void *a GCC_UNUSED)
{
    Err error_code = S_OK;
    bool is_end = false;
    enum PttUIThreadState expected_state = PTTUI_THREAD_STATE_START;

    error_code = pttui_thread_set_buffer_state(PTTUI_THREAD_STATE_START);
    if (error_code) return NULL;

    struct timespec req = {0, NS_DEFAULT_SLEEP_THREAD};
    struct timespec rem = {};
    int ret = 0;

    while (1) {
        error_code = _pttui_thread_is_end(&is_end);
        if (error_code) break;

        if (is_end) break;

        error_code = pttui_thread_get_expected_state(&expected_state);
        if (error_code) break;

        switch (expected_state) {
        case PTTUI_THREAD_STATE_INIT_EDIT:
            pttui_thread_set_buffer_state(expected_state);
            error_code = vedit3_init_buffer();
            break;
        case PTTUI_THREAD_STATE_EDIT:
            pttui_thread_set_buffer_state(expected_state);
            error_code = vedit3_buffer();
            break;
        default:
            pttui_thread_set_buffer_state(expected_state);
            break;
        }

        if (error_code) {
            fprintf(stderr, "pttui_thread.pttui_thread_disp_buffer: pid: %d user: %s expected_state: %d e: %d\n", expected_state, error_code);
            break;
        }

        ret = nanosleep(&req, &rem);
        if(ret) break;
    }

    Err error_code_set_state = pttui_thread_set_buffer_state(PTTUI_THREAD_STATE_END);
    if (!error_code && error_code_set_state) error_code = error_code_set_state;

    return NULL;
}

Err
pttui_thread_set_expected_state(enum PttUIThreadState thread_state)
{
    // lock
    Err error_code = pttui_thread_lock_wrlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);
    if (error_code) return error_code;

    // do op
    PTTUI_THREAD_EXPECTED_STATE = thread_state;

    // release lock
    pttui_thread_lock_unlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);

    return S_OK;
}

Err
pttui_thread_get_expected_state(enum PttUIThreadState *thread_state)
{
    // lock
    Err error_code = pttui_thread_lock_rdlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);
    if (error_code) return error_code;

    // do op
    *thread_state = PTTUI_THREAD_EXPECTED_STATE;

    // release lock
    pttui_thread_lock_unlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);

    return S_OK;
}

Err
pttui_thread_set_buffer_state(enum PttUIThreadState thread_state)
{
    Err error_code = pttui_thread_lock_wrlock(LOCK_PTTUI_THREAD_BUFFER_STATE);
    if (error_code) return error_code;

    PTTUI_THREAD_BUFFER_STATE = thread_state;

    pttui_thread_lock_unlock(LOCK_PTTUI_THREAD_BUFFER_STATE);

    return S_OK;
}

Err
pttui_thread_get_buffer_state(enum PttUIThreadState *thread_state)
{
    Err error_code = pttui_thread_lock_rdlock(LOCK_PTTUI_THREAD_BUFFER_STATE);
    if (error_code) return error_code;

    *thread_state = PTTUI_THREAD_BUFFER_STATE;

    pttui_thread_lock_unlock(LOCK_PTTUI_THREAD_BUFFER_STATE);

    return S_OK;
}

Err
_pttui_thread_is_end(bool *is_end)
{
    *is_end = false;
    return S_OK;
}

Err
pttui_thread_wait_buffer_loop(enum PttUIThreadState expected_state, int n_iter)
{
    Err error_code = S_OK;
    int ret_sleep = 0;
    enum PttUIThreadState current_state = PTTUI_THREAD_STATE_START;

    struct timespec req = {0, NS_DEFAULT_SLEEP_PTTUI_THREAD_WAIT_BUFFER_LOOP};
    struct timespec rem = {};

    int i = 0;
    for (i = 0; i < n_iter; i++) {
        error_code = pttui_thread_get_buffer_state(&current_state);
        if (error_code) break;

        if (expected_state == current_state) break; // XXX maybe re-edit too quickly

        ret_sleep = nanosleep(&req, &rem);
        if (ret_sleep) {
            error_code = S_ERR;
            break;
        }
    }
    if (i == n_iter) error_code = S_ERR_BUSY;

    return error_code;
}
