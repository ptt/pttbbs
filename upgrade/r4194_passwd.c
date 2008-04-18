#include "bbs.h"

int trans = 0;
int partial = 0;
int justified = 0;
int regformed = 0;

char * const str_space = " \t\n\r";


////////////////////////////////////////////////////////////////////////////
// Regform Utilities
////////////////////////////////////////////////////////////////////////////
#define FN_REGFORM "regform"
#define FN_REQLIST "reg.wait"

// TODO define and use structure instead, even in reg request file.
typedef struct {
    char userid[IDLEN+1];
    char name  [20];
    char career[40];
    char addr  [50];
    char phone [20];
} RegformEntry;

// regform format utilities
int
load_regform_entry(RegformEntry *pre, FILE *fp)
{
    char buf[STRLEN];
    char *v;

    memset(pre, 0, sizeof(RegformEntry));
    while (fgets(buf, sizeof(buf), fp))
    {
        if (buf[0] == '-')
            break;
        buf[sizeof(buf)-1] = 0;
        v = strchr(buf, ':');
        if (v == NULL)
            continue;
        *v++ = 0;
        if (*v == ' ') v++;
        chomp(v);

        if (strcmp(buf, "uid") == 0)
            strlcpy(pre->userid, v, sizeof(pre->userid));
        else if (strcmp(buf, "name") == 0)
            strlcpy(pre->name, v, sizeof(pre->name));
        else if (strcmp(buf, "career") == 0)
            strlcpy(pre->career, v, sizeof(pre->career));
        else if (strcmp(buf, "addr") == 0)
            strlcpy(pre->addr, v, sizeof(pre->addr));
        else if (strcmp(buf, "phone") == 0)
            strlcpy(pre->phone, v, sizeof(pre->phone));
    }
    return pre->userid[0] ? 1 : 0;
}

int
regfrm_load(const char *userid, RegformEntry *pre)
{
    FILE *fp = NULL;
    char fn[PATHLEN];
    int ret = 0;
    sethomefile(fn, userid, FN_REGFORM);
    if (!dashf(fn))
        return 0;

    fp = fopen(fn, "rt");
    if (!fp)
        return 0;
    ret = load_regform_entry(pre, fp);
    fclose(fp);
    return ret;
}

// working queue
FILE *
regq_init_pull()
{
    FILE *fp = tmpfile(), *src =NULL;
    char buf[STRLEN];
    if (!fp) return NULL;
    src = fopen(FN_REQLIST, "rt");
    if (!src) { fclose(fp); return NULL; }
    while (fgets(buf, sizeof(buf), src))
	fputs(buf, fp);
    fclose(src);
    rewind(fp);
    return fp;
}

int 
regq_pull(FILE *fp, char *uid)
{
    char buf[STRLEN];
    size_t idlen = 0;
    uid[0] = 0;
    if (fgets(buf, sizeof(buf), fp) == NULL)
	return 0;
    idlen = strcspn(buf, str_space);
    if (idlen < 1) return 0;
    if (idlen > IDLEN) idlen = IDLEN;
    strlcpy(uid, buf, idlen+1);
    return 1;
}

int
regq_end_pull(FILE *fp)
{
    // no need to unlink because fp is a tmpfile.
    if (!fp) return 0;
    fclose(fp);
    return 1;
}

void transform(userec_t *new, userec_t *old, int i)
{
    char fn[1024];
    char *s, *s2;
    memcpy(new, old, sizeof(userec_t));

    // already converted?
    if (old->version == PASSWD_VERSION || !old->userid[0])
	return;

    if (old->version != 2275 && old->userid[0])
    {
	fprintf(stderr, "invalid record at %d: %s\n", i, old->userid);
	if (i == 1)
	    memset(new, 0, sizeof(userec_t));
	return;
    }

    // update version info
    new->version = PASSWD_VERSION;
    // zero out
    memset(new->career,  0, sizeof(new->career));
    memset(new->phone,   0, sizeof(new->phone));
    memset(new->chkpad0, 0, sizeof(new->chkpad0));
    memset(new->chkpad1, 0, sizeof(new->chkpad1));
    memset(new->chkpad2, 0, sizeof(new->chkpad2));

    // quick skip
    if (!old->justify[0] || !old->userid[0])
	return;

    printf("%s ", old->userid);
    printf("%s -> ", old->justify);

    s = old->justify;

    // process only if justify is truncated.
    while (strlen(old->justify) == sizeof(old->justify) -1)
    {
	RegformEntry re;

	// try regform first.
	if (regfrm_load(old->userid, &re)) {
	    // trust the form.
	    strlcpy(new->career, re.career, sizeof(new->career));
	    strlcpy(new->phone,  re.phone,  sizeof(new->phone));
	    // justify will be updated someday... throw it
	    printf("<regq> [%s][%s][%s]\n", new->career, new->phone, new->justify);
	    regformed ++;
	    trans++;
	    return;
	}

	// try to build from ~/justify, if exists.
	sethomefile(fn, old->userid, "justify");
	if (dashf(fn)) {
	    FILE *fp = fopen(fn, "rt");
	    fn[0] = 0;
	    fgets(fn, sizeof(fn), fp);
	    fclose(fp);
	    chomp(fn);

	    if (strlen(fn) > strlen(s))
	    {
		char *xs;
		int quotes = 3;
		s = fn;
		// some justifies have multiple entries without \n...
		xs = s + strlen(s);
		while (quotes > 0 && xs > s)
		    if (*--xs == ':')
			quotes--;
		if (xs > s)
		    s = xs+1;

		justified ++;
	    }
	}

	break;
    }

    // build career/phone
    s2 = strchr(s, ':');
    if (s2) {
	*s2++ = 0;
	while (*s && !isdigit(*s) && *s != '(') s++;
	strlcpy(new->phone,   s,  sizeof(new->phone));
	strlcpy(new->justify, s2, sizeof(new->justify));
	s = s2;
	s2 = strchr(s, ':');
	if (s2)
	{
	    *s2++ = 0;
	}
	else if (new->userlevel & PERM_LOGINOK)
	{
	    // skip non-loginOK users
	    partial ++;
	    printf(" <partial> ");
	}
	strlcpy(new->career, s, sizeof(new->career));
	if (s2)
	    strlcpy(new->justify, s2, sizeof(new->justify));
	else
	    strlcpy(new->justify, "", sizeof(new->justify));
    }
    printf("[%s][%s][%s]\n", new->career, new->phone, new->justify);

    trans++;
}

int main(void)
{
    int fd, fdw;
    userec_t new;
    userec_t old;
    int i = 0;

    printf("sizeof(userec_t)=%lu\n", sizeof(userec_t));
    printf("You're going to convert your .PASSWDS\n");
    printf("The new file will be named .PASSWDS.trans.tmp\n");

    if (chdir(BBSHOME) < 0) {
	perror("chdir");
	exit(-1);
    }

    if ((fd = open(FN_PASSWD, O_RDONLY)) < 0 ||
	 (fdw = open(FN_PASSWD".trans.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0 ) {
	perror("open");
	exit(-1);
    }

    while (read(fd, &old, sizeof(old)) > 0) {
	transform(&new, &old, ++i);
	write(fdw, &new, sizeof(new));
    }

    close(fd);
    close(fdw);

    // TODO use FN_REQLIST to process other forms
    printf("total %d records translated (%d partial, %d justified, %d regformed).\n", 
	    trans, partial, justified, regformed);
    return 0;
}
