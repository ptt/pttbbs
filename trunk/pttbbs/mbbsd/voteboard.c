/* $Id: voteboard.c,v 1.18 2003/06/28 08:47:45 kcwu Exp $ */
#include "bbs.h"

#define VOTEBOARD "NewBoard"

void
do_voteboardreply(fileheader_t * fhdr)
{
    char            genbuf[1024];
    char            reason[50];
    char            fpath[80];
    char            oldfpath[80];
    char            opnion[10];
    char           *ptr;
    FILE           *fp;
    fileheader_t    votefile;
    int             len;
    int             i, j;
    int             fd;
    time_t          endtime;
    int             hastime = 0;


    clear();
    if (!(currmode & MODE_POST)) {
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
    assert(fp);

    len = strlen(cuser.userid);

    while (fgets(genbuf, sizeof(genbuf), fp)) {
	if (!strncmp(genbuf, "連署結束時間", 12)) {
	    hastime = 1;
	    ptr = strchr(genbuf, '(');
	    assert(ptr);
	    sscanf(ptr + 1, "%ld", &endtime);
	    if (endtime < now) {
		prints("連署時間已過");
		pressanykey();
		fclose(fp);
		return;
	    }
	}
	if (!strncmp(genbuf + 4, cuser.userid, len)) {
	    move(5, 10);
	    prints("您已經連署過本篇了");
	    opnion[0] = 'n';
	    getdata(7, 0, "要修改您之前的連署嗎？(Y/N) [N]", opnion, 3, LCECHO);
	    if (opnion[0] != 'y') {
		fclose(fp);
		return;
	    }
	    strlcpy(reason, genbuf + 19, sizeof(reason));
	}
    }
    fclose(fp);

    if ((fd = open(oldfpath, O_RDONLY)) == -1)
	return;

    fp = fopen(fpath, "w");

    if (!fp)
	return;
    i = 0;
    while (fp) {
	j = 0;
	do {
	    if (read(fd, genbuf + j, 1) <= 0) {
		flock(fd, LOCK_UN);
		close(fd);
		fclose(fp);
		unlink(fpath);
		return;
	    }
	    j++;
	} while (genbuf[j - 1] != '\n');
	genbuf[j] = '\0';
	i++;
	if (!strncmp("----------", genbuf, 10))
	    break;
	if (i > 3)
	    prints(genbuf);
	fprintf(fp, "%s", genbuf);
    }
    if (!hastime) {
	now += 14 * 24 * 60 * 60;
	fprintf(fp, "連署結束時間: (%ld)%s", now, ctime(&now));
	now -= 14 * 24 * 60 * 60;
    }
    fprintf(fp, "%s", genbuf);

    do {
	if (!getdata(18, 0, "請問您 (Y)支持 (N)反對 這個議題：", opnion, 3, LCECHO)) {
	    flock(fd, LOCK_UN);
	    close(fd);
	    fclose(fp);
	    unlink(fpath);
	    return;
	}
    } while (opnion[0] != 'y' && opnion[0] != 'n');

    if (!getdata(20, 0, "請問您與這個議題的關係或連署理由為何：",
		 reason, sizeof(reason), DOECHO)) {
	flock(fd, LOCK_UN);
	close(fd);
	fclose(fp);
	unlink(fpath);
	return;
    }
    flock(fd, LOCK_EX);
    i = 0;

    while (fp) {
	i++;
	j = 0;
	do {
	    if (read(fd, genbuf + j, 1) <= 0) {
		flock(fd, LOCK_UN);
		close(fd);
		fclose(fp);
		unlink(fpath);
		return;
	    }
	    j++;
	} while (genbuf[j - 1] != '\n');
	genbuf[j] = '\0';
	if (!strncmp("----------", genbuf, 10))
	    break;
	if (strncmp(genbuf + 4, cuser.userid, len))
	    fprintf(fp, "%3d.%s", i, genbuf + 4);
	else
	    i--;
    }
    if (opnion[0] == 'y')
	fprintf(fp, "%3d.%-15s%-34s 來源:%s\n", i, cuser.userid, reason, cuser.lasthost);
    i = 0;
    fprintf(fp, "%s", genbuf);
    while (fp) {
	i++;
	j = 0;
	do {
	    if (!read(fd, genbuf + j, 1))
		break;
	    j++;
	} while (genbuf[j - 1] != '\n');
	genbuf[j] = '\0';
	if (j <= 3)
	    break;
	if (strncmp(genbuf + 4, cuser.userid, len))
	    fprintf(fp, "%3d.%s", i, genbuf + 4);
	else
	    i--;
    }
    if (opnion[0] == 'n')
	fprintf(fp, "%3d.%-15s%-34s 來源:%s\n", i, cuser.userid, reason, cuser.lasthost);
    flock(fd, LOCK_UN);
    close(fd);
    fclose(fp);
    unlink(oldfpath);
    rename(fpath, oldfpath);
}

int
do_voteboard()
{
    fileheader_t    votefile;
    char            topic[100];
    char            title[80];
    char            genbuf[1024];
    char            fpath[80];
    FILE           *fp;
    int             temp, i;

    clear();
    if (!(currmode & MODE_POST)) {
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
    prints("(1)申請新板 (2)廢除舊板 (3)連署板主 (4)罷免板主\n");
    if (!strcmp(currboard, VOTEBOARD))
	prints("(5)連署小組長 (6)罷免小組長 ");
    if (!strcmp(currboard, VOTEBOARD) && HAS_PERM(PERM_SYSOP))
	prints("(7)站民公投");
    prints("(8)申請新群組");

    do {
	getdata(3, 0, "請輸入連署類別：", topic, 3, DOECHO);
	temp = atoi(topic);
    } while (temp <= 0 && temp >= 9);

    switch (temp) {
    case 1:
	do {
	    if (!getdata(4, 0, "請輸入看板英文名稱：", topic, IDLEN + 1, DOECHO))
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

	if (!getdata(5, 0, "請輸入看板中文名稱：", topic, 20, DOECHO))
	    return FULLUPDATE;
	strcat(genbuf, topic);
	strcat(genbuf, "\n看板類別: ");
	if (!getdata(6, 0, "請輸入看板類別：", topic, 20, DOECHO))
	    return FULLUPDATE;
	strcat(genbuf, topic);
	strcat(genbuf, "\n板主名單: ");
	getdata(7, 0, "請輸入板主名單：", topic, IDLEN * 3 + 3, DOECHO);
	strcat(genbuf, topic);
	strcat(genbuf, "\n申請原因: \n");
	outs("請輸入申請原因(至多五行)，要清楚填寫不然不會核准喔");
	for (i = 9; i < 13; i++) {
	    if (!getdata(i, 0, "：", topic, 60, DOECHO))
		break;
	    strcat(genbuf, topic);
	    strcat(genbuf, "\n");
	}
	if (i == 9)
	    return FULLUPDATE;
	break;
    case 2:
	do {
	    if (!getdata(4, 0, "請輸入看板英文名稱：", topic, IDLEN + 1, DOECHO))
		return FULLUPDATE;
	    else if (getbnum(topic) <= 0)
		outs("本名稱並不存在");
	    else
		break;
	} while (temp > 0);
	snprintf(title, sizeof(title), "[廢除舊板] %s", topic);
	snprintf(genbuf, sizeof(genbuf),
		 "%s\n\n%s%s\n", "廢除舊板", "英文名稱: ", topic);
	strcat(genbuf, "\n廢除原因: \n");
	outs("請輸入廢除原因(至多五行)，要清楚填寫不然不會核准喔");
	for (i = 8; i < 13; i++) {
	    if (!getdata(i, 0, "：", topic, 60, DOECHO))
		break;
	    strcat(genbuf, topic);
	    strcat(genbuf, "\n");
	}
	if (i == 8)
	    return FULLUPDATE;

	break;
    case 3:
	do {
	    if (!getdata(4, 0, "請輸入看板英文名稱：", topic, IDLEN + 1, DOECHO))
		return FULLUPDATE;
	    else if (getbnum(topic) <= 0)
		outs("本名稱並不存在");
	    else
		break;
	} while (temp > 0);
	snprintf(title, sizeof(title), "[連署板主] %s", topic);
	snprintf(genbuf, sizeof(genbuf), "%s\n\n%s%s\n%s%s", "連署板主", "英文名稱: ", topic, "申請 ID : ", cuser.userid);
	strcat(genbuf, "\n申請政見: \n");
	outs("請輸入申請政見(至多五行)，要清楚填寫不然不會核准喔");
	for (i = 8; i < 13; i++) {
	    if (!getdata(i, 0, "：", topic, 60, DOECHO))
		break;
	    strcat(genbuf, topic);
	    strcat(genbuf, "\n");
	}
	if (i == 8)
	    return FULLUPDATE;
	break;
    case 4:
	do {
	    if (!getdata(4, 0, "請輸入看板英文名稱：", topic, IDLEN + 1, DOECHO))
		return FULLUPDATE;
	    else if ((i = getbnum(topic)) <= 0)
		outs("本名稱並不存在");
	    else
		break;
	} while (temp > 0);
	snprintf(title, sizeof(title), "[罷免板主] %s", topic);
	snprintf(genbuf, sizeof(genbuf),
		 "%s\n\n%s%s\n%s", "罷免板主", "英文名稱: ",
		 topic, "板主 ID : ");
	do {
	    if (!getdata(6, 0, "請輸入板主ID：", topic, IDLEN + 1, DOECHO))
		return FULLUPDATE;
	    else if (!userid_is_BM(topic, bcache[i - 1].BM))
		outs("不是該板的板主");
	    else
		break;
	} while (temp > 0);
	strcat(genbuf, topic);
	strcat(genbuf, "\n罷免原因: \n");
	outs("請輸入罷免原因(至多五行)，要清楚填寫不然不會核准喔");
	for (i = 8; i < 13; i++) {
	    if (!getdata(i, 0, "：", topic, 60, DOECHO))
		break;
	    strcat(genbuf, topic);
	    strcat(genbuf, "\n");
	}
	if (i == 8)
	    return FULLUPDATE;
	break;
    case 5:
	if (!getdata(4, 0, "請輸入小組中英文名稱：", topic, 30, DOECHO))
	    return FULLUPDATE;
	snprintf(title, sizeof(title), "[連署小組長] %s", topic);
	snprintf(genbuf, sizeof(genbuf),
		 "%s\n\n%s%s\n%s%s", "連署小組長", "小組名稱: ",
		 topic, "申請 ID : ", cuser.userid);
	strcat(genbuf, "\n申請政見: \n");
	outs("請輸入申請政見(至多五行)，要清楚填寫不然不會核准喔");
	for (i = 8; i < 13; i++) {
	    if (!getdata(i, 0, "：", topic, 60, DOECHO))
		break;
	    strcat(genbuf, topic);
	    strcat(genbuf, "\n");
	}
	if (i == 8)
	    return FULLUPDATE;
	break;
    case 6:

	if (!getdata(4, 0, "請輸入小組中英文名稱：", topic, 30, DOECHO))
	    return FULLUPDATE;
	snprintf(title, sizeof(title), "[罷免小組長] %s", topic);
	snprintf(genbuf, sizeof(genbuf), "%s\n\n%s%s\n%s",
		 "罷免小組長", "小組名稱: ", topic, "小組長 ID : ");
	if (!getdata(6, 0, "請輸入小組長ID：", topic, IDLEN + 1, DOECHO))
	    return FULLUPDATE;
	strcat(genbuf, topic);
	strcat(genbuf, "\n罷免原因: \n");
	outs("請輸入罷免原因(至多五行)，要清楚填寫不然不會核准喔");
	for (i = 8; i < 13; i++) {
	    if (!getdata(i, 0, "：", topic, 60, DOECHO))
		break;
	    strcat(genbuf, topic);
	    strcat(genbuf, "\n");
	}
	if (i == 8)
	    return FULLUPDATE;
	break;
    case 7:
	if (!HAS_PERM(PERM_SYSOP))
	    return FULLUPDATE;
	if (!getdata(4, 0, "請輸入公投主題：", topic, 30, DOECHO))
	    return FULLUPDATE;
	snprintf(title, sizeof(title), "%s %s", "[站民公投]", topic);
	snprintf(genbuf, sizeof(genbuf),
		 "%s\n\n%s%s\n", "站民公投", "公投主題: ", topic);
	strcat(genbuf, "\n公投原因: \n");
	outs("請輸入公投原因(至多五行)，要清楚填寫不然不會核准喔");
	for (i = 8; i < 13; i++) {
	    if (!getdata(i, 0, "：", topic, 60, DOECHO))
		break;
	    strcat(genbuf, topic);
	    strcat(genbuf, "\n");
	}
	if (i == 8)
	    return FULLUPDATE;
	break;
    case 8:
	if (!getdata(4, 0, "請輸入群組中英文名稱：", topic, 30, DOECHO))
	    return FULLUPDATE;
	snprintf(title, sizeof(title), "[申請新群組] %s", topic);
	snprintf(genbuf, sizeof(genbuf), "%s\n\n%s%s\n%s%s",
		 "申請群組", "群組名稱: ", topic, "申請 ID : ", cuser.userid);
	strcat(genbuf, "\n申請政見: \n");
	outs("請輸入申請政見(至多五行)，要清楚填寫不然不會核准喔");
	for (i = 8; i < 13; i++) {
	    if (!getdata(i, 0, "：", topic, 60, DOECHO))
		break;
	    strcat(genbuf, topic);
	    strcat(genbuf, "\n");
	}
	if (i == 8)
	    return FULLUPDATE;
	break;
    default:
	return FULLUPDATE;
    }
    strcat(genbuf, "連署結束時間: ");
    now += 14 * 24 * 60 * 60;
    snprintf(topic, sizeof(topic), "(%ld)", now);
    strcat(genbuf, topic);
    strcat(genbuf, ctime(&now));
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
    fprintf(fp, "%s%s %s%s\n%s%s\n%s%s", "作者: ", cuser.userid,
	    "看板: ", currboard,
	    "標題: ", title,
	    "時間: ", ctime(&now));
    fprintf(fp, "%s\n", genbuf);
    fclose(fp);
    strlcpy(votefile.owner, cuser.userid, sizeof(votefile.owner));
    strlcpy(votefile.title, title, sizeof(votefile.title));
    setbdir(genbuf, currboard);
    if (append_record(genbuf, &votefile, sizeof(votefile)) != -1)
	setbtotal(currbid);
    do_voteboardreply(&votefile);
    return FULLUPDATE;
}
