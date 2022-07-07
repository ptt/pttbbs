#include "testmocksysrandom.h"

#ifdef BAZELTEST

ssize_t mock_getrandom(void *buf, size_t buflen, unsigned int flags __attribute__((unused))) {
  static unsigned char count = 0;
  unsigned char *buf_uc = (unsigned char *)buf;

  for(size_t i = 0; i < buflen; i++) {
    buf_uc[i] = count;
    count++;
  }

  return buflen;
}

#endif //BAZELTEST
