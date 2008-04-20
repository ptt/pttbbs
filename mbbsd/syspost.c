/* $Id$ */
#include "bbs.h"

int 
post_msg_fpath(const char* bname, const char* title, const char *msg, const char* author, char *fname)
{
    FILE           *fp;
    int             bid;
    fileheader_t    fhdr;
    char	    dirfn[PATHLEN];

    // fname should be lengthed in PATHLEN
    assert(fname);

    /* 在 bname 板發表新文章 */
    setbpath(fname, bname);
    stampfile(fname, &fhdr);
    fp = fopen(fname, "w");

    if (!fp)
	return -1;

    fprintf(fp, "作者: %s 看板: %s\n標題: %s \n", author, bname, title);
    fprintf(fp, "時間: %s\n", ctime4(&now));

    /* 文章的內容 */
    fputs(msg, fp);
    fclose(fp);

    /* 將檔案加入列表 */
    strlcpy(fhdr.title, title, sizeof(fhdr.title));
    strlcpy(fhdr.owner, author, sizeof(fhdr.owner));
    setbdir(dirfn, bname);
    if (append_record(dirfn, &fhdr, sizeof(fhdr)) != -1)
	if ((bid = getbnum(bname)) > 0)
	    setbtotal(bid);
    return 0;
}

int 
post_msg(const char* bname, const char* title, const char *msg, const char* author)
{
    char fname[PATHLEN];
    return post_msg_fpath(bname, title, msg, author, fname);
}

int
post_file(const char *bname, const char *title, const char *filename, const char *author)
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
post_change_perm(int oldperm, int newperm, const char *sysopid, const char *userid)
{
    char genbuf[(NUMPERMS+1) * STRLEN*2] = "", reason[30] = "";
    char title[TTLEN+1];
    char *s = genbuf;
    int  i, flag = 0;

    // generate log (warning: each line should <= STRLEN*2)
    for (i = 0; i < NUMPERMS; i++) {
	if (((oldperm >> i) & 1) != ((newperm >> i) & 1)) {
	    sprintf(s, "   站長" ANSI_COLOR(1;32) "%s%s%s%s" ANSI_RESET "的權限\n",
		    sysopid,
	       (((oldperm >> i) & 1) ? ANSI_COLOR(1;33) "關閉" : ANSI_COLOR(1;33) "開啟"),
		    userid, str_permid[i]);
	    s += strlen(s);
	    flag++;
	}
    }

    if (!flag) // nothing changed
	return;

    clrtobot();
    clear();
    while (!getdata(5, 0, "請輸入理由以示負責：",
		reason, sizeof(reason), DOECHO));
    sprintf(s, "\n   " ANSI_COLOR(1;37) "站長%s修改權限理由是：%s\n" ANSI_RESET,
	    cuser.userid, reason);

    snprintf(title, sizeof(title), "[公安報告] 站長%s修改%s權限報告",
	    cuser.userid, userid);

    post_msg(BN_SECURITY, title, genbuf, "[系統安全局]");
}

void
post_violatelaw(const char *crime, const char *police, const char *reason, const char *result)
{
    char title[TTLEN+1];
    char msg[ANSILINELEN];

    snprintf(title, sizeof(title), "[報告] %s:%-*s 判決", crime,
	    (int)(37 - strlen(reason) - strlen(crime)), reason);

    snprintf(msg, sizeof(msg), 
	    ANSI_COLOR(1;32) "%s" ANSI_RESET "判決：\n"
	    "     " ANSI_COLOR(1;32) "%s" ANSI_RESET "因" ANSI_COLOR(1;35) "%s" ANSI_RESET "行為，\n"
	    "違反本站站規，處以" ANSI_COLOR(1;35) "%s" ANSI_RESET "，特此公告\n",
	    police, crime, reason, result);

    if (!strstr(police, "警察")) {
	post_msg("PoliceLog", title, msg, "[" BBSMNAME "法院]");

	snprintf(msg, sizeof(msg), 
		ANSI_COLOR(1;32) "%s" ANSI_RESET "判決：\n"
		"     " ANSI_COLOR(1;32) "%s" ANSI_RESET "因" ANSI_COLOR(1;35) "%s" ANSI_RESET "行為，\n"
		"違反本站站規，處以" ANSI_COLOR(1;35) "%s" ANSI_RESET "，特此公告\n",
		"站務警察", crime, reason, result);
    }

    post_msg("ViolateLaw", title, msg, "[" BBSMNAME "法院]");
}

void
post_newboard(const char *bgroup, const char *bname, const char *bms)
{
    char            genbuf[ANSILINELEN], title[TTLEN+1];

    snprintf(title, sizeof(title), "[新板成立] %s", bname);
    snprintf(genbuf, sizeof(genbuf),
	     "%s 開了一個新板 %s : %s\n\n新任板主為 %s\n\n恭喜*^_^*\n",
	     cuser.userid, bname, bgroup, bms);

    post_msg("Record", title, genbuf, "[系統]");
}

void
post_policelog(const char *bname, const char *atitle, const char *action, const char *reason, const int toggle)
{
    char            genbuf[ANSILINELEN], title[TTLEN+1];

    snprintf(title, sizeof(title), "[%s][%s] %s by %s", action, toggle ? "開啟" : "關閉", bname, cuser.userid);
    snprintf(genbuf, sizeof(genbuf),
	     "%s (%s) %s %s 看板 %s 功\能\n原因 : %s\n%s%s\n",
	     cuser.userid, fromhost, toggle ? "開啟" : "關閉", bname, action,
	     reason, atitle ? "文章標題 : " : "", atitle ? atitle : "");

    post_msg("PoliceLog", title, genbuf, "[系統]");
}
