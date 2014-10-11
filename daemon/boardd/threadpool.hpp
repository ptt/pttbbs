// Copyright (c) 2014, Robert Wang <robert@arctic.tw>
// The MIT License.
#ifndef _THREADPOOL_HPP
#   define _THREADPOOL_HPP
#include <thread>
#include "queue.hpp"

namespace {

template <class Job>
void WorkerThread(Queue<Job *> *q)
{
  while (true) {
    Job *job;
    if (!q->Pop(job))
      return;
    job->Run();
    delete job;
  }
}

}  // namespace

class Job
{
public:
  virtual ~Job() {}
  virtual void Run() = 0;
};

template <class Job>
class ThreadPool
{
public:
  ThreadPool(size_t num)
      : queue_(num * 4)
  {
    for (size_t i = 0; i < num; ++i)
      threads_.push_back(std::move(std::thread(WorkerThread<Job>, &queue_)));
  }

  ~ThreadPool()
  {
    queue_.Close();
    for (auto &t : threads_)
      t.join();
  }

  void Do(Job *job)
  {
    queue_.Push(job);
  }

private:
  Queue<Job *> queue_;
  std::vector<std::thread> threads_;
};

#endif
