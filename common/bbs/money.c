#include <stdio.h>
#include <assert.h>
#include "cmbbs.h"

const char*
money_level(int money)
{
    int i = 0;

    static const char *money_msg[] =
    {
	"債台高築", "赤貧", "清寒", "普通", "小康",
	"小富", "中富", "大富翁", "富可敵國", "比爾蓋\天", NULL
    };
    while (money_msg[i] && money > 10)
	i++, money /= 10;

    if(!money_msg[i])
	i--;
    return money_msg[i];
}

