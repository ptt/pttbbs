#include "mod_ptt.h"
#include <time.h>
#include <math.h>
#define KUSERINFO   "$userinfo$"
#define KUSERID     "$userid$"
#define KUSERNAME   "$username$"
#define KUSERMONEY  "$usermoney$"
#define KTIMEYEAR   "$timeyear$"    
#define KTIMEMONTH  "$timemonth$"
#define KTIMEMDAY   "$timemday$"
#define KTIMEWEEK   "$timeweek$"
#define KSERVERTIME "$servertime$"
#define KTIMEHOUR   "$hour$"
#define KTIMEMINUTE "$minute$"
#define KTIMESECOND "$second$"
#define KMSGONLOAD  "$MSGonLoad$"
#define KFCSONLOAD  "$FCSonLoad$"
#define KHTMLTITLE    "$HTMLtitle$"
#define KSCROLLTITLE  "$SCROLLtitle$"
#define KBGCOLOR    "$BGCOLOR$"
#define KBGSOUND    "$BGSOUND$"
#define KINCHTML    "$file:"


        
char *
ap_parseline(request_rec *r, const char *str, char *str_substiute[]) {        
  int i, off=0;
  char *strbuf, *po;
  pool *p = r->pool;
  strbuf = pstrdup(p, str);

  if(str_substiute == NULL) return strbuf;
  for(i=0; str_substiute[i]!=NULL; i+=2)
   {
    off = 0;
    while(po = strstr(strbuf + off, str_substiute[i]))
     {
       *po = 0;
       strbuf = pstrcat(p, strbuf, str_substiute[i+1] !=NULL
	      ? str_substiute[i+1] : "", po+strlen(str_substiute[i]), NULL);
       off += str_substiute[i+1] ? strlen(str_substiute[i+1]) : 0;
     } 
   }
  return strbuf;
}

char *
ap_standard_parseline(request_rec *r, char *str)
{
  time_t now = time(NULL);    
   pool *p = r->pool;        
   char *str_substiute[]=     //   關鍵字的代換表
      {
        KUSERID, "ptt",
	KUSERNAME, "name",
/*
        KTIMEYEAR, whatyear(p,&now),
        KTIMEMONTH,whatmonth(p,&now),
        KTIMEMDAY, whatday(p,&now),
        KTIMEWEEK, whatweek(p,&now),
        KSERVERTIME, whattime(p,&now),
        KTIMEHOUR,     whathour(p,&now),
        KTIMEMINUTE,   whatminute(p,&now),
        KTIMESECOND,   whatsecond(p,&now),
*/
        KUSERINFO, NULL,
        KUSERMONEY, "100",
        NULL,NULL
      };           
   return ap_parseline(r, str, str_substiute);
}

int
ap_showfile(request_rec *r, char *filename, char *table[], char *t2[], FILE *fo)
{
 pool *p = r->pool;
 FILE *fp = pfopen(p, filename, "r");
 char *str, *incfile, buf[512];

 if (!fp) {
#if DEBUG
    rputs(filename, r);
#endif
    return -1;
 }

 while (fgets(buf, 512, fp))
   {

     str = ap_standard_parseline(r,
       ap_parseline(r,
         ap_parseline(r, buf, table),
         t2));   

    if ((incfile = strstr(str, KINCHTML)) != NULL) {
      incfile += strlen(KINCHTML);
      incfile = strtok(incfile, "$");
#if 0
      if (*incfile != '/')
        incfile = ap_pstrcat(p, TEMPLATEDIR "/", incfile, NULL);
#endif
      ap_showfile(r, incfile, table, t2, fo);
    } else   if(fo) fputs(str, fo);
      else   rputs(str, r);          
   }

 pfclose(p, fp);
 return 0;
}

char *
add_href(pool *p, char *l)
{
  char *href[] = {"http://", "ftp://", "gopher://", "file://",
		  "telnet://", "mailto:", NULL},
       *end, url[PATHLEN], *tl, *po;
  int i, urllen, off;

  tl = pstrdup(p, l);

  for(i=0; href[i]!=NULL; i++)
   {
    off = 0;
    //Ptt: while 應該用 while 但 while 有bug
    if((po = strstr(tl + off, href[i])))
     {
       for(end = po; (*end>='&' && *end<='z')||*end=='~' ; end++);
       urllen = end - po;

       if(urllen > PATHLEN)
             {
                off += PATHLEN; continue;
             }

       strncpy(url, po, urllen);
       *po= 0;
       url[urllen] = 0;
       tl = psprintf(p, "%s<a href=\"%s\" target=\"%s\">%s</a>%s", tl,
                url, "new", url, end);
       off += 2 * urllen + 15;
     }
   }                 

  return tl;
}

/*
  format:
    char *name, void  *ptr, size_t length
    char *name,  int  *ptr, -1
    char *name, char **ptr, 0
*/
int
GetQueryDatas(request_rec *r, int **table)
{
   int rc, i, match=0, dlen, maxlen;
   char *name, *value, *str;
   pool *p = r->pool;

   for(i=1; table[i]!=NULL; i+=3)
	{
	 if((int) table[i+1]) *table[i]=0;
	 else (char *) (*table[i]) = pstrdup(p, "");
	}
   do
    {
	 if((rc = unescape(r, &name, &value))<0) break; 
         for(i=0; table[i]; i+=3)
	 {
            if(!strcmp((char *)table[i], name)) break;

#if 0
/* SiE990313 test: 抓 未定義的變數 */
	    if(!strcmp((char *)table[i], "$SPARE$"))
	    {
		(char *) (*table[i + 1]) = pstrdup(p, name);
		continue;
	    }
#endif
	 }
         if(table[i] && table[i+1])
            {
                    if((maxlen = (int) table[i+2]) > 0)
                          {
		            if(!*value) continue;
			    dlen = strlen((char *)table[i+1]); 
                            str  = ap_escape_html(p, value);
			    if(maxlen > dlen)
                              strexam(
                                 strncat((char *)table[i+1],str,maxlen-dlen)
                              );
                          }
                    else  if((int) table[i+2] == 0) // for file upload
			  {
//                           str  = ap_escape_html(p, value);
	                     //if(value) 
			     (*table[i+1]) = (int) value;
//			     else (*table[i+1]) = (int) pstrdup(p,"");
			  }
		    else
                          {
		             if(!*value) continue;
                             *(table[i+1]) += atoi(value);
                          }
		match++;
            }
   } while(rc>0);
   return match;
}


int
ap_url_redirect(request_rec *r, char *url)
{
  r->status = REDIRECT;
  
  ap_table_setn(r->headers_out, "Location", pstrdup(r->pool, url));
  return MOVED;
  //return  REDIRECT, MOVED 或 HTTP_SEE_OTHER 都可
}

char *
pint2str(pool* p, int c)
{
 return ap_psprintf(p,"%d",c);
}

char*
preplace(request_rec *r, char* src, char* sig, char* rep)
{
  char *ptr;
  int siglen=strlen(sig), off=0;

  while((ptr = strstr(src+off,sig)))
  {
    *ptr = 0;
    src = pstrcat(r->pool, src, rep, NULL);
    off = strlen(src);
    src = pstrcat(r->pool, src, ptr+siglen, NULL);
  }
  return src;
}       

int
hex2dig(char *str)
{
  int base,ret=0,i,len=strlen(str);

  for(i=0; i<len; i++)
  {
    if(str[i]>='a' && str[i]<='f') base = str[i]-'a'+10;
    else if(str[i]>='A' && str[i]<='F') base = str[i]-'A'+10;
    else if(str[i]>='0' && str[i]<='9') base = str[i]-'0';
    else return -1;    
   
    ret += base*pow(16,len-i-1);
  }
  return ret;
}          

char *
pstrncpy(pool* p, char tmp[], int n)
{
   char *ptr;
   ptr=pstrdup(p,tmp);
   *(ptr+n)=0;
   return psprintf(p,"%s",ptr);
}    

char *
GetPathToken(request_rec *r, int count)
{
  char *str, *token, *sepr = "/\\?&";
  int i;
  str = pstrdup(r->pool, r->path_info + 1);
  token = strtok(str, sepr);
  for( i = 1 ; i < count; i++)
    {
       token = strtok(NULL, sepr);
       if(!token) return "-1";     /* Heat:count超過也要考慮 */
    }
  return token;
}  


/*
  個人信件夾檔保管方式:

  mail/@/rec/FF/F/UUDDTT
  mail/@/dat/FF/F/UUDDTT

*/

