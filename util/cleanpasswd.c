/* $Id$ */
#define _UTIL_C_
#include "bbs.h"

/* 當資料欄位有異動 例如用了舊的欄位 可用這個程式清除舊值 */
int clean_unused_var(userec_t *rec)
{
    rec->goodpost = 0;
    rec->badpost = 0;
    rec->goodsale = 0;
    rec->badsale = 0;
    memset(rec->pad, 0, sizeof(rec->pad));
    return 0;
}

int main(int argc, char *argv[])
{
    int i, fd, fdw;
    userec_t user;
    
    setgid(BBSGID);
    setuid(BBSUID);
    chdir(BBSHOME);

    if ((fd = open(BBSHOME"/.PASSWDS", O_RDONLY)) < 0){
	perror("open .PASSWDS error");
	exit(-1);
    }	    

    if ((fdw = open(BBSHOME"/.PASSWDS.new", O_WRONLY | O_TRUNC | O_CREAT, 0644)) < 0){
	perror("open .PASSWDS.new error");
	exit(-1);
    }

    for(i = 0; i < MAX_USERS; i++){
	if (read(fd, &user, sizeof(user)) != sizeof(user))
	    break;
	clean_unused_var(&user);
	write(fdw, &user, sizeof(user));
    }
    close(fd);
    close(fdw);

    if (i != MAX_USERS)
	fprintf(stderr, "ERROR\n");
    else{
	fprintf(stderr, "DONE\n");
	system("/bin/mv " BBSHOME "/.PASSWDS " BBSHOME "/.PASSWDS.bak");
	system("/bin/mv " BBSHOME "/.PASSWDS.new " BBSHOME "/.PASSWDS");
    }
    return 0;
}
