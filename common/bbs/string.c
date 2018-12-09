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

int
expand_esc_star(char *buf, const char *src, int szbuf)
{
    assert(*src == KEY_ESC && *(src+1) == '*');
    src += 2;
    switch(*src)
    {
        //
        // secure content
        //
        case 't':   // current time
            strlcpy(buf, Cdate(&now), szbuf);
            return 1;
        case 'u':   // current online users
            snprintf(buf, szbuf, "%d", SHM->UTMPnumber);
            return 1;
        //
        // insecure content
        //
        case 's':   // current user id
            strlcpy(buf, cuser.userid, szbuf);
            return 2;
        case 'n':   // current user nickname
            strlcpy(buf, cuser.nickname, szbuf);
            return 2;
    }

    // unknown characters, return from star.
    strlcpy(buf, src-1, szbuf);
    return 0;
}

void
strip_ansi_movecmd(char *s) {
    const char *pattern_movecmd = "ABCDfjHJRu";
    const char *pattern_ansi_code = "0123456789;,[";

    while (*s) {
        char *esc = strchr(s, ESC_CHR);
        if (!esc)
            return;
        s = ++esc;
        while (*esc && strchr(pattern_ansi_code, *esc))
            esc++;
        if (strchr(pattern_movecmd, *esc)) {
            *esc = 's';
        }
    }
}

void
strip_esc_star(char *s) {
    int len;
    while (*s) {
        char *esc = strstr(s, ESC_STR "*");
        if (!esc)
            return;
        len = strlen(esc);
        // 3: ESC * [a-z]
        if (len < 3) {
            *esc = 0;
            return;
        }
        if (isalpha(esc[2])) {
            memmove(esc, esc + 3, len - 3 + 1);
        } else {
            // Invalid esc_star. Remove esc instead.
            memmove(esc, esc + 1, len - 1 + 1);
        }
    }
}

char *
Ptt_prints(char *str, size_t size, int mode)
{
    char           *strbuf = alloca(size);
    int             r, w;
    for( r = w = 0 ; str[r] != 0 && w < ((int)size - 1) ; ++r )
    {
        if( str[r] != ESC_CHR )
        {
            strbuf[w++] = str[r];
            continue;
        }

        if( str[++r] != '*' ){
            if (w+2 >= (int)size-1) break;
            strbuf[w++] = ESC_CHR;
            strbuf[w++] = str[r];
            continue;
        }

        /* Note, w will increased by copied length after */
        expand_esc_star(strbuf+w, &(str[r-1]), size-w);
        r ++;
        w += strlen(strbuf+w);
    }
    strbuf[w] = 0;
    strip_ansi(str, strbuf, mode);
    return str;
}

