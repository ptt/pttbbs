#ifndef _PTTBBS_DAEMON_BOARDD_EVBUFFER_HPP_
#define _PTTBBS_DAEMON_BOARDD_EVBUFFER_HPP_
#include "daemon/boardd/macros.hpp"
extern "C" {
#include <event2/buffer.h>
#include "daemon/boardd/boardd.h"
}

class Evbuffer final {
 public:
  Evbuffer();
  ~Evbuffer();

  evbuffer *Get() { return buf_; }
  bool ConvertUTF8();

 private:
  DISABLE_COPY_AND_ASSIGN(Evbuffer);
  evbuffer *buf_;
};

#endif
