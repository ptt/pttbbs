/* $Id: checkmoney.c 1645 2004-03-31 01:24:38Z ptt $ */
#define _UTIL_C_
#include "bbs.h"


int main(int argc, char*argv[]) {
    userec_t user;
    char *userid;
    int i, money;

    if(argc<2) 
       printf("user %s <PASSWD file to compare>", argv[0]);
    attach_SHM();
   
    if(passwd_init())
	exit(1);
    for(i = 1; i <= MAX_USERS; i++) {
        userid=SHM->userid[i-1]; 
        money =SHM->money[i-1];
	if(!userid[0]) continue;
        if(get_record(argv[1], &user, sizeof(user), i) == -1)continue;
        printf("%s %d %s %d\n", userid, money, user.userid, user.money); 
      
    }

    return 0;
}
