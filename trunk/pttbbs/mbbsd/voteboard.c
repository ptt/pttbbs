/* $Id: voteboard.c,v 1.2 2002/04/16 15:27:27 in2 Exp $ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/file.h>
#include "config.h"
#include "pttstruct.h"
#include "modes.h"
#include "common.h"
#include "perm.h"
#include "proto.h"

#define VOTEBOARD "NewBoard"

extern char currboard[];
extern int currbid;
extern boardheader_t *bcache;
extern int currmode;
extern userec_t cuser;

void do_voteboardreply(fileheader_t *fhdr){
    char genbuf[1024];
    char reason[60];
    char fpath[80];
    char oldfpath[80];
    char opnion[10];
    char *ptr;
    FILE *fo, *fp;
    fileheader_t votefile;
    int len;
    int i, j;
    int fd;
    time_t endtime, now = time(NULL);
    int hastime = 0;
   

    clear();
    if(!(currmode & MODE_POST)) {
	move(5, 10);
	outs("對不起，您目前無法在此發表文章！");
	pressanykey();
	return;
    }   
    
    setbpath(fpath, currboard);
    stampfile(fpath, &votefile);

    setbpath(oldfpath, currboard);
   
    strcat(oldfpath, "/");
    strcat(oldfpath, fhdr->filename);
   
    fp = fopen(oldfpath, "r");
   
    len = strlen(cuser.userid);
   
    while(fgets(genbuf, 1024, fp)){
	if (!strncmp(genbuf, "連署結束時間", 12)){
	    hastime = 1;
	    ptr = strchr(genbuf, '(');
	    sscanf(ptr+1, "%ld", &endtime);
	    if (endtime < now){
		prints("連署時間已過");
		pressanykey();
		fclose(fp);
		return;
	    }
	}
	if (!strncmp(genbuf+4, cuser.userid, len)){
	    move(5, 10);
	    prints("您已經連署過本篇了");
	    opnion[0] = 'n';
	    getdata(7, 0, "要修改您之前的連署嗎？(Y/N) [N]", opnion, 3, LCECHO);
	    if (opnion[0] != 'y'){
		fclose(fp);
		return;
	    }
	    strcpy(reason, genbuf+19);
	}
    }
    fclose(fp);                  
   
    if((fd = open(oldfpath, O_RDONLY)) == -1)
	return;
    flock(fd, LOCK_EX);
   
    fo = fopen(fpath, "w");
   
    if (!fo)
	return;
    i = 0;
    while(fo){
	j = 0;
	do{
	    if (read(fd, genbuf+j, 1)<=0){
		flock(fd, LOCK_UN);
		close(fd);
		fclose(fo);
		unlink(fpath);
		return;
	    }
	    j++;
	}while(genbuf[j-1] !='\n');
	genbuf[j] = '\0';        
	i++;
	if (!strncmp("----------", genbuf, 10))
	    break;
	if (i > 3)
	    prints(genbuf);
	fprintf(fo, "%s", genbuf);    
    }
    if (!hastime){
	now += 14*24*60*60;
	fprintf(fo, "連署結束時間: (%ld)%s", now, ctime(&now));
	now -= 14*24*60*60;
    }
   
    fprintf(fo, "%s", genbuf);

    do{
	if (!getdata(18, 0, "請問您 (Y)支持 (N)反對 這個議題：", opnion, 3, LCECHO)){
	    flock(fd, LOCK_UN);
	    close(fd);           
	    fclose(fo);
	    unlink(fpath);
	    return;
	}
    }while(opnion[0] != 'y' && opnion[0] != 'n');      
   
    if (!getdata(20, 0, "請問您與這個議題的關係或連署理由為何：", reason, 40, DOECHO)){
	flock(fd, LOCK_UN);
	close(fd);           
	fclose(fo);
	unlink(fpath);
	return;   
    }
   
    i = 0;
   
    while(fo){
	i++;
	j = 0;
	do{
	    if (read(fd, genbuf+j, 1)<=0){
		flock(fd, LOCK_UN);
		close(fd);
		fclose(fo);
		unlink(fpath);
		return;
	    }      
	    j++;
	}while(genbuf[j-1] !='\n');   
	genbuf[j] = '\0';
	if (!strncmp("----------", genbuf, 10))
	    break;
	if (strncmp(genbuf+4, cuser.userid, len))
	    fprintf(fo, "%3d.%s", i, genbuf+4);
	else
	    i--;
    }
    if (opnion[0] == 'y')
	fprintf(fo, "%3d.%-15s%-34s 來源:%s\n", i, cuser.userid, reason,cuser.lasthost);
    i = 0;
    fprintf(fo, "%s", genbuf);
    while(fo){
	i++;
	j = 0;
	do{
	    if (!read(fd, genbuf+j, 1))
		break;
	    j++;
	}while(genbuf[j-1] !='\n');
	genbuf[j] = '\0';
	if (j <= 3)
	    break;         
	if (strncmp(genbuf+4, cuser.userid, len))
	    fprintf(fo, "%3d.%s", i, genbuf+4);
	else
	    i--;
    }
    if (opnion[0] == 'n')
	fprintf(fo, "%3d.%-15s%-34s 來源:%s\n", i, cuser.userid, reason,cuser.lasthost);
    flock(fd, LOCK_UN);  
    close(fd);
    fclose(fo);
    unlink(oldfpath);
    rename(fpath, oldfpath);
#ifdef MDCACHE
    close(updatemdcache(NULL, fpath));
#endif   
}

int do_voteboard() {
    fileheader_t votefile;
    char topic[100];
    char title[80];
    char genbuf[1024];
    char fpath[80];
    FILE* fp;
    int temp, i;
    time_t now = time(NULL);

    clear();
    if(!(currmode & MODE_POST)) {
	move(5, 10);
	outs("對不起，您目前無法在此發表文章！");
	pressanykey();
	return FULLUPDATE;
    }       
    
    move(0, 0);
    clrtobot();
    prints("您正在使用 PTT 的連署系統\n");
    prints("本連署系統將詢問您一些問題，請小心回答才能開始連署\n");
    prints("任意提出連署案者，將被列入本系統不受歡迎使用者喔\n");
    pressanykey();
    move(0, 0);
    clrtobot();    
    prints("(1)申請新版 (2)廢除舊版 (3)連署版主 (4)罷免版主\n");
    if (!strcmp(currboard, VOTEBOARD))
	prints("(5)連署小組長 (6)罷免小組長 ");
    if (!strcmp(currboard, VOTEBOARD) && HAS_PERM(PERM_SYSOP))
	prints("(7)站民公投");
    prints("(8)申請新群組");
       
    do{       
	getdata(3, 0, "請輸入連署類別：", topic, 3, DOECHO);
	temp = atoi(topic);
    }while(temp <= 0 && temp >= 9);
    
    switch(temp){
    case 1:    
	do{
	    if (!getdata(4, 0, "請輸入看版英文名稱：", topic, IDLEN+1, DOECHO))
		return FULLUPDATE;
	    else if (invalid_brdname(topic))
		outs("不是正確的看版名稱");
	    else if (getbnum(topic) > 0)
		outs("本名稱已經存在");
	    else
		break;
	}while(temp > 0);          
	sprintf(title, "[申請新版] %s", topic);
	sprintf(genbuf, "%s\n\n%s%s\n%s","申請新版", "英文名稱: ", topic, "中文名稱: ");
       
	if (!getdata(5, 0, "請輸入看版中文名稱：", topic, 20, DOECHO))
	    return FULLUPDATE;
	strcat(genbuf, topic);
	strcat(genbuf, "\n看版類別: ");
	if (!getdata(6, 0, "請輸入看版類別：", topic, 20, DOECHO))
	    return FULLUPDATE;
	strcat(genbuf, topic);       
	strcat(genbuf, "\n版主名單: ");
	getdata(7, 0, "請輸入版主名單：", topic, IDLEN * 3 + 3, DOECHO);
	strcat(genbuf, topic);
	strcat(genbuf, "\n申請原因: \n");
	outs("請輸入申請原因(至多五行)，要清楚填寫不然不會核准喔");
	for(i= 8;i<13;i++){
	    if (!getdata(i, 0, "：", topic, 60, DOECHO))
		break;
	    strcat(genbuf, topic);
	    strcat(genbuf, "\n");
	}
	if (i==8)
	    return FULLUPDATE;
	break;
    case 2:
	do{
	    if (!getdata(4, 0, "請輸入看版英文名稱：", topic, IDLEN+1, DOECHO))
		return FULLUPDATE;
	    else if (getbnum(topic) <= 0)
		outs("本名稱並不存在");
	    else
		break;
	}while(temp > 0);          
	sprintf(title, "[廢除舊版] %s", topic);
	sprintf(genbuf, "%s\n\n%s%s\n","廢除舊版", "英文名稱: ", topic);
	strcat(genbuf, "\n廢除原因: \n");
	outs("請輸入廢除原因(至多五行)，要清楚填寫不然不會核准喔");
	for(i= 8;i<13;i++){
	    if (!getdata(i, 0, "：", topic, 60, DOECHO))
		break;
	    strcat(genbuf, topic);
	    strcat(genbuf, "\n");
	}
	if (i==8)
	    return FULLUPDATE;
          
	break;
    case 3:    
	do{
	    if (!getdata(4, 0, "請輸入看版英文名稱：", topic, IDLEN+1, DOECHO))
		return FULLUPDATE;
	    else if (getbnum(topic) <= 0)
		outs("本名稱並不存在");
	    else
		break;
	}while(temp > 0);          
	sprintf(title, "[連署版主] %s", topic);
	sprintf(genbuf, "%s\n\n%s%s\n%s%s","連署版主", "英文名稱: ", topic, "申請 ID : ", cuser.userid);
	strcat(genbuf, "\n申請政見: \n");
	outs("請輸入申請政見(至多五行)，要清楚填寫不然不會核准喔");
	for(i= 8;i<13;i++){
	    if (!getdata(i, 0, "：", topic, 60, DOECHO))
		break;
	    strcat(genbuf, topic);
	    strcat(genbuf, "\n");
	}
	if (i==8)
	    return FULLUPDATE;       
	break;       
    case 4:    
	do{
	    if (!getdata(4, 0, "請輸入看版英文名稱：", topic, IDLEN+1, DOECHO))
		return FULLUPDATE;
	    else if ((i = getbnum(topic)) <= 0)
		outs("本名稱並不存在");
	    else
		break;
	}while(temp > 0);          
	sprintf(title, "[罷免版主] %s", topic);
	sprintf(genbuf, "%s\n\n%s%s\n%s","罷免版主", "英文名稱: ", topic, "版主 ID : ");
	do{
	    if (!getdata(6, 0, "請輸入版主ID：", topic, IDLEN + 1, DOECHO))
		return FULLUPDATE;
	    else if (!userid_is_BM(topic, bcache[i-1].BM))
		outs("不是該版的版主");
	    else
		break;
	}while(temp > 0);
	strcat(genbuf, topic);
	strcat(genbuf, "\n罷免原因: \n");
	outs("請輸入罷免原因(至多五行)，要清楚填寫不然不會核准喔");
	for(i= 8;i<13;i++){
	    if (!getdata(i, 0, "：", topic, 60, DOECHO))
		break;
	    strcat(genbuf, topic);
	    strcat(genbuf, "\n");
	}
	if (i==8)
	    return FULLUPDATE;
	break;       
    case 5:    
	if (!getdata(4, 0, "請輸入小組中英文名稱：", topic, 30, DOECHO))
	    return FULLUPDATE;
	sprintf(title, "[連署小組長] %s", topic);
	sprintf(genbuf, "%s\n\n%s%s\n%s%s","連署小組長", "小組名稱: ", topic, "申請 ID : ", cuser.userid);
	strcat(genbuf, "\n申請政見: \n");
	outs("請輸入申請政見(至多五行)，要清楚填寫不然不會核准喔");
	for(i= 8;i<13;i++){
	    if (!getdata(i, 0, "：", topic, 60, DOECHO))
		break;
	    strcat(genbuf, topic);
	    strcat(genbuf, "\n");
	}
	if (i==8)
	    return FULLUPDATE;
	break;
    case 6:
           
	if (!getdata(4, 0, "請輸入小組中英文名稱：", topic, 30, DOECHO))
	    return FULLUPDATE;
	sprintf(title, "[罷免小組長] %s", topic);
	sprintf(genbuf, "%s\n\n%s%s\n%s","罷免小組長", "小組名稱: ", topic, "小組長 ID : ");
	if (!getdata(6, 0, "請輸入小組長ID：", topic, IDLEN + 1, DOECHO))
	    return FULLUPDATE;
	strcat(genbuf, topic);
	strcat(genbuf, "\n罷免原因: \n");
	outs("請輸入罷免原因(至多五行)，要清楚填寫不然不會核准喔");
	for(i= 8;i<13;i++){
	    if (!getdata(i, 0, "：", topic, 60, DOECHO))
		break;
	    strcat(genbuf, topic);
	    strcat(genbuf, "\n");
	}
	if (i==8)
	    return FULLUPDATE;
	break;       
    case 7:    
	if (!HAS_PERM(PERM_SYSOP))
	    return FULLUPDATE;
	if (!getdata(4, 0, "請輸入公投主題：", topic, 30, DOECHO))
	    return FULLUPDATE;
	sprintf(title, "%s %s", "[站民公投]", topic);
	sprintf(genbuf, "%s\n\n%s%s\n","站民公投", "公投主題: ", topic);
	strcat(genbuf, "\n公投原因: \n");
	outs("請輸入公投原因(至多五行)，要清楚填寫不然不會核准喔");
	for(i= 8;i<13;i++){
	    if (!getdata(i, 0, "：", topic, 60, DOECHO))
		break;
	    strcat(genbuf, topic);
	    strcat(genbuf, "\n");
	}
	if (i==8)
	    return FULLUPDATE;
	break;              
    case 8:
	if(!getdata(4, 0, "請輸入群組中英文名稱：", topic, 30, DOECHO))
	    return FULLUPDATE;
	sprintf(title, "[申請新群組] %s", topic);
	sprintf(genbuf, "%s\n\n%s%s\n%s%s","申請群組", "群組名稱: ", topic, "申請 ID : ", cuser.userid);
	strcat(genbuf, "\n申請政見: \n");
	outs("請輸入申請政見(至多五行)，要清楚填寫不然不會核准喔");
	for(i= 8;i<13;i++){
	    if (!getdata(i, 0, "：", topic, 60, DOECHO))
		break;
	    strcat(genbuf, topic);
	    strcat(genbuf, "\n");
	}
	if (i==8)
	    return FULLUPDATE;
	break;
    default:
	return FULLUPDATE;
    }
    strcat(genbuf, "連署結束時間: ");
    now += 14*24*60*60;    
    sprintf(topic, "(%ld)", now);
    strcat(genbuf, topic);
    strcat(genbuf, ctime(&now));
    now -= 14*24*60*60;
    strcat(genbuf, "----------支持----------\n");
    strcat(genbuf, "----------反對----------\n");    
    outs("開始連署嘍");
    setbpath(fpath, currboard);
    stampfile(fpath, &votefile);
    
    if (!(fp = fopen(fpath, "w"))){
	outs("開檔失敗，請稍候重來一次");
	return FULLUPDATE;
    }
    fprintf(fp, "%s%s %s%s\n%s%s\n%s%s", "作者: ", cuser.userid,
	    "看板: ", currboard, 
	    "標題: ", title,
	    "時間: ", ctime(&now));
    fprintf(fp, "%s\n", genbuf);
    fclose(fp);
    strcpy(votefile.owner, cuser.userid);
    strcpy(votefile.title, title);
    votefile.savemode = 'S';
    setbdir(genbuf, currboard);
    if(append_record(genbuf, &votefile, sizeof(votefile)) != -1)
	setbtotal(currbid);
    do_voteboardreply(&votefile);
    return FULLUPDATE;
}
