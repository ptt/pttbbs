/* $Id$ */
#include "bbs.h"

int
post_msg(char *bname, char *title, char *msg, char *author)
{
    FILE           *fp;
    int             bid;
    fileheader_t    fhdr;
    char            genbuf[256];

    /* 在 bname 板發表新文章 */
    snprintf(genbuf, sizeof(genbuf), "boards/%c/%s", bname[0], bname);
    stampfile(genbuf, &fhdr);
    fp = fopen(genbuf, "w");

    if (!fp)
	return -1;

    fprintf(fp, "作者: %s 看板: %s\n標題: %s \n", author, bname, title);
    fprintf(fp, "時間: %s\n", ctime(&now));

    /* 文章的內容 */
    fputs(msg, fp);
    fclose(fp);

    /* 將檔案加入列表 */
    strlcpy(fhdr.title, title, sizeof(fhdr.title));
    strlcpy(fhdr.owner, author, sizeof(fhdr.owner));
    setbdir(genbuf, bname);
    if (append_record(genbuf, &fhdr, sizeof(fhdr)) != -1)
	if ((bid = getbnum(bname)) > 0)
	    setbtotal(bid);
    return 0;
}

int
post_file(char *bname, char *title, char *filename, char *author)
{
    int             size = dashs(filename);
    char           *msg;
    FILE           *fp;

    if (size <= 0)
	return -1;
    if (!(fp = fopen(filename, "r")))
	return -1;
    msg = (char *)malloc(size + 1);
    size = fread(msg, 1, size, fp);
    msg[size] = 0;
    size = post_msg(bname, title, msg, author);
    fclose(fp);
    free(msg);
    return size;
}

void
post_change_perm(int oldperm, int newperm, char *sysopid, char *userid)
{
    FILE           *fp;
    fileheader_t    fhdr;
    char            genbuf[200], reason[30];
    int             i, flag = 0;

    strlcpy(genbuf, "boards/S/Security", sizeof(genbuf));
    stampfile(genbuf, &fhdr);
    if (!(fp = fopen(genbuf, "w")))
	return;

    fprintf(fp, "作者: [系統安全局] 看板: Security\n"
	    "標題: [公安報告] 站長修改權限報告\n"
	    "時間: %s\n", ctime(&now));
    for (i = 5; i < NUMPERMS; i++) {
	if (((oldperm >> i) & 1) != ((newperm >> i) & 1)) {
	    fprintf(fp, "   站長\033[1;32m%s%s%s%s\033[m的權限\n",
		    sysopid,
	       (((oldperm >> i) & 1) ? "\033[1;33m關閉" : "\033[1;33m開啟"),
		    userid, str_permid[i]);
	    flag++;
	}
    }

    if (flag) {
	clrtobot();
	clear();
	while (!getdata_str(5, 0, "請輸入理由以示負責：",
			    reason, sizeof(reason), DOECHO, "看板板主:"));
	fprintf(fp, "\n   \033[1;37m站長%s修改權限理由是：%s\033[m",
		cuser.userid, reason);
	fclose(fp);

	snprintf(fhdr.title, sizeof(fhdr.title),
		 "[公安報告] 站長%s修改%s權限報告",
		 cuser.userid, userid);
	strlcpy(fhdr.owner, "[系統安全局]", sizeof(fhdr.owner));
	append_record("boards/S/Security/.DIR", &fhdr, sizeof(fhdr));
    } else
	fclose(fp);
}

void
post_violatelaw(char *crime, char *police, char *reason, char *result)
{
    char            genbuf[200];
    fileheader_t    fhdr;
    FILE           *fp;
/*
    strlcpy(genbuf, "boards/S/Security", sizeof(genbuf));
    stampfile(genbuf, &fhdr);
    if (!(fp = fopen(genbuf, "w")))
	return;
    fprintf(fp, "作者: [Ptt法院] 看板: Security\n"
	    "標題: [報告] %-20s 違法判決報告\n"
	    "時間: %s\n"
	    "\033[1;32m%s\033[m判決：\n     \033[1;32m%s\033[m"
	    "因\033[1;35m%s\033[m行為，\n違反本站站規，處以\033[1;35m%s\033[m，特此公告",
	    crime, ctime(&now), police, crime, reason, result);
    fclose(fp);
    snprintf(fhdr.title, sizeof(fhdr.title),
	     "[報告] %-20s 違法判決報告", crime);
    strlcpy(fhdr.owner, "[Ptt法院]", sizeof(fhdr.owner));
    append_record("boards/S/Security/.DIR", &fhdr, sizeof(fhdr));

*/
    strlcpy(genbuf, "boards/V/ViolateLaw", sizeof(genbuf));
    stampfile(genbuf, &fhdr);
    if (!(fp = fopen(genbuf, "w")))
	return;
    fprintf(fp, "作者: [Ptt法院] 看板: ViolateLaw\n"
	    "標題: [報告] %-20s 違法判決報告\n"
	    "時間: %s\n"
	    "\033[1;32m%s\033[m判決：\n     \033[1;32m%s\033[m"
	    "因\033[1;35m%s\033[m行為，\n違反本站站規，處以\033[1;35m%s\033[m，特此公告",
	    crime, ctime(&now), police, crime, reason, result);
    fclose(fp);
    snprintf(fhdr.title, sizeof(fhdr.title),
	     "[報告] %s: %s 判決報告", reason, crime);
    strlcpy(fhdr.owner, "[Ptt法院]", sizeof(fhdr.owner));

    append_record("boards/V/ViolateLaw/.DIR", &fhdr, sizeof(fhdr));

}

void
post_newboard(char *bgroup, char *bname, char *bms)
{
    char            genbuf[256], title[128];
    snprintf(title, sizeof(title), "[新板成立] %s", bname);
    snprintf(genbuf, sizeof(genbuf),
	     "%s 開了一個新板 %s : %s\n\n新任板主為 %s\n\n恭喜*^_^*\n",
	     cuser.userid, bname, bgroup, bms);
    post_msg("Record", title, genbuf, "[系統]");
}

void
give_money_post(char *userid, int money)
{
    FILE           *fp;
    fileheader_t    fhdr;
    time_t          now = time(0);
    char            genbuf[200];

    strlcpy(genbuf, "boards/S/Security", sizeof(genbuf));
    stampfile(genbuf, &fhdr);
    if (!(fp = fopen(genbuf, "w")))
	return;
    fprintf(fp, "作者: [系統安全局] 看板: Security\n"
	    "標題: [公安報告] 站長%s使用紅包機報告\n"
	    "時間: %s\n", cuser.userid, ctime(&now));
    clrtobot();
    clear();
    fprintf(fp, "\n   站長\033[1;32m%s\033[m給\033[1;33m%s %d 元\033[m",
	    cuser.userid, userid, money);

    fclose(fp);
    snprintf(fhdr.title, sizeof(fhdr.title), "[公安報告] 站長%s使用紅包機報告",
	     cuser.userid);
    strlcpy(fhdr.owner, "[系統安全局]", sizeof(fhdr.owner));
    append_record("boards/S/Security/.DIR", &fhdr, sizeof(fhdr));
}
