#include "cmbbs.h"
#include "var.h"

void syncnow(void);

// Now() is a maple3 flavor API.
const char *
Now()
{
	syncnow();
	return Cdate(&now);
}

