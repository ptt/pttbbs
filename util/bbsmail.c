/* $Id$ */
#define _UTIL_C_
#include "bbs.h"

#define	LOG_FILE	(BBSHOME "/etc/mailog")

#ifdef HMM_USE_ANTI_SPAM
extern char *notitle[], *nofrom[], *nocont[];
#endif


int
strip_ansi(char *buf, const char *str, int mode)
{
    register int    ansi, count = 0;

    for (ansi = 0; *str /* && *str != '\n' */ ; str++) {
	if (*str == 27) {
	    if (mode) {
		if (buf)
		    *buf++ = *str;
		count++;
	    }
	    ansi = 1;
	} else if (ansi && strchr("[;1234567890mfHABCDnsuJKc=n", *str)) {
	    if ((mode == NO_RELOAD && !strchr("c=n", *str)) ||
		(mode == ONLY_COLOR && strchr("[;1234567890m", *str))) {
		if (buf)
		    *buf++ = *str;
		count++;
	    }
	    if (strchr("mHn ", *str))
		ansi = 0;
	} else {
	    ansi = 0;
	    if (buf)
		*buf++ = *str;
	    count++;
	}
    }
    if (buf)
	*buf = '\0';
    return count;
}

int mailalertuid(int tuid)
{
    userinfo_t *uentp=NULL;
    if(tuid>0 && (uentp = (userinfo_t *)search_ulist(tuid)) )
         uentp->alerts|=ALERT_NEW_MAIL;
    return 0;
}      

void
mailog(msg)
    char *msg;
{
    FILE *fp;

    if ((fp = fopen(LOG_FILE, "a")))
    {
	time_t now;
	struct tm *p;

	time(&now);
	p = localtime(&now);
	fprintf(fp, "%02d/%02d/%02d %02d:%02d:%02d <bbsmail> %s\n",
		p->tm_year % 100, p->tm_mon + 1, p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec,
		msg);
	fclose(fp);
    }
}

#ifdef USE_ICONV
void str_decode_M3(unsigned char *str);
#endif

int mail2bbs(char *userid)
{
    int     uid;
    fileheader_t mymail;
    char genbuf[512], title[512], sender[512], filename[512], *ip, *ptr;
    time_t tmp_time;
    struct stat st;
    FILE *fout;
    userec_t xuser;

    /* check if the userid is in our bbs now */
    if( !(uid = getuser(userid, &xuser)) ){
	sprintf(genbuf, "BBS user <%s> not existed", userid);
	puts(genbuf);
	mailog(genbuf);
	return -1;//EX_NOUSER;
    }

    if( xuser.uflag2 & REJ_OUTTAMAIL )
	return -1; //不接受站外信

    sprintf(filename, BBSHOME "/home/%c/%s", xuser.userid[0], xuser.userid);

    if( stat(filename, &st) == -1 ){
	if( mkdir(filename, 0755) == -1 ){
	    printf("mail box create error %s \n", filename);
	    return -1;
	}
    }
    else if( !(st.st_mode & S_IFDIR) ){
	printf("mail box error\n");
	return -1;
    }

/* parse header */
    while( fgets(genbuf, sizeof(genbuf), stdin) ){
	if( genbuf[0] == '\n' )
	    break;
	if( strncmp(genbuf, "Subject: ", 9) == 0 ){
	    strlcpy(title, genbuf + 9, sizeof(title));
#ifdef USE_ICONV
	    str_decode_M3(title);
#endif
	    continue;
	}
	if( strncmp(genbuf, "Content-Type:", 13) == 0 ){
	    if( strstr(genbuf, "multipart") && !strstr(genbuf, "report") )
		return -1;
	}
	if( strncmp(genbuf, "From", 4) == 0 ){
	    if( (ip = strchr(genbuf, '<')) && (ptr = strrchr(ip, '>')) ){
		*ptr = '\0';
		if (ip[-1] == ' ')
		    ip[-1] = '\0';
		ptr = (char *) strchr(genbuf, ' ');
		if( ptr )
		    while (*ptr == ' ')
			ptr++;
		else
		    ptr = "unknown";
		sprintf(sender, "%s (%s)", ip + 1, ptr);
	    }
	    else{
		strtok(genbuf, " \t\n\r");
		ptr = strtok(NULL, " \t\n\r");
		if(ptr)
	   	 strlcpy(sender, ptr, sizeof(sender));
	    }
	    continue;
	}
    }

    if( (ptr = strchr(sender, '\n')) )
	*ptr = '\0';

    if( (ptr = strchr(title, '\n')) )
	*ptr = '\0';

    if( strchr(sender, '@') == NULL )	/* 由 local host 寄信 */
	strcat(sender, "@" MYHOSTNAME);

/* allocate a file for the new mail */
    stampfile(filename, &mymail);

#ifdef HMM_USE_ANTI_SPAM
    for (n = 0; notitle[n]; n++)
	if (strstr(title, notitle[n]))
	{
	    sprintf(genbuf, "Title <%s> not accepted", title);
	    puts(genbuf);
	    mailog(genbuf);
	    return -1;
	}	
    for (n = 0; nofrom[n]; n++)
	if (strstr(sender, nofrom[n]))
	{
	    sprintf(genbuf, "From <%s> not accepted", sender);
	    puts(genbuf);
	    mailog(genbuf);
	    return -1;
	}	
#endif

    if ((fout = fopen(filename, "w")) == NULL)
    {
	printf("Cannot open %s\n", filename);
	return -1;
    }

    if (!title[0])
	sprintf(title, "來自 %.64s", sender);
    title[TTLEN] = 0;
    time(&tmp_time);
    fprintf(fout, "作者: %s\n標題: %s\n時間: %s\n",
	    sender, title, ctime(&tmp_time));

/* copy the stdin to the specified file */
    while (fgets(genbuf, 255, stdin))
    {
#ifdef HMM_USE_ANTI_SPAM
	for (n = 0; nocont[n]; n++)
	    if (strstr(genbuf, nocont[n]))
	    {
		fclose(fout);
		unlink(filename);
		sprintf(genbuf, "Content <%s> not accepted", nocont[n]);
		puts(genbuf);
		mailog(genbuf);
		return -1;
	    }
#endif
	fputs(genbuf, fout);
    }
    fclose(fout);

    sprintf(genbuf, "%s => %s", sender, xuser.userid);
    mailog(genbuf);

/* append the record to the MAIL control file */
    strip_ansi(title, title, 0);
    strlcpy(mymail.title, title, sizeof(mymail.title));

    if (strtok(sender, " .@\t\n\r"))
	strcat(sender, ".");
    sender[IDLEN + 1] = '\0';
    strlcpy(mymail.owner, sender, sizeof(mymail.owner));

    sprintf(genbuf, BBSHOME "/home/%c/%s/.DIR", xuser.userid[0], xuser.userid);
    mailalertuid(uid);
    return append_record(genbuf, &mymail, sizeof(mymail));
}


int
main(int argc, char* argv[])
{
    char receiver[512];

/* argv[1] is userid in bbs   */

    if (argc < 2){
	printf("Usage:\t%s <bbs_uid>\n", argv[0]);
	exit(-1);
    }
    (void) setgid(BBSGID);
    (void) setuid(BBSUID);
    attach_SHM();
    if( passwd_init() )
	return 0;
    chdir(BBSHOME);

    strlcpy(receiver, argv[1], sizeof(receiver));

    strtok(receiver,".");
    if (mail2bbs(receiver)){
	/* eat mail queue */
	while (fgets(receiver, sizeof(receiver), stdin))
	    ;
    }
    return 0;
}
