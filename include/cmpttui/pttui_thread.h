/* $Id$ */
#ifndef PTTUI_THREAD_H
#define PTTUI_THREAD_H

#include "pttconst.h"

#ifdef __cplusplus
extern "C" {
#endif

enum PttUIThreadState {
    PTTUI_THREAD_STATE_START,
    PTTUI_THREAD_STATE_END,

    N_PTTUI_THREAD_STATE
};

Err InitPttUIThread();

Err DestroyPttUIThread();

void *PttUIThreadBuffer(void *a);

Err PttUIThreadSetExpectedState(enum PttUIThreadState thread_state);
Err PttUIThreadGetExpectedState(enum PttUIThreadState *thread_state);

Err PttUIThreadSetBufferState(enum PttUIThreadState thread_state);
Err PttUIThreadGetBufferState(enum PttUIThreadState *thread_state);

/**
 * @brief main-thread waiting for buffer-thread
 * @details [USED-IN-MAIN-THREAD]
 *          waiting for buffer-thread
 *
 * @param expected_state expected-state
 * @param n_iter n-iterations
 *
 * @return Err
 */
Err PttUIThreadWaitBufferLoop(enum PttUIThreadState expected_state, int n_iter);

#ifdef __cplusplus
}
#endif

#endif /* PTTUI_THREAD_H */
