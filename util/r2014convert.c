#include "bbs.h"

int main(){
    int orig_fd, new_fd;
    userec_t u;
    int count = 0;

    orig_fd = open(BBSHOME "/.AngelTrans", O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (orig_fd == -1) {
	if (errno == EEXIST) {
	    char c;
	    printf("It seems your .PASSWD file has been transfered, "
		    "do it any way?[y/N] ");
	    fflush(stdout);
	    scanf("%c", &c);
	    if (c != 'y' && c != 'Y')
		return 0;
	} else {
	    perror("opening " BBSHOME "/.AngelTrans for marking");
	    return 0;
	}
    } else {
	time4_t t = (time4_t)time(NULL);
	char* str = ctime(&t);
	write(orig_fd, str, strlen(str));
    }

    orig_fd = open(BBSHOME "/.PASSWDS", O_RDONLY);
    if( orig_fd < 0 ){
	perror("opening " BBSHOME "/.PASSWDS for reading");
	return 1;
    }
    printf("Reading from " BBSHOME "/.PASSWDS\n");

    new_fd = open(BBSHOME "/.PASSWDS.NEW", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if( new_fd < 0 ){
	perror("opening " BBSHOME "/.PASSWDS.NEW for writing");
	return 1;
    }
    printf("Writing to " BBSHOME "/.PASSWDS.NEW\n");

    while(read(orig_fd, &u, sizeof(userec_t)) == sizeof(userec_t)){
	// clear 0x400, 0x800, and 0x3000
	u.uflag2 &= ~(0x400 | 0x800 | 0x3000);
	if( u.userlevel & OLD_PERM_NOOUTMAIL )
	    u.uflag2 |= REJ_OUTTAMAIL;
	u.userlevel &= ~000001000000;
	bzero(u.myangel, IDLEN + 1);
	write(new_fd, &u, sizeof(userec_t));
	++count;
    }

    close(orig_fd);
    close(new_fd);
    printf("Done, totally %d accounts translated\n", count);

    printf("Moving files....\n");
    system("mv -i " BBSHOME "/.PASSWDS " BBSHOME "/.PASSWDS.old");
    system("mv -i " BBSHOME "/.PASSWDS.NEW " BBSHOME "/.PASSWDS");
    printf("Done, old password file is now at " BBSHOME "/.PASSWDS.old\n");

    return 0;
}
