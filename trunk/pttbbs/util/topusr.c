/* $Id: topusr.c,v 1.3 2002/11/02 11:02:44 in2 Exp $ */
/* ¨Ï¥ÎªÌ ¤W¯¸°O¿ý/¤å³¹½g¼Æ ±Æ¦æº] */
#define _UTIL_C_
#include "bbs.h"

#define REAL_INFO
struct manrec
{
    char userid[IDLEN + 1];
    char username[23];
    int values[3];
};
typedef struct manrec manrec;
struct manrec *allman[3];

userec_t aman;
manrec theman;
int num;
FILE *fp;

#define TYPE_POST       0
#define TYPE_LOGIN      1
#define TYPE_MONEY      2


void
 top(type)
{
    static char *str_type[3] =
    {"µoªí¦¸¼Æ", "¶i¯¸¦¸¼Æ", " ¤j´I¯Î "};
    int i, j, rows = (num + 1) / 2;
    char buf1[80], buf2[80];

    if (type != 2)
	fprintf(fp, "\n\n");

    fprintf(fp, "\
[1;36m  ¢~¢w¢w¢w¢w¢w¢¡           [%dm    %8.8s±Æ¦æº]    [36;40m               ¢~¢w¢w¢w¢w¢w¢¡[m\n\
[1;36m  ¦W¦¸¢w¥N¸¹¢w¢w¢w¼ÊºÙ¢w¢w¢w¢w¢w¢w¼Æ¥Ø¢w¢w¦W¦¸¢w¥N¸¹¢w¢w¢w¼ÊºÙ¢w¢w¢w¢w¢w¢w¼Æ¥Ø[m\
", type + 44, str_type[type]);
    for (i = 0; i < rows; i++)
    {
        char ch=' ';
        int value;

        if(allman[type][i].values[type] > 1000000000)
		{ value=allman[type][i].values[type]/1000000; ch='M';}
        else if(allman[type][i].values[type] > 1000000)
		{ value=allman[type][i].values[type]/1000; ch='K';}
        else {value=allman[type][i].values[type]; ch=' ';}
	sprintf(buf1, "[%2d] %-11.11s%-16.16s%5d%c",
		i + 1, allman[type][i].userid, allman[type][i].username,
	        value, ch);
	j = i + rows;
        if(allman[type][j].values[type] > 1000000000)
		{ value=allman[type][j].values[type]/1000000; ch='M';}
        else if(allman[type][j].values[type] > 1000000)
		{ value=allman[type][j].values[type]/1000; ch='K';}
        else {value=allman[type][j].values[type]; ch=' ';}

	sprintf(buf2, "[%2d] %-11.11s%-16.16s%4d%c",
		j + 1, allman[type][j].userid, allman[type][j].username,
		value, ch);
	if (i < 3)
	    fprintf(fp, "\n [1;%dm%-40s[0;37m%s", 31 + i, buf1, buf2);
	else
	    fprintf(fp, "\n %-40s%s", buf1, buf2);
    }
}


#ifdef  HAVE_TIN
int
 post_in_tin(char *name)
{
    char buf[256];
    FILE *fh;
    int counter = 0;

    sprintf(buf, "%s/home/%c/%s/.tin/posted", home_path, name[0], name);
    fh = fopen(buf, "r");
    if (fh == NULL)
	return 0;
    else
    {
	while (fgets(buf, 255, fh) != NULL)
	    counter++;
	fclose(fh);
	return counter;
    }
}
#endif				/* HAVE_TIN */
int
 not_alpha(ch)
register char ch;
{
    return (ch < 'A' || (ch > 'Z' && ch < 'a') || ch > 'z');
}

int
 not_alnum(ch)
register char ch;
{
    return (ch < '0' || (ch > '9' && ch < 'A') ||
	    (ch > 'Z' && ch < 'a') || ch > 'z');
}

int
 bad_user_id(userid)
char *userid;
{
    register char ch;
    if (strlen(userid) < 2)
	return 1;
    if (not_alpha(*userid))
	return 1;
    while((ch = *(++userid)))
    {
	if (not_alnum(ch))
	    return 1;
    }
    return 0;
}

int main(argc, argv)
int argc;
char **argv;
{
    int i, j;

    if (argc < 3)
    {
	printf("Usage: %s <num_top> <out-file>\n", argv[0]);
	exit(1);
    }

    num = atoi(argv[1]);
    if (num == 0)
	num = 30;

    if(passwd_mmap())
    {
	printf("Sorry, the data is not ready.\n");
	exit(0);
    }
    for(i=0; i<3; i++)
     {
       allman[i]=malloc(sizeof(manrec) * num);
       memset(allman[i],0,sizeof(manrec) * num);    
     }
    for(j = 1; j <= MAX_USERS; j++) {
	passwd_query(j, &aman);
        aman.userid[IDLEN]=0;
        aman.username[22]=0;
	if((aman.userlevel & PERM_NOTOP) || !aman.userid[0] || 
	   bad_user_id(aman.userid) || 
	   strchr(aman.userid, '.'))
	{
	    continue;
	}
	else {
	    strcpy(theman.userid, aman.userid);
	    strcpy(theman.username, aman.username);
	    theman.values[TYPE_LOGIN] = aman.numlogins;
            theman.values[TYPE_POST] =  aman.numposts;
            theman.values[TYPE_MONEY] = aman.money;
            for(i=0; i<3; i++)
	     {
	        int k,l;
                for(k=num-1; k>=0 && allman[i][k].values[i]<theman.values[i];
			 k--);
	        k++;
	        if(k<num)
	                {
		          for(l=num-1; l>k; l--)
				  memcpy(&allman[i][l], &allman[i][l-1], 
					  sizeof(manrec));
	                  memcpy(&allman[i][k], &theman, sizeof(manrec));	
		        } 
	     }
	}
    }
    

    if ((fp = fopen(argv[2], "w")) == NULL)
    {
	printf("cann't open topusr\n");
	return 0;
    }

    top(TYPE_MONEY);
    top(TYPE_POST);
    top(TYPE_LOGIN);

    fclose(fp);
    return 0;
}
