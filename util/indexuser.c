/* $Id$ */
#define _UTIL_C_
#include "bbs.h"
#define INDEXPATH BBSHOME"/index"

unsigned string_hash(unsigned char *s)
{
    unsigned int v = 0;
    while (*s)
    {
        v = (v << 8) | (v >> 24);
        v ^= toupper(*s++);     /* note this is case insensitive */
    }
    return (v * 2654435769UL) >> (32 - HASH_BITS);
}
                                        

int main(int argc, char **argv)
{
    int j;
    userec_t u;
    char buf[256];

    attach_SHM();
    if(passwd_init())
    {
	printf("Sorry, the data is not ready.\n");
	exit(0);
    }
    system("rm -rf "INDEXPATH"/realname");
    system("rm -rf "INDEXPATH"/email");
    system("rm -rf "INDEXPATH"/ident");
    mkdir(INDEXPATH"/realname",0700);
    mkdir(INDEXPATH"/email",0700);
    mkdir(INDEXPATH"/ident",0700);
    for(j = 1; j <= MAX_USERS; j++) {
	passwd_query(j, &u);
        if(!u.userid[0]) continue;
        if(u.realname[0])
         {
          sprintf(buf,INDEXPATH"/realname/%X",string_hash(u.realname));
          append_record(buf, (fileheader_t*)&j, sizeof(j));
         }
        if(u.email[0])
         {
          sprintf(buf,INDEXPATH"/email/%X",string_hash(u.email));
          append_record(buf, (fileheader_t*)&j, sizeof(j));
         }
        if(u.ident[0])
         {
          sprintf(buf,INDEXPATH"/ident/%X",string_hash(u.ident));
          append_record(buf, (fileheader_t*)&j, sizeof(j));
         }
    }
    return 0;
}
