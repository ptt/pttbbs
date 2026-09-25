#include "cmbbs.h"

SHM_t *SHM = NULL;
boardheader_t *bcache = NULL;
time4_t now = 0;
time4_t login_start_time = 0;
char * const str_reply = "Re:";
char * const str_forward = "Fw:";
