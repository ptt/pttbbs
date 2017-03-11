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
  const uint8_t *p = reinterpret_cast<const uint8_t *>(big5);
  while (*p) {
    if (isascii(*p))
      utf8->push_back(*p);
    else if (!p[1]) {
      utf8->push_back('?');
      break;
    } else {
      uint8_t cs[4];
      utf8->append(
          reinterpret_cast<const char *>(cs),
          ucs2utf(b2u_table[static_cast<uint16_t>(p[0]) << 8 | p[1]], cs));
      p++;
    }
    p++;
  }
}

}  // namespace strings
