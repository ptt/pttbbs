// Copyright (c) 2014, Robert Wang <robert@arctic.tw>
// The MIT License.
#ifndef _THREADPOOL_H_
#   define _THREADPOOL_H_

struct threadpool_ctx;
typedef struct threadpool_ctx *threadpool_t;
typedef void *threadpool_job_t;

threadpool_t threadpool_new(size_t num);
void threadpool_free(threadpool_t p);
void threadpool_do(threadpool_t p, threadpool_job_t job);

typedef void (*threadpool_job_func)(void *ctx);
threadpool_job_t threadpool_job_new(threadpool_job_func func, void *ctx);
void threadpool_job_free(threadpool_job_t job);

#endif
