#include "bbs.h"

#define PERMCHECK(s) {s, #s}

static int get_offset(int mask) {
    int i;
    for (i = 0; mask; i++, mask >>= 1)
        if (mask & 0x1)
            return i;
    assert(mask);
    return -1;
}

typedef struct {
    uint32_t mask;
    char *desc, *list;
    const char *caption;
    int count;
} check_item;

int main(void) {
    int     fd, i;
    userec_t usr;
    check_item checks[] = {
        PERMCHECK(PERM_BBSADM),
        PERMCHECK(PERM_SYSOP),
        PERMCHECK(PERM_ACCOUNTS),
        PERMCHECK(PERM_CHATROOM),
        PERMCHECK(PERM_BOARD),
        PERMCHECK(PERM_PRG),
        PERMCHECK(PERM_VIEWSYSOP),
        PERMCHECK(PERM_POLICE_MAN),
        PERMCHECK(PERM_SYSSUPERSUBOP),
        PERMCHECK(PERM_ACCTREG),
#if 0
        PERMCHECK(PERM_SYSSUBOP),
        PERMCHECK(PERM_ACTION),
        PERMCHECK(PERM_PAINT),
#endif
#ifdef PLAY_ANGEL
        PERMCHECK(ROLE_ANGEL_CIA),
        PERMCHECK(ROLE_ANGEL_ACTIVITY),
        PERMCHECK(ROLE_ANGEL_ARCHANGEL),
#endif
        PERMCHECK(ROLE_POLICE_ANONYMOUS),
        {0, NULL},
    };

    chdir(BBSHOME);
    attach_SHM();

    if ((fd = open(".PASSWDS", O_RDONLY)) < 0) {
	perror(".PASSWDS");
	return 1;
    }

    // new version
    while (read(fd, &usr, sizeof(usr)) == sizeof(usr)) {
        if (!*usr.userid)
            continue;
        if (!usr.userlevel && !usr.role)
            continue;
        for (i = 0; checks[i].mask; i++) {
            int mask = checks[i].mask;
            uint32_t *pvalue = NULL;
            const char *desc = checks[i].desc;
            char *list = checks[i].list;
            size_t need_len = sizeof(usr.userid) + sizeof(usr.realname) + 4;

            if (strncmp(desc, "PERM_", 4) == 0) {
                if (!checks[i].caption)
                    checks[i].caption = str_permid[get_offset(mask)];
                pvalue = &usr.userlevel;
            } else if (strncmp(desc, "ROLE_", 5) == 0) {
                if (!checks[i].caption)
                    checks[i].caption = str_roleid[get_offset(mask)];
                pvalue = &usr.role;
            } else
                assert(pvalue);
            
            if (!(*pvalue & mask))
                continue;

            // append to list
            checks[i].list = (char *)realloc(
                list, (list ? strlen(list) : 0) + need_len);
            assert(checks[i].list);
            if (!list)
                checks[i].list[0] = 0;
            checks[i].count++;
            list = checks[i].list;
            list += strlen(list);
            sprintf(list, " %-*s %s\n", IDLEN, usr.userid,
                    usr.realname);
        }
    }

    // Now, iterate through all permissions and print out list.
    for (i = 0; checks[i].mask; i++) {
        printf("%s %s\n%stotal %d users\n\n",
               checks[i].desc, checks[i].caption,
               checks[i].list ? checks[i].list : "",
               checks[i].count);
    }
    close(fd);
    return 0;
}
