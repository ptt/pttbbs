#define _UTIL_C_
#include "bbs.h"
#include <time.h>

// old uflag2 bits
#ifndef OUF2_FAVNOHILIGHT
#define OUF2_FAVNOHILIGHT    0x00000010  /* false if hilight favorite */
#define OUF2_FAVNEW_FLAG     0x00000020  /* true if add new board into one's fav */
#define OUF2_FOREIGN         0x00000100  /* true if a foreign */
#define OUF2_LIVERIGHT       0x00000200  /* true if get "liveright" already */
#define OUF2_REJ_OUTTAMAIL   0x00000400  /* true if don't accept outside mails */
#endif

const unsigned int ufmap_from[] = {
    OUF2_FAVNOHILIGHT,
    OUF2_FAVNEW_FLAG,
    OUF2_FOREIGN,
    OUF2_LIVERIGHT,
    OUF2_REJ_OUTTAMAIL,
    0,
};

const unsigned int ufmap_to[] = {
    UF_FAV_NOHILIGHT,
    UF_FAV_ADDNEW,
    UF_FOREIGN,
    UF_LIVERIGHT,
    UF_REJ_OUTTAMAIL,
    0,
};

const char *ufmap_names [] = {
    "FAV_NOHILIGHT",
    "FAV_ADDNEW",
    "FOREIGN",
    "LIVERIGHT",
    "REJ_OUTTAMAIL",
};

int transform(userec_t *new, userec_t *old, int i)
{
    userec_t *u = new;
    int dirty = 0;

    memcpy(new, old, sizeof(userec_t));
    if (!u->userid[0])
	return 0;

    for (i = 0; ufmap_from[i]; i++)
    {
	int ov = (u->deprecated_uflag2 & ufmap_from[i]) ? 1 : 0,
	    nv = (u->uflag & ufmap_to[i]) ? 1 : 0;
	if (ov == nv)
	    continue;

	if (dirty++ == 0) printf("%s: %08X => [", u->userid, u->uflag);
	u->uflag &= ~(ufmap_to[i]);
	if (u->deprecated_uflag2 & ufmap_from[i])
	{
	    u->uflag |= ufmap_to[i];
	    printf("%s ", ufmap_names[i]);
	}
    }
    if (!dirty)
	return 0;

    printf("] => %08X\n", u->uflag);
    // force convert!
    // passwd_update(n+1, u);

    return 0;
}

int main(void)
{
    int fd, fdw;
    userec_t new;
    userec_t old;
    int i = 0;

    printf("sizeof(userec_t)=%u\n", (unsigned int)sizeof(userec_t));
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

    // printf("total %d records converted.\n", accs);
    return 0;
}
