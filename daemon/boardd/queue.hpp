// Copyright (c) 2014, Robert Wang <robert@arctic.tw>
// The MIT License.
#ifndef _QUEUE_H_
#define _QUEUE_H_
#include <queue>
#include <mutex>
#include <condition_variable>
#include <stdexcept>  //runtime_error
#include <utility>  //forward, move

template <class T>
class Queue
{
public:
  Queue() noexcept
    :Queue{0}{}
  Queue(size_t max) noexcept
    : eof_(false)
    , max_(max)
  {}
  
  template<class T2>
  void Push(T2 &&elem)
  {
    std::unique_lock<std::mutex> lk(mu_);
    while (true) {
      if (eof_)
        throw std::runtime_error("Queue closed");
      if (max_ && qu_.size() >= max_) {
        cv_poped_.wait(lk);
        continue;
      }
      qu_.emplace(std::forward<T2>(elem));
      cv_pushed_.notify_one();
      return;
    }
  }

  bool Pop(T &elem)
  {
    std::unique_lock<std::mutex> lk(mu_);
    while (qu_.empty()) {
      if (eof_)
        return false;
      cv_pushed_.wait(lk);
    }
    elem = std::move(qu_.front());
    qu_.pop();
    cv_poped_.notify_one();
    return true;
  }

  void Close()
  {
    std::unique_lock<std::mutex> lk(mu_);
    eof_ = true;
    cv_pushed_.notify_all();
    cv_poped_.notify_all();
  }

private:
  std::queue<T> qu_;
  std::mutex mu_;
  std::condition_variable cv_pushed_;
  std::condition_variable cv_poped_;
  bool eof_;
  size_t max_;
};

#endif
