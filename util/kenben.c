#include <stdio.h>


void main()
{
  FILE * fin, * fout;
  char line[255], line2[255];
  int i;
  char genbuf[255], tok[20];
  fin = fopen("M.1006277896.A","r");
  while(!feof(fin))
  {
    fgets(line,255,fin);
    line[12] = '\0';

    sprintf(genbuf, "cd "BBSHOME"/boards/%c/%s;grep "
	    "超過一個月無廣告以外的本站文章發表。"
	    " *.A > "BBSHOME"/pttbbs/util/kenken.txt", line[0], line);
    system(genbuf);
    
    fout = fopen("kenken.txt","r");
    while(!feof(fout))
    {       
      line2[0] = '\0';
      fgets(line2,255,fout);
      if(strlen(line2) <= 10) break;
      sscanf(line2,"%s:",tok);
      for(i = 0; i < 20;i++)
      {
       if(tok[i] == ':')
       {
         tok[i] = '\0';
	 break;
       }
      }
      sprintf(genbuf, "cd "BBSHOME"/boards/%c/%s;rm %s", line[0], line, tok);
//      printf("%s \n", genbuf); 
      system(genbuf);   
    }
  }

  fclose(fin);

}
