// Copyright (c) 2014, Robert Wang <robert@arctic.tw>
// The MIT License.
#include "threadpool.hpp"
extern "C" {
#include "threadpool.h"
}

namespace {

class FuncJob : public Job
{
public:
  using Func = void (*)(void *);
  FuncJob(Func f, void *ctx) : f(f), ctx(ctx) {}
  void Run() { f(ctx); }
private:
  Func f;
  void *ctx;
};

}  // namespace

struct threadpool_ctx {
  ThreadPool<FuncJob> *pool;
};

threadpool_t threadpool_new(size_t num)
{
  threadpool_t p = new threadpool_ctx;
  p->pool = new ThreadPool<FuncJob>(num);
  return p;
}

void threadpool_free(threadpool_t p)
{
  delete p->pool;
  delete p;
}

void threadpool_do(threadpool_t p, threadpool_job_t job)
{
  p->pool->Do(reinterpret_cast<FuncJob *>(job));
}

threadpool_job_t threadpool_job_new(threadpool_job_func func, void *ctx)
{
  return reinterpret_cast<threadpool_job_t>(new FuncJob(func, ctx));
}

void threadpool_job_free(threadpool_job_t job)
{
  delete reinterpret_cast<FuncJob *>(job);
}
