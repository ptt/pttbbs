#include "bank.h"

int sockfd;

char* readline(char *str, int timeout){
    int len;
    struct timeval tv;
    fd_set set;

    tv.tv_sec = timeout;
    tv.tv_usec = 0;

    FD_ZERO(&set);
    FD_SET(sockfd, &set);

    if (select(sockfd + 1, &set, NULL, NULL,!timeout ? NULL : &tv) <= 0) { ///
	perror("select");
	exit(-1);
    }

    if ((len = read(sockfd, str, STRLEN * sizeof(char))) < 0){
	herror("readline");
	exit(-1);
    }
    if (len == 0)
	return NULL;
    str[len] = 0;
    read(sockfd, &len, sizeof(char)); // chomp
    return str;
}

void writeln(char *str){
    char n = '\n';
    write(sockfd, str, strlen(str));
    write(sockfd, &n, sizeof(n));
}

void getcmd(char *buf, char *cmd, char *param){
    int i;
    for(i = 0; buf[i] && buf[i] != ' ' && i < STRLEN; i++)
	cmd[i] = buf[i];
    cmd[i] = 0;
    if (!buf[i]){
	param = NULL;
	return;
    }
    buf = &buf[i + 1];
    for(i = 0; buf[i] && i < STRLEN; i++)
	param[i] = buf[i];
    param[i] = 0;
}
