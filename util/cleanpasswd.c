/* $Id: toplazyBM.c 1096 2003-08-15 06:13:29Z victor $ */
#define _UTIL_C_
#include "bbs.h"

extern userec_t xuser;

/* 當資料欄位有異動 例如用了舊的欄位 可用這個程式清除舊值 */
int clean_unused_var(userec_t *rec)
{
    rec->goodpost = 0;
    rec->badpost = 0;
    rec->goodsale = 0;
    rec->badsale = 0;
    memset(rec->pad, 0, sizeof(rec->pad));
    return 0;
}

int main(int argc, char *argv[])
{
    attach_SHM();
    if(passwd_init())
	exit(1);      
    if (passwd_apply(clean_unused_var) == 0)
	printf("ERROR\n");
    return 0;
}
