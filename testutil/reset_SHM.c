#include "bbs.h"
#include "var.h"
#include "testutil.h"

void reset_SHM() {
  bzero(SHM, sizeof(SHM_t));
}
