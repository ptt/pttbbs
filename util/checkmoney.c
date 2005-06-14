/* $Id$ */
#define _UTIL_C_
#include "bbs.h"


int main(int argc, char*argv[]) {
    userec_t user;
    char userhomepwd[256];
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
	if(!userid[0] || money==-1 ) continue;
        if(get_record(argv[1], &user, sizeof(user), i) == -1)continue;
	if(money-user.money > 1000000 ||( money<100 && user.money>2000)) 
	{    
	  sprintf(userhomepwd, BBSHOME "/%c/%s/.passwd", user.userid[0],
		    user.userid);
          printf("%s %d ", userid, money); 
	  money= user.money;
	  SHM->money[i-1]  = money; 
	  if(get_record(userhomepwd, &user, sizeof(user), 1) == -1)
	       get_record(BBSHOME"/.PASSWD",  &user, sizeof(user), i);     
	  user.money = money;
          printf("become %d\n",  money); 
	  substitute_record(BBSHOME"/.PASSWD",  &user, sizeof(user), i);
	  substitute_record(userhomepwd,  &user, sizeof(user), 1);
	  }
    }

    return 0;
}
