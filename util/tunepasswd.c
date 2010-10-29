/* $Id$ */
#include "bbs.h"

int tune(int num) {
    int i, j, fin, fout;
    userec_t u;
    
    if((fin = open(FN_PASSWD, O_RDONLY)) == -1) {
	perror(FN_PASSWD);
	return 1;
    }
    if(flock(fin, LOCK_EX)) {
	printf("Lock failed!\n");
	return 1;
    }
    if((fout = OpenCreate(FN_PASSWD ".tune" , O_WRONLY)) == -1) {
	perror(FN_PASSWD ".tune");
	flock(fin, LOCK_UN);
	close(fin);
	return 1;
    }
    
    for(i = j = 0; i < num; i++) {
	read(fin, &u, sizeof(u));
	if(u.userid[0]) {
	    if(j == MAX_USERS) {
		printf("MAX_USERS is too small!\n");
		close(fout);
		unlink(FN_PASSWD ".tune");
		flock(fin, LOCK_UN);
		close(fin);
		return 1;
	    }
	    write(fout, &u, sizeof(u));
	    j++;
	}
    }
    for(memset(&u, 0, sizeof(u)); j < MAX_USERS; j++) {
	write(fout, &u, sizeof(u));
    }
    close(fout);
    
    /* backup */
    unlink(FN_PASSWD "~");
    link(FN_PASSWD, FN_PASSWD "~");
    unlink(FN_PASSWD);
    link(FN_PASSWD ".tune", FN_PASSWD);
    unlink(FN_PASSWD ".tune");
    
    flock(fin, LOCK_UN);
    close(fin);
    return 0;
}

int main() {
    struct stat sb;
    
    if(stat(FN_PASSWD, &sb)) {
	perror("stat");
	return 1;
    }
    if(sb.st_size != sizeof(userec_t) * MAX_USERS) {
	printf("size and MAX_USERS do not match!\n");
	if(tune(sb.st_size / sizeof(userec_t)) == 0)
	    printf(FN_PASSWD " has been tuned successfully!\n");
    } else
	printf("Nothing to do.\n");
    return 0;
}
