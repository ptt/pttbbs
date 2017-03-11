#ifndef _PTTBBS_DAEMON_BOARDD_MACROS_HPP_
#define _PTTBBS_DAEMON_BOARDD_MACROS_HPP_

#define DISABLE_COPY_AND_ASSIGN(Class) \
  Class(const Class &) = delete; \
  Class &operator=(const Class &) = delete

#define RETURN_ON_FAIL(Expr) \
  do { \
    Status s = (Expr); \
    if (!s.ok()) \
      return s; \
  } while (0)

#endif
