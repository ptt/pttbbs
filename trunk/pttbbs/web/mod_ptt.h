#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_log.h"
#include "http_main.h"
#include "http_protocol.h"
#include "util_script.h"
#include "config.h"
#include "pttstruct.h"
#include "modes.h"
#include "common.h"
#include "proto.h"

#include <stdio.h>


#define PATHLEN 512
