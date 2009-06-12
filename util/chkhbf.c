/* $Id$ */
#define _UTIL_C_
#include "bbs.h"

struct {
    char    userid[IDLEN + 1];
    time_t  lastlogin, expire;
} explist[MAX_FRIEND];

void usage(void)
{
    fprintf(stderr, "通知隱板板主是否有板友過期/已經過期\n"
	    "usage: chkhbf [-a] [board name [board name]]\n");
}

int mailalertuid(int tuid)
{
    userinfo_t *uentp=NULL;
    if(tuid>0 && (uentp = (userinfo_t *)search_ulist(tuid)) )
	    uentp->alerts |=ALERT_NEW_MAIL;
    return 0;
}      

char *CTIMEx(char *buf, time_t t)
{
    strcpy(buf, ctime(&t));
    buf[strlen(buf) - 1] = 0;
    return buf;
}

void informBM(char *userid, boardheader_t *bptr, int nEXP)
{
    int     uid, i;
    char    filename[256], buf[64];
    fileheader_t mymail;
    FILE    *fp;
    if( !(uid = searchuser(userid, userid)) )
	return;
    sprintf(filename, BBSHOME "/home/%c/%s", userid[0], userid);
    stampfile(filename, &mymail);
    if( (fp = fopen(filename, "w")) == NULL )
	return;

    printf("brdname: %s, BM: %s\n", bptr->brdname, userid);
    fprintf(fp,
	    "作者: 系統通知.\n"
	    "標題: 警告: 貴板板友即將過期/已經過期\n"
	    "時間: %s\n"
	    " %s 的板主您好: \n"
	    "    下列貴板板友即將過期或已經過期:\n"
	    "------------------------------------------------------------\n",
	    CTIMEx(buf, now), bptr->brdname);
    for( i = 0 ; i < nEXP ; ++i )
	if( explist[i].expire == -1 )
	    fprintf(fp, "%-15s  已經過期\n", explist[i].userid);
	else
	    fprintf(fp, "%-15s  即將在 %s 過期\n",
		    explist[i].userid, CTIMEx(buf, explist[i].expire));
    fprintf(fp,
	    "------------------------------------------------------------\n"
	    "說明:\n"
	    "    為了節省系統資源, 系統將自動清除掉超過四個月未上站\n"
	    "的使用者. 此時若有某位您不認識的使用者恰好註冊了該帳號,\n"
	    "將會視為貴板板友而放行進入.\n"
	    "    建議您暫時將這些使用者自貴板的板友名單中移除.\n"
	    "\n"
	    "    這封信件是由程式自動發出, 請不要直接回覆這封信. 若\n"
	    "您有相關問題請麻煩至看板 SYSOP, 或是直接與看板總管聯繫. :)\n"
	    "\n"
	    BBSNAME "站長群敬上"
	    );
    fclose(fp);

    strcpy(mymail.title, "警告: 貴板板友即將過期/已經過期");
    strcpy(mymail.owner, "系統通知.");
    sprintf(filename, BBSHOME "/home/%c/%s/.DIR", userid[0], userid);
    mailalertuid(uid);
    append_record(filename, &mymail, sizeof(mymail));
}

void chkhbf(boardheader_t *bptr)
{
    char    fn[256], chkuser[256];
    int     i, nEXP = 0;
    FILE    *fp;
    userec_t xuser;

    sprintf(fn, "boards/%c/%s/visable", bptr->brdname[0], bptr->brdname);
    if( (fp = fopen(fn, "rt")) == NULL )
	return;
    while( fgets(chkuser, sizeof(chkuser), fp) != NULL ){
	for( i = 0 ; chkuser[i] != 0 && i < sizeof(chkuser) ; ++i )
	    if( !isalnum(chkuser[i]) ){
		chkuser[i] = 0;
		break;
	    }
	if( passwd_load_user(chkuser, &xuser) < 1 || strcasecmp(chkuser, STR_GUEST) == 0 ){
	    strcpy(explist[nEXP].userid, chkuser);
	    explist[nEXP].expire = -1;
	    ++nEXP;
	}
	else if( (xuser.lastlogin < now - 86400 * 90) &&
		 !(xuser.userlevel & PERM_XEMPT) ){
	    strcpy(explist[nEXP].userid, chkuser);
	    explist[nEXP].lastlogin = xuser.lastlogin;
	    explist[nEXP].expire = xuser.lastlogin + 86400 * 120;
	    ++nEXP;
	}
    }
    fclose(fp);
    if( nEXP ){
	char    BM[IDLEN * 3 + 3], *p;
	strlcpy(BM, bptr->BM, sizeof(BM));
	for( p = BM ; *p != 0 ; ++p )
	    if( !isalpha(*p) && !isdigit(*p) )
		*p = ' ';
	for( i = 0, p = strtok(BM, " ") ; p != NULL ;
	     ++i, p = strtok(NULL, " ") )
	    informBM(p, bptr, nEXP);
    }
}

int main(int argc, char **argv)
{
    int     ch, allboards = 0, i;
    boardheader_t  *bptr;
    while( (ch = getopt(argc, argv, "ah")) != -1 )
	switch( ch ){
	case 'a':
	    allboards = 1;
	    break;
	case 'h':
	    usage();
	    return 0;
	}

    chdir(BBSHOME);
    attach_SHM();
    argc -= optind;
    argv += optind;
    now = time(NULL);
    if( allboards ){
	for( i = 0 ; i < MAX_BOARD ; ++i ){
	    bptr = &bcache[i];
	    if( bptr->brdname[0] &&
		!(bptr->brdattr & (BRD_TOP | BRD_GROUPBOARD)) &&
		bptr->brdattr & BRD_HIDE )
		chkhbf(bptr);
	}
    }
    else if( argc > 0 ){
	int     bid;
	for( i = 0 ; i < argc ; ++i )
	    if( (bid = getbnum(argv[i])) != 0 ) // XXX: bid start 1
		chkhbf(getbcache(bid));
	    else
		fprintf(stderr, "%s not found\n", argv[i]);
    }
    else
	usage();

    return 0;
}
