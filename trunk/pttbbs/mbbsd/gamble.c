/* $Id: gamble.c,v 1.26 2002/07/21 08:18:41 in2 Exp $ */
#include "bbs.h"

#ifndef _BBS_UTIL_C_
#define MAX_ITEM	8	//³Ì¤j ½ä¶µ(item) ­Ó¼Æ
#define MAX_ITEM_LEN	30	//³Ì¤j ¨C¤@½ä¶µ¦W¦rªø«×
#define MAX_SUBJECT_LEN 650	//8*81 = 648 ³Ì¤j ¥DÃDªø«×

static char     betname[MAX_ITEM][MAX_ITEM_LEN];
static int      currbid;

int 
post_msg(char *bname, char *title, char *msg, char *author)
{
    FILE           *fp;
    int             bid;
    fileheader_t    fhdr;
    char            genbuf[256];

    /* ¦b bname ªOµoªí·s¤å³¹ */
    sprintf(genbuf, "boards/%c/%s", bname[0], bname);
    stampfile(genbuf, &fhdr);
    fp = fopen(genbuf, "w");

    if (!fp)
	return -1;

    fprintf(fp, "§@ªÌ: %s ¬ÝªO: %s\n¼ÐÃD: %s \n", author, bname, title);
    fprintf(fp, "®É¶¡: %s\n", ctime(&now));

    /* ¤å³¹ªº¤º®e */
    fprintf(fp, "%s", msg);
    fclose(fp);

    /* ±NÀÉ®×¥[¤J¦Cªí */
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


static int 
load_ticket_record(char *direct, int ticket[])
{
    char            buf[256];
    int             i, total = 0;
    FILE           *fp;
    sprintf(buf, "%s/" FN_TICKET_RECORD, direct);
    if (!(fp = fopen(buf, "r")))
	return 0;
    for (i = 0; i < MAX_ITEM && fscanf(fp, "%9d ", &ticket[i]); i++)
	total = total + ticket[i];
    fclose(fp);
    return total;
}

static int 
show_ticket_data(char *direct, int *price, boardheader_t * bh)
{
    int             i, count, total = 0, end = 0, ticket[MAX_ITEM] = {0, 0, 0, 0, 0, 0, 0, 0};
    FILE           *fp;
    char            genbuf[256], t[25];

    clear();
    if (bh) {
	sprintf(genbuf, "%s ½ä½L", bh->brdname);
	if (bh->endgamble && now < bh->endgamble && bh->endgamble - now < 3600) {
	    sprintf(t, "«Ê½L­Ë¼Æ %d ¬í", bh->endgamble - now);
	    showtitle(genbuf, t);
	} else
	    showtitle(genbuf, BBSNAME);
    } else
	showtitle("Ptt½ä½L", BBSNAME);
    move(2, 0);
    sprintf(genbuf, "%s/" FN_TICKET_ITEMS, direct);
    if (!(fp = fopen(genbuf, "r"))) {
	prints("\n¥Ø«e¨Ã¨S¦³Á|¿ì½ä½L\n");
	sprintf(genbuf, "%s/" FN_TICKET_OUTCOME, direct);
	if (more(genbuf, NA));
	return 0;
    }
    fgets(genbuf, MAX_ITEM_LEN, fp);
    *price = atoi(genbuf);
    for (count = 0; fgets(betname[count], MAX_ITEM_LEN, fp) && count < MAX_ITEM; count++)
	strtok(betname[count], "\r\n");
    fclose(fp);

    prints("\033[32m¯¸³W:\033[m 1.¥iÁÊ¶R¥H¤U¤£¦PÃþ«¬ªº±m²¼¡C¨C±i­nªá \033[32m%d\033[m ¤¸¡C\n"
	   "      2.%s\n"
      "      3.¶}¼ú®É¥u¦³¤@ºØ±m²¼¤¤¼ú, ¦³ÁÊ¶R¸Ó±m²¼ªÌ, «h¥i¨ÌÁÊ¶Rªº±i¼Æ§¡¤À"
	   "Á`½äª÷¡C\n"
	   "      4.¨Cµ§¼úª÷¥Ñ¨t²Î©â¨ú 5% ¤§µ|ª÷%s¡C\n\n"
	   "\033[32m%s:\033[m", *price,
     bh ? "¦¹½ä½L¥ÑªO¥D­t³dÁ|¿ì¨Ã¥B¨M©w¶}¼ú®É¶¡µ²ªG, ¯¸ªø¤£ºÞ, Ä@½äªA¿é¡C" :
	   "¨t²Î¨C¤Ñ 2:00 11:00 16:00 21:00 ¶}¼ú¡C",
	   bh ? ", ¨ä¤¤ 2% ¤Àµ¹¶}¼úªO¥D" : "",
	   bh ? "ªO¥D¦Û­q³W«h¤Î»¡©ú" : "«e´X¦¸¶}¼úµ²ªG");


    sprintf(genbuf, "%s/" FN_TICKET, direct);
    if (!dashf(genbuf)) {
	sprintf(genbuf, "%s/" FN_TICKET_END, direct);
	end = 1;
    }
    show_file(genbuf, 8, -1, NO_RELOAD);
    move(15, 0);
    prints("\033[1;32m¥Ø«e¤Uª`ª¬ªp:\033[m\n");

    total = load_ticket_record(direct, ticket);

    prints("\033[33m");
    for (i = 0; i < count; i++) {
	prints("%d.%-8s: %-7d", i + 1, betname[i], ticket[i]);
	if (i == 3)
	    prints("\n");
    }
    prints("\033[m\n\033[42m ¤Uª`Á`ª÷ÃB:\033[31m %d ¤¸ \033[m", total * (*price));
    if (end) {
	prints("\n½ä½L¤w¸g°±¤î¤Uª`\n");
	return -count;
    }
    return count;
}

static void 
append_ticket_record(char *direct, int ch, int n, int count)
{
    FILE           *fp;
    int             ticket[8] = {0, 0, 0, 0, 0, 0, 0, 0}, i;
    char            genbuf[256];
    sprintf(genbuf, "%s/" FN_TICKET_USER, direct);

    if ((fp = fopen(genbuf, "a"))) {
	fprintf(fp, "%s %d %d\n", cuser.userid, ch, n);
	fclose(fp);
    }
    load_ticket_record(direct, ticket);
    ticket[ch] += n;
    sprintf(genbuf, "%s/" FN_TICKET_RECORD, direct);
    if ((fp = fopen(genbuf, "w"))) {
	for (i = 0; i < count; i++)
	    fprintf(fp, "%d ", ticket[i]);
	fclose(fp);
    }
}

#define lockreturn0(unmode, state) if(lockutmpmode(unmode, state)) return 0
int 
ticket(int bid)
{
    int             ch, n, price, count, end = 0;
    char            path[128], fn_ticket[128];
    boardheader_t  *bh = NULL;

    if (bid) {
	bh = getbcache(bid);
	setbpath(path, bh->brdname);
	setbfile(fn_ticket, bh->brdname, FN_TICKET);
	currbid = bid;
    } else
	strcpy(path, "etc/");

    lockreturn0(TICKET, LOCK_MULTI);
    while (1) {
	count = show_ticket_data(path, &price, bh);
	if (count <= 0) {
	    pressanykey();
	    break;
	}
	move(20, 0);
	reload_money();
	prints("\033[44m¿ú: %-10d  \033[m\n\033[1m½Ð¿ï¾Ü­nÁÊ¶RªººØÃþ(1~%d)"
	       "[Q:Â÷¶}]\033[m:", cuser.money, count);
	ch = igetch();
	/*--
	  Tim011127
	  ¬°¤F±±¨îCS°ÝÃD ¦ý¬O³oÃäÁÙ¤£¯à§¹¥þ¸Ñ¨M³o°ÝÃD,
	  ­Yuser³q¹LÀË¬d¤U¥h, ­è¦nªO¥D¶}¼ú, ÁÙ¬O·|³y¦¨userªº³o¦¸¬ö¿ý
	  «Ü¦³¥i¯à¶]¨ì¤U¦¸½ä½Lªº¬ö¿ý¥h, ¤]«Ü¦³¥i¯à³QªO¥D·s¶}½ä½L®É¬~±¼
	  ¤£¹L³oÃä¦Ü¤Ö¥i¥H°µ¨ìªº¬O, ³»¦h¥u·|¦³¤@µ§¸ê®Æ¬O¿ùªº
	--*/
	if (bid && !dashf(fn_ticket)) {
	    move(b_lines - 1, 0);
	    prints("«z!! ­@£«®º...ªO¥D¤w¸g°±¤î¤Uª`¤F ¤£¯à½äÂP");
	    pressanykey();
	    break;
	}
	if (ch == 'q' || ch == 'Q')
	    break;
	ch -= '1';
	if (end || ch >= count || ch < 0)
	    continue;
	n = 0;
	ch_buyitem(price, "etc/buyticket", &n);
	if (n > 0)
	    append_ticket_record(path, ch, n, count);
    }
    unlockutmpmode();
    return 0;
}

int 
openticket(int bid)
{
    char            path[128], buf[256], outcome[128];
    int             i, money = 0, count, bet, price, total = 0, ticket[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    boardheader_t  *bh = getbcache(bid);
    FILE           *fp, *fp1;

    setbpath(path, bh->brdname);
    count = -show_ticket_data(path, &price, bh);
    if (count == 0) {
	setbfile(buf, bh->brdname, FN_TICKET_END);
	unlink(buf);
//Ptt:	¦³bug
	    return 0;
    }
    lockreturn0(TICKET, LOCK_MULTI);
    do {
	do {
	    getdata(20, 0,
		    "\033[1m¿ï¾Ü¤¤¼úªº¸¹½X(0:¤£¶}¼ú 99:¨ú®ø°h¿ú)\033[m:", buf, 3, LCECHO);
	    bet = atoi(buf);
	    move(0, 0);
	    clrtoeol();
	} while ((bet < 0 || bet > count) && bet != 99);
	if (bet == 0) {
	    unlockutmpmode();
	    return 0;
	}
	getdata(21, 0, "\033[1m¦A¦¸½T»{¿é¤J¸¹½X\033[m:", buf, 3, LCECHO);
    } while (bet != atoi(buf));

    if (fork()) {
	//Ptt ¥ Îfork ¨ ¾¤î¤£¥¿±`Â_ ½ u ¬ ~¿ú
	    move(22, 0);
	prints("¨t²Î±N©óµy«á¦Û°Ê§â¤¤¼úµ²ªG¤½§G©ó¬ÝªO ­Y°Ñ¥[ªÌ¦h·|»Ý­n´X¤ÀÄÁ®É¶¡..");
	pressanykey();
	unlockutmpmode();
	return 0;
    }
    close(0);
    close(1);
    setproctitle("open ticket");
#ifdef CPULIMIT
    {
	struct rlimit   rml;
	rml.rlim_cur = RLIM_INFINITY;
	rml.rlim_max = RLIM_INFINITY;
	setrlimit(RLIMIT_CPU, &rml);
    }
#endif


    bet -= 1;			/* Âà¦¨¯x°}ªºindex */

    total = load_ticket_record(path, ticket);
    setbfile(buf, bh->brdname, FN_TICKET_END);
    if (!(fp1 = fopen(buf, "r")))
	exit(1);

    /* ÁÙ¨S¶}§¹¼ú¤£¯à½ä³Õ ¥u­nmv¤@¶µ´N¦n */
    if (bet != 98) {
	money = total * price;
	demoney(money * 0.02);
	mail_redenvelop("[½ä³õ©âÀY]", cuser.userid, money * 0.02, 'n');
	money = ticket[bet] ? money * 0.95 / ticket[bet] : 9999999;
    } else {
	vice(price * 10, "½ä½L°h¿ú¤âÄò¶O");
	money = price;
    }
    setbfile(outcome, bh->brdname, FN_TICKET_OUTCOME);
    if ((fp = fopen(outcome, "w"))) {
	fprintf(fp, "½ä½L»¡©ú\n");
	while (fgets(buf, 256, fp1)) {
	    buf[255] = 0;
	    fprintf(fp, "%s", buf);
	}
	fprintf(fp, "¤Uª`±¡ªp\n");

	fprintf(fp, "\033[33m");
	for (i = 0; i < count; i++) {
	    fprintf(fp, "%d.%-8s: %-7d", i + 1, betname[i], ticket[i]);
	    if (i == 3)
		fprintf(fp, "\n");
	}
	fprintf(fp, "\033[m\n");

	if (bet != 98) {
	    fprintf(fp, "\n\n¶}¼ú®É¶¡¡G %s \n\n"
		    "¶}¼úµ²ªG¡G %s \n\n"
		    "©Ò¦³ª÷ÃB¡G %d ¤¸ \n"
		    "¤¤¼ú¤ñ¨Ò¡G %d±i/%d±i  (%f)\n"
		    "¨C±i¤¤¼ú±m²¼¥i±o %d ªT¢Þ¹ô \n\n",
	    Cdatelite(&now), betname[bet], total * price, ticket[bet], total,
		    (float)ticket[bet] / total, money);

	    fprintf(fp, "%s ½ä½L¶}¥X:%s ©Ò¦³ª÷ÃB:%d ¤¸ ¼úª÷/±i:%d ¤¸ ¾÷²v:%1.2f\n\n",
		    Cdatelite(&now), betname[bet], total * price, money,
		    total ? (float)ticket[bet] / total : 0);
	} else
	    fprintf(fp, "\n\n½ä½L¨ú®ø°h¿ú¡G %s \n\n", Cdatelite(&now));

    }
    fclose(fp1);

    setbfile(buf, bh->brdname, FN_TICKET_END);
    setbfile(path, bh->brdname, FN_TICKET_LOCK);
    rename(buf, path);
    /*
     * ¥H¤U¬Oµ¹¿ú°Ê§@
     */
    setbfile(buf, bh->brdname, FN_TICKET_USER);
    if ((bet == 98 || ticket[bet]) && (fp1 = fopen(buf, "r"))) {
	int             mybet, uid;
	char            userid[IDLEN];

	while (fscanf(fp1, "%s %d %d\n", userid, &mybet, &i) != EOF) {
	    if (bet == 98 && mybet >= 0 && mybet < count) {
		fprintf(fp, "%s ¶R¤F %d ±i %s, °h¦^ %d ªT¢Þ¹ô\n"
			,userid, i, betname[mybet], money);
		sprintf(buf, "%s ½ä³õ°h¿ú! $ %d", bh->brdname, money * i);
	    } else if (mybet == bet) {
		fprintf(fp, "®¥³ß %s ¶R¤F%d ±i %s, Àò±o %d ªT¢Þ¹ô\n"
			,userid, i, betname[mybet], money);
		sprintf(buf, "%s ¤¤¼ú«¨! $ %d", bh->brdname, money * i);
	    } else
		continue;
	    if ((uid = searchuser(userid)) == 0)
		continue;
	    deumoney(uid, money * i);
	    mail_id(userid, buf, "etc/ticket.win", "Ptt½ä³õ");
	}
	fclose(fp1);
    }
    fclose(fp);

    if (bet != 98)
	sprintf(buf, "[¤½§i] %s ½ä½L¶}¼ú", bh->brdname);
    else
	sprintf(buf, "[¤½§i] %s ½ä½L¨ú®ø", bh->brdname);
    post_file(bh->brdname, buf, outcome, "[½ä¯«]");
    post_file("Record", buf + 7, outcome, "[°¨¸ô±´¤l]");

    setbfile(buf, bh->brdname, FN_TICKET_RECORD);
    unlink(buf);
    setbfile(buf, bh->brdname, FN_TICKET_USER);
    unlink(buf);
    setbfile(buf, bh->brdname, FN_TICKET_LOCK);
    unlink(buf);
    exit(1);
    return 0;
}

int 
ticket_main()
{
    ticket(0);
    return 0;
}
#endif
