/* $id$ */
#ifndef INCLUDE_BBS_H
#define INCLUDE_BBS_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#include <netdb.h>
#include <time.h>
#include <ctype.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/telnet.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/mman.h>
#include <machine/param.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

#include "config.h"
#include "pttstruct.h"
#include "common.h"
#include "perm.h"
#include "modes.h"
#include "proto.h"
#include "gomo.h"

#ifndef INCLUDE_VAR_H
  #include "var.h"
#endif
#ifdef FreeBSD
  #include <machine/limits.h>
#else
  #include <limits.h>
#endif
#ifdef DEBUG
static int Sizeof(char *what,int size,char *file,int line)
{
  if(size==4)
    printf("WARRNING\r\n");
  printf("%s(%d): sizeof(%s)=%d\r\n",file,line,what,size);
  return size;
}
#define SIZEOF(a) Sizeof(#a,sizeof(a),__FILE__,__LINE__)
#else
#define SIZEOF(a) sizeof(a)
#endif
#endif /* INCLUDE_BBS_H */
