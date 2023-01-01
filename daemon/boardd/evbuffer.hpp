#ifndef _PTTBBS_DAEMON_BOARDD_EVBUFFER_HPP_
#define _PTTBBS_DAEMON_BOARDD_EVBUFFER_HPP_
#include "daemon/boardd/macros.hpp"
#include <string_view>
extern "C" {
#include <event2/buffer.h>
#include "daemon/boardd/boardd.h"
}

class Evbuffer final {
 public:
  Evbuffer();
  ~Evbuffer();

  evbuffer *GetBuffer() { return buf_; }
  bool ConvertUTF8();
  std::string_view StringView();

 private:
  DISABLE_COPY_AND_ASSIGN(Evbuffer);
  evbuffer *buf_;
};

#endif
