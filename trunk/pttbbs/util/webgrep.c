/* $Id: webgrep.c,v 1.2 2002/06/19 13:38:01 lwms Exp $ */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main()
{
    char genbuf[256], *str, *buf;
    while (fgets(genbuf, 255, stdin))
    {
	register int ansi;
	if (!strncmp(genbuf, "References", 10))
	    break;
	str = genbuf;
	buf = genbuf;
	if (!strncmp(genbuf, "lynx: Can't access", 18))
	{
	    printf("氣象報導小姐休假中,請到Record板翻過去資料.");
	    break;
	}
	for (ansi = 0; *str; str++)
	{
	    if (*str == '[' && strchr("0123456789", *(str + 1)))
	    {
		ansi = 1;
	    }
	    else if (ansi)
	    {
		if (!strchr("0123456789]", *str))
		{
		    ansi = 0;
		    if (str)
			*buf++ = *str;
		}
	    }
	    else
	    {
		if (str)
		    *buf++ = *str;
	    }
	}
	*buf = 0;
	printf(genbuf);
    }
    return 0;
}
