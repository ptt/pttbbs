/* 
 * 若您使用的是 Current Ptt，則不必做任何的修正。
 * 若您使用的是其他版本的 bbs，請將 Makefile 中的 -DPTTBBS 那行取消掉，
 * 並自行於下面的 #elseif 之後加入適當的程式碼。
 */

#define RATE 0.95

int bank_moneyof(int uid){
#ifdef PTTBBS
    printf("[server] : uid = %d\n", uid);
    return moneyof(uid);
#else
    //
#endif
}

int bank_searchuser(char *userid){

#ifdef PTTBBS
    return searchuser(userid);
#else
    //
#endif
}

int bank_deumoney(char *user, int money){

#ifdef PTTBBS

    money *= RATE;
    printf("give user: %s money: %d add:%d\n", user, moneyof(bank_searchuser(user)), money);
    deumoney(bank_searchuser(user), money);
#else
    //
#endif
}
