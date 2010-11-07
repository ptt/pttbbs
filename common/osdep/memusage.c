#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>

#include "osdep.h"

void get_memusage(int buflen, char *buf)
{
    struct rusage ru;
#ifdef __linux__
    int vmdata=0, vmstk=0;
    FILE * fp;
    char file_buf[128];
    if ((fp = fopen("/proc/self/status", "r"))) {
	while (fgets(file_buf, sizeof(file_buf), fp)) {
	    sscanf(file_buf, "VmData: %d", &vmdata);
	    sscanf(file_buf, "VmStk: %d", &vmstk);
	}
	fclose(fp);
    }
#endif

    getrusage(RUSAGE_SELF, &ru);
    snprintf(buf, buflen,
#ifdef __i386__
	    "sbrk: %u KB, "
#endif
#ifdef __linux__
	    "VmData: %d KB, VmStk: %d KB, "
#endif
	    "idrss: %d KB, isrss: %d KB",
#ifdef __i386__
	    ((unsigned int)sbrk(0) - 0x8048000) / 1024,
#endif
#ifdef __linux__
	    vmdata, vmstk,
#endif
	    (int)ru.ru_idrss, (int)ru.ru_isrss);
}

