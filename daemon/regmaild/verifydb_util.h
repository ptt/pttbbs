#ifndef _DAEMON_REGMAILD_VERIFYDB_UTIL_H_
#define _DAEMON_REGMAILD_VERIFYDB_UTIL_H_

#include "flatbuffers/flatbuffers.h"

inline const char *CStrOrEmpty(const flatbuffers::String *s) {
  return s ? s->c_str() : "";
}

inline const char *StrOrEmpty(const char *s) { return s ? s : ""; }

#endif
