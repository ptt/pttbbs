#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    FILE *fp;
    char buf[100];
    int i, j, count[8]={0,0,0,0,0,0,0,0};
    if(argc<2) 
     {
        printf("useage example:%s ticket.user\n", argv[0]);
        return 0;
     }
    fp=fopen(argv[1], "r");
    if(!fp) return 0;
    while(fscanf(fp,"%s %d %d", buf, &i,  &j)!=EOF )
             {
                    count[i]+=j;
             }
    for(i=0; i<8; i++)
         {
             printf("%d) %d\n",i+1, count[i]);
         }
    return 0;
}

