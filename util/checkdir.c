/* $Id: checkdir.c 1356 2003-11-22 02:16:02Z victor $ */
/*
typedef struct fileheader_t {
    char    filename[FNLEN];
    char    recommend;
    char    owner[IDLEN + 2];
    char    date[6];
    char    title[TTLEN + 1];
    int     money;   
    unsigned char   filemode; 
} fileheader_t;

*/
#include "bbs.h"
void dumpfh(fileheader_t fh)
{
    char *c;
    printf("dumping fh\n"); 
    for( c= (char*)&fh; (c-(char*)&fh)<sizeof(fh); c++) 
       if(isprint(*c)) printf("%c",*c);
       else printf("[%02d]",(unsigned int)*c);
    printf("\n"); 
}
int main(int argc, char **argv) {
    int count=0;
    fileheader_t pfh, fh;
    FILE *fp, *fo=NULL;
    int offset=0;
    
    if(argc < 2) {
	fprintf(stderr, "Usage: %s \n", argv[0]);
	return 1;
    }
    
    if(!(fp=fopen(argv[1],"r")))
       {printf("fileopen error!\n");
        return 0;}
    if(argc >2)
        fo=fopen(argv[2],"w");

    for(count=0; fread(&fh, sizeof(fh), 1, fp) >0; count++)
      {
        if(fh.owner[0]=='M' && fh.owner[1]=='.')
         {
          count--;
          fseek(fp,FNLEN+1-sizeof(fh),SEEK_CUR);
          printf("%d,offset forth!---dump\n", count);
          dumpfh(pfh);
          dumpfh(fh);
          offset=1;
          continue;
         }
        if(fh.filename[1]!='.' && offset==1)
         {
          fseek(fp,-FNLEN-1-sizeof(fh),SEEK_CUR);
          printf("%d,offset back!\n", count);
          offset=0;
          continue;
         }
        if(fh.filename[0]==0 || fh.owner[0]==0 || fh.owner[0]=='-')
         {
           if(fh.filename[0]==0) unlink(fh.filename);
           continue;
         }
         pfh=fh; 
        if(fo)
           fwrite(&fh, sizeof(fh), 1, fo);
      }
    fclose(fp);
    if(fo) fclose(fo);
    return 0;
}
