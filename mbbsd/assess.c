/* $Id$ */
#include "bbs.h"

#ifdef ASSESS

/* do (*num) + n, n is integer. */
inline static void inc(unsigned char *num, int n)
{
    if (n >= 0 && UCHAR_MAX - *num <= n)
	(*num) = UCHAR_MAX;
    else if (n < 0 && *num < -n)
	(*num) = 0;
    else
	(*num) += n;
}

#define modify_column(_attr) \
int inc_##_attr(const char *userid, int num) \
{ \
    userec_t xuser; \
    int uid = getuser(userid, &xuser);\
    if( uid > 0 ){ \
	inc(&xuser._attr, num); \
	passwd_sync_update(uid, &xuser); \
	return xuser._attr; }\
    return 0;\
}

modify_column(badpost);  /* inc_badpost */

static char * const badpost_reason[] = {
    "廣告", "不當用辭", "人身攻擊"
};

#define DIM(x)	(sizeof(x)/sizeof(x[0]))

int assign_badpost(const char *userid, fileheader_t *fhdr, 
	const char *newpath, const char *comment)
{
    char genbuf[STRLEN];
    char rptpath[PATHLEN];
    int i, tusernum = searchuser(userid, NULL);

    strcpy(rptpath, newpath);
    assert(tusernum > 0 && tusernum < MAX_USERS);
    move(b_lines - 2, 0);
    clrtobot();
    for (i = 0; i < DIM(badpost_reason); i++)
	prints("%d.%s ", i + 1, badpost_reason[i]);

    prints("%d.%s", i + 1, "其他");
    getdata(b_lines - 1, 0, "請選擇[0:取消劣文]:", genbuf, 3, LCECHO);
    i = genbuf[0] - '1';
    if (i < 0 || i > DIM(badpost_reason))
    {
	vmsg("取消設定劣文。");
	return -1;
    }

    if (i < DIM(badpost_reason))
	sprintf(genbuf,"劣%s文退回(%s)", comment ? "推" : "", badpost_reason[i]);
    else if(i==DIM(badpost_reason))
    {
	char *s = genbuf;
	strcpy(genbuf, comment ? "劣推文退回(" : "劣文退回(");
	s += strlen(genbuf);
	getdata_buf(b_lines, 0, "請輸入原因", s, 50, DOECHO);
	// 對於 comment 目前可以重來，但非comment 文直接刪掉所以沒法 cancel
	if (!*s && comment)
	{
	    vmsg("取消設定劣文。");
	    return -1;
	}
	strcat(genbuf,")");
    }

    assert(i >= 0 && i <= DIM(badpost_reason));
    if (fhdr) strncat(genbuf, fhdr->title, 64-strlen(genbuf)); 

#ifdef USE_COOLDOWN
    add_cooldowntime(tusernum, 60);
    add_posttimes(tusernum, 15); //Ptt: 凍結 post for 1 hour
#endif

    if (!(inc_badpost(userid, 1) % 5)){
	userec_t xuser;
	post_violatelaw(userid, BBSMNAME " 系統警察", 
		"劣文累計 5 篇", "罰單一張");
	mail_violatelaw(userid, BBSMNAME " 系統警察", 
		"劣文累計 5 篇", "罰單一張");
	kick_all(userid);
	passwd_sync_query(tusernum, &xuser);
	xuser.money = moneyof(tusernum);
	xuser.vl_count++;
	xuser.userlevel |= PERM_VIOLATELAW;
	xuser.timeviolatelaw = now;  
	passwd_sync_update(tusernum, &xuser);
    }

#ifdef BAD_POST_RECORD
    // we also change rptpath because such record contains more information
    {
	int rpt_bid = getbnum(BAD_POST_RECORD);
	if (rpt_bid > 0) {
	    fileheader_t report_fh;
	    char rptdir[PATHLEN];
	    FILE *fp;

	    setbpath(rptpath, BAD_POST_RECORD);
	    stampfile(rptpath, &report_fh);

	    strcpy(report_fh.owner, "[" BBSMNAME "警察局]");
	    snprintf(report_fh.title, sizeof(report_fh.title),
		    "%s 板 %s 板主給予 %s 一篇劣%s文",
		    currboard, cuser.userid, userid, comment ? "推" : "");
	    Copy(newpath, rptpath);
	    fp = fopen(rptpath, "at");

	    if (fp)
	    {
		fprintf(fp, "\n劣文原因: %s\n", genbuf);
		if (comment)
		    fprintf(fp, "\n被劣推文項目:\n%s", comment);
		fprintf(fp, "\n");
		fclose(fp);
	    }

	    setbdir(rptdir, BAD_POST_RECORD);
	    append_record(rptdir, &report_fh, sizeof(report_fh));

	    touchbtotal(rpt_bid);
	}
    }
#endif /* defined(BAD_POST_RECORD) */

    sendalert(userid,  ALERT_PWD_PERM);
    mail_id(userid, genbuf, rptpath, cuser.userid);

    return 0;
}

// 推文目前的設計一整個就是無法管理，
// 不過在重寫整套短文回應系統前也是沒辦法的事，
// 先寫個應應急的吧
int
bad_comment(const char *fn)
{
    FILE *fp = NULL;
    char buf[ANSILINELEN];
    char uid[IDLEN+1];
    int done = 0;
    int i = 0, c;

    vs_hdr("劣推文");
    usercomplete("請輸入要劣推文的 ID: ", uid);
    if (!*uid)
	return -1;

    fp = fopen(fn, "rt");
    if (!fp)
	return -1;

    vs_hdr2(" 劣推文 ", uid);
    // search file for it
    while (fgets(buf, sizeof(buf), fp) && *buf)
    {
	if (strstr(buf, uid) == NULL)
	    continue;
	if (strstr(buf, ANSI_COLOR(33)) == NULL)
	    continue;

	// found something, let's show it
	move(2, 0); clrtobot();
	prints("第 %d 項推文:\n", ++i);
	outs(buf);

	move (5, 0);
	outs("請問是要劣這個推文嗎？(Y:確定,N:找下個,Q:離開) [y/N/q]: ");
	c = vkey();
	if (isascii(c)) c = tolower(c);
	if (c == 'q')
	    break;
	if (c != 'y')
	    continue;

	if (assign_badpost(uid, NULL, fn, buf) != 0)
	    continue;

	done = 1;
	vmsg("已劣推文。");
	break;
    }
    fclose(fp);

    if (!done)
    {
	vmsgf("找不到其它「%s」的推文了", uid);
	return -1;
    }

    return 0;
}

#endif // ASSESS
