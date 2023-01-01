#include "daemon/boardd/evbuffer.hpp"
#include <string_view>
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

std::string_view Evbuffer::StringView() {
  size_t len = evbuffer_get_length(buf_);
  return {reinterpret_cast<const char *>(evbuffer_pullup(buf_, len)), len};
}
