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
                                        

int main()
{
    int j;
    userec_t u;
    char buf[256];

    if(passwd_mmap())
    {
	printf("Sorry, the data is not ready.\n");
	exit(0);
    }
    system("rm -f "INDEXPATH"/realname/* ");
    system("rm -f "INDEXPATH"/email/* ");
    system("rm -f "INDEXPATH"/ident/* ");
    for(j = 1; j <= MAX_USERS; j++) {
	passwd_query(j, &u);
        if(!u.userid[0]) continue;
        sprintf(buf,INDEXPATH"/realname/%X",string_hash(u.realname));
        append_record(buf, &j, sizeof(j));
        sprintf(buf,INDEXPATH"/email/%X",string_hash(u.email));
        append_record(buf, &j, sizeof(j));
        sprintf(buf,INDEXPATH"/ident/%X",string_hash(u.ident));
        append_record(buf, &j, sizeof(j));
    }
    return 0;
}
