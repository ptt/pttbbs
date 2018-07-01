#include "bbs.h"

#define VOTEBOARD "NewBoard"

char *CheckVoteRestrictionBoard(int bid, size_t sz_msg, char *msg)
{
    boardheader_t *bh = getbcache(bid);
    if ((currmode & MODE_BOARD) || HasUserPerm(PERM_SYSOP))
	return NULL;

    return get_restriction_reason(
        cuser.numlogindays, cuser.badpost,
        bh->vote_limit_logins,
        bh->vote_limit_badpost,
        sz_msg, msg);
}

char *CheckVoteRestrictionFile(
        const fileheader_t * fhdr, size_t sz_msg, char *msg)
{
    if ((currmode & MODE_BOARD) || HasUserPerm(PERM_SYSOP))
        return NULL;

    return get_restriction_reason(
        cuser.numlogindays, cuser.badpost,
        fhdr->multi.vote_limits.logins,
        fhdr->multi.vote_limits.badpost,
        sz_msg, msg);
}

static char *
find_vote_entry_userid(char *s) {
    // format: [SPACE*]NUM.USERID REASON 來源:IP
    while (*s == ' ')
        s++;
    while (isascii(*s) && isdigit(*s))
        s++;
    if (*s == '.')
        return s + 1;
    else
        return NULL;
}

void
do_voteboardreply(const fileheader_t * fhdr)
{
    char            genbuf[256];
    char            reason[36]="";
    char            fpath[80];
    char            oldfpath[80];
    char            opnion[10];
    char           *ptr, *uid;
    FILE           *fo,*fi;
    fileheader_t    votefile;
    int             yes=0, no=0, len;
    int             fd;
    unsigned long   endtime=0;


    clear();
    if (!CheckPostPerm()||HasUserPerm(PERM_NOCITIZEN)) {
	vmsg("對不起，您目前無法在此發表文章！");
	return;
    }

    if (currmode & MODE_SELECT) {
	vmsg("請先退出搜尋模式後再進行連署。");
	return;
    }

    if (CheckVoteRestrictionFile(fhdr, sizeof(genbuf), genbuf))
    {
	vmsgf("未達投票資格限制: %s", genbuf);
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
	char *space;

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

        uid = find_vote_entry_userid(genbuf);
        if (!uid)
            continue;

	space = strpbrk(uid, " \n");
	if(space) *space='\0';
	if (!strncasecmp(uid, cuser.userid, IDLEN)) {
	    move(5, 10);
	    outs("您已經連署過本篇了");
	    getdata(17, 0, "要修改您之前的連署嗎？(Y/N) [N]", opnion, 3, LCECHO);
            if (*opnion == 'y')
                break;
            fclose(fi);
            return;
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
    if (!getdata(20, 0, genbuf, reason, 35, DOECHO)) {
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
	fprintf(fo, "連署結束時間: (%d)%s\n\n", now, Cdate(&now));
	now -= 14 * 24 * 60 * 60;
    }
    fputs(genbuf, fo);
    len = strlen(cuser.userid);
    for(yes=0; fgets(genbuf, sizeof(genbuf), fi);) {
	if (!strncmp("----------", genbuf, 10))
	    break;
	if (strlen(genbuf)<30)
            continue;
        uid = find_vote_entry_userid(genbuf);
        if (!uid || (uid[len] == ' ' && strncasecmp(uid, cuser.userid, len) == 0))
            continue;
	fprintf(fo, "%3d.%s", ++yes, uid);
      }
    if (opnion[0] == 'y')
	fprintf(fo, "%3d.%-15s%-34s 來源:%s\n", ++yes, cuser.userid, reason, cuser.lasthost);
    fputs(genbuf, fo);

    for(no=0; fgets(genbuf, sizeof(genbuf), fi);) {
	if (!strncmp("----------", genbuf, 10))
	    break;
	if (strlen(genbuf)<30)
            continue;
        uid = find_vote_entry_userid(genbuf);
        if (!uid || (uid[len] == ' ' && strncasecmp(uid, cuser.userid, len) == 0))
            continue;
	fprintf(fo, "%3d.%s", ++no, uid);
    }
    if (opnion[0] == 'n')
	fprintf(fo, "%3d.%-15s%-34s 來源:%s\n", ++no, cuser.userid, reason, cuser.lasthost);
    fprintf(fo, "----------總計----------\n");
    fprintf(fo, "支持人數:%-9d反對人數:%-9d\n", yes, no);
    fprintf(fo, "\n--\n※ 發信站: " BBSNAME "(" MYHOSTNAME
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
    char            title[STRLEN];
    char            genbuf[2048];
    char            fpath[PATHLEN];
    FILE           *fp;
    int             temp;

    if (!CheckPostPerm()) {
	vmsg("對不起，您目前無法在此發表文章！");
	return FULLUPDATE;
    }
    if (CheckVoteRestrictionBoard(currbid, sizeof(genbuf), genbuf))
    {
	vmsgf("未達投票資格限制: %s", genbuf);
	return FULLUPDATE;
    }
    vs_hdr(BBSNAME "連署系統");
    outs("您正在使用" BBSNAME "的連署系統，請小心回答下列問題才能開始連署\n");
    outs("任意提出連署案者，將被列入不受歡迎使用者喔\n\n");
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
	    else if (!is_valid_brdname(topic))
		outs("不是正確的看板名稱");
	    else if (getbnum(topic) > 0)
		outs("本名稱已經存在");
	    else
		break;
	} while (temp > 0);
	snprintf(title, sizeof(title), "[申請新板] %s", topic);
	snprintf(genbuf, sizeof(genbuf),
		 "%s\n\n%s%s\n%s", "申請新板", "英文名稱: ", topic, "中文名稱: ");

	if (!getdata(8, 0, "請輸入看板中文名稱：", topic, BTLEN + 1, DOECHO))
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
	if (!getbnum(topic))
	{
	    vmsg("無此看版。");
	    return FULLUPDATE;
	}
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
	if (!getbnum(topic))
	{
	    vmsg("無此看版。");
	    return FULLUPDATE;
	}
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
	if (!getbnum(topic))
	{
	    vmsg("無此看版。");
	    return FULLUPDATE;
	}
	snprintf(title, sizeof(title), "[罷免板主] %s", topic);
	snprintf(genbuf, sizeof(genbuf),
		 "%s\n\n%s%s\n%s", "罷免板主", "英文名稱: ",
		 topic, "板主 ID : ");
        temp=getbnum(topic);
	assert(0<=temp-1 && temp-1<MAX_BOARD);
	do {
	    if (!getdata(7, 0, "請輸入板主ID：", topic, IDLEN + 1, DOECHO))
		return FULLUPDATE;
        }while (!is_uBM(bcache[temp - 1].BM, topic));
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
    strcat(genbuf, Cdate(&now));
    strcat(genbuf, "\n\n");
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
    fprintf(fp, ANSI_COLOR(1;33) "若想加入連署請按 y 回應" ANSI_RESET "\n\n");
    fprintf(fp, "%s\n", genbuf);
    fclose(fp);
    strlcpy(votefile.owner, cuser.userid, sizeof(votefile.owner));
    strlcpy(votefile.title, title, sizeof(votefile.title));
    votefile.filemode |= FILE_VOTE;
    /* use lower 16 bits of 'money' to store limits */
    /* lower 8 bits are posts, higher 8 bits are logins */
    votefile.multi.vote_limits.logins  = bcache[currbid - 1].vote_limit_logins;
    votefile.multi.vote_limits.badpost = bcache[currbid - 1].vote_limit_badpost;
    setbdir(genbuf, currboard);
    if (append_record(genbuf, &votefile, sizeof(votefile)) != -1)
	setbtotal(currbid);
    do_voteboardreply(&votefile);
    return FULLUPDATE;
}
