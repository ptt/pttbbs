/* Ptt : 常用字串函式整理 */            
#include "mod_ptt.h"

// 缺少 escape_url的東西

/*
 * 檔案判別
 */      

 /* 傳回:路徑大小 */

off_t
dashs(fname)
  char *fname;
{
  struct stat st;

  if (!stat(fname, &st))
        return (st.st_size);
  else
        return 0; /* 無此檔是 size 0 */
}   

 /* 傳回:路徑時間 */
long
dasht(char *fname)
{
  struct stat st;

  if (!stat(fname, &st))
        return (st.st_mtime);
  else
        return -1;
}

 /* 傳回:路徑是否為link檔 */
int
dashl(char *fname)
{
  struct stat st;   
  return (lstat(fname, &st) == 0 && S_ISLNK(st.st_mode));
}

 /* 傳回:路徑是否為檔案 */
dashf(char *fname)
{
  struct stat st;

  return (stat(fname, &st) == 0 && S_ISREG(st.st_mode));
}

/* 字串的小寫 */
#define char_lower(c)  ((c >= 'A' && c <= 'Z') ? c|32 : c)                   

void
str_lower(t, s)
  char *t, *s;
{
  register char ch;
  do
  {
    ch = *s++;
    *t++ = char_lower(ch);
  } while (ch);
}                

int
str_ncmp(s1, s2, n)
  char *s1, *s2;
  int n;
{
  int c1, c2;

  while (n--)
  {
    c1 = *s1++;
    if (c1 >= 'A' && c1 <= 'Z')
      c1 |= 32;

    c2 = *s2++;
    if (c2 >= 'A' && c2 <= 'Z')
      c2 |= 32;

    if (c1 -= c2)
      return (c1);

    if (!c2)  
      break;
  }
  return 0;
}  
char *
memstr(char *mem, char *str, int size)
{                        
  char *loc, *ptr=mem, *end=mem+size;
  int  len = strlen(str);
  while((loc = memchr(ptr, *str, size)))
   {
     if(!strncmp(loc, str, len))
	return loc;
     ptr = loc +1;
     size = (end - ptr);
     if(size <=0 ) return NULL;
   }
  return NULL;
}

/* 傳回: strstr的比較但不計大小寫的結果 */
/*
char *
strcasestr(str, tag)
  char *str, *tag;             
{
  char buf[BUFLEN];

  str_lower(buf, str);
  return  strstr(buf, tag);
}
*/
                      
/* 傳回: 一個checksum 給字串比較用 */

int str_checksum(char *str)
{
  int n=1;
  if(strlen(str) < 6) return 0;
  while(*str)
        n += *str++ ^ n;
  return n;
}
                    
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

char
ch2ph(char ch)
{
  static const
  //             a   b   c   d   e   f   g   h   i   j   k   l   m   n   o
  char table[]={'2','2','2','3','3','3','4','4','4','5','5','5','6','6','6',
  //      p   q   r   s   t   u   v   w   x   y   z
         '7','1','7','7','8','8','8','9','9','9','1'};
  static char d;
  d = char_lower(ch);
  if(d>='a' && d<='z') d=table[d-'a'];
  return d;
}

void
archiv32(chrono, fname)
  time_t chrono;        /* 32 bits */
  char *fname;          /* 7 chars */
{
  char *str;
  char radix32[32] = {
  '0', '1', '2', '3', '4', '5', '6', '7',
  '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
  'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
  'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
  };

  str = fname + 7;
  *str = '\0';
  for (;;)
  {
    *(--str) = radix32[chrono & 31];
    if (str == fname)
      return;
    chrono >>= 5;
  }
}

/* ARPANET時間格式 */
char *
Atime(clock) 
  time_t *clock;
{
  static char datemsg[40];
  /* ARPANET format: Thu, 11 Feb 1999 06:00:37 +0800 (CST) */
  /* strftime(datemsg, 40, "%a, %d %b %Y %T %Z", localtime(clock)); */
  /* time zone的傳回值不知和ARPANET格式是否一樣,先硬給,同sendmail*/
  strftime(datemsg, 40, "%a, %d %b %Y %T +0800 (CST)", localtime(clock));
  return (datemsg);
}


/* userid是否合法 */
int
bad_user_id(userid)
  char *userid;
{
  register char ch;
  if (strlen(userid) < 2)
    return 1;
  if (not_alpha(*userid))
    return 1;
  if (!strcasecmp(userid, "new") || !strcasecmp(userid, "guest") ||
      !strcasecmp(userid, "root") || !strcasecmp(userid, "webadm") ||
      !strncasecmp(userid,"yam",3)
     )
    return 1;

  while (ch = *(++userid))             
  {
    if (not_alnum(ch) || ch == '0')
      return 1;
  }
  return 0;
}    


int
logfile(pool *p, char *filename,char *buf)
{
    FILE *fp;
    time_t now;
    if( (fp = ap_pfopen(p, filename, "a" )) ) {
   
        fputs( buf, fp );
        if(!strchr(buf,'\n')) 
	  { 
	    now = time(NULL);
	    fputs(ctime(&now), fp);
	  }
        ap_pfclose(p, fp );
        return 0;
    }
    else{ 
        printf("error open file");
        return -1; 
    }
}

/* 檢查中文斷字問題 */
char *strexam(char *s)
{
  int i;
  int odd;
  for (i = 0, odd = 0; s[i]; i++)
    if (odd) odd = 0;
    else if (s[i] & 128) odd = 1;
  if (odd) s[--i] = 0;
  return s;
}

int
maxline(char *data,char *newline, int max)
{
  register int n, nlen=strlen(newline);
  register char *po;

  for(n=0, po = data; (po = strstr(po, newline)); po+=nlen)
   {
      if(++n>=max) {*po=0; return -1;}
   }
  return n;
}
