/* $Id$ */
#include "bbs.h"

#define VOTEBOARD "NewBoard"
void
do_voteboardreply(const fileheader_t * fhdr)
{
    char            genbuf[256];
    char            reason[36]="";
    char            fpath[80];
    char            oldfpath[80];
    char            opnion[10];
    char           *ptr;
    FILE           *fo,*fi;
    fileheader_t    votefile;
    int             yes=0, no=0, len;
    int             fd;
    unsigned long   endtime=0;


    clear();
    if (!CheckPostPerm()||HasUserPerm(PERM_NOCITIZEN)) {
	move(5, 10);
	vmsg("對不起，您目前無法在此發表文章！");
	return;
    }
    if ( cuser.numlogins < ((unsigned int)(fhdr->multi.vote_limits.logins) * 10) ||
	    cuser.numposts < ((unsigned int)(fhdr->multi.vote_limits.posts) * 10) ) {
	move(5, 10);
	vmsg("你的上站數/文章數不足喔！");
	return;
    }
    setbpath(fpath, currboard);
    stampfile(fpath, &votefile);

    setbpath(oldfpath, currboard);

    strcat(oldfpath, "/");
    strcat(oldfpath, fhdr->filename);

    fi = fopen(oldfpath, "r");
    assert(fi);

    while (fgets(genbuf, sizeof(genbuf), fi)) {

        if (yes>=0)
           {
            if (!strncmp(genbuf, "----------",10))
               {yes=-1; continue;}
            else 
                yes++;
           }
        if (yes>3) outs(genbuf);

	if (!strncmp(genbuf, "連署結束時間", 12)) {
	    ptr = strchr(genbuf, '(');
	    assert(ptr);
	    sscanf(ptr + 1, "%lu", &endtime);
	    if (endtime < (unsigned long)now) {
		vmsg("連署時間已過");
		fclose(fi);
		return;
	    }
	}
        if(yes>=0) continue; 

        strtok(genbuf+4," \n");
	if (!strncmp(genbuf + 4, cuser.userid, IDLEN)) {
	    move(5, 10);
	    outs("您已經連署過本篇了");
	    getdata(17, 0, "要修改您之前的連署嗎？(Y/N) [N]", opnion, 3, LCECHO);
	    if (opnion[0] != 'y') {
		fclose(fi);
		return;
	    }
	    strlcpy(reason, genbuf + 19, 34);
            break;
	}
    }
    fclose(fi);
    do {
	if (!getdata(19, 0, "請問您 (Y)支持 (N)反對 這個議題：", opnion, 3, LCECHO)) {
	    return;
	}
    } while (opnion[0] != 'y' && opnion[0] != 'n');
    sprintf(genbuf, "請問您與這個議題的關係或%s理由為何：",
	    opnion[0] == 'y' ? "支持" : "反對");
    if (!getdata_buf(20, 0, genbuf, reason, 35, DOECHO)) {
	return;
    }
    if ((fd = open(oldfpath, O_RDONLY)) == -1)
	return;
    if(flock(fd, LOCK_EX)==-1 )
       {close(fd); return;}
    if(!(fi = fopen(oldfpath, "r")))
       {flock(fd, LOCK_UN); close(fd); return;}
     
    if(!(fo = fopen(fpath, "w")))
       {
        flock(fd, LOCK_UN);
        close(fd);
        fclose(fi);
	return;
       }

    while (fgets(genbuf, sizeof(genbuf), fi)) {
        if (!strncmp("----------", genbuf, 10))
	    break;
	fputs(genbuf, fo);
    }
    if (!endtime) {
	now += 14 * 24 * 60 * 60;
	fprintf(fo, "連署結束時間: (%d)%s\n", now, ctime4(&now));
	now -= 14 * 24 * 60 * 60;
    }
    fputs(genbuf, fo);
    len = strlen(cuser.userid); 
    for(yes=0; fgets(genbuf, sizeof(genbuf), fi);) {
	if (!strncmp("----------", genbuf, 10))
	    break;
	if (strlen(genbuf)<30 || (genbuf[4+len]==' ' && !strncmp(genbuf + 4, cuser.userid, len)))
            continue;
	fprintf(fo, "%3d.%s", ++yes, genbuf + 4);
      }
    if (opnion[0] == 'y')
	fprintf(fo, "%3d.%-15s%-34s 來源:%s\n", ++yes, cuser.userid, reason, cuser.lasthost);
    fputs(genbuf, fo);

    for(no=0; fgets(genbuf, sizeof(genbuf), fi);) {
	if (!strncmp("----------", genbuf, 10))
	    break;
	if (strlen(genbuf)<30 || (genbuf[4+len]==' ' && !strncmp(genbuf + 4, cuser.userid, len)))
            continue;
	fprintf(fo, "%3d.%s", ++no, genbuf + 4);
    }
    if (opnion[0] == 'n')
	fprintf(fo, "%3d.%-15s%-34s 來源:%s\n", ++no, cuser.userid, reason, cuser.lasthost);
    fprintf(fo, "----------總計----------\n");
    fprintf(fo, "支持人數:%-9d反對人數:%-9d\n", yes, no);
    fprintf(fo, "\n--\n※ 發信站 :" BBSNAME "(" MYHOSTNAME
                ") \n◆ From: 連署文章\n");

    flock(fd, LOCK_UN);
    close(fd);
    fclose(fi);
    fclose(fo);
    unlink(oldfpath);
    rename(fpath, oldfpath);
}

int
do_voteboard(int type)
{
    fileheader_t    votefile;
    char            topic[100];
    char            title[80];
    char            genbuf[1024];
    char            fpath[80];
    FILE           *fp;
    int             temp;

    clear();
    if (!CheckPostPerm()) {
	move(5, 10);
	vmsg("對不起，您目前無法在此發表文章！");
	return FULLUPDATE;
    }
    if ( cuser.firstlogin > (now - (time4_t)bcache[currbid - 1].vote_limit_regtime * 2592000) ||
	    cuser.numlogins < ((unsigned int)(bcache[currbid - 1].vote_limit_logins) * 10) ||
	    cuser.numposts < ((unsigned int)(bcache[currbid - 1].vote_limit_posts) * 10) ) {
	move(5, 10);
	vmsg("你不夠資深喔！");
	return FULLUPDATE;
    }
    move(0, 0);
    clrtobot();
    outs("您正在使用 PTT 的連署系統\n");
    outs("本連署系統將詢問您一些問題，請小心回答才能開始連署\n");
    outs("任意提出連署案者，將被列入不受歡迎使用者喔\n");
    move(4, 0);
    clrtobot();
    outs("(1)活動連署 (2)記名公投 ");
    if(type==0)
      outs("(3)申請新板 (4)廢除舊板 (5)連署板主 \n(6)罷免板主 (7)連署小組長 (8)罷免小組長 (9)申請新群組\n");

    do {
	getdata(6, 0, "請輸入連署類別 [0:取消]：", topic, 3, DOECHO);
	temp = atoi(topic);
    } while (temp < 0 || temp > 9 || (type && temp>2));
    switch (temp) {
    case 0:
         return FULLUPDATE;
    case 1:
	if (!getdata(7, 0, "請輸入活動主題：", topic, 30, DOECHO))
	    return FULLUPDATE;
	snprintf(title, sizeof(title), "%s %s", "[活動連署]", topic);
	snprintf(genbuf, sizeof(genbuf),
		 "%s\n\n%s%s\n", "活動連署", "活動主題: ", topic);
	strcat(genbuf, "\n活動內容: \n");
	break;
    case 2:
	if (!getdata(7, 0, "請輸入公投主題：", topic, 30, DOECHO))
	    return FULLUPDATE;
	snprintf(title, sizeof(title), "%s %s", "[記名公投]", topic);
	snprintf(genbuf, sizeof(genbuf),
		 "%s\n\n%s%s\n", "記名公投", "公投主題: ", topic);
	strcat(genbuf, "\n公投原因: \n");
	break;
    case 3:
	do {
	    if (!getdata(7, 0, "請輸入看板英文名稱：", topic, IDLEN + 1, DOECHO))
		return FULLUPDATE;
	    else if (invalid_brdname(topic))
		outs("不是正確的看板名稱");
	    else if (getbnum(topic) > 0)
		outs("本名稱已經存在");
	    else
		break;
	} while (temp > 0);
	snprintf(title, sizeof(title), "[申請新板] %s", topic);
	snprintf(genbuf, sizeof(genbuf),
		 "%s\n\n%s%s\n%s", "申請新板", "英文名稱: ", topic, "中文名稱: ");

	if (!getdata(8, 0, "請輸入看板中文名稱：", topic, 20, DOECHO))
	    return FULLUPDATE;
	strcat(genbuf, topic);
	strcat(genbuf, "\n看板類別: ");
	if (!getdata(9, 0, "請輸入看板類別：", topic, 20, DOECHO))
	    return FULLUPDATE;
	strcat(genbuf, topic);
	strcat(genbuf, "\n板主名單: ");
	getdata(10, 0, "請輸入板主名單：", topic, IDLEN * 3 + 3, DOECHO);
	strcat(genbuf, topic);
	strcat(genbuf, "\n申請原因: \n");
	break;
    case 4:
        move(1,0); clrtobot();
        generalnamecomplete("請輸入看板英文名稱：",
                            topic, IDLEN+1,
                            SHM->Bnumber,
                            &completeboard_compar,
                            &completeboard_permission,
                            &completeboard_getname);
	snprintf(title, sizeof(title), "[廢除舊板] %s", topic);
	snprintf(genbuf, sizeof(genbuf),
		 "%s\n\n%s%s\n", "廢除舊板", "英文名稱: ", topic);
	strcat(genbuf, "\n廢除原因: \n");
	break;
    case 5:
        move(1,0); clrtobot();
        generalnamecomplete("請輸入看板英文名稱：",
                            topic, IDLEN+1,
                            SHM->Bnumber,
                            &completeboard_compar,
                            &completeboard_permission,
                            &completeboard_getname);
	snprintf(title, sizeof(title), "[連署板主] %s", topic);
	snprintf(genbuf, sizeof(genbuf), "%s\n\n%s%s\n%s%s", "連署板主", "英文名稱: ", topic, "申請 ID : ", cuser.userid);
	strcat(genbuf, "\n申請政見: \n");
	break;
    case 6:
        move(1,0); clrtobot();
        generalnamecomplete("請輸入看板英文名稱：",
                            topic, IDLEN+1,
                            SHM->Bnumber,
                            &completeboard_compar,
                            &completeboard_permission,
                            &completeboard_getname);
	snprintf(title, sizeof(title), "[罷免板主] %s", topic);
	snprintf(genbuf, sizeof(genbuf),
		 "%s\n\n%s%s\n%s", "罷免板主", "英文名稱: ",
		 topic, "板主 ID : ");
        temp=getbnum(topic);
	assert(0<=temp-1 && temp-1<MAX_BOARD);
	do {
	    if (!getdata(7, 0, "請輸入板主ID：", topic, IDLEN + 1, DOECHO))
		return FULLUPDATE;
        }while (!userid_is_BM(topic, bcache[temp - 1].BM));
	strcat(genbuf, topic);
	strcat(genbuf, "\n罷免原因: \n");
	break;
    case 7:
	if (!getdata(7, 0, "請輸入小組中英文名稱：", topic, 30, DOECHO))
	    return FULLUPDATE;
	snprintf(title, sizeof(title), "[連署小組長] %s", topic);
	snprintf(genbuf, sizeof(genbuf),
		 "%s\n\n%s%s\n%s%s", "連署小組長", "小組名稱: ",
		 topic, "申請 ID : ", cuser.userid);
	strcat(genbuf, "\n申請政見: \n");
	break;
    case 8:
	if (!getdata(7, 0, "請輸入小組中英文名稱：", topic, 30, DOECHO))
	    return FULLUPDATE;
	snprintf(title, sizeof(title), "[罷免小組長] %s", topic);
	snprintf(genbuf, sizeof(genbuf), "%s\n\n%s%s\n%s",
		 "罷免小組長", "小組名稱: ", topic, "小組長 ID : ");
	if (!getdata(8, 0, "請輸入小組長ID：", topic, IDLEN + 1, DOECHO))
	    return FULLUPDATE;
	strcat(genbuf, topic);
	strcat(genbuf, "\n罷免原因: \n");
	break;
    case 9:
	if (!getdata(7, 0, "請輸入群組中英文名稱：", topic, 30, DOECHO))
	    return FULLUPDATE;
	snprintf(title, sizeof(title), "[申請新群組] %s", topic);
	snprintf(genbuf, sizeof(genbuf), "%s\n\n%s%s\n%s%s",
		 "申請群組", "群組名稱: ", topic, "申請 ID : ", cuser.userid);
	strcat(genbuf, "\n申請政見: \n");
	break;
    default:
	return FULLUPDATE;
    }
    outs("請輸入簡介或政見(至多五行)，要清楚填寫");
    for (temp = 12; temp < 17; temp++) {
	    if (!getdata(temp, 0, "：", topic, 60, DOECHO))
		break;
	    strcat(genbuf, topic);
	    strcat(genbuf, "\n");
	}
    if (temp == 11)
	    return FULLUPDATE;
    strcat(genbuf, "連署結束時間: ");
    now += 14 * 24 * 60 * 60;
    snprintf(topic, sizeof(topic), "(%d)", now);
    strcat(genbuf, topic);
    strcat(genbuf, ctime4(&now));
    strcat(genbuf, "\n");
    now -= 14 * 24 * 60 * 60;
    strcat(genbuf, "----------支持----------\n");
    strcat(genbuf, "----------反對----------\n");
    outs("開始連署嘍");
    setbpath(fpath, currboard);
    stampfile(fpath, &votefile);

    if (!(fp = fopen(fpath, "w"))) {
	outs("開檔失敗，請稍候重來一次");
	return FULLUPDATE;
    }
    fprintf(fp, "%s%s %s%s\n%s%s\n%s%s\n", "作者: ", cuser.userid,
	    "看板: ", currboard,
	    "標題: ", title,
	    "時間: ", ctime4(&now));
    fprintf(fp, "%s\n", genbuf);
    fclose(fp);
    strlcpy(votefile.owner, cuser.userid, sizeof(votefile.owner));
    strlcpy(votefile.title, title, sizeof(votefile.title));
    votefile.filemode |= FILE_VOTE;
    /* use lower 16 bits of 'money' to store limits */
    /* lower 8 bits are posts, higher 8 bits are logins */
    votefile.multi.vote_limits.regtime = bcache[currbid - 1].vote_limit_regtime;
    votefile.multi.vote_limits.logins = bcache[currbid - 1].vote_limit_logins;
    votefile.multi.vote_limits.posts = bcache[currbid - 1].vote_limit_posts;
    setbdir(genbuf, currboard);
    if (append_record(genbuf, &votefile, sizeof(votefile)) != -1)
	setbtotal(currbid);
    do_voteboardreply(&votefile);
    return FULLUPDATE;
}
