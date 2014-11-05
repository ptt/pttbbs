#include "bbs.h"

#define DEFINEROLE(s) {s, #s}

typedef struct {
    int role;
    const char *name;
} rolerec_t;

rolerec_t known_roles[] = {
    DEFINEROLE(ROLE_ANGEL_CIA),
    DEFINEROLE(ROLE_ANGEL_ACTIVITY),
    DEFINEROLE(ROLE_ANGEL_ARCHANGEL),
    DEFINEROLE(ROLE_POLICE_ANONYMOUS),
    {0}
};

void list_roles(struct userec_t *pusr) {
    int i;

    printf("%s's roles: %#08x", pusr->userid, pusr->role);
    for (i = 0; known_roles[i].role; i++) {
        if (pusr->role & known_roles[i].role)
            printf(" %s", known_roles[i].name);
    }
    printf("\n");
}

void toggle_role(struct userec_t *pusr, const char *role) {
    int i;
    for (i = 0; known_roles[i].role; i++) {
        if (strcasecmp(role, known_roles[i].name) == 0)
            break;
    }
    if (!known_roles[i].role) {
        fprintf(stderr, "Unknown role name: %s\n", role);
        exit(1);
    }
    pusr->role ^= known_roles[i].role;
}

int main(int argc, char *argv[]) {
    int unum;
    userec_t usr;
    const char *userid;

    if (argc < 2) {
        fprintf(stderr, "usage: %s userid [role-list-to-toggle]\n",
                argv[0]);
        return 1;
    }
    userid = argv[1];
    argc--, argv++;

    chdir(BBSHOME);
    attach_SHM();

    unum = passwd_load_user(userid, &usr);
    if (unum < 1) {
        fprintf(stderr, "invalid user: %s\n", userid);
        return 1;
    }

    if (argc > 1) {
        while (argc-- > 1)
            toggle_role(&usr, *++argv);
        passwd_update(unum, &usr);
    } else {
        list_roles(&usr);
    }
    return 0;
}
