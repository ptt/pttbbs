/* $Id$ */
#include "bbs.h"
#include "xchatd.h"

#define SERVER_USAGE
#undef  MONITOR                 /* ºÊ·þ chatroom ¬¡°Ê¥H¸Ñ¨MªÈ¯É */
#undef  DEBUG                   /* µ{¦¡°£¿ù¤§¥Î */

#ifdef  DEBUG
#define MONITOR
#endif


#define CHAT_PIDFILE    "log/chat.pid"
#define CHAT_LOGFILE    "log/chat.log"
#define CHAT_INTERVAL   (60 * 30)
#define SOCK_QLEN       1


/* name of the main room (always exists) */


#define MAIN_NAME       "main"
#define MAIN_TOPIC      "²i¯ù¥i°^¦è¤Ñ¦ò"


#define ROOM_LOCKED     1
#define ROOM_SECRET     2
#define ROOM_OPENTOPIC  4
#define ROOM_HANDUP     8
#define ROOM_ALL        (NULL)


#define LOCKED(room)    (room->rflag & ROOM_LOCKED)
#define SECRET(room)    (room->rflag & ROOM_SECRET)
#define OPENTOPIC(room) (room->rflag & ROOM_OPENTOPIC)
#define RHANDUP(room)    (room->rflag & ROOM_HANDUP)

#define RESTRICTED(usr) (usr->uflag == 0)       /* guest */
#define CHATSYSOP(usr)  (usr->uflag & ( PERM_SYSOP | PERM_CHATROOM))
/* Thor: SYSOP »P CHATROOM³£¬O chatÁ`ºÞ */
#define PERM_ROOMOP     PERM_CHAT       /* Thor: ­É PERM_CHAT¬° PERM_ROOMOP */
#define PERM_HANDUP     PERM_BM		/* ­É PERM_BM ¬°¦³¨S¦³Á|¤â¹L */
#define PERM_SAY        PERM_NOTOP	/* ­É PERM_NOTOP ¬°¦³¨S¦³µoªíÅv */

/* ¶i¤J®É»Ý²MªÅ              */
/* Thor: ROOMOP¬°©Ð¶¡ºÞ²z­û */
#define ROOMOP(usr)  (usr->uflag & ( PERM_ROOMOP | PERM_SYSOP | PERM_CHATROOM))
#define CLOAK(usr)      (usr->uflag & PERM_CLOAK)
#define HANDUP(usr)  (usr->uflag & PERM_HANDUP) 
#define SAY(usr)      (usr->uflag & PERM_SAY)
/* Thor: ²á¤Ñ«ÇÁô¨­³N */


/* ----------------------------------------------------- */
/* ChatRoom data structure                               */
/* ----------------------------------------------------- */

typedef struct ChatRoom ChatRoom;
typedef struct ChatUser ChatUser;
typedef struct UserList UserList;
typedef struct ChatCmd ChatCmd;
typedef struct ChatAction ChatAction;

struct ChatUser
{
    struct ChatUser *unext;
    int sock;                     /* user socket */
    ChatRoom *room;
    UserList *ignore;
    int userno;
    int uflag;
    int clitype;                  /* Xshadow: client type. 1 for common client,
				   * 0 for bbs only client */
    time4_t uptime;               /* Thor: unused */
    char userid[IDLEN + 1];       /* real userid */
    char chatid[9];               /* chat id */
    char lasthost[30];            /* host address */
    char ibuf[80];                /* buffer for non-blocking receiving */
    int isize;                    /* current size of ibuf */
};


struct ChatRoom
{
    struct ChatRoom *next, *prev;
    char name[IDLEN];
    char topic[48];               /* Let the room op to define room topic */
    int rflag;                    /* ROOM_LOCKED, ROOM_SECRET, ROOM_OPENTOPIC */
    int occupants;                /* number of users in room */
    UserList *invite;
};


struct UserList
{
    struct UserList *next;
    int userno;
    char userid[IDLEN + 1];
};


struct ChatCmd
{
    char *cmdstr;
    void (*cmdfunc) ();
    int exact;
};


static ChatRoom mainroom;
static ChatUser *mainuser;
static fd_set mainfds;
static int maxfds;              /* number of sockets to select on */
static int totaluser;           /* current number of connections */
static char chatbuf[256];       /* general purpose buffer */
static int common_client_command;

static char msg_not_op[] = "¡» ±z¤£¬O³o¶¡²á¤Ñ«Çªº Op";
static char msg_no_such_id[] = "¡» ¥Ø«e¨S¦³¤H¨Ï¥Î [%s] ³o­Ó²á¤Ñ¥N¸¹";
static char msg_not_here[] = "¡» [%s] ¤£¦b³o¶¡²á¤Ñ«Ç";


#define FUZZY_USER      ((ChatUser *) -1)


typedef struct userec_t ACCT;

/* ----------------------------------------------------- */
/* acct_load for check acct                              */
/* ----------------------------------------------------- */

int
acct_load(ACCT *acct, char *userid)
{
    int id;
    if((id=searchuser(userid))<0)
	return -1;
    return get_record(FN_PASSWD, acct, sizeof(ACCT), id);  
}

/* ----------------------------------------------------- */
/* usr_fpath for check acct                              */
/* ----------------------------------------------------- */
char *str_home_file = "home/%c/%s/%s";

void
usr_fpath(char *buf, char *userid, char *fname)
{
    sprintf(buf, str_home_file, userid[0], userid, fname);
}

/* ----------------------------------------------------- */
/* chkpasswd for check passwd                            */
/* ----------------------------------------------------- */
char *crypt(const char*, const char*);

int
chkpasswd(const char *passwd, const char *test)
{
    char *pw;
    char pwbuf[PASSLEN];

    strlcpy(pwbuf, test, PASSLEN);
    pw = crypt(pwbuf, passwd);
    return (!strncmp(pw, passwd, PASSLEN));
}

/* ----------------------------------------------------- */
/* operation log and debug information                   */
/* ----------------------------------------------------- */


static int flog;                /* log file descriptor */


static void
logit(char *key, char *msg)
{
    time4_t now;
    struct tm *p;
    char buf[512];

    now = (time4_t)time(NULL);
    p = localtime4(&now);
    snprintf(buf, sizeof(buf), "%02d/%02d %02d:%02d:%02d %-13s%s\n",
	    p->tm_mon + 1, p->tm_mday,
	    p->tm_hour, p->tm_min, p->tm_sec, key, msg);
    write(flog, buf, strlen(buf));
}


static void
log_init()
{
    flog = open(CHAT_LOGFILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    logit("START", "chat daemon");
}


static void
log_close()
{
    close(flog);
}


#ifdef  DEBUG
static void
debug_user()
{
    register ChatUser *user;
    int i;
    char buf[80];

    i = 0;
    for (user = mainuser; user; user = user->unext)
    {
	snprintf(buf, sizeof(buf), "%d) %s %s", ++i, user->userid, user->chatid);
	logit("DEBUG_U", buf);
    }
}


static void
debug_room()
{
    register ChatRoom *room;
    int i;
    char buf[80];

    i = 0;
    room = &mainroom;

    do
    {
	snprintf(buf, sizeof(buf), "%d) %s %d", ++i, room->name, room->occupants);
	logit("DEBUG_R", buf);
    } while (room = room->next);
}
#endif                          /* DEBUG */


/* ----------------------------------------------------- */
/* string routines                                       */
/* ----------------------------------------------------- */


static int valid_chatid(register char *id) {
    register int ch, len;
    
    for(len = 0; (ch = *id); id++) {
	/* Thor: check for endless */
	if(ch == '/' || ch == '*' || ch == ':')
	    return 0;
	if(++len > 8)
	    return 0;
    }
    return len;
}

/* Case Independent strcmp : 1 ==> euqal */


static int
str_equal(unsigned char *s1, unsigned char *s2)
{
    return strcasecmp(s1, s2)==0;
}


/* ----------------------------------------------------- */
/* match strings' similarity case-insensitively          */
/* ----------------------------------------------------- */
/* str_match(keyword, string)                            */
/* ----------------------------------------------------- */
/* 0 : equal            ("foo", "foo")                   */
/* -1 : mismatch        ("abc", "xyz")                   */
/* ow : similar         ("goo", "good")                  */
/* ----------------------------------------------------- */


static int
str_match(unsigned char *s1, unsigned char *s2)
{
    register int c1, c2;

    for (;;)
    {                             /* Thor: check for endless */
	c2 = *s2;
	c1 = *s1;
	if (!c1)
	{
	    return c2;
	}

	if (c1 >= 'A' && c1 <= 'Z')
	    c1 |= 32;

	if (c2 >= 'A' && c2 <= 'Z')
	    c2 |= 32;

	if (c1 != c2)
	    return -1;

	s1++;
	s2++;
    }
}


/* ----------------------------------------------------- */
/* search user/room by its ID                            */
/* ----------------------------------------------------- */


static ChatUser *
cuser_by_userid(char *userid)
{
    register ChatUser *cu;

    for (cu = mainuser; cu; cu = cu->unext)
    {
	if (str_equal(userid, cu->userid))
	    break;
    }
    return cu;
}


static ChatUser *
cuser_by_chatid(char *chatid)
{
    register ChatUser *cu;

    for (cu = mainuser; cu; cu = cu->unext)
    {
	if (str_equal(chatid, cu->chatid))
	    break;
    }
    return cu;
}


static ChatUser *
fuzzy_cuser_by_chatid(char *chatid)
{
    register ChatUser *cu, *xuser;
    int mode;
    int count=0;

    xuser = NULL;

    for (cu = mainuser; cu; cu = cu->unext)
    {
	mode = str_match(chatid, cu->chatid);
	if (mode == 0)
	    return cu;

	if (mode > 0) {
	    xuser = cu;
	    count++;
	}
    }
    if(count>1) return FUZZY_USER;
    return xuser;
}


static ChatRoom *croom_by_roomid(char *roomid) {
    ChatRoom *room;
    
    for(room=&mainroom; room; room=room->next)
	if(str_equal(roomid, room->name))
	    break;
    return room;
}


/* ----------------------------------------------------- */
/* UserList routines                                     */
/* ----------------------------------------------------- */


static void
list_free(UserList *list)
{
    UserList *tmp;

    while (list)
    {
	tmp = list->next;

	free(list);
	list = tmp;
    }
}


static void
list_add(UserList **list, ChatUser *user)
{
    UserList *node;

    if((node = (UserList *) malloc(sizeof(UserList)))) {
	/* Thor: ¨¾¤îªÅ¶¡¤£°÷ */
	strcpy(node->userid, user->userid);
	node->userno = user->userno;
	node->next = *list;
	*list = node;
    }
}


static int
list_delete(UserList **list, char *userid)
{
    UserList *node;

    while((node = *list)) {
	if (str_equal(node->userid, userid))
	{
	    *list = node->next;
	    free(node);
	    return 1;
	}
	list = &node->next;         /* Thor: list­n¸òµÛ«e¶i */
    }

    return 0;
}


static int
list_belong(UserList *list, int userno)
{
    while (list)
    {
	if (userno == list->userno)
	    return 1;
	list = list->next;
    }
    return 0;
}


/* ------------------------------------------------------ */
/* non-blocking socket routines : send message to users   */
/* ------------------------------------------------------ */


static void
Xdo_send(int nfds, fd_set *wset, char *msg)
{
    struct timeval zerotv;   /* timeval for selecting */
    int sr;

    /* Thor: for future reservation bug */

    zerotv.tv_sec = 0;
    zerotv.tv_usec = 16384;  /* Ptt: §ï¦¨16384 Á×§K¤£«ö®Éfor loop¦Ycpu time
				16384 ¬ù¨C¬í64¦¸ */

    sr = select(nfds + 1, NULL, wset, NULL, &zerotv);

    /* FIXME ­Y select() timeout, ©Î¦³ªº write ready ¦³ªº¨S¦³. «h¥i¯à·|º|±µ msg? */
    if (sr > 0)
    {
	register int len;

	len = strlen(msg) + 1;
	while (nfds >= 0)
	{
	    if (FD_ISSET(nfds, wset))
	    {
		send(nfds, msg, len, 0);/* Thor: ¦pªGbufferº¡¤F, ¤´·| block */
		if (--sr <= 0)
		    return;
	    }
	    nfds--;
	}
    }
}


static void
send_to_room(ChatRoom *room, char *msg, int userno, int number)
{
    ChatUser *cu;
    fd_set wset, *wptr;
    int sock, max;
    char sendbuf[256];
    int clitype;                  /* ¤À¬° bbs client ¤Î common client ¨â¦¸³B²z */

    for (clitype = (number == MSG_MESSAGE || !number) ? 0 : 1; clitype < 2; clitype++)
    {

	FD_ZERO(wptr = &wset);
	max = -1;

	for (cu = mainuser; cu; cu = cu->unext)
	{
	    if (room == cu->room || room == ROOM_ALL)
	    {
		if (cu->clitype == clitype && (!userno || !list_belong(cu->ignore, userno)))
		{
		    sock = cu->sock;
		    FD_SET(sock, wptr);
		    if (max < sock)
			max = sock;
		}
	    }
	}

	if (max < 0)
	    continue;

	if (clitype)
	{
	    if (strlen(msg))
		snprintf(sendbuf, sizeof(sendbuf), "%3d %s", number, msg);
	    else
		snprintf(sendbuf, sizeof(sendbuf), "%3d", number);

	    Xdo_send(max, wptr, sendbuf);
	}
	else
	    Xdo_send(max, wptr, msg);
    }
}


static void
send_to_user(ChatUser *user, char *msg, int userno, int number)
{
    if (!user->clitype && number && number != MSG_MESSAGE)
	return;

    if (!userno || !list_belong(user->ignore, userno))
    {
	fd_set wset, *wptr;
	int sock;
	char sendbuf[256];

	sock = user->sock;
	FD_ZERO(wptr = &wset);
	FD_SET(sock, wptr);

	if (user->clitype)
	{
	    if (strlen(msg))
		snprintf(sendbuf, sizeof(sendbuf), "%3d %s", number, msg);
	    else
		snprintf(sendbuf, sizeof(sendbuf), "%3d", number);
	    Xdo_send(sock, wptr, sendbuf);
	}
	else
	    Xdo_send(sock, wptr, msg);
    }
}

#if 0
static void
send_to_sock(int sock, char *msg)         /* Thor: unused */
{
    fd_set wset, *wptr;

    FD_ZERO(wptr = &wset);
    FD_SET(sock, wptr);
    Xdo_send(sock, wptr, msg);
}
#endif

/* ----------------------------------------------------- */

static void
room_changed(ChatRoom *room)
{
    if (!room)
	return;

    snprintf(chatbuf, sizeof(chatbuf), "= %s %d %d %s", room->name, room->occupants, room->rflag, room->topic);
    send_to_room(ROOM_ALL, chatbuf, 0, MSG_ROOMNOTIFY);
}

static void
user_changed(ChatUser *cu)
{
    if (!cu)
	return;

    snprintf(chatbuf, sizeof(chatbuf), "= %s %s %s %s", cu->userid, cu->chatid, cu->room->name, cu->lasthost);
    if (ROOMOP(cu))
	strcat(chatbuf, " Op");
    send_to_room(cu->room, chatbuf, 0, MSG_USERNOTIFY);
}

static void
exit_room(ChatUser *user, int mode, char *msg)
{
    ChatRoom *room;

    if(user->room == NULL)
	return;

    room = user->room;
    user->room = NULL;
    user->uflag &= ~PERM_ROOMOP;

    room->occupants--;

    if (room->occupants > 0)
    {
	char *chatid;

	chatid = user->chatid;
	switch (mode)
	{
	    case EXIT_LOGOUT:

		sprintf(chatbuf, "¡» %s Â÷¶}¤F ...", chatid);
		if (msg && *msg)
		{
		    strcat(chatbuf, ": ");
		    msg[79] = 0;          /* Thor:¨¾¤î¤Óªø */
		    strncat(chatbuf, msg, 80);
		}
		break;

	    case EXIT_LOSTCONN:

		sprintf(chatbuf, "¡» %s ¦¨¤FÂ_½uªº­·ºåÅo", chatid);
		break;

	    case EXIT_KICK:

		sprintf(chatbuf, "¡» «¢«¢¡I%s ³Q½ð¥X¥h¤F", chatid);
		break;
	}
	if (!CLOAK(user))         /* Thor: ²á¤Ñ«ÇÁô¨­³N */
	    send_to_room(room, chatbuf, 0, MSG_MESSAGE);

	if (list_belong(room->invite, user->userno)) {
	    list_delete(&(room->invite), user->userid);
	}

	sprintf(chatbuf, "- %s", user->userid);
	send_to_room(room, chatbuf, 0, MSG_USERNOTIFY);
	room_changed(room);

	return;
    }

    /* Now, room->occupants==0 */
    if (room != &mainroom)
    {                           /* Thor: ¤H¼Æ¬°0®É,¤£¬Omainroom¤~free */
	register ChatRoom *next;

	sprintf(chatbuf, "- %s", room->name);
	send_to_room(ROOM_ALL, chatbuf, 0, MSG_ROOMNOTIFY);

	room->prev->next = room->next;
	if((next = room->next))
	    next->prev = room->prev;
	list_free(room->invite);

	free(room);
    }
}


/* ----------------------------------------------------- */
/* chat commands                                         */
/* ----------------------------------------------------- */

/* ----------------------------------------------------- */
/* (.ACCT) ¨Ï¥ÎªÌ±b¸¹ (account) subroutines              */
/* ----------------------------------------------------- */

static char datemsg[32];

char *
Ctime(time4_t *clock)
{
    struct tm *t = localtime4(clock);
    const char *week = "¤é¤@¤G¤T¥|¤­¤»";

    snprintf(datemsg, sizeof(datemsg), "%d¦~%2d¤ë%2d¤é%3d:%02d:%02d ¬P´Á%.2s",
	    t->tm_year - 11, t->tm_mon + 1, t->tm_mday,
	    t->tm_hour, t->tm_min, t->tm_sec, &week[t->tm_wday << 1]);
    return (datemsg);
}

static void
chat_query(ChatUser *cu, char *msg)
{
    char str[256];
    int i;
    ACCT xuser;
    FILE *fp;

    if (acct_load(&xuser, msg) >= 0)
    {
	snprintf(chatbuf, sizeof(chatbuf), "%s(%s) ¦@¤W¯¸ %d ¦¸¡A¤å³¹ %d ½g",
		xuser.userid, xuser.username, xuser.numlogins, xuser.numposts);
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);

	snprintf(chatbuf, sizeof(chatbuf), "³Ìªñ(%s)±q(%s)¤W¯¸", Ctime(&xuser.lastlogin),
		(xuser.lasthost[0] ? xuser.lasthost : "¥~¤ÓªÅ"));
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);

	usr_fpath(chatbuf, xuser.userid, "plans");
	fp = fopen(chatbuf, "rt");
	if(fp) {
	    i = 0;
	    while (fgets(str, sizeof(str), fp)) {
		int len=strlen(str);
		if (len==0)
		    continue;

		str[len - 1] = 0;
		send_to_user(cu, str, 0, MSG_MESSAGE);
		if (++i >= MAX_QUERYLINES)
		    break;
	    }
	    fclose(fp);
	}
    }
    else
    {
	snprintf(chatbuf, sizeof(chatbuf), msg_no_such_id, msg);
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
    }
}

static void
chat_clear(ChatUser *cu, char *msg)
{
    if (cu->clitype)
	send_to_user(cu, "", 0, MSG_CLRSCR);
    else
	send_to_user(cu, "/c", 0, MSG_MESSAGE);
}

static void
chat_date(ChatUser *cu, char *msg)
{
    time4_t thetime;

    thetime = time(NULL);
    sprintf(chatbuf, "¡» ¼Ð·Ç®É¶¡: %s", Ctime(&thetime));
    send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
}


static void
chat_topic(ChatUser *cu, char *msg)
{
    ChatRoom *room;
    char *topic;

    if (!ROOMOP(cu) && !OPENTOPIC(cu->room))
    {
	send_to_user(cu, msg_not_op, 0, MSG_MESSAGE);
	return;
    }

    if (*msg == '\0')
    {
	send_to_user(cu, "¡° ½Ð«ü©w¸ÜÃD", 0, MSG_MESSAGE);
	return;
    }

    room = cu->room;
    assert(room);
    topic = room->topic;
    strlcpy(topic, msg, sizeof(room->topic));

    if (cu->clitype)
	send_to_room(room, topic, 0, MSG_TOPIC);
    else
    {
	sprintf(chatbuf, "/t%s", topic);
	send_to_room(room, chatbuf, 0, 0);
    }

    room_changed(room);

    sprintf(chatbuf, "¡» %s ±N¸ÜÃD§ï¬° [1;32m%s[m", cu->chatid, topic);
    if (!CLOAK(cu))               /* Thor: ²á¤Ñ«ÇÁô¨­³N */
	send_to_room(room, chatbuf, 0, MSG_MESSAGE);
}


static void
chat_version(ChatUser *cu, char *msg)
{
    sprintf(chatbuf, "%d %d", XCHAT_VERSION_MAJOR, XCHAT_VERSION_MINOR);
    send_to_user(cu, chatbuf, 0, MSG_VERSION);
}

static void
chat_nick(ChatUser *cu, char *msg)
{
    char *chatid, *str;
    ChatUser *xuser;

    chatid = nextword(&msg);
    chatid[8] = '\0';
    if (!valid_chatid(chatid))
    {
	send_to_user(cu, "¡° ³o­Ó²á¤Ñ¥N¸¹¬O¤£¥¿½Tªº", 0, MSG_MESSAGE);
	return;
    }

    xuser = cuser_by_chatid(chatid);
    if (xuser != NULL && xuser != cu)
    {
	send_to_user(cu, "¡° ¤w¸g¦³¤H±¶¨¬¥ýµnÅo", 0, MSG_MESSAGE);
	return;
    }

    str = cu->chatid;

    snprintf(chatbuf, sizeof(chatbuf), "¡° %s ±N²á¤Ñ¥N¸¹§ï¬° [1;33m%s[m", str, chatid);
    if (!CLOAK(cu))               /* Thor: ²á¤Ñ«ÇÁô¨­³N */
	send_to_room(cu->room, chatbuf, cu->userno, MSG_MESSAGE);

    strcpy(str, chatid);

    user_changed(cu);

    if (cu->clitype)
	send_to_user(cu, chatid, 0, MSG_NICK);
    else
    {
	snprintf(chatbuf, sizeof(chatbuf), "/n%s", chatid);
	send_to_user(cu, chatbuf, 0, 0);
    }
}

static void
chat_list_rooms(ChatUser *cuser, char *msg)
{
    ChatRoom *cr;

    if (RESTRICTED(cuser))
    {
	send_to_user(cuser, "¡° ±z¨S¦³Åv­­¦C¥X²{¦³ªº²á¤Ñ«Ç", 0, MSG_MESSAGE);
	return;
    }

    if (common_client_command)
	send_to_user(cuser, "", 0, MSG_ROOMLISTSTART);
    else
	send_to_user(cuser, "[7m ½Í¤Ñ«Ç¦WºÙ  ¢x¤H¼Æ¢x¸ÜÃD        [m", 0, MSG_MESSAGE);

    for(cr = &mainroom; cr; cr = cr->next) {
	if (!SECRET(cr) || CHATSYSOP(cuser) || (cr == cuser->room && ROOMOP(cuser)))
	{
	    if (common_client_command)
	    {
		snprintf(chatbuf, sizeof(chatbuf), "%s %d %d %s", cr->name, cr->occupants, cr->rflag, cr->topic);
		send_to_user(cuser, chatbuf, 0, MSG_ROOMLIST);
	    }
	    else
	    {
		snprintf(chatbuf, sizeof(chatbuf), " %-12s¢x%4d¢x%s", cr->name, cr->occupants, cr->topic);
		if (LOCKED(cr))
		    strcat(chatbuf, " [Âê¦í]");
		if (SECRET(cr))
		    strcat(chatbuf, " [¯µ±K]");
		if (OPENTOPIC(cr))
		    strcat(chatbuf, " [¸ÜÃD]");
		send_to_user(cuser, chatbuf, 0, MSG_MESSAGE);
	    }

	}
    }

    if (common_client_command)
	send_to_user(cuser, "", 0, MSG_ROOMLISTEND);
}


static void
chat_do_user_list(ChatUser *cu, char *msg, ChatRoom *theroom)
{
    ChatRoom *myroom, *room;
    ChatUser *user;

    int start, stop, curr = 0;
    start = atoi(nextword(&msg));
    stop = atoi(nextword(&msg));

    myroom = cu->room;

#ifdef DEBUG
    logit(cu->chatid, "do user list");
#endif

    if (common_client_command)
	send_to_user(cu, "", 0, MSG_USERLISTSTART);
    else
	send_to_user(cu, "[7m ²á¤Ñ¥N¸¹¢x¨Ï¥ÎªÌ¥N¸¹  ¢x²á¤Ñ«Ç [m", 0, MSG_MESSAGE);

    for (user = mainuser; user; user = user->unext)
    {

	room = user->room;
	if ((theroom != ROOM_ALL) && (theroom != room))
	    continue;

	if (myroom != room)
	{
	    if (RESTRICTED(cu) ||
		(room && SECRET(room) && !CHATSYSOP(cu)))
		continue;
	}

	if (CLOAK(user))            /* Thor: Áô¨­³N */
	    continue;


	curr++;
	if (start && curr < start)
	    continue;
	else if (stop && (curr > stop))
	    break;

	if (common_client_command)
	{
	    if (!room)
		continue;               /* Xshadow: ÁÙ¨S¶i¤J¥ô¦ó©Ð¶¡ªº´N¤£¦C¥X */

	    snprintf(chatbuf, sizeof(chatbuf), "%s %s %s %s", user->chatid, user->userid, room->name, user->lasthost);
	    if (ROOMOP(user))
		strcat(chatbuf, " Op");
	}
	else
	{
	    snprintf(chatbuf, sizeof(chatbuf), " %-8s¢x%-12s¢x%s", user->chatid, user->userid, room ? room->name : "[¦bªù¤f±r«Þ]");
	    if (ROOMOP(user))
		strcat(chatbuf, " [Op]");
	}

#ifdef  DEBUG
	logit("list_U", chatbuf);
#endif

	send_to_user(cu, chatbuf, 0, common_client_command ? MSG_USERLIST : MSG_MESSAGE);
    }
    if (common_client_command)
	send_to_user(cu, "", 0, MSG_USERLISTEND);
}

static void
chat_list_by_room(ChatUser *cu, char *msg)
{
    ChatRoom *whichroom;
    char *roomstr;

    roomstr = nextword(&msg);
    if (*roomstr == '\0')
	whichroom = cu->room;
    else
    {
	if ((whichroom = croom_by_roomid(roomstr)) == NULL)
	{
	    snprintf(chatbuf, sizeof(chatbuf), "¡° ¨S¦³ [%s] ³o­Ó²á¤Ñ«Ç", roomstr);
	    send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	    return;
	}

	if (whichroom != cu->room && SECRET(whichroom) && !CHATSYSOP(cu))
	{                           /* Thor: ­n¤£­n´ú¦P¤@roomÁöSECRET¦ý¥i¥H¦C?
				     * Xshadow: §Ú§ï¦¨¦P¤@ room ´N¥i¥H¦C */
	    send_to_user(cu, "¡° µLªk¦C¥X¦b¯µ±K²á¤Ñ«Çªº¨Ï¥ÎªÌ", 0, MSG_MESSAGE);
	    return;
	}
    }
    chat_do_user_list(cu, msg, whichroom);
}


static void
chat_list_users(ChatUser *cu, char *msg)
{
    chat_do_user_list(cu, msg, ROOM_ALL);
}

static void
chat_chatroom(ChatUser *cu, char *msg)
{
    if (common_client_command)
	send_to_user(cu, "§å½ð½ð¯ùÃÀÀ] 4 21", 0, MSG_CHATROOM);
}

static void
chat_map_chatids(ChatUser *cu, ChatRoom *whichroom) /* Thor: ÁÙ¨S¦³§@¤£¦P¶¡ªº */
{
    int c;
    ChatRoom *myroom, *room;
    ChatUser *user;

    myroom = whichroom;
    send_to_user(cu,
		 "[7m ²á¤Ñ¥N¸¹ ¨Ï¥ÎªÌ¥N¸¹  ¢x ²á¤Ñ¥N¸¹ ¨Ï¥ÎªÌ¥N¸¹  ¢x ²á¤Ñ¥N¸¹ ¨Ï¥ÎªÌ¥N¸¹ [m", 0, MSG_MESSAGE);

    c = 0;

    for (user = mainuser; user; user = user->unext)
    {
	room = user->room;
	if (whichroom != ROOM_ALL && whichroom != room)
	    continue;
	if (myroom != room)
	{
	    if (RESTRICTED(cu) ||     /* Thor: ­n¥ýcheck room ¬O¤£¬OªÅªº */
		(room && SECRET(room) && !CHATSYSOP(cu)))
		continue;
	}
	if (CLOAK(user))            /* Thor:Áô¨­³N */
	    continue;
	sprintf(chatbuf + (c * 24), " %-8s%c%-12s%s",
		user->chatid, ROOMOP(user) ? '*' : ' ',
		user->userid, (c < 2 ? "¢x" : "  "));
	if (++c == 3)
	{
	    send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	    c = 0;
	}
    }
    if (c > 0)
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
}


static void
chat_map_chatids_thisroom(ChatUser *cu, char *msg)
{
    chat_map_chatids(cu, cu->room);
}


static void
chat_setroom(ChatUser *cu, char *msg)
{
    char *modestr;
    ChatRoom *room;
    char *chatid;
    int sign;
    int flag;
    char *fstr = NULL;

    if (!ROOMOP(cu))
    {
	send_to_user(cu, msg_not_op, 0, MSG_MESSAGE);
	return;
    }

    modestr = nextword(&msg);
    sign = 1;
    if (*modestr == '+')
	modestr++;
    else if (*modestr == '-')
    {
	modestr++;
	sign = 0;
    }
    if (*modestr == '\0')
    {
	send_to_user(cu,
		     "¡° ½Ð«ü©wª¬ºA: {[+(³]©w)][-(¨ú®ø)]}{[l(Âê¦í)][s(¯µ±K)][t(¶}©ñ¸ÜÃD)}", 0, MSG_MESSAGE);
	return;
    }

    room = cu->room;
    chatid = cu->chatid;

    while (*modestr)
    {
	flag = 0;
	switch (*modestr)
	{
	case 'l':
	case 'L':
	    flag = ROOM_LOCKED;
	    fstr = "Âê¦í";
	    break;

	case 's':
	case 'S':
	    flag = ROOM_SECRET;
	    fstr = "¯µ±K";
	    break;

	case 't':
	case 'T':
	    flag = ROOM_OPENTOPIC;
	    fstr = "¶}©ñ¸ÜÃD";
	    break;
	case 'h':
	case 'H':
	    flag = ROOM_HANDUP;
	    fstr = "Á|¤âµo¨¥";
	    break;

	default:
	    sprintf(chatbuf, "¡° ª¬ºA¿ù»~¡G[%c]", *modestr);
	    send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	}

	/* Thor: check room ¬O¤£¬OªÅªº, À³¸Ó¤£¬OªÅªº */
	if (flag && (room->rflag & flag) != sign * flag)
	{
	    room->rflag ^= flag;
	    sprintf(chatbuf, "¡° ¥»²á¤Ñ«Ç³Q %s %s [%s] ª¬ºA",
		    chatid, sign ? "³]©w¬°" : "¨ú®ø", fstr);
	    if (!CLOAK(cu))           /* Thor: ²á¤Ñ«ÇÁô¨­³N */
		send_to_room(room, chatbuf, 0, MSG_MESSAGE);
	}
	modestr++;
    }
    room_changed(room);
}

static char *chat_msg[] =
{
    "[//]help", "MUD-like ªÀ¥æ°Êµü",
    "[/h]elp op", "½Í¤Ñ«ÇºÞ²z­û±M¥Î«ü¥O",
    "[/a]ct <msg>", "°µ¤@­Ó°Ê§@",
    "[/b]ye [msg]", "¹D§O",
    "[/c]lear  [/d]ate", "²M°£¿Ã¹õ  ¥Ø«e®É¶¡",
    /* "[/d]ate", "¥Ø«e®É¶¡", *//* Thor: «ü¥O¤Ó¦h */

#if 0
    "[/f]ire <user> <msg>", "µo°e¼ö°T",   /* Thor.0727: ©M flag ½Äkey */
#endif

    "[/i]gnore [user]", "©¿²¤¨Ï¥ÎªÌ",
    "[/j]oin <room>", "«Ø¥ß©Î¥[¤J½Í¤Ñ«Ç",
    "[/l]ist [start [stop]]", "¦C¥X½Í¤Ñ«Ç¨Ï¥ÎªÌ",
    "[/m]sg <id|user> <msg>", "¸ò <id> »¡®¨®¨¸Ü",
    "[/n]ick <id>", "±N½Í¤Ñ¥N¸¹´«¦¨ <id>",
    "[/p]ager", "¤Á´«©I¥s¾¹",
    "[/q]uery <user>", "¬d¸ßºô¤Í",
    "[/r]oom", "¦C¥X¤@¯ë½Í¤Ñ«Ç",
    "[/t]ape", "¶}Ãö¿ý­µ¾÷",
    "[/u]nignore <user>", "¨ú®ø©¿²¤",

#if 0
    "[/u]sers", "¦C¥X¯¸¤W¨Ï¥ÎªÌ",
#endif

    "[/w]ho", "¦C¥X¥»½Í¤Ñ«Ç¨Ï¥ÎªÌ",
    "[/w]hoin <room>", "¦C¥X½Í¤Ñ«Ç<room> ªº¨Ï¥ÎªÌ",
    NULL
};


static char *room_msg[] =
{
    "[/f]lag [+-][lsth]", "³]©wÂê©w¡B¯µ±K¡B¶}©ñ¸ÜÃD¡BÁ|¤âµo¨¥",
    "[/i]nvite <id>", "ÁÜ½Ð <id> ¥[¤J½Í¤Ñ«Ç",
    "[/kick] <id>", "±N <id> ½ð¥X½Í¤Ñ«Ç",
    "[/o]p <id>", "±N Op ªºÅv¤OÂà²¾µ¹ <id>",
    "[/topic] <text>", "´«­Ó¸ÜÃD",
    "[/w]all", "¼s¼½ (¯¸ªø±M¥Î)",
    NULL
};


static void
chat_help(ChatUser *cu, char *msg)
{
    char **table, *str;

    if (str_equal(nextword(&msg), "op"))
    {
	send_to_user(cu, "½Í¤Ñ«ÇºÞ²z­û±M¥Î«ü¥O", 0, MSG_MESSAGE);
	table = room_msg;
    }
    else
    {
	table = chat_msg;
    }

    while((str = *table++)) {
	snprintf(chatbuf, sizeof(chatbuf), "  %-20s- %s", str, *table++);
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
    }
}


static void
chat_private(ChatUser *cu, char *msg)
{
    char *recipient;
    ChatUser *xuser;
    int userno;

    userno = 0;
    recipient = nextword(&msg);
    xuser = (ChatUser *) fuzzy_cuser_by_chatid(recipient);
    if (xuser == NULL)
    {                             /* Thor.0724: ¥Î userid¤]¥i¶Ç®¨®¨¸Ü */
	xuser = cuser_by_userid(recipient);
    }
    if (xuser == NULL)
    {
	sprintf(chatbuf, msg_no_such_id, recipient);
    }
    else if (xuser == FUZZY_USER)
    {                             /* ambiguous */
	strcpy(chatbuf, "¡° ½Ð«ü©ú²á¤Ñ¥N¸¹");
    }
    else if (*msg)
    {
	userno = cu->userno;
	sprintf(chatbuf, "[1m*%s*[m ", cu->chatid);
	msg[79] = 0;                /* Thor:¨¾¤î¤Óªø */
	strncat(chatbuf, msg, 80);
	send_to_user(xuser, chatbuf, userno, MSG_MESSAGE);

	if (xuser->clitype)
	{                           /* Xshadow: ¦pªG¹ï¤è¬O¥Î client ¤W¨Óªº */
	    sprintf(chatbuf, "%s %s ", cu->userid, cu->chatid);
	    msg[79] = 0;
	    strncat(chatbuf, msg, 80);
	    send_to_user(xuser, chatbuf, userno, MSG_PRIVMSG);
	}
	if (cu->clitype)
	{
	    sprintf(chatbuf, "%s %s ", xuser->userid, xuser->chatid);
	    msg[79] = 0;
	    strncat(chatbuf, msg, 80);
	    send_to_user(cu, chatbuf, 0, MSG_MYPRIVMSG);
	}

	sprintf(chatbuf, "%s> ", xuser->chatid);
	strncat(chatbuf, msg, 80);
    }
    else
    {
	sprintf(chatbuf, "¡° ±z·Q¹ï %s »¡¤°»ò¸Ü©O¡H", xuser->chatid);
    }
    send_to_user(cu, chatbuf, userno, MSG_MESSAGE);       /* Thor: userno ­n§ï¦¨ 0
							   * ¶Ü? */
}


static void
chat_cloak(ChatUser *cu, char *msg)
{
    if (CHATSYSOP(cu))
    {
	cu->uflag ^= PERM_CLOAK;
	sprintf(chatbuf, "¡» %s", CLOAK(cu) ? MSG_CLOAKED : MSG_UNCLOAK);
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
    }
}



/* ----------------------------------------------------- */


static void
arrive_room(ChatUser *cuser, ChatRoom *room)
{
    char *rname;

    /* Xshadow: ¤£¥²°eµ¹¦Û¤v, ¤Ï¥¿´«©Ð¶¡´N·|­«·s build user list */
    snprintf(chatbuf, sizeof(chatbuf), "+ %s %s %s %s", cuser->userid, cuser->chatid, room->name, cuser->lasthost);
    if (ROOMOP(cuser))
	strcat(chatbuf, " Op");
    send_to_room(room, chatbuf, 0, MSG_USERNOTIFY);

    cuser->room = room;
    room->occupants++;
    rname = room->name;

    room_changed(room);

    if (cuser->clitype)
    {
	send_to_user(cuser, rname, 0, MSG_ROOM);
	send_to_user(cuser, room->topic, 0, MSG_TOPIC);
    }
    else
    {
	sprintf(chatbuf, "/r%s", rname);
	send_to_user(cuser, chatbuf, 0, 0);
	sprintf(chatbuf, "/t%s", room->topic);
	send_to_user(cuser, chatbuf, 0, 0);
    }

    sprintf(chatbuf, "¡° [32;1m%s[m ¶i¤J [33;1m[%s][m ¥]´[",
	    cuser->chatid, rname);
    if (!CLOAK(cuser))            /* Thor: ²á¤Ñ«ÇÁô¨­³N */
	send_to_room(room, chatbuf, cuser->userno, MSG_MESSAGE);
}


static int
enter_room(ChatUser *cuser, char *rname, char *msg)
{
    ChatRoom *room;
    int create;

    create = 0;
    room = croom_by_roomid(rname);
    if (room == NULL)
    {
	/* new room */

#ifdef  MONITOR
	logit(cuser->userid, "create new room");
#endif

	room = (ChatRoom *) malloc(sizeof(ChatRoom));
	if (room == NULL)
	{
	    send_to_user(cuser, "¡° µLªk¦A·sÅP¥]´[¤F", 0, MSG_MESSAGE);
	    return 0;
	}

	memset(room, 0, sizeof(ChatRoom));
	strlcpy(room->name, rname, IDLEN);
	strcpy(room->topic, "³o¬O¤@­Ó·s¤Ñ¦a");

	snprintf(chatbuf, sizeof(chatbuf), "+ %s 1 0 %s", room->name, room->topic);
	send_to_room(ROOM_ALL, chatbuf, 0, MSG_ROOMNOTIFY);

	if (mainroom.next != NULL)
	    mainroom.next->prev = room;
	room->next = mainroom.next;
	mainroom.next = room;
	room->prev = &mainroom;

	create = 1;
    }
    else
    {
	if (cuser->room == room)
	{
	    sprintf(chatbuf, "¡° ±z¥»¨Ó´N¦b [%s] ²á¤Ñ«ÇÅo :)", rname);
	    send_to_user(cuser, chatbuf, 0, MSG_MESSAGE);
	    return 0;
	}

	if (!CHATSYSOP(cuser) && LOCKED(room) && !list_belong(room->invite, cuser->userno))
	{
	    send_to_user(cuser, "¡° ¤º¦³´c¤ü¡A«D½Ð²ö¤J", 0, MSG_MESSAGE);
	    return 0;
	}
    }

    exit_room(cuser, EXIT_LOGOUT, msg);
    arrive_room(cuser, room);

    if (create)
	cuser->uflag |= PERM_ROOMOP;

    return 0;
}


static void
logout_user(ChatUser *cuser)
{
    int sock;
    ChatUser *xuser, *prev;

    sock = cuser->sock;
    shutdown(sock, 2);
    close(sock);

    FD_CLR(sock, &mainfds);

#if 0   /* Thor: ¤]³\¤£®t³o¤@­Ó */
    if (sock >= maxfds)
	maxfds = sock - 1;
#endif

    list_free(cuser->ignore);

    xuser = mainuser;
    if (xuser == cuser)
    {
	mainuser = cuser->unext;
    }
    else
    {
	do
	{
	    prev = xuser;
	    xuser = xuser->unext;
	    if (xuser == cuser)
	    {
		prev->unext = cuser->unext;
		break;
	    }
	} while (xuser);
    }

#ifdef DEBUG
    sprintf(chatbuf, "%p", cuser);
    logit("free cuser", chatbuf);
#endif

    free(cuser);

    totaluser--;
}


static void
print_user_counts(ChatUser *cuser)
{
    ChatRoom *room;
    int num, userc, suserc, roomc, number;

    userc = suserc = roomc = 0;

    for(room = &mainroom; room; room = room->next) {
	num = room->occupants;
	if (SECRET(room))
	{
	    suserc += num;
	    if (CHATSYSOP(cuser))
		roomc++;
	}
	else
	{
	    userc += num;
	    roomc++;
	}
    }

    number = (cuser->clitype) ? MSG_MOTD : MSG_MESSAGE;

    sprintf(chatbuf,
	    "¡ó Åwªï¥úÁ{¡i§å½ð½ð¯ùÃÀÀ]¡j¡A¥Ø«e¶}¤F [1;31m%d[m ¶¡¥]´[", roomc);
    send_to_user(cuser, chatbuf, 0, number);

    sprintf(chatbuf, "¡ó ¦@¦³ [1;36m%d[m ¤H¨ÓÂ\\Àsªù°}", userc);
    if (suserc)
	sprintf(chatbuf + strlen(chatbuf), " [%d ¤H¦b¯µ±K²á¤Ñ«Ç]", suserc);
    send_to_user(cuser, chatbuf, 0, number);
}


static int
login_user(ChatUser *cu, char *msg)
{
    int utent;

    char *passwd;
    char *userid;
    char *chatid;
    struct sockaddr_in from;
    int fromlen;
    struct hostent *hp;


    ACCT acct;
    int level;

    /*
     * Thor.0819: SECURED_CHATROOM : /! userid chatid passwd , userno
     * el ¦bcheck§¹passwd«á¨ú±o
     */
    /* Xshadow.0915: common client support : /-! userid chatid password */

    /* ¶Ç°Ñ¼Æ¡Guserlevel, userid, chatid */

    /* client/server ª©¥»¨Ì¾Ú userid §ì .PASSWDS §PÂ_ userlevel */

    userid = nextword(&msg);
    chatid = nextword(&msg);


#ifdef  DEBUG
    logit("ENTER", userid);
#endif
    /* Thor.0730: parse space before passwd */
    passwd = msg;

    /* Thor.0813: ¸õ¹L¤@ªÅ®æ§Y¥i, ¦]¬°¤Ï¥¿¦pªGchatid¦³ªÅ®æ, ±K½X¤]¤£¹ï */
    /* ´Nºâ±K½X¹ï, ¤]¤£·|«ç»ò¼Ë:p */
    /* ¥i¬O¦pªG±K½X²Ä¤@­Ó¦r¬OªÅ®æ, ¨º¸õ¤Ó¦hªÅ®æ·|¶i¤£¨Ó... */
    if (*passwd == ' ')
	passwd++;

    /* Thor.0729: load acct */
    if (!*userid || (acct_load(&acct, userid) < 0))
    {

#ifdef  DEBUG
	logit("noexist", chatid);
#endif

	if (cu->clitype)
	    send_to_user(cu, "¿ù»~ªº¨Ï¥ÎªÌ¥N¸¹", 0, ERR_LOGIN_NOSUCHUSER);
	else
	    send_to_user(cu, CHAT_LOGIN_INVALID, 0, 0);

	return -1;
    }
    else if(strncmp(passwd, acct.passwd, PASSLEN) &&
	    !chkpasswd(acct.passwd, passwd))
    {
#ifdef  DEBUG
	logit("fake", chatid);
#endif

	if (cu->clitype)
	    send_to_user(cu, "±K½X¿ù»~", 0, ERR_LOGIN_PASSERROR);
	else
	    send_to_user(cu, CHAT_LOGIN_INVALID, 0, 0);
	return -1;
    }

    /* Thor.0729: if ok, read level.  */
    level = acct.userlevel;
    /* Thor.0819: read userno for client/server bbs */
    utent = searchuser(acct.userid);
    assert(utent);

    /* Thor.0819: for client/server bbs */
/*
  for (xuser = mainuser; xuser; xuser = xuser->unext)
  {
  if (xuser->userno == utent)
  {

  #ifdef  DEBUG
  logit("enter", "bogus");
  #endif
  if (cu->clitype)
  send_to_user(cu, "½Ð¤Å¬£»º¤À¨­¶i¤J²á¤Ñ«Ç !!", 0, ERR_LOGIN_USERONLINE);
  else
  send_to_user(cu, CHAT_LOGIN_BOGUS, 0, 0);
  return -1;    
  }
  }
*/
    if (!valid_chatid(chatid))
    {
#ifdef  DEBUG
	logit("enter", chatid);
#endif

	if (cu->clitype)
	    send_to_user(cu, "¤£¦Xªkªº²á¤Ñ«Ç¥N¸¹ !!", 0, ERR_LOGIN_NICKERROR);
	else
	    send_to_user(cu, CHAT_LOGIN_INVALID, 0, 0);
	return 0;
    }

    if (cuser_by_chatid(chatid) != NULL)
    {
	/* chatid in use */

#ifdef  DEBUG
	logit("enter", "duplicate");
#endif

	if (cu->clitype)
	    send_to_user(cu, "³o­Ó¥N¸¹¤w¸g¦³¤H¨Ï¥Î", 0, ERR_LOGIN_NICKINUSE);
	else
	    send_to_user(cu, CHAT_LOGIN_EXISTS, 0, 0);
	return 0;
    }

    cu->userno = utent;
    cu->uflag = level & ~(PERM_ROOMOP | PERM_CLOAK | PERM_HANDUP | PERM_SAY);
    /* Thor: ¶i¨Ó¥ý²MªÅROOMOP(¦PPERM_CHAT), CLOAK */
    strcpy(cu->userid, userid);
    strlcpy(cu->chatid, chatid, sizeof(cu->chatid));

    /* Xshadow: ¨ú±o client ªº¨Ó·½ */
    fromlen = sizeof(from);
    if (!getpeername(cu->sock, (struct sockaddr *) & from, &fromlen))
    {
	if ((hp = gethostbyaddr((char *) &from.sin_addr, sizeof(struct in_addr), from.sin_family)))
	    strcpy(cu->lasthost, hp->h_name);
	else
	    strcpy(cu->lasthost, (char *) inet_ntoa(from.sin_addr));
    }
    else
	strcpy(cu->lasthost, "[¥~¤ÓªÅ]");

    if (cu->clitype)
	send_to_user(cu, "¶¶§Q", 0, MSG_LOGINOK);
    else
	send_to_user(cu, CHAT_LOGIN_OK, 0, 0);

    arrive_room(cu, &mainroom);

    send_to_user(cu, "", 0, MSG_MOTDSTART);
    print_user_counts(cu);
    send_to_user(cu, "", 0, MSG_MOTDEND);

#ifdef  DEBUG
    logit("enter", "OK");
#endif

    return 0;
}


static void
chat_act(ChatUser *cu, char *msg)
{
    if (*msg && (!RHANDUP(cu->room) || SAY(cu) || ROOMOP(cu)))
    {
	sprintf(chatbuf, "%s [36m%s[m", cu->chatid, msg);
	send_to_room(cu->room, chatbuf, cu->userno, MSG_MESSAGE);
    }
}


static void
chat_ignore(ChatUser *cu, char *msg)
{

    if (RESTRICTED(cu))
    {
	strcpy(chatbuf, "¡° ±z¨S¦³ ignore §O¤HªºÅv§Q");
    }
    else
    {
	char *ignoree;

	ignoree = nextword(&msg);
	if (*ignoree)
	{
	    ChatUser *xuser;

	    xuser = cuser_by_userid(ignoree);

	    if (xuser == NULL)
	    {
		sprintf(chatbuf, msg_no_such_id, ignoree);
	    }
	    else if (xuser == cu || CHATSYSOP(xuser) ||
		     (ROOMOP(xuser) && (xuser->room == cu->room)))
	    {
		sprintf(chatbuf, "¡» ¤£¥i¥H ignore [%s]", ignoree);
	    }
	    else
	    {

		if (list_belong(cu->ignore, xuser->userno))
		{
		    sprintf(chatbuf, "¡° %s ¤w¸g³Q­áµ²¤F", xuser->chatid);
		}
		else
		{
		    list_add(&(cu->ignore), xuser);
		    sprintf(chatbuf, "¡» ±N [%s] ¥´¤J§N®c¤F :p", xuser->chatid);
		}
	    }
	}
	else
	{
	    UserList *list;

	    if((list = cu->ignore))
	    {
		int len;
		char buf[16];

		send_to_user(cu, "¡» ³o¨Ç¤H³Q¥´¤J§N®c¤F¡G", 0, MSG_MESSAGE);
		len = 0;
		do
		{
		    sprintf(buf, "%-13s", list->userid);
		    strcpy(chatbuf + len, buf);
		    len += 13;
		    if (len >= 78)
		    {
			send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
			len = 0;
		    }
		} while((list = list->next));

		if (len == 0)
		    return;
	    }
	    else
	    {
		strcpy(chatbuf, "¡» ±z¥Ø«e¨Ã¨S¦³ ignore ¥ô¦ó¤H");
	    }
	}
    }

    send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
}


static void
chat_unignore(ChatUser *cu, char *msg)
{
    char *ignoree;

    ignoree = nextword(&msg);

    if (*ignoree)
    {
	sprintf(chatbuf, (list_delete(&(cu->ignore), ignoree)) ?
		"¡» [%s] ¤£¦A³Q§A§N¸¨¤F" :
		"¡» ±z¨Ã¥¼ ignore [%s] ³o¸¹¤Hª«", ignoree);
    }
    else
    {
	strcpy(chatbuf, "¡» ½Ð«ü©ú user ID");
    }
    send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
}


static void
chat_join(ChatUser *cu, char *msg)
{
    if (RESTRICTED(cu))
    {
	send_to_user(cu, "¡° ±z¨S¦³¥[¤J¨ä¥L²á¤Ñ«ÇªºÅv­­", 0, MSG_MESSAGE);
    }
    else
    {
	char *roomid = nextword(&msg);

	if (*roomid)
	    enter_room(cu, roomid, msg);
	else
	    send_to_user(cu, "¡° ½Ð«ü©w²á¤Ñ«Çªº¦W¦r", 0, MSG_MESSAGE);
    }
}


static void
chat_kick(ChatUser *cu, char *msg)
{
    char *twit;
    ChatUser *xuser;
    ChatRoom *room;

    if (!ROOMOP(cu))
    {
	send_to_user(cu, msg_not_op, 0, MSG_MESSAGE);
	return;
    }

    twit = nextword(&msg);
    xuser = cuser_by_chatid(twit);

    if (xuser == NULL)
    {
	sprintf(chatbuf, msg_no_such_id, twit);
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	return;
    }

    room = cu->room;
    if (room != xuser->room || CLOAK(xuser))
    {                             /* Thor: ²á¤Ñ«ÇÁô¨­³N */
	sprintf(chatbuf, msg_not_here, twit);
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	return;
    }

    if (CHATSYSOP(xuser))
    {                             /* Thor: ½ð¤£¨« CHATSYSOP */
	sprintf(chatbuf, "¡» ¤£¥i¥H kick [%s]", twit);
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	return;
    }

    exit_room(xuser, EXIT_KICK, (char *) NULL);

    if (room == &mainroom)
	logout_user(xuser);
    else
	enter_room(xuser, MAIN_NAME, (char *) NULL);
}


static void
chat_makeop(ChatUser *cu, char *msg)
{
    char *newop;
    ChatUser *xuser;
    ChatRoom *room;

    if (!ROOMOP(cu))
    {
	send_to_user(cu, msg_not_op, 0, MSG_MESSAGE);
	return;
    }

    newop = nextword(&msg);
    xuser = cuser_by_chatid(newop);

    if (xuser == NULL)
    {
	sprintf(chatbuf, msg_no_such_id, newop);
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	return;
    }

    if (cu == xuser)
    {
	sprintf(chatbuf, "¡° ±z¦­´N¤w¸g¬O Op ¤F°Ú");
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	return;
    }

    room = cu->room;

    if (room != xuser->room || CLOAK(xuser))
    {                             /* Thor: ²á¤Ñ«ÇÁô¨­³N */
	sprintf(chatbuf, msg_not_here, xuser->chatid);
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	return;
    }

    cu->uflag &= ~PERM_ROOMOP;
    xuser->uflag |= PERM_ROOMOP;

    user_changed(cu);
    user_changed(xuser);

    sprintf(chatbuf, "¡° %s ±N Op Åv¤OÂà²¾µ¹ %s",
	    cu->chatid, xuser->chatid);
    if (!CLOAK(cu))               /* Thor: ²á¤Ñ«ÇÁô¨­³N */
	send_to_room(room, chatbuf, 0, MSG_MESSAGE);
}



static void
chat_invite(ChatUser *cu, char *msg)
{
    char *invitee;
    ChatUser *xuser;
    ChatRoom *room;
    UserList **list;

    if (!ROOMOP(cu))
    {
	send_to_user(cu, msg_not_op, 0, MSG_MESSAGE);
	return;
    }

    invitee = nextword(&msg);
    xuser = cuser_by_chatid(invitee);
    if (xuser == NULL)
    {
	sprintf(chatbuf, msg_no_such_id, invitee);
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	return;
    }

    room = cu->room;
    assert(room);
    list = &(room->invite);

    if (list_belong(*list, xuser->userno))
    {
	sprintf(chatbuf, "¡° %s ¤w¸g±µ¨ü¹LÁÜ½Ð¤F", xuser->chatid);
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	return;
    }
    list_add(list, xuser);

    sprintf(chatbuf, "¡° %s ÁÜ½Ð±z¨ì [%s] ²á¤Ñ«Ç",
	    cu->chatid, room->name);
    send_to_user(xuser, chatbuf, 0, MSG_MESSAGE); /* Thor: ­n¤£­n¥i¥H ignore? */
    sprintf(chatbuf, "¡° %s ¦¬¨ì±zªºÁÜ½Ð¤F", xuser->chatid);
    send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
}


static void
chat_broadcast(ChatUser *cu, char *msg)
{
    if (!CHATSYSOP(cu))
    {
	send_to_user(cu, "¡° ±z¨S¦³¦b²á¤Ñ«Ç¼s¼½ªºÅv¤O!", 0, MSG_MESSAGE);
	return;
    }
    if (*msg == '\0')
    {
	send_to_user(cu, "¡° ½Ð«ü©w¼s¼½¤º®e", 0, MSG_MESSAGE);
	return;
    }
    sprintf(chatbuf, "[1m¡° " BBSNAME "½Í¤Ñ«Ç¼s¼½¤¤ [%s].....[m",
	    cu->chatid);
    send_to_room(ROOM_ALL, chatbuf, 0, MSG_MESSAGE);
    sprintf(chatbuf, "¡» %s", msg);
    send_to_room(ROOM_ALL, chatbuf, 0, MSG_MESSAGE);
}


static void
chat_goodbye(ChatUser *cu, char *msg)
{
    exit_room(cu, EXIT_LOGOUT, msg);
    /* Thor: ­n¤£­n¥[ logout_user(cu) ? */
}


/* --------------------------------------------- */
/* MUD-like social commands : action             */
/* --------------------------------------------- */

struct ChatAction
{
    char *verb;                   /* °Êµü */
    char *chinese;                /* ¤¤¤åÂ½Ä¶ */
    char *part1_msg;              /* ¤¶µü */
    char *part2_msg;              /* °Ê§@ */
};


static ChatAction party_data[] =
{
    {"aluba", "ªü¾|¤Ú", "§â", "¬[¤W¬W¤lªü¾|¤Ú!!"},
    {"aodre", "´º¥õ", "¹ï", "ªº´º¥õ¦³¦p·Ê·Ê¦¿¤ô,³sºø¤£µ´¡K¡K"},
    {"bearhug", "¼ö¾Ö", "¼ö±¡ªº¾Ö©ê", ""},
    {"blade", "¤@¤M", "¤@¤M±Òµ{§â", "°e¤W¦è¤Ñ"},
    {"bless", "¯¬ºÖ", "¯¬ºÖ", "¤ß·Q¨Æ¦¨"},
    {"board", "¥D¾÷ªO", "§â", "§ì¥h¸÷¥D¾÷ªO"},
    {"bokan", "®ð¥\\", "Âù´x·L¦X¡A»W¶Õ«Ýµo¡K¡K¬ðµM¶¡¡A¹q¥ú¥E²{¡A¹ï", "¨Ï¥X¤F¢Ðo--¢Ùan¡I"},
  {"bow", "Áù°`", "²¦°`²¦·qªº¦V", "Áù°`"},
  {"box", "¹õ¤§¤º", "¶}©l½üÂ\\¦¡²¾¦ì¡A¹ï", "§@¨xÅ¦§ðÀ»"},
  {"boy", "¥­©³Áç", "±q­I«á®³¥X¤F¥­©³Áç¡A§â", "ºV©ü¤F"},
  {"bye", "ÙTÙT", "¦V", "»¡ÙTÙT!!"},
  {"call", "©I³ê", "¤jÁnªº©I³ê,°Ú~~", "°Ú~~~§A¦b­þ¸Ì°Ú°Ú°Ú°Ú~~~~"},
  {"caress", "»´¼¾", "»´»´ªº¼¾ºNµÛ", ""},
  {"clap", "¹ª´x", "¦V", "¼ö¯P¹ª´x"},
  {"claw", "§ì§ì", "±q¿ß«}¼Ö¶é­É¤F°¦¿ß¤ö¡A§â", "§ì±o¦º¥h¬¡¨Ó"},
  {"comfort", "¦w¼¢", "·Å¨¥¦w¼¢", ""},
  {"cong", "®¥³ß", "±q­I«á®³¥X¤F©Ô¬¶¡AËé¡IËé¡I®¥³ß", ""},
  {"cpr", "¤f¹ï¤f", "¹ïµÛ", "°µ¤f¹ï¤f¤H¤u©I§l"},
  {"cringe", "¤^¼¦", "¦V", "¨õ°`©}½¥¡A·n§À¤^¼¦"},
  {"cry", "¤j­ú", "¦V", "Àz°Þ¤j­ú"},
  {"dance", "¸õ»R", "©Ô¤F", "ªº¤â½¡½¡°_»R"  },
  {"destroy", "·´·À", "²½°_¤F¡y·¥¤j·´·À©G¤å¡z¡AÅF¦V", ""},
  {"dogleg", "ª¯»L", "¹ï", "ª¯»L"},
  {"drivel", "¬y¤f¤ô", "¹ïµÛ", "¬y¤f¤ô"},
  {"envy", "¸r¼}", "¦V", "¬yÅS¥X¸r¼}ªº²´¥ú"},
  {"eye", "°e¬îªi", "¹ï", "ÀW°e¬îªi"},
  {"fire", "¾R°Ý", "®³µÛ¤õ¬õªºÅK´Î¨«¦V", ""},
  {"forgive", "­ì½Ì", "±µ¨ü¹Dºp¡A­ì½Ì¤F", ""},
  {"french", "ªk¦¡§k", "§â¦ÞÀY¦ù¨ì", "³ïÄV¸Ì¡ã¡ã¡ã«z¡I¤@­Ó®öº©ªºªk°ê¤ó²`§k"},
  {"giggle", "¶Ì¯º", "¹ïµÛ", "¶Ì¶Ìªº§b¯º"},
  {"glue", "¸É¤ß", "¥Î¤T¬í½¦¡A§â", "ªº¤ßÂH¤F°_¨Ó"},
  {"goodbye", "§i§O", "²\\²´¨L¨Lªº¦V", "§i§O"},
  {"grin", "¦l¯º", "¹ï", "ÅS¥X¨¸´cªº¯º®e"},
  {"growl", "©H­ý", "¹ï", "©H­ý¤£¤w"},
  {"hand", "´¤¤â", "¸ò", "´¤¤â"},
  {"hide", "¸ú", "¸ú¦b", "­I«á"},
  {"hospitl", "°eÂå°|", "§â", "°e¶iÂå°|"},
  {"hug", "¾Ö©ê", "»´»´¦a¾Ö©ê", ""},
  {"hrk", "ª@Às®±", "¨IÃ­¤F¨­§Î¡A¶×»E¤F¤º«l¡A¹ï", "¨Ï¥X¤F¤@°O¢Öo--¢àyu--¢Ùan¡I¡I¡I"},
  {"jab", "ÂW¤H", "·Å¬XªºÂWµÛ", ""},
  {"judo", "¹LªÓºL", "§ì¦í¤F", "ªº¦çÃÌ¡AÂà¨­¡K¡K°Ú¡A¬O¤@°O¹LªÓºL¡I"},
  {"kickout", "½ð", "¥Î¤j¸}§â", "½ð¨ì¤s¤U¥h¤F"},
  {"kick", "½ð¤H", "§â", "½ðªº¦º¥h¬¡¨Ó"},
  {"kiss", "»´§k", "»´§k", "ªºÁyÀU"},
  {"laugh", "¼J¯º", "¤jÁn¼J¯º", ""},
  {"levis", "µ¹§Ú", "»¡¡Gµ¹§Ú", "¡I¨ä¾l§K½Í¡I"},
  {"lick", "»Q", "¨g»Q", ""},
  {"lobster", "À£¨î", "¬I®i°f½¼§Î©T©w¡A§â", "À£¨î¦b¦aªO¤W"},
  {"love", "ªí¥Õ", "¹ï", "²`±¡ªºªí¥Õ"},
  {"marry", "¨D±B", "±·µÛ¤E¦Ê¤E¤Q¤E¦·ª´ºÀ¦V", "¨D±B"},
  {"no", "¤£­n°Ú", "«÷©R¹ïµÛ", "·nÀY~~~~¤£­n°Ú~~~~"},
  {"nod", "ÂIÀY", "¦V", "ÂIÀYºÙ¬O"},
  {"nudge", "³»¨{¤l", "¥Î¤â¨y³»", "ªºªÎ¨{¤l"},
  {"pad", "©çªÓ»H", "»´©ç", "ªºªÓ»H"},
  {"pettish", "¼»¼b", "¸ò", "ÜÝÁnÜÝ®ð¦a¼»¼b"},
  {"pili", "ÅRÆE", "¨Ï¥X §g¤l­· ¤Ñ¦a®Ú ¯ë­YÄb ¤T¦¡¦X¤@¥´¦V", "~~~~~~"},
  {"pinch", "À¾¤H", "¥Î¤Oªº§â", "À¾ªº¶Â«C"},
  {"roll", "¥´ºu", "©ñ¥X¦hº¸³Oªº­µ¼Ö,", "¦b¦a¤Wºu¨Óºu¥h"},
  {"protect", "«OÅ@", "«OÅ@µÛ", ""},
  {"pull", "©Ô", "¦º©R¦a©Ô¦í", "¤£©ñ"},
  {"punch", "´~¤H", "¬½¬½´~¤F", "¤@¹y"},
  {"rascal", "­A¿à", "¸ò", "­A¿à"},
  {"recline", "¤JÃh", "Æp¨ì", "ªºÃh¸ÌºÎµÛ¤F¡K¡K"},
  {"respond", "­t³d", "¦w¼¢", "»¡¡G¡y¤£­n­ú¡A§Ú·|­t³dªº¡K¡K¡z"},
  {"shrug", "ÁqªÓ", "µL©`¦a¦V", "Áq¤FÁqªÓ»H"},
  {"sigh", "¼Û®ð", "¹ï", "¼Û¤F¤@¤f®ð"},
  {"slap", "¥´¦Õ¥ú", "°Ô°Ôªº¤Ú¤F", "¤@¹y¦Õ¥ú"},
  {"smooch", "¾Ö§k", "¾Ö§kµÛ", ""},
  {"snicker", "ÅÑ¯º", "¼K¼K¼K..ªº¹ï", "ÅÑ¯º"},
  {"sniff", "¤£®h", "¹ï", "¶á¤§¥H»ó"},
  {"spank", "¥´§¾§¾", "¥Î¤Ú´x¥´", "ªºÁv³¡"},
  {"squeeze", "ºò¾Ö", "ºòºò¦a¾Ö©êµÛ", ""},
  {"sysop", "¥l³ê", "¥s¥X¤F§å½ð½ð¡A§â", "½ò«ó¤F¡I"},
  {"thank", "·PÁÂ", "¦V", "·PÁÂ±o¤­Åé§ë¦a"},
  {"tickle", "·kÄo", "©B¼T!©B¼T!·k", "ªºÄo"},
  {"wake", "·n¿ô", "»´»´¦a§â", "·n¿ô"},
  {"wave", "´§¤â", "¹ïµÛ", "«÷©Rªº·n¤â"},
  {"welcome", "Åwªï", "Åwªï", "¶i¨Ó¤K¨ö¤@¤U"},
  {"what", "¤°»ò", "»¡¡G¡y", "­ù¤½½M±K«zÃ÷Å¥¬Y?¡H?¡S?¡z"},
  {"whip", "Ã@¤l", "¤â¤W®³µÛÄúÀë¡A¥ÎÃ@¤lµh¥´", ""},
  {"wink", "¯w²´", "¹ï", "¯«¯µªº¯w¯w²´·ú"},
  {"zap", "²r§ð", "¹ï", "ºÆ¨gªº§ðÀ»"},
  {NULL, NULL, NULL, NULL}
};

static int
party_action(ChatUser *cu, char *cmd, char *party)
{
    ChatAction *cap;
    char *verb;

    for (cap = party_data; (verb = cap->verb); cap++)
    {
	if (!str_equal(cmd, verb))
	    continue;
	if (*party == '\0')
	    party = "¤j®a";
	else
	{
	    ChatUser *xuser;

	    xuser = fuzzy_cuser_by_chatid(party);
	    if (xuser == NULL)
	    {                       /* Thor.0724: ¥Î userid¤]¹À³q */
		xuser = cuser_by_userid(party);
	    }

	    if (xuser == NULL)
	    {
		sprintf(chatbuf, msg_no_such_id, party);
		send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
		return 0;
	    }
	    else if (xuser == FUZZY_USER)
	    {
		sprintf(chatbuf, "¡° ½Ð«ü©ú²á¤Ñ¥N¸¹");
		send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
		return 0;
	    }
	    else if (cu->room != xuser->room || CLOAK(xuser))
	    {
		sprintf(chatbuf, msg_not_here, party);
		send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
		return 0;
	    }
	    else
	    {
		party = xuser->chatid;
	    }
	}
	sprintf(chatbuf, "[1;32m%s [31m%s[33m %s [31m%s[m",
		cu->chatid, cap->part1_msg, party, cap->part2_msg);
	send_to_room(cu->room, chatbuf, cu->userno, MSG_MESSAGE);
	return 0;
    }
    return 1;
}


/* --------------------------------------------- */
/* MUD-like social commands : speak              */
/* --------------------------------------------- */


static ChatAction speak_data[] =
{
    { "ask", "¸ß°Ý", "°Ý", NULL },
    { "chant", "ºq¹|", "°ªÁnºq¹|", NULL },
    { "cheer", "³Üªö", "³Üªö", NULL },
    { "chuckle", "»´¯º", "»´¯º", NULL },
    { "curse", "·t·F", "·t·F", NULL },
    /* {"curse", "©G½|", NULL}, */
    { "demand", "­n¨D", "­n¨D", NULL },
    { "frown", "½K¬ÜÀY", "ÂÙ¬Ü", NULL },
    { "groan", "©D§u", "©D§u", NULL },
    { "grumble", "µo¨cÄÌ", "µo¨cÄÌ", NULL },
    { "guitar", "¼u°Û", "Ãä¼uµÛ¦N¥L¡AÃä°ÛµÛ", NULL },
    /* {"helpme", "©I±Ï","¤jÁn©I±Ï",NULL}, */
    { "hum", "³ä³ä", "³ä³ä¦Û»y", NULL },
    { "moan", "«è¹Ä", "«è¹Ä", NULL },
    { "notice", "±j½Õ", "±j½Õ", NULL },
    { "order", "©R¥O", "©R¥O", NULL },
    { "ponder", "¨H«ä", "¨H«ä", NULL },
    { "pout", "äþ¼L", "äþµÛ¼L»¡", NULL },
    { "pray", "¬èÃ«", "¬èÃ«", NULL },
    { "request", "Àµ¨D", "Àµ¨D", NULL },
    { "shout", "¤j½|", "¤j½|", NULL },
    { "sing", "°Ûºq", "°Ûºq", NULL },
    { "smile", "·L¯º", "·L¯º", NULL },
    { "smirk", "°²¯º", "°²¯º", NULL },
    { "swear", "µo»}", "µo»}", NULL },
    { "tease", "¼J¯º", "¼J¯º", NULL },
    { "whimper", "¶ã«|", "¶ã«|ªº»¡", NULL },
    { "yawn", "«¢¤í", "Ãä¥´«¢¤íÃä»¡", NULL },
    { "yell", "¤j³Û", "¤j³Û", NULL },
    { NULL, NULL, NULL, NULL }
};


static int
speak_action(ChatUser *cu, char *cmd, char *msg)
{
    ChatAction *cap;
    char *verb;

    for (cap = speak_data; (verb = cap->verb); cap++)
    {
	if (!str_equal(cmd, verb))
	    continue;
	sprintf(chatbuf, "[1;32m%s [31m%s¡G[33m %s[m",
		cu->chatid, cap->part1_msg, msg);
	send_to_room(cu->room, chatbuf, cu->userno, MSG_MESSAGE);
	return 0;
    }
    return 1;
}


/* -------------------------------------------- */
/* MUD-like social commands : condition          */
/* -------------------------------------------- */


static ChatAction condition_data[] =
{
    { "applaud", "©ç¤â", "°Ô°Ô°Ô°Ô°Ô°Ô°Ô....", NULL },
    { "ayo", "­üËç³Þ", "­üËç³Þ~~~", NULL },
    { "back", "§¤¦^¨Ó", "¦^¨Ó§¤¥¿Ä~Äò¾Ä¾Ô", NULL },
    { "blood", "¦b¦å¤¤", "­Ë¦b¦åªy¤§¤¤", NULL },
    { "blush", "Áy¬õ", "Áy³£¬õ¤F", NULL },
    { "broke", "¤ß¸H", "ªº¤ß¯}¸H¦¨¤@¤ù¤@¤ùªº", NULL },
    /* {"bokan", "Bo Kan! Bo Kan!", NULL}, */
    { "careles", "¨S¤H²z", "¶ã¡ã¡ã³£¨S¦³¤H²z§Ú :~~~~", NULL },
    { "chew", "¶ß¥Ê¤l", "«Ü±y¶¢ªº¶ß°_¥Ê¤l¨Ó¤F", NULL },
    { "climb", "ª¦¤s", "¦Û¤vºCºCª¦¤W¤s¨Ó¡K¡K", NULL },
    { "cold", "·P«_¤F", "·P«_¤F,¶ý¶ý¤£Åý§Ú¥X¥hª± :~~~(", NULL },
    { "cough", "«y¹Â", "«y¤F´XÁn", NULL },
    { "die", "¼ÉÀÅ", "·í³õ¼ÉÀÅ", NULL },
    { "faint", "©ü­Ë", "·í³õ©ü­Ë", NULL },
    { "flop", "­»¿¼¥Ö", "½ò¨ì­»¿¼¥Ö... ·Æ­Ë¡I", NULL },
    { "fly", "ÄÆÄÆµM", "ÄÆÄÆµM", NULL },
    { "frown", "ÂÙ¬Ü", "ÂÙ¬Ü", NULL },
    { "gold", "®³ª÷µP", "°ÛµÛ¡G¡yª÷£|£±£½ª÷£|£±£½  ¥X°ê¤ñÁÉ! ±o«a­x¡A®³ª÷µP¡A¥úºa­Ë¾H¨Ó¡I¡z", NULL },
    { "gulu", "¨{¤l¾j", "ªº¨{¤lµo¥X©BÂP~~~©BÂP~~~ªºÁn­µ", NULL },
    { "haha", "«z«¢«¢", "«z«¢«¢«¢.....^o^", NULL },
    /* {"haha", "¤j¯º","«z«¢«¢«¢«¢«¢«¢«¢«¢~~~~!!!!!", NULL}, */
    { "helpme", "¨D±Ï", "¤j³Û~~~±Ï©R°Ú~~~~", NULL },
    { "hoho", "¨þ¨þ¯º", "¨þ¨þ¨þ¯º­Ó¤£°±", NULL },
    { "happy", "°ª¿³", "°ª¿³±o¦b¦a¤W¥´ºu", NULL },
    /* {"happy", "°ª¿³", "¢ç¢Ï¡I *^_^*", NULL}, */
    /* {"happy", "", "r-o-O-m....Å¥¤F¯u²n¡I", NULL}, */
    /* {"hurricane", "¢Ö¢÷---¢à£B¢ý--¢Ù¢é¢ö¡I¡I¡I", NULL}, */
    { "idle", "§b¦í¤F", "§b¦í¤F", NULL },
    { "jacky", "®Ì®Ì", "µl¤l¯ëªº®Ì¨Ó®Ì¥h", NULL },

#if 0
    /* Thor.0729: ¤£ª¾¨ä·N */
    { "lag", "ºô¸ôºC", "lllllllaaaaaaaaaaaagggggggggggggg.................", NULL },
#endif

    { "luck", "©¯¹B", "«z¡IºÖ®ð°Õ¡I", NULL },
    { "macarn", "¤@ºØ»R", "¶}©l¸õ°_¤F¢Ûa¢Ña¢àe¢Üa¡ã¡ã¡ã¡ã", NULL },
    { "miou", "ØpØp", "ØpØp¤f­]¤f­]¡ã¡ã¡ã¡ã¡ã", NULL },
    { "mouth", "«ó¼L", "«ó¼L¤¤!!", NULL },
    { "nani", "«ç»ò·|", "¡G©`£®°Ú®º??", NULL },
    { "nose", "¬y»ó¦å", "¬y»ó¦å", NULL },
    { "puke", "¹Ã¦R", "¹Ã¦R¤¤", NULL },
    /* {"puke", "¯uäú¤ß¡A§ÚÅ¥¤F³£·Q¦R", NULL}, */
    { "rest", "¥ð®§", "¥ð®§¤¤¡A½Ð¤Å¥´ÂZ", NULL },
    { "reverse", "Â½¨{", "Â½¨{", NULL },
    { "room", "¶}©Ð¶¡", "r-o-O-m-r-O-¢Ý-Mmm-rR¢à........", NULL },
    { "shake", "·nÀY", "·n¤F·nÀY", NULL },
    { "sleep", "ºÎµÛ", "­w¦bÁä½L¤WºÎµÛ¤F¡A¤f¤ô¬y¶iÁä½L¡A³y¦¨·í¾÷¡I", NULL },
    /* {"sleep", "Zzzzzzzzzz¡A¯uµL²á¡A³£§ÖºÎµÛ¤F", NULL}, */
    { "so", "´NÂæ¤l", "´NÂæ¤l!!", NULL },
    { "sorry", "¹Dºp", "¶ã°Ú!!§Ú¹ï¤£°_¤j®a,§Ú¹ï¤£°_°ê®aªÀ·|~~~~~~¶ã°Ú~~~~~", NULL },
    { "story", "Á¿¥j", "¶}©lÁ¿¥j¤F", NULL },
    { "strut", "·nÂ\\¨«", "¤j·n¤jÂ\\¦a¨«", NULL },
    { "suicide", "¦Û±þ", "¦Û±þ", NULL },
    { "tea", "ªw¯ù", "ªw¤F³ý¦n¯ù", NULL },
    { "think", "«ä¦Ò", "¬nµÛÀY·Q¤F¤@¤U", NULL },
    { "tongue", "¦R¦Þ", "¦R¤F¦R¦ÞÀY", NULL },
    { "wall", "¼²Àð", "¶]¥h¼²Àð", NULL },
    { "wawa", "«z«z", "«z«z«z~~~~~!!!!!  ~~~>_<~~~", NULL },
    /* {"wawa","«z«z«z......>_<",NULL},  */
    { "www", "¨L¨L", "¨L¨L¨L!!!", NULL },
    { "zzz", "¥´©I", "©IÂP~~~~ZZzZz£C¢èZZzzZzzzZZ...", NULL },
    { NULL, NULL, NULL, NULL }
};


static int
condition_action(ChatUser *cu, char *cmd)
{
    ChatAction *cap;
    char *verb;

    for (cap = condition_data; (verb = cap->verb); cap++)
    {
	if (str_equal(cmd, verb))
	{
	    sprintf(chatbuf, "[1;32m%s [31m%s[m",
		    cu->chatid, cap->part1_msg);
	    send_to_room(cu->room, chatbuf, cu->userno, MSG_MESSAGE);
	    return 1;
	}
    }
    return 0;
}


/* --------------------------------------------- */
/* MUD-like social commands : help               */
/* --------------------------------------------- */


static char *dscrb[] =
{
    "[1;37m¡i Verb + Nick¡G   °Êµü + ¹ï¤è¦W¦r ¡j[36m   ¨Ò¡G//kick piggy[m",
    "[1;37m¡i Verb + Message¡G°Êµü + ­n»¡ªº¸Ü ¡j[36m   ¨Ò¡G//sing ¤Ñ¤Ñ¤ÑÂÅ[m",
    "[1;37m¡i Verb¡G°Êµü ¡j    ¡ô¡õ¡GÂÂ¸Ü­«´£[m", NULL
};
ChatAction *catbl[] =
{
    party_data, speak_data, condition_data, NULL
};

static void
chat_partyinfo(ChatUser *cu, char *msg)
{
    if (!common_client_command)
	return;                     /* only allow common client to retrieve it */

    sprintf(chatbuf, "3 °Ê§@  ¥æ½Í  ª¬ºA");
    send_to_user(cu, chatbuf, 0, MSG_PARTYINFO);
}

static void
chat_party(ChatUser *cu, char *msg)
{
    int kind, i;
    ChatAction *cap;

    if (!common_client_command)
	return;

    kind = atoi(nextword(&msg));
    if (kind < 0 || kind > 2)
	return;

    sprintf(chatbuf, "%d  %s", kind, kind == 2 ? "I" : "");

    /* Xshadow: ¥u¦³ condition ¤~¬O immediate mode */
    send_to_user(cu, chatbuf, 0, MSG_PARTYLISTSTART);

    cap = catbl[kind];
    for (i = 0; cap[i].verb; i++)
    {
	sprintf(chatbuf, "%-10s %-20s", cap[i].verb, cap[i].chinese);
	/* for (j=0;j<1000000;j++); */
	send_to_user(cu, chatbuf, 0, MSG_PARTYLIST);
    }

    sprintf(chatbuf, "%d", kind);
    send_to_user(cu, chatbuf, 0, MSG_PARTYLISTEND);
}


#define SCREEN_WIDTH    80
#define MAX_VERB_LEN    8
#define VERB_NO         10

static void
view_action_verb(ChatUser *cu, char cmd)       /* Thor.0726: ·s¥[°Êµü¤ÀÃþÅã¥Ü */
{
    register int i;
    register char *p, *q, *data, *expn;
    register ChatAction *cap;

    send_to_user(cu, "/c", 0, MSG_CLRSCR);

    data = chatbuf;

    if (cmd < '1' || cmd > '3')
    {                             /* Thor.0726: ¼g±o¤£¦n, ·Q¿ìªk§ï¶i... */
	for (i = 0; (p = dscrb[i]); i++)
	{
	    sprintf(data, "  [//]help %d          - MUD-like ªÀ¥æ°Êµü   ²Ä %d Ãþ", i + 1, i + 1);
	    send_to_user(cu, data, 0, MSG_MESSAGE);
	    send_to_user(cu, p, 0, MSG_MESSAGE);
	    send_to_user(cu, " ", 0, MSG_MESSAGE);    /* Thor.0726: ´«¦æ, »Ý­n " "
						       * ¶Ü? */
	}
    }
    else
    {
	send_to_user(cu, dscrb[cmd-'1'], 0, MSG_MESSAGE);

	expn = chatbuf + 100;       /* Thor.0726: À³¸Ó¤£·|overlap§a? */

	*data = '\0';
	*expn = '\0';

	cap = catbl[cmd-'1'];

	for (i = 0; (p = cap[i].verb); i++)
	{
	    q = cap[i].chinese;

	    strcat(data, p);
	    strcat(expn, q);

	    if (((i + 1) % VERB_NO) == 0 || cap[i+1].verb==NULL)
	    {
		send_to_user(cu, data, 0, MSG_MESSAGE);
		send_to_user(cu, expn, 0, MSG_MESSAGE); /* Thor.0726: Åã¥Ü¤¤¤åµù¸Ñ */
		*data = '\0';
		*expn = '\0';
	    }
	    else
	    {
		strncat(data, "        ", MAX_VERB_LEN - strlen(p));
		strncat(expn, "        ", MAX_VERB_LEN - strlen(q));
	    }
	}
    }
    /* send_to_user(cu, " ",0); *//* Thor.0726: ´«¦æ, »Ý­n " " ¶Ü? */
}

/* ----------------------------------------------------- */
/* chat user service routines                            */
/* ----------------------------------------------------- */


static ChatCmd chatcmdlist[] =
{
    {"act", chat_act, 0},
    {"bye", chat_goodbye, 0},
    {"chatroom", chat_chatroom, 1}, /* Xshadow: for common client */
    {"clear", chat_clear, 0},
    {"cloak", chat_cloak, 2},
    {"date", chat_date, 0},
    {"flags", chat_setroom, 0},
    {"help", chat_help, 0},
    {"ignore", chat_ignore, 1},
    {"invite", chat_invite, 0},
    {"join", chat_join, 0},
    {"kick", chat_kick, 1},
    {"msg", chat_private, 0},
    {"nick", chat_nick, 0},
    {"operator", chat_makeop, 0},
    {"party", chat_party, 1},       /* Xshadow: party data for common client */
    {"partyinfo", chat_partyinfo, 1},       /* Xshadow: party info for common
					   * client */

    {"query", chat_query, 0},

    {"room", chat_list_rooms, 0},
    {"unignore", chat_unignore, 1},
    {"whoin", chat_list_by_room, 1},
    {"wall", chat_broadcast, 2},

    {"who", chat_map_chatids_thisroom, 0},
    {"list", chat_list_users, 0},
    {"topic", chat_topic, 1},
    {"version", chat_version, 1},

    {NULL, NULL, 0}
};

/* Thor: 0 ¤£¥Î exact, 1 ­n exactly equal, 2 ¯µ±K«ü¥O */


static int
command_execute(ChatUser *cu)
{
    char *cmd, *msg;
    ChatCmd *cmdrec;
    int match;

    msg = cu->ibuf;

    /* Validation routine */

    if (cu->room == NULL)
    {
	/* MUST give special /! or /-! command if not in the room yet */
	if(strncmp(msg, "/!", 2)==0) {
	    cu->clitype = 0;
	    return login_user(cu, msg+2);
	}
	if(strncmp(msg, "/-!", 3)==0) {
	    cu->clitype = 1;
	    return login_user(cu, msg+3);
	}
	return -1;
    }

    if(msg[0] == '\0')
	return 0;

    /* If not a "/"-command, it goes to the room. */
    if (msg[0] != '/')
    {
	char buf[16];

	sprintf(buf, "%s:", cu->chatid);
	sprintf(chatbuf, "%-10s%s", buf, msg);
	if (!CLOAK(cu))           /* Thor: ²á¤Ñ«ÇÁô¨­³N */
	    send_to_room(cu->room, chatbuf, cu->userno, MSG_MESSAGE);
	return 0;
    }

    msg++;
    cmd = nextword(&msg);
    match = 0;

    if (*cmd == '/')
    {
	cmd++;
	if (!*cmd || str_equal(cmd, "help"))
	{
	    /* Thor.0726: °Êµü¤ÀÃþ */
	    cmd = nextword(&msg);
	    view_action_verb(cu, *cmd);
	    match = 1;
	}
	else if (party_action(cu, cmd, msg) == 0)
	    match = 1;
	else if (speak_action(cu, cmd, msg) == 0)
	    match = 1;
	else
	    match = condition_action(cu, cmd);
    }
    else
    {
	char *str;

	common_client_command = 0;
	if((*cmd == '-')) {
	    if(cu->clitype) {
		cmd++;                  /* Xshadow: «ü¥O±q¤U¤@­Ó¦r¤¸¤~¶}©l */
		common_client_command = 1;
	    }
	}
	for(cmdrec = chatcmdlist; (str = cmdrec->cmdstr); cmdrec++)
	{
		switch (cmdrec->exact)
		{
		case 1:                   /* exactly equal */
		    match = str_equal(cmd, str);
		    break;
		case 2:                   /* Thor: secret command */
		    if (CHATSYSOP(cu))
			match = str_equal(cmd, str);
		    break;
		default:                  /* not necessary equal */
		    match = str_match(cmd, str) >= 0;
		    break;
		}

		if (match)
		{
		    cmdrec->cmdfunc(cu, msg);
		    break;
		}
	    }
    }

    if (!match)
    {
	sprintf(chatbuf, "¡» «ü¥O¿ù»~¡G/%s", cmd);
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
    }
    return 0;
}


/* ----------------------------------------------------- */
/* serve chat_user's connection                          */
/* ----------------------------------------------------- */


static int
cuser_serve(ChatUser *cu)
{
    register int ch, len, isize;
    register char *str, *cmd;
    char buf[80];

    str = buf;
    len = recv(cu->sock, str, sizeof(buf) - 1, 0);
    if (len <= 0)
    {
	/* disconnected */

	exit_room(cu, EXIT_LOSTCONN, (char *) NULL);
	return -1;
    }

#if 0
    /* Xshadow: ±N°e¹Fªº¸ê®Æ©¾¹ê¬ö¿ý¤U¨Ó */
    memcpy(logbuf, buf, sizeof(buf));
    for (ch = 0; ch < sizeof(buf); ch++)
	if (!logbuf[ch])
	    logbuf[ch] = '$';

    logbuf[len + 1] = '\0';
    logit("recv: ", logbuf);
#endif

#if 0
    logit(cu->userid, str);
#endif

    isize = cu->isize;
    cmd = cu->ibuf;
    while (len--) {
	ch = *str++;

	if (ch == '\r' || ch == '\0')
	    continue;
	if (ch == '\n')
	{
	    cmd[isize]='\0';
	    isize = 0;

	    if (command_execute(cu) < 0)
		return -1;

	    continue;
	}
	if (isize < sizeof(cu->ibuf)-1)
	    cmd[isize++] = ch;
    }
    cu->isize = isize;
    return 0;
}


/* ----------------------------------------------------- */
/* chatroom server core routines                         */
/* ----------------------------------------------------- */

static int
start_daemon()
{
    int fd, value;
    char buf[80];
    struct sockaddr_in fsin;
    struct linger ld;
    struct rlimit limit;
    time_t dummy;
    struct tm *dummy_time;

    /*
     * More idiot speed-hacking --- the first time conversion makes the C
     * library open the files containing the locale definition and time zone.
     * If this hasn't happened in the parent process, it happens in the
     * children, once per connection --- and it does add up.
     */

    time(&dummy);
    dummy_time = gmtime(&dummy);
    dummy_time = localtime(&dummy);
    strftime(buf, 80, "%d/%b/%Y:%H:%M:%S", dummy_time);

    /* --------------------------------------------------- */
    /* speed-hacking DNS resolve                           */
    /* --------------------------------------------------- */

    gethostname(buf, sizeof(buf));

    /* Thor: ¸U¤@server©|¥¼±µ¨üconnection, ´N¦^¥hªº¸Ü, client ²Ä¤@¦¸·|¶i¤J¥¢±Ñ */
    /* ©Ò¥H²¾¦Ü listen «á */

    /* --------------------------------------------------- */
    /* detach daemon process                               */
    /* --------------------------------------------------- */

    close(0);
    close(1);
    close(2);

    if (fork())
	exit(0);

    chdir(BBSHOME);

    setsid();

    attach_SHM();
    /* --------------------------------------------------- */
    /* adjust the resource limit                           */
    /* --------------------------------------------------- */

    getrlimit(RLIMIT_NOFILE, &limit);
    limit.rlim_cur = limit.rlim_max;
    setrlimit(RLIMIT_NOFILE, &limit);

#if 0
    while (fd)
    {
	close(--fd);
    }

    value = getpid();
    setpgrp(0, value);

    if ((fd = open("/dev/tty", O_RDWR)) >= 0)
    {
	ioctl(fd, TIOCNOTTY, 0);    /* Thor : ¬°¤°»òÁÙ­n¥Î  tty? */
	close(fd);
    }
#endif

    fd = open(CHAT_PIDFILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0)
    {
	/* sprintf(buf, "%5d\n", value); */
	sprintf(buf, "%5d\n", (int)getpid());
	write(fd, buf, 6);
	close(fd);
    }

#if 0
    /* ------------------------------ */
    /* trap signals                   */
    /* ------------------------------ */

    for (fd = 1; fd < NSIG; fd++)
    {

	Signal(fd, SIG_IGN);
    }
#endif

    fd = socket(PF_INET, SOCK_STREAM, 0);

#if 0
    value = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, value | O_NDELAY);
#endif

    value = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &value, sizeof(value));

#if 0
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *) &value, sizeof(value));

    value = 81920;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *) &value, sizeof(value));
#endif

    ld.l_onoff = ld.l_linger = 0;
    setsockopt(fd, SOL_SOCKET, SO_LINGER, (char *) &ld, sizeof(ld));

    memset((char *) &fsin, 0, sizeof(fsin));
    fsin.sin_family = AF_INET;
    fsin.sin_port = htons(NEW_CHATPORT);
    fsin.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *) & fsin, sizeof(fsin)) < 0)
	exit(1);

    listen(fd, SOCK_QLEN);

    return fd;
}


static void
free_resource(int fd)
{
    static int loop = 0;
    register ChatUser *user;
    register int sock, num;

    /* ­«·s­pºâ maxfd */
    num = 0;
    for (user = mainuser; user; user = user->unext)
    {
	num++;
	sock = user->sock;
	if (fd < sock)
	    fd = sock;
    }

    sprintf(chatbuf, "%d, %d user (maxfds %d -> %d)", ++loop, num, maxfds, fd+1);
    logit("LOOP", chatbuf);

    maxfds = fd + 1;
}


#ifdef  SERVER_USAGE
static void
server_usage()
{
    struct rusage ru;
    char buf[2048];

    if (getrusage(RUSAGE_SELF, &ru))
	return;

    sprintf(buf, "\n[Server Usage]\n\n"
	    "user time: %.6f\n"
	    "system time: %.6f\n"
	    "maximum resident set size: %lu P\n"
	    "integral resident set size: %lu\n"
	    "page faults not requiring physical I/O: %ld\n"
	    "page faults requiring physical I/O: %ld\n"
	    "swaps: %ld\n"
	    "block input operations: %ld\n"
	    "block output operations: %ld\n"
	    "messages sent: %ld\n"
	    "messages received: %ld\n"
	    "signals received: %ld\n"
	    "voluntary context switches: %ld\n"
	    "involuntary context switches: %ld\n"
	    "\n",

	    (double) ru.ru_utime.tv_sec + (double) ru.ru_utime.tv_usec / 1000000.0,
	    (double) ru.ru_stime.tv_sec + (double) ru.ru_stime.tv_usec / 1000000.0,
	    ru.ru_maxrss,
	    ru.ru_idrss,
	    ru.ru_minflt,
	    ru.ru_majflt,
	    ru.ru_nswap,
	    ru.ru_inblock,
	    ru.ru_oublock,
	    ru.ru_msgsnd,
	    ru.ru_msgrcv,
	    ru.ru_nsignals,
	    ru.ru_nvcsw,
	    ru.ru_nivcsw);

    write(flog, buf, strlen(buf));
}
#endif


static void
abort_server()
{
    log_close();
    exit(1);
}


static void
reaper()
{
    int state;

    while (waitpid(-1, &state, WNOHANG | WUNTRACED) > 0)
    {
    }
}


int
main()
{
    register int msock, csock, nfds;
    register ChatUser *cu;
    register fd_set *rptr, *xptr;
    fd_set rset, xset;
    struct timeval tv;
    time4_t uptime, tmaintain;

    msock = start_daemon();

    setgid(BBSGID);
    setuid(BBSUID);

    log_init();

//    Signal(SIGBUS, SIG_IGN);
//    Signal(SIGSEGV, SIG_IGN);
    Signal(SIGPIPE, SIG_IGN);
    Signal(SIGURG, SIG_IGN);

    Signal(SIGCHLD, reaper);
    Signal(SIGTERM, abort_server);

#ifdef  SERVER_USAGE
    Signal(SIGPROF, server_usage);
#endif

    /* ----------------------------- */
    /* init variable : rooms & users */
    /* ----------------------------- */

    mainuser = NULL;
    memset(&mainroom, 0, sizeof(mainroom));
    strcpy(mainroom.name, MAIN_NAME);
    strcpy(mainroom.topic, MAIN_TOPIC);

    /* ----------------------------------- */
    /* main loop                           */
    /* ----------------------------------- */

    FD_ZERO(&mainfds);
    FD_SET(msock, &mainfds);
    rptr = &rset;
    xptr = &xset;
    maxfds = msock + 1;

    tmaintain = time(0) + CHAT_INTERVAL;

    for (;;)
    {
	uptime = time(0);
	if (tmaintain < uptime)
	{
	    tmaintain = uptime + CHAT_INTERVAL;

	    /* client/server ª©¥»§Q¥Î ping-pong ¤èªk§PÂ_ user ¬O¤£¬OÁÙ¬¡µÛ */
	    /* ¦pªG client ¤w¸gµ²§ô¤F¡A´NÄÀ©ñ¨ä resource */

	    free_resource(msock);
	}

	memcpy(rptr, &mainfds, sizeof(fd_set));
	memcpy(xptr, &mainfds, sizeof(fd_set));

	/* Thor: for future reservation bug */

	tv.tv_sec = CHAT_INTERVAL;
	tv.tv_usec = 0;

	nfds = select(maxfds, rptr, NULL, xptr, &tv);

	/* free idle user & chatroom's resource when no traffic */
	if (nfds == 0)
	    continue;

	/* check error condition */
	if (nfds < 0)
	    continue;

	/* accept new connection */
	if (FD_ISSET(msock, rptr)) {
	    csock = accept(msock, NULL, NULL);

	    if(csock < 0) {
		if(errno != EINTR) {
		    // TODO
		}
	    } else {
		cu = (ChatUser *) malloc(sizeof(ChatUser));
		if(cu == NULL) {
		    close(csock);
		    logit("accept", "malloc fail");
		} else {
		    memset(cu, 0, sizeof(ChatUser));
		    cu->sock = csock;
		    cu->unext = mainuser;
		    mainuser = cu;

		    totaluser++;
		    FD_SET(csock, &mainfds);
		    if (csock >= maxfds)
			maxfds = csock + 1;

#ifdef  DEBUG
		    logit("accept", "OK");
#endif
		}
	    }
	}

	for (cu = mainuser; cu && nfds>0; cu = cu->unext) {
	    csock = cu->sock;
	    if (FD_ISSET(csock, xptr)) {
		logout_user(cu);
		nfds--;
	    } else if (FD_ISSET(csock, rptr)) {
		if (cuser_serve(cu) < 0)
		    logout_user(cu);
		nfds--;
	    }
	}

	/* end of main loop */
    }
}
