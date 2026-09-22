#include "daemon/boardd/strings.hpp"
extern "C" {
#include "cmbbs.h"
}

namespace strings {

std::string b2u(const std::string &big5) {
  return b2u(big5.c_str());
}

std::string b2u(const char *big5) {
  std::string utf8;
  b2u(&utf8, big5);
  return utf8;
}

void b2u(std::string *utf8, const char *big5) {
  utf8_ctx ctx;
  const uint8_t *p = reinterpret_cast<const uint8_t *>(big5);
  while (*p) {
    if (isascii(*p))
      utf8->push_back(*p);
    else if (!p[1]) {
      utf8->push_back('?');
      break;
    } else {
      int len = utf8_from_ucs(
          &ctx, b2u_table[static_cast<uint16_t>(p[0]) << 8 | p[1]]);
      utf8->append(reinterpret_cast<const char *>(ctx.buf), len);
      p++;
    }
    p++;
  }
}

std::string u2b(const std::string &utf8) {
  return u2b(utf8.c_str());
}

std::string u2b(const char *utf8) {
  std::string big5;
  u2b(&big5, utf8);
  return big5;
}

void u2b(std::string *big5, const char *utf8) {
  utf8_ctx ctx;
  utf8_init(&ctx);
  const uint8_t *p = reinterpret_cast<const uint8_t *>(utf8);
  while (*p) {
    if (!utf8_add_byte(&ctx, *p++)) {
      if (!utf8_pending(&ctx))
        big5->push_back('?');
      continue;
    }
    int ucs = utf8_get_ucs(&ctx);
    uint16_t b5 = (ucs >= 0 && ucs < 0x10000) ? u2b_table[ucs] : 0;
    if (b5 == 0)
      b5 = '?';
    if (b5 >> 8)
      big5->push_back(b5 >> 8);
    big5->push_back(b5 & 0xFF);
  }
}

}  // namespace strings
