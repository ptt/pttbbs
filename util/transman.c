/* $Id: transman 1298 2003-11-04 12:27:48Z Ptt $ */
// tools to translate the format of eagle bbs -> Ptt bbs */

#include "bbs.h"

int transman(char *path)
{
  char name[128];
  char buf[512], filename[512], *direct="";
  int n=0;
  fileheader_t fh;
  FILE *fp;

  chdir(path);

  fp = fopen(".Names", "r");
  if(fp)
   for(n=0; fgets(buf,512,fp)>0; n++)
   {
     strtok(buf,"\r\n");
     if(buf[0]=='#') continue;
     if(buf[0]=='N') 
        strcpy(name, buf+5);
     else 
       if(buf[0]=='P') 
        { 
         direct = buf+7;
         strcpy(filename, ".");
         stampfile(filename, &fh);
         unlink(filename);
         if(dashd(direct))
                 {
                  sprintf(fh.title, "¡» %s", name);
                  transman(direct);     
                 }
         else
                  sprintf(fh.title, "¡º %s", name);
         rename(direct, filename);
         append_record(".DIR", &fh, sizeof(fh)); 
        }
   }
  chdir("..");
  return n;
}

int main(int argc, char* argv[])
{
  if(argc < 2)
   {
    printf("%s <path>\n", argv[0]);
    return 0;
   }

  transman(argv[1]); 
  return 0;
}
