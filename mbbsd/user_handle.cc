extern "C" {
  #include "bbs.h"
}

#include <string>
#include "user_handle.hpp"

namespace userhandle {

// InitUserHandle
// Initialize UserHandle by setting userid as user.userid and generation as user.firstlogin
//
// Return:
//     bool: true: success (user is not NULL and userid is valid) / false: fail.
//     out_user_handle: initialized user handle.
bool InitUserHandle(const userec_t *user, UserHandle &out_user_handle) {
  if(user == NULL || !is_validuserid(user->userid)) {
    return false;
  }

  out_user_handle.userid = user->userid;
  out_user_handle.generation = user->firstlogin;
  out_user_handle.email = user->email;

  return true;
}

} //namespace
