#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cmbbs.h"

void obfuscate_ipstr(char *s)
{
    s = strrchr(s, '.');
    if (!s) return;
    if (!*++s) return;
    // s points to a.'b'
    *s++ = '*';
    *s = 0;
}
