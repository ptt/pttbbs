#ifndef _PTTBBS_DAEMON_BOARDD_STRINGS_HPP_
#define _PTTBBS_DAEMON_BOARDD_STRINGS_HPP_
#include <string>

namespace strings {

std::string b2u(const std::string &big5);
std::string b2u(const char *big5);
void b2u(std::string *utf8, const char *big5);

}  // namespace strings

#endif
