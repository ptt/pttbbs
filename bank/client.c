#include "bank.h"

extern int sockfd;

char* chomp(char *str){
    int len;
    len = strlen(str);
    if(len > 0 && str[len - 1] == '\n')
	str[len - 1] = 0;
    return str;
}

void client_work(FILE *fp){
    char buf[STRLEN];
    while(fgets(buf, sizeof(buf), stdin) != NULL){
	printf("before sending\n");
	chomp(buf);
	writeln(buf);
	if(!strcasecmp(buf, "quit"))
	    break;
	printf("> %s\n", readline(buf, 0));
    }
}

void init_net(char *addr){
    struct hostent *host = NULL;
    struct sockaddr_in serv_addr;

    if((host = gethostbyname(addr)) == NULL){
	herror("gethostbyname");
	exit(-1);
    }
    bcopy(host->h_addr, &serv_addr.sin_addr, host->h_length);

    if((sockfd = socket(PF_INET, SOCK_STREAM, 0)) < 0){
	herror("socket");
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port        = htons(PORT);
    inet_pton(AF_INET, addr, &serv_addr.sin_addr);

    connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
}

int main(int argc, char **argv){
//    if(argc != 3)
//	exit(-1);

    init_net(argv[1]);
    client_work(stdin);

    return 0;
}
