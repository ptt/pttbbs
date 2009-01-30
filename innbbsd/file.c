#include <stdio.h>
#include <stdarg.h>

/*
 * The same as sprintf, but return the new string
 * fileglue("%s/%s",home,".newsrc");
 */
char           *
fileglue(char *fmt,...)
{
    va_list         ap;
    static char     gluebuffer[8192];
    va_start(ap, fmt);
    vsprintf(gluebuffer, fmt, ap);
    va_end(ap);
    return gluebuffer;
}
