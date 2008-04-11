#include "bbs.h"
#include "cmsys.h"

// Now() is a maple3 flavor API.
const char *
Now()
{
	syncnow();
	return Cdate(&now);
}

