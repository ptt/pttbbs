/* $Id$ */
/* 把 SHM 中的 money 全部寫回 .PASSWDS */
#define _UTIL_C_
#include "bbs.h"

time4_t now;
extern SHM_t   *SHM;

int main()
{
    int num, pwdfd, money;
    userec_t u;

    attach_SHM();

    if ((pwdfd = open(fn_passwd, O_WRONLY)) < 0)
	exit(1);
    for (num=1;num <= SHM->number;num++) {
	lseek(pwdfd, sizeof(userec_t) * (num - 1) +
		((char *)&u.money - (char *)&u), SEEK_SET);
	money = moneyof(num);
	write(pwdfd, &money, sizeof(int));
    }
    close(pwdfd);
	
    return 0;
}
