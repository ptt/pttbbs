/*  使用方法: 自己先用 r->method_number 判斷是 M_GET 或 M_POST
 */
#include "mod_ptt.h"

unsigned char xtoc(char ch1, char ch2) {
  unsigned char d, up1, up2;

  if (!isxdigit(ch1) || !isxdigit(ch2)) return ' ';

  up1 = toupper(ch1);
  up2 = toupper(ch2);

  if (up1>='0' && up1<='9') d = up1-'0';
  else d = up1-'A'+10;
  d <<= 4;
  if (up2>='0' && up2<='9') d += up2-'0';
  else d += up2-'A'+10;

  return d;
}

char * cstoxs(pool *p, char *str)
{
  char * newstr = pstrdup(p,""), *po = str;

  for(po = str; *po; po++)
    {
	if(isprint(*po) && *po!=' ') newstr=psprintf(p, "%s%c", newstr,*po);
	else newstr=psprintf(p, "%s%%%X", newstr, *po& 0xFF);
    }
  return newstr;
}

int  // Ptt: 我重寫拿掉mygetchar 並加上fileupload的功能 
inital_postdata(request_rec *r, int *fileupload, char *boundary, 
        char **data)
{ 
  char *buf, *mb;
  int  size, off = 0, len_read; 
  void (*handler)();
 
  buf = table_get(r->subprocess_env,"CONTENT_TYPE"); 
  if(!buf || 
     (strncasecmp(buf,"application/x-www-form-urlencoded",33) && 
             strncasecmp(buf,"multipart/form-data",19)))
                return -1; 

  if(!strncasecmp(buf,"multipart/form-data",19)) { 
		if(!fileupload) return -1;
                *fileupload=1; 
                mb = strchr(buf,'='); 
                if(mb) {
			strcpy(boundary,mb+1);  
			strtok(boundary, " ;\n");
		       }
                else return -1; 
        } 
 
  buf = table_get(r->subprocess_env,"CONTENT_LENGTH"); 
  if(buf==NULL) return -1; 
 
  size = atoi(buf); 
  mb = (char *)palloc(r->pool,(size+1)*sizeof(char)); 
 
  if(!mb || 
     (setup_client_block(r ,REQUEST_CHUNKED_ERROR) != 0) ||               
     (should_client_block(r) == 0)) 
     return -1; 
                 
  hard_timeout("args.c copy script args", r);              
  handler = signal(SIGPIPE, SIG_IGN);

  while(size-off > 0 && (len_read = get_client_block 
                (r, mb + off, size - off)) > 0) { 
                        off += len_read; 
        } 

  signal(SIGPIPE, handler);
  kill_timeout(r);

  if(len_read < 0) return -1; // 讀的中途有中斷
  mb[size]=0; // Ptt:截掉不需要吧 因為不是用字串處理方式
 (*data) = mb; 
  return size; 
} 

#define ENCODED_NAME     " name=\""
#define ENCODED_FILENAME " filename=\"" 
#define ENCODED_CONTENT  "Content-Type: "

//#define FILEDATAFILE DATABASEDIR "/" FDATA

int 
unescape(request_rec *r, char** name, char** value) {
  static char *sp=NULL,  boundary[150]; 
  static int fileupload,  boundarylen, dsize; 
  pool *p = r->pool;
  char   ch, ch1, *dp, *p0, *p1, *temp_sp, *ftype;
  int type;

  if (sp==NULL) {
    fileupload=0;
    switch (r->method_number) { 
	    case M_GET: 
	       if (r->args) sp = pstrdup(p, r->args);
	       else {sp=NULL; return -1;}
	       break;
	    case M_POST:
	       if ((dsize = 
		 inital_postdata(r, &fileupload, boundary,&sp))<0) 
	           {sp=NULL; return -1; }
	       if(fileupload) boundarylen = strlen(boundary);
	       break;
	    default: 
	       sp=NULL;
 	       return -1; 
	}
   }

  if(fileupload) 
   {	
	if(dsize >0 && (temp_sp = memstr(sp, boundary, dsize)))
         { 
	   temp_sp += boundarylen; 
           p0 = strstr(temp_sp, "\r\n\r\n"); 
           p1 = strstr(temp_sp, "\n\n"); 

           if(!((*name) = strstr(temp_sp, ENCODED_NAME))) 
		        {sp=NULL; return -1; }
	   (*name) += strlen(ENCODED_NAME);
	   strtok((*name) + 1 , "\"");
	   if( (!p0 && p1) || (p0 && p1 && p1<p0)) 
		{
			(*value) = sp = p1 + 2;
			type = 2;
		}
	   else if(p0)  
		{
                        (*value) = sp = p0 + 4; 
			type = 4;
		}
	   else 
		{
	          sp=NULL; 
 	 	  return -1;
	        }

          if((p1 = memstr(temp_sp, boundary, dsize - boundarylen))) 
		  temp_sp = p1;
	  else
		  temp_sp = sp + dsize;

	  *(temp_sp - type) = 0;

          // 檢查是不是file 若是就給fileinfo
          if((dp=memstr(
  	      (*name) + strlen(*name) +1, ENCODED_FILENAME, dsize)) && 
	      dp < *value)
            {
		dp += strlen(ENCODED_FILENAME);
		p0 = strchr(dp,'"');
                *p0 = 0;
                if(p0 - dp>3)
		{
                 fileheader_t *file=(fileheader_t *)pcalloc(p, sizeof(fileheader_t));
                 if((ftype = strstr(p0 + 1, ENCODED_CONTENT)))
		  {
 		   ftype +=  strlen(ENCODED_CONTENT);
		   strtok(ftype, "\r\n");
		  }
                 while(p0 > dp && *p0 !='/' && *p0 !='\\') p0--;
                 strcpy(file->name, p0+1);    
		 strcpy(file->content_type, ftype);
		 file->icontent_type = filetype(ftype);
	//	 if(file->icontent_type & (FILE_TXT | FILE_HTML))
         //          file->size = strlen(sp);
          //       else
		   file->size = temp_sp - sp - type;
		 file->uploadtime = time(NULL); 
		 file->data = (*value);
                 (*value) = (char *)file;
		}
               else
                {
                 (*value) = NULL;
                }      
            }     

	  dsize -= (temp_sp - sp);
	  sp = temp_sp;
	  return 1;
         } 
        else 
         { 
          sp=NULL; 
          return -1; 
         }   
  }

  dp = (*name) = sp;  
  while(1) {
    ch = *sp++;
    switch(ch) {
      case '+':
        *dp++ = ' ';
        break;
      case '=':
        *dp++ = '\0';
	(*value) = dp;
	break;
      case '&': case '|':
        *dp++ = '\0';
        return 1;
      case '\0': case EOF:
        *dp = '\0'; 
	sp=NULL;
        return 0;
      case '%':
        ch = *sp++; ch1 = *sp++;
        *dp++ = xtoc(ch, ch1);
        break;
      default:
	*dp++=ch;
        break;
    }
  }
}

