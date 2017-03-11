#include "daemon/boardd/evbuffer.hpp"
extern "C" {
#include <event2/buffer.h>
}

Evbuffer::Evbuffer() : buf_(evbuffer_new()) {}

Evbuffer::~Evbuffer() { evbuffer_free(buf_); }

bool Evbuffer::ConvertUTF8() {
  buf_ = evbuffer_b2u(buf_);
  if (buf_ != nullptr)
    return true;
  buf_ = evbuffer_new();
  return false;
}
