/* Ptt : 常用函式整理 */

/*
 * 時間處理
 */
#include <time.h>
#ifdef _BBS_UTIL
 #undef pstrdup
 #define pstrdup(p, str) strdup(str)
#endif

int 
mygetdate(time_t clock, int *year, int *mon, int *mday, int *week)
{
  struct tm *mytm = localtime(&clock);
  if(year) *year = mytm->tm_year; 	/* 98 */
  if(mon)  *mon  = mytm->tm_mon + 1;    /* 1~12 */
  if(mday) *mday = mytm->tm_mday;       /* 1~31 */
  if(week) *week = mytm->tm_wday;       /* 0~6 */
  return 0;
}

char *
Cdatenum_slash(pool *p,time_t *clock)                         /* 98/04/21 */
{
  char foo[22];
  struct tm *mytm = localtime(clock);
  strftime(foo, 22, "%y/%m/%d", mytm);
  return pstrdup(p, foo);
}          

char * 
Cdatenum(pool *p,time_t *clock) 		                 /* 980421 */	
{
  char foo[22];
  struct tm *mytm = localtime(clock); 
  strftime(foo, 22, "%y%m%d", mytm); 
  return pstrdup(p, foo); 
}


#ifndef _BBS_UTIL
char * 
Cdatefullnum(pool *p,time_t *clock) 			/* 19980421 */
{
  char foo[22];
  struct tm *mytm = localtime(clock); 
  strftime(foo, 22, "%Y%m%d", mytm); 
  return pstrdup(p, foo); 
}
#else
char * 
Cdatefullnum(char *p,time_t *clock) 			/* 19980421 */
{
  static char foo[22];
  struct tm *mytm = localtime(clock); 
  strftime(foo, 22, "%Y%m%d", mytm); 
  return foo;
}
#endif

char * 
Cdate(char *p,time_t *clock)
{
  char foo[22];
  struct tm *mytm = localtime(clock); 
  strftime(foo, 22, "%D %T %a", mytm); 
  return pstrdup(p, foo); 
}

char * 
Cdatelite(char *p,time_t *clock)
{
  char foo[18];
  struct tm *mytm = localtime(clock); 
  strftime(foo, 18, "%D %T", mytm); 
  return pstrdup(p, foo); 
}       
   
char *
whattime(char *p,time_t *clock)
{
  char foo[18];
  struct tm *mytm = localtime(clock);
  strftime(foo, 18, "%H:%M:%S", mytm);
  return pstrdup(p, foo);
} 

char *
whatyear(char *p,time_t *clock)
{
  char foo[6];
  struct tm *mytm = localtime(clock);
  strftime(foo, 6, "%Y", mytm);
  return pstrdup(p, foo);
}                  

char *
whatmonth(char *p,time_t *clock)
{
  char foo[4];
  struct tm *mytm = localtime(clock);
  strftime(foo, 4, "%m", mytm);
  return pstrdup(p, foo);
}                

char *
whatday(char *p,time_t *clock)
{
  char foo[4];
  struct tm *mytm = localtime(clock);
  strftime(foo, 4, "%d", mytm);
  return pstrdup(p, foo);
}                 
      
char *
C_week(pool *p, int a)
{
  char foo[5]="";
  switch(a)
   {
    case 0:
    case 7:
	strcpy(foo,"日");
	break;
    case 1:
        strcpy(foo,"一");
        break;
    case 2:
        strcpy(foo,"二");
        break;
    case 3:
        strcpy(foo,"三");
        break;
    case 4:
        strcpy(foo,"四");
        break;
    case 5:
        strcpy(foo,"五");
        break;
    case 6:
        strcpy(foo,"六");
        break;
   }
  return pstrdup(p, foo);
} 

char *
whatweek(char *p,time_t *clock)
{
  struct tm *mytm = localtime(clock);
  return C_week(p, mytm->tm_wday);
}              

char *
whathour(char *p,time_t *clock)
{
  char foo[4]="";  
  struct tm *mytm = localtime(clock);
  strftime(foo, 6, "%H", mytm);
  return pstrdup(p, foo);   
}

char *
whatminute(char *p,time_t *clock)
{
  char foo[4]="";  
  struct tm *mytm = localtime(clock);
  strftime(foo, 6, "%M", mytm);
  return pstrdup(p, foo);
}        

char *
whatsecond(char *p,time_t *clock)
{
  char foo[4]="";  
  struct tm *mytm = localtime(clock);
  strftime(foo, 6, "%S", mytm);
  return pstrdup(p, foo);
}        

char *
Wholetime(char *p,time_t *clock)                  /* 19980421 */
{
  char foo[40];
  struct tm *mytm = localtime(clock);
  strftime(foo, 40, "%Y年%m月%d日%H時%M分%S秒", mytm);
  return pstrdup(p, foo);
}     
