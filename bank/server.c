#include "bank.h"

extern int sockfd;

SHM_t *SHM;
char *fn_passwd = ".PASSWDS";
char flag;

typedef struct map_t {
    char *key;
    int (*fp)(char *);
} map_t;

void sig_handler(int signo){
    pid_t pid;
    while( (pid = waitpid(-1, NULL, WNOHANG)) > 0)
	fprintf(stderr, "Child %d terminated\n", pid);
    exit(0);
}

int list(char *param){
    printf("list: %s\n", param);
    writemsg(SUCCESS, "list success");
    return 1;
}

int query(char *param){
    char buf[STRLEN];
    sprintf(buf, "%s's money is %d", param, bank_moneyof(bank_searchuser(param)));
    writemsg(SUCCESS, buf);
    return 1;
}

int give(char param[STRLEN]){
    char user[STRLEN], money[STRLEN];
    char tmp[STRLEN];
    printf("give: %s\n", param);
    getcmd(param, user, money);
    bank_deumoney(user, atoi(money));
    sprintf(tmp, "give %s %s", user, money);
    writemsg(SUCCESS, tmp);
    return 1;
}

int quit(char *param){
    printf("[server] client quit\n");
    exit(0);
}

map_t map[] = {
    {"list", list},
    {"query", query},
    {"give", give},
    {"quit", quit},
    {"", NULL},
};

void writemsg(int status, char *str){
    //write(sockfd, &status. sizeof(status));
    writeln(str);
}

void serv_work(void){
    int i, status = ERROR;
    char buf[STRLEN], cmd[STRLEN], param[STRLEN];

    fprintf(stderr, "new child\n");

    attach_SHM();

    while(readline(buf, 600) != NULL){
	printf("[server] read: ");
	puts(buf);
	getcmd(buf, cmd, param);
	for (i = 0; map[i].fp; i++){
	    if (!strcasecmp(map[i].key, cmd)){
		status = (*(map[i].fp))(param);
		break;
	    }
	}
	if(!map[i].fp){
	    writemsg(ERROR, "Wrong command");
	    strcpy(param, "error");
	}
	printf("[server] complete\n");
    }
}

int check_perm(char *ip){
}

int main(int argc, char **argv){
    int listenfd;
    pid_t pid;
    socklen_t addr_len;
    struct sockaddr_in serv_addr;

    listenfd = socket(PF_INET, SOCK_STREAM, 0);

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port        = htons(PORT);

    bind(listenfd,(struct sockaddr *) &serv_addr, sizeof(serv_addr));
    listen(listenfd, 5);
    signal(SIGCHLD, sig_handler);

    while(1){
	addr_len = sizeof(serv_addr);

	if( (sockfd = accept(listenfd,(struct sockaddr *) &serv_addr, &addr_len)) < 0){
	    if(errno == EINTR)
		continue;
	    else
		herror("accept");
	}
#define NOFORK 1
#if NOFORK
	if((pid = fork()) < 0)
	    perror("fork");
	
	else if(pid == 0){
#endif
	    int len;
	    struct sockaddr_in name;
	    close(listenfd);

	    if(getpeername(sockfd,(struct sockaddr *) &name, &len) < 0){
		perror("getpeername");
		exit(-1);
	    }
	    else{
		printf("address: %s\n", inet_ntoa(((struct sockaddr_in *) &name)->sin_addr));
	    }
	    serv_work();
	    exit(0);
#if NOFORK
	}
#endif
	close(sockfd);
    }
    return 0;
}
