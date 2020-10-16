extern "C" {
#include "bbs.h"
}

#ifdef USE_VERIFYDB_ACCOUNT_RECOVERY

# ifndef USE_VERIFYDB
#   error "USE_VERIFYDB_ACCOUNT_RECOVERY requires USE_VERIFYDB"
# endif

void recover_account() {
  vs_hdr("¨ú¦^±b¸¹");
  pressanykey();
  exit(0);
}

#endif
