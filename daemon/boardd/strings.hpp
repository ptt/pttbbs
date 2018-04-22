#ifndef _PTTBBS_DAEMON_BOARDD_STRINGS_HPP_
#define _PTTBBS_DAEMON_BOARDD_STRINGS_HPP_
#include <string>

namespace strings {

std::string b2u(const std::string &big5);
std::string b2u(const char *big5);
void b2u(std::string *utf8, const char *big5);

std::string u2b(const std::string &utf8);
std::string u2b(const char *utf8);
void u2b(std::string *big5, const char *utf8);

}  // namespace strings

#endif
