#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include "cmbbs.h"
#include "common.h"
#include "var.h"

// Board names are letters, digits, '_', '-' and '.'. New names (not
// existing) must also be BRDNAME_MIN_NEW long or more, begin with a letter
// and have no '.', as Lemmy's community names (3 or more of [a-zA-Z0-9_]);
// '-' stays, as a quarter of the boards have it.
#define BRDNAME_MIN_NEW 3

static bool
check_brdname(const char *brdname, bool existing)
{
    int i;
    int len;

    assert(brdname);

    len = strlen(brdname);
    if (len < (existing ? 2 : BRDNAME_MIN_NEW) || len > IDLEN)
	return false;

    for (i = 0; i < len; i++) {
	unsigned char ch = (unsigned char)brdname[i];
	if (i == 0) {
	    if (!isalpha(ch) && !(existing && isdigit(ch)))
		return false;
	} else {
	    if (!isalnum(ch) && ch != '_' && ch != '-' &&
		!(existing && ch == '.'))
		return false;
	}
    }
    return true;
}

// Whether brdname may name a new board (or the new name of a board):
// 3 to IDLEN letters, digits, '_' or '-', a letter first.
bool
is_valid_brdname(const char *brdname)
{
    return check_brdname(brdname, false);
}

// Whether brdname may be the name of a board that exists: as
// is_valid_brdname(), but older boards may begin with a digit (e.g.
// 8words, 2nd_NTUCCC), have a '.' (S.H.E) or only 2 letters (F4), which
// the rules of new names no longer allow. Still a single safe path
// component (never '.' first, nor '/').
bool
is_valid_existing_brdname(const char *brdname)
{
    return check_brdname(brdname, true);
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
    if (!title)
        return "";
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
