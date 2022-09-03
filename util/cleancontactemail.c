#define _UTIL_C_
#include "bbs.h"

/* ������즳���� �Ҧp�ΤF�ª���� �i�γo�ӵ{���M���­� */
int clean_unused_var(userec_t *rec)
{
    // ensure valid user id.
    if(!is_validuserid(rec->userid)) {
      return -1;
    }

    // Log.
    //XXX setuserfile
    char logfn[PATHLEN];
    sethomefile(logfn, rec->userid, FN_USERSECURITY);

    //XXX syncnow();
    now = time(0);

    log_filef(logfn, LOG_CREAT, "%s (CleanContactEmail) %s\n",
              Cdatelite(&now), rec->email);

    //clean
    memset(rec->email, 0, sizeof(rec->email));
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

    if ((fdw = OpenCreate(BBSHOME"/.PASSWDS.new", O_WRONLY | O_TRUNC)) < 0){
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
