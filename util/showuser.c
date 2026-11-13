#define _UTIL_C_
#include "bbs.h"

void
showuser_display(const userec_t * u, int uid)
{
    int             diff = 0;
    char            genbuf[200];

    printf("使用者 %s 的註冊資料 (uid = %d)\n", u->userid, uid);
    printf("\t代號暱稱: %s (%s)\n", u->userid, u->nickname);
    printf("\t真實姓名: %s %s\n", u->realname,
            u->uflag & UF_FOREIGN ? "(外籍)" : "");
    printf("\t職業學歷: %s\n", u->career);
    printf("\t居住地址: %s\n", u->address);

    printf("\t電子信箱: %s\n", u->email);
    printf("\t%6s幣: %d " MONEYNAME "\n", BBSMNAME, u->money);
    printf("\t註冊日期: %s (已滿 %d 天)\n",
	    Cdate(&u->firstlogin),
	    (int)((time(NULL) - u->firstlogin)/DAY_SECONDS));
    printf("\t上次上站: %s (來自 %s)\n",
	    Cdate(&u->lastlogin), u->lasthost);
    if (IS_ENABLED(CONFIG_ANGEL)) {
        printf("\t 小天使 : %s (上次呼叫: %s, 得到天使: %s)\n",
                *u->myangel ? u->myangel : "[無]",
                u->timeplayangel ?  Cdate(&u->timeplayangel) : "[無]",
                u->timesetangel ? Cdate(&u->timesetangel) : "[無]");
    }
    userlevel_str(u->userlevel, genbuf);
    printf("\t帳號權限: %s %#x\n\t%*s", genbuf, u->userlevel, 9, "");
    int iprinted = 0;
    for (diff = 0; diff < 32; diff++)
        if (u->userlevel & (1 << diff)) {
            if (++iprinted % 6 == 0)
                printf("\n\t%*s", 9, "");
            printf(" %s", str_permid[diff]);
        }
    if (iprinted)
        printf("\n");

    int i;
    printf("\t帳號角色(role): %#x", u->role);
    for (i = 0; i < 32; i++)
        if (u->role & (1 << i))
            printf(" %s", str_roleid[i]);
    printf("\n");

    printf("\t認證資料: %s\n", u->justify);
    printf("\t使用記錄: %s %d " STR_LOGINDAYS_QTY " / 文章 %d 篇\n",
            STR_LOGINDAYS, u->numlogindays, u->numposts);

    sethomedir(genbuf, u->userid);
    printf("\t私人信箱: %d 封  (購買信箱: %d 封)\n",
	   get_num_records(genbuf, sizeof(fileheader_t)),
	   u->exmailbox);

    if (IS_ENABLED(CONFIG_ASSESS))
        printf("\t劣文數目: %u\n", (unsigned int)u->badpost);

    user_flags_str(u, genbuf, sizeof(genbuf));
    printf("\n\t其它資訊: %s\n", genbuf);
}

int check(void *data GCC_UNUSED, int n GCC_UNUSED, userec_t *u) {

    if (!u->userid[0])
        return 0;

    printf("%-*s\n", IDLEN, u->userid);
    return 0;
}

int grep_by_name(void *name, int n, userec_t *u) {
    if (!u->userid[0])
	return 0;

    if (strncmp((const char *)name, u->realname, IDLEN) == 0)
	showuser_display(u, n - 1);

    return 0;
}

void
list_uhash(const char *userid)
{
    int             h, p, times;
    h = StringHash(userid)%(1<<HASH_BITS);
    p = SHM->hash_head[h];

    printf("hash = %d\n", h);
    for (times = 0; times < MAX_USERS && p != -1 && p < MAX_USERS ; ++times) {
	if (strcasecmp(SHM->userid[p], userid) == 0) {
	    printf("shm uhash: %d\n", p + 1);
	}
	p = SHM->next_in_hash[p];
    }
}

void
showuser_display2(const userec_t * u, int uid)
{
    printf("passwd userid: %s\n", u->userid);
    printf("shm userid: %s\n", SHM->userid[uid - 1]);
    list_uhash(u->userid);
    showuser_display(u, uid);
}

int main(int argc, char **argv)
{
    if(argc == 2 &&
	    (strncmp("-h", argv[1], 3) == 0 ||
	     strncmp("--help", argv[1], 7) == 0)) {
	fprintf(stderr, "usage: %s userid +uid ...\n"
		"usage: %s --name <name>\n", argv[0], argv[0]);
	return 0;
    }

    chdir(BBSHOME);
    attach_SHM();

    if(passwd_init())
	exit(1);

    if(argc <= 1) {
	passwd_fast_apply(NULL, check);
    } else if (argc == 3 && strcmp("--name", argv[1]) == 0) {
	char *name = strdup(argv[2]);
	passwd_fast_apply(name, grep_by_name);
	free(name);
    } else {
	userec_t record;
	int uid, i;
	for(i = 1; i < argc; i++) {
	    if(argv[i][0] == '+') {
		if(sscanf(&argv[i][1], "%d", &uid) == 1) {
		    if(passwd_query(uid, &record) == 0)
			showuser_display2(&record, uid);
		    else
			fprintf(stderr, "%s: no such uid\n", argv[i]);
		} else
		    fprintf(stderr, "%s: invalid uid format\n", argv[i]);
	    }
	    else if(is_validuserid(argv[i])) {
		if((uid = passwd_load_user(argv[i], &record)) >= 1)
		    showuser_display2(&record, uid);
		else
		    fprintf(stderr, "%s: user not found\n", argv[i]);
	    }
	    else
		fprintf(stderr, "%s: not a valid userid\n", argv[i]);

	    printf("\n");
	}
    }
    return 0;
}
