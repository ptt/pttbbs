// Copyright (c) 2015, Robert Wang <robert@arctic.tw>
// The MIT License.
#ifndef _BOARDD_SESSION_HPP
#   define _BOARDD_SESSION_HPP
#include <mutex>
extern "C" {
#include <event2/bufferevent.h>
}

typedef int (*LineFunc)(struct evbuffer *output, void *ctx, char *line);

template <class T>
class RefCounted {
 public:
  virtual ~RefCounted() {}
  void add_ref() {
      std::lock_guard<std::mutex> _(mutex_);
      ++count_;
  }
  void dec_ref() {
      std::lock_guard<std::mutex> _(mutex_);
      if (--count_ == 0)
	  delete this;
  }
 private:
  std::mutex mutex_;
  int count_ = 0;
};

class Session : public RefCounted<Session> {
 public:
  Session(LineFunc process_line,
	  struct event_base *base, evutil_socket_t fd,
	  struct sockaddr *address, int socklen);
  virtual ~Session();

  void on_error(short what);
  void on_read();
  void shutdown();

  void process_line(void *ctx, char *line);

 private:
  LineFunc process_line_;
  bufferevent *bev_;
  std::mutex mutex_;

  void send_and_resume(evbuffer *buf);
};

#endif
