#define _UTIL_C_
#include "bbs.h"

/* 當資料欄位有異動 例如用了舊的欄位 可用這個程式清除舊值 */
int migrate_is_contact_email(userec_t *rec)
{
    char buf[8193] = {};
    char buf2[4097] = {};
    char logfn[PATHLEN] = {};
    FILE *fin = NULL;
    bool is_contact_email = false;

    if(!rec->userid[0]) {
        fprintf(stderr, "migrate_is_contact_email: invalid user-id\n");
        return -1;
    }

    // Log.
    sethomefile(logfn, rec->userid, FN_USERSECURITY);
    fin = fopen(logfn, "r");
    if(fin == NULL) {
        fprintf(stderr, "migrate_is_contact_email: unable to open user.security: userid: %s\n", rec->userid); 
        return -1;
    }
 
    fprintf(stderr, "to while-loop: userid: %s is_contact_email: %d\n", rec->userid, rec->is_contact_email);
    while(fgets(buf2, 4096, fin)) {
        fprintf(stderr, "after fgets: buf2: |%s|\n", buf2);
        if(strstr(buf2, "(ContactEmail)")) {
            is_contact_email = true;
            break;
        }
        memcpy(buf + 4096, buf2, 4096);
        if(strstr(buf, "(ContactEmail)")) {
            is_contact_email = true;
            break;
        }
        memcpy(buf, buf2, 4096);
        bzero(buf2, 4096);
    }
    fclose(fin);

    if(!is_contact_email) {
        fprintf(stderr, "migrate_is_contact_email: not is_contact_email: userid: %s\n", rec->userid);
        return 0;
    }

    fprintf(stderr, "migrate_is_contact_email: set is_contact_email: userid: %s\n", rec->userid);
    rec->is_contact_email = 1;
    return 0;
}

int main()
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

    if ((fdw = OpenCreate(BBSHOME"/.PASSWDS.new", O_WRONLY | O_TRUNC)) < 0){
	perror("open .PASSWDS.new error");
	exit(-1);
    }

    for(i = 0; i < MAX_USERS; i++){
	if (read(fd, &user, sizeof(user)) != sizeof(user))
	    break;
	migrate_is_contact_email(&user);
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
