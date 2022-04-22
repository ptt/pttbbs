extern "C" {
#include "bbs.h"
#include "daemons.h"
}

#include "passwd_notify.hpp"

#include <string>

namespace passwdnotify {

void NotifyUser(const std::string &userid,
                              const std::string &fromhost,
                              const std::string &email) {
  std::string subject;
  subject.append(" " BBSNAME " - ");
  subject.append(userid);
  subject.append("(");
  subject.append(fromhost);
  subject.append("), 您的密碼已更變");
  bsmtp("etc/passwdchanged", subject.c_str(), email.c_str(), "non-exist");
}

} // namespace

void passwd_notify_me() {
  passwdnotify::NotifyUser(std::string(cuser.userid), std::string(fromhost), std::string(cuser.email));
}
