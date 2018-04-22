#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include "cmbbs.h"
#include "common.h"
#include "var.h"

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

static int *
_set_ptype(int *ptype, int type) {
    if (ptype) {
        *ptype = type;
    }
    return NULL;
}

/**
 * 給定文章標題 title，傳回指到主題的部分的指標。
 * @param title
 */
const char *
subject_ex(const char *title, int *ptype)
{
    do {
        if (str_case_starts_with(title, str_reply)) {
            title += strlen(str_reply);
            ptype = _set_ptype(ptype, SUBJECT_REPLY);
        } else if (str_case_starts_with(title, str_forward)) {
            title += strlen(str_forward);
            ptype = _set_ptype(ptype, SUBJECT_FORWARD);
#ifdef USE_LEGACY_FORWARD
        } else if (str_starts_with(title, str_legacy_forward)) {
            title += strlen(str_legacy_forward);
            ptype = _set_ptype(ptype, SUBJECT_FORWARD);
#endif
        } else {
            ptype = _set_ptype(ptype, SUBJECT_NORMAL);
            break;
        }
        if (*title == ' ')
            title ++;
    } while (1);
    return title;
}

const char *
subject(const char *title) {
    return subject_ex(title, NULL);
}
