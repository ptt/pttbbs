#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
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

bool
is_valid_brdname(const char *brdname)
{
    int i;
    char ch;
    int len;

    assert(brdname);

    len = strlen(brdname);
    if (len < 2 || len > IDLEN)
	return false;

    for (i = 0; i < len; i++) {
	char ch = brdname[i];
	if (i == 0) {
	    if (!isalpha((int)ch))
		return false;
	} else {
	    if (!isalnum(ch) && ch != '_' && ch != '-' && ch != '.')
		return false;
	}
    }
    return true;
}

