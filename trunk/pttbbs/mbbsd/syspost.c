/* $Id: syspost.c,v 1.6 2002/05/13 03:20:04 ptt Exp $ */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include "config.h"
#include "pttstruct.h"
#include "perm.h"
#include "common.h"
#include "proto.h"

extern char *str_permid[];
extern userec_t cuser;
extern time_t now;
void post_change_perm(int oldperm, int newperm, char *sysopid, char *userid) {
    FILE *fp;
    fileheader_t fhdr;
    char genbuf[200], reason[30];
    int i, flag=0;
    
    strcpy(genbuf, "boards/S/Security");
    stampfile(genbuf, &fhdr);
    if(!(fp = fopen(genbuf,"w")))
	return;
    
    fprintf(fp, "作者: [系統安全局] 看板: Security\n"
	    "標題: [公安報告] 站長修改權限報告\n"
	    "時間: %s\n", ctime(&now));
    for(i = 5; i < NUMPERMS; i++) {
        if(((oldperm >> i) & 1) != ((newperm >> i) & 1)) {
            fprintf (fp, "   站長\033[1;32m%s%s%s%s\033[m的權限\n",
		     sysopid,
		     (((oldperm >> i) & 1) ? "\033[1;33m關閉":"\033[1;33m開啟"),
		     userid, str_permid[i]);
            flag++;
        }
    }
    
    if(flag) {
	clrtobot();
	clear();
	while(!getdata_str(5, 0, "請輸入理由以示負責：",
			   reason, sizeof(reason), DOECHO, "看版版主:"));
	fprintf(fp, "\n   \033[1;37m站長%s修改權限理由是：%s\033[m",
		cuser.userid, reason);
	fclose(fp);
	
	sprintf(fhdr.title, "[公安報告] 站長%s修改%s權限報告",
		cuser.userid, userid);
	strcpy(fhdr.owner, "[系統安全局]");
	append_record("boards/S/Security/.DIR", &fhdr, sizeof(fhdr));
    }
}

void post_violatelaw(char* crime, char* police, char* reason, char* result){
    char genbuf[200];
    fileheader_t fhdr;
    FILE *fp;            
    strcpy(genbuf, "boards/S/Security");
    stampfile(genbuf, &fhdr);
    if(!(fp = fopen(genbuf,"w")))
        return;
    fprintf(fp, "作者: [Ptt法院] 看板: Security\n"
	    "標題: [報告] %-20s 違法判決報告\n"
	    "時間: %s\n"
	    "\033[1;32m%s\033[m判決：\n     \033[1;32m%s\033[m"
	    "因\033[1;35m%s\033[m行為，\n違反本站站規，處以\033[1;35m%s\033[m，特此公告",
	    crime, ctime(&now), police, crime, reason, result);
    fclose(fp);
    sprintf(fhdr.title, "[報告] %-20s 違法判決報告", crime);
    strcpy(fhdr.owner, "[Ptt法院]");
    append_record("boards/S/Security/.DIR", &fhdr, sizeof(fhdr));
    
    strcpy(genbuf, "boards/V/ViolateLaw");
    stampfile(genbuf, &fhdr);
    if(!(fp = fopen(genbuf,"w")))
        return;
    fprintf(fp, "作者: [Ptt法院] 看板: ViolateLaw\n"
	    "標題: [報告] %-20s 違法判決報告\n"
	    "時間: %s\n"
	    "\033[1;32m%s\033[m判決：\n     \033[1;32m%s\033[m"
	    "因\033[1;35m%s\033[m行為，\n違反本站站規，處以\033[1;35m%s\033[m，特此公告",
	    crime, ctime(&now), police, crime, reason, result);
    fclose(fp);
    sprintf(fhdr.title, "[報告] %-20s 違法判決報告", crime);
    strcpy(fhdr.owner, "[Ptt法院]");
    
    append_record("boards/V/ViolateLaw/.DIR", &fhdr, sizeof(fhdr));
                                 
}

void post_newboard(char* bgroup, char* bname, char* bms){
    char genbuf[256], title[128];
    sprintf(title, "[新版成立] %s", bname);
    sprintf(genbuf, "%s 開了一個新版 %s : %s\n\n新任版主為 %s\n\n恭喜*^_^*\n",
	     cuser.userid, bname, bgroup, bms);
    post_msg("Record", title,  genbuf, "[系統]");  
}

