extern "C" {
#include "bbs.h"
}

#include <string>
#include "user_handle.hpp"

namespace user_handle {

// InitUserHandle
//
// Initializing user-handle from userec.
//
// Params:
//   u: userec
//   user: UserHandle
//
// Return:
//   bool: true: ok false: err
bool InitUserHandle(const userec_t *u, UserHandle &user) {
  if (u == NULL || !is_validuserid(u->userid)) {
    return false;
  }

  user.userid = u->userid;
  user.generation = u->firstlogin;

  return true;
}

} // namespace user_handle
