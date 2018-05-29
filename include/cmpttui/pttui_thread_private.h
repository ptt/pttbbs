/* $Id$ */
#ifndef PTTUI_THREAD_PRIVATE_H
#define PTTUI_THREAD_PRIVATE_H

#include "pttconst.h"
#include "cmpttui/pttui_thread_lock.h"
#include "cmpttui/pttui_thread.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <pthread.h>

#define NS_DEFAULT_SLEEP_THREAD 20000000  // 20 ms

#define NS_DEFAULT_SLEEP_PTTUI_THREAD_WAIT_BUFFER_LOOP 100000000 // 100 ms

#define SEC_DEFAULT_WAIT_JOIN_THREAD 10 // 10 secs

extern enum PttUIThreadState _PTTUI_THREAD_EXPECTED_STATE;
extern enum PttUIThreadState _PTTUI_THREAD_BUFFER_STATE;

extern pthread_t _PTTUI_THREAD_BUFFER;

#ifdef __cplusplus
}
#endif

#endif /* PTTUI_THREAD_PRIVATE_H */
