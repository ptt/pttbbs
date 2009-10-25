/* $Id$ */
#include "bbs.h"
#include "xchatd.h"
#include <sys/wait.h>
#include <netdb.h>

#define SERVER_USAGE
#undef  MONITOR                 /* 監督 chatroom 活動以解決糾紛 */
#undef  DEBUG                   /* 程式除錯之用 */

#ifdef  DEBUG
#define MONITOR
#endif

/* self-test:
 * random test, 隨機產生各種 client input, 目的為找到讓 server
 * crash 的狀況, 因此 client 並未檢驗 server 傳過來的 data.
 * server 也不會讀取 bbs 實際的檔案或 SHM.
 * 根據 gcov(1) 此 self-test coverage 約 90%
 *
 * 如何測試:
 * define SELFTEST & SELFTESTER, 執行時隨意加參數(argc>1)就會跑 test child.
 * test server 僅進行 100 秒.
 *
 * Hint:
 * 配合 valgrind 尋找 memory related bug.
 */
//#define SELFTEST
//#define SELFTESTER

#ifdef SELFTEST
// 另開 port
#undef NEW_CHATPORT
#define NEW_CHATPORT 12333
// only test 100 secs
#undef CHAT_INTERVAL
#define CHAT_INTERVAL 100
#endif

#define CHAT_PIDFILE    "log/chat.pid"
#define CHAT_LOGFILE    "log/chat.log"
#define CHAT_INTERVAL   (60 * 30)
#define SOCK_QLEN       1


/* name of the main room (always exists) */

#define MAIN_NAME       "main"
#define MAIN_TOPIC      "烹茶可貢西天佛"

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
/* Thor: SYSOP 與 CHATROOM都是 chat總管 */
#define PERM_ROOMOP     PERM_CHAT       /* Thor: 借 PERM_CHAT為 PERM_ROOMOP */
#define PERM_HANDUP     PERM_BM		/* 借 PERM_BM 為有沒有舉手過 */
#define PERM_SAY        PERM_NOTOP	/* 借 PERM_NOTOP 為有沒有發表權 */

/* 進入時需清空              */
/* Thor: ROOMOP為房間管理員 */
#define ROOMOP(usr)  (usr->uflag & ( PERM_ROOMOP | PERM_SYSOP | PERM_CHATROOM))
#define CLOAK(usr)      (usr->uflag & PERM_CLOAK)
#define HANDUP(usr)  (usr->uflag & PERM_HANDUP) 
#define SAY(usr)      (usr->uflag & PERM_SAY)
/* Thor: 聊天室隱身術 */

#define CHATID_LEN  (8)
#define TOPIC_LEN   (48)

// 訊息色彩 (WOW style is good!)
#define COLOR_PRIVATEMSG    ANSI_COLOR(1;35)
#define COLOR_ANNOUNCE	    ANSI_COLOR(1;33)

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
    ChatRoom *room;
    UserList *ignore;
    int sock;                     /* user socket */
    int userno;			  /* userno 是 PASSWD 來的 unum, 幾乎是唯一的 */
    int uflag;
    int isize;                    /* current size of ibuf */
    uint32_t numlogindays;	  /* login counter */
    uint32_t numposts;		  /* post number */
    char userid[IDLEN+1];         /* real userid */
    char lasthost[IPV4LEN+1];     /* host address */
    char nickname[24];		  /* BBS nickname */
    char chatid[CHATID_LEN + 1];  /* chat id */
    char ibuf[80];                /* buffer for non-blocking receiving */
};


struct ChatRoom
{
    struct ChatRoom *next, *prev;
    char name[IDLEN + 1];
    char topic[TOPIC_LEN + 1];    /* Let the room op to define room topic */
    int rflag;                    /* ROOM_LOCKED, ROOM_SECRET, ROOM_OPENTOPIC */
    int occupants;                /* number of users in room */
    UserList *invite;
    UserList *ban;
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

static char msg_not_op[] = "◆ 您不是這間聊天室的 Op";
static char msg_no_such_id[] = "◆ 目前沒有人使用 [%s] 這個聊天代號";
static char msg_no_such_uid[] = "◆ 目前沒有 [%s] 這個使用者 ID";
static char msg_not_here[] = "◆ [%s] 不在這間聊天室";

time4_t boot_time;

#define FUZZY_USER      ((ChatUser *) -1)

#undef cuser
typedef struct userec_t ACCT;

/* ----------------------------------------------------- */
/* string utilities					 */
/* ----------------------------------------------------- */
void 
chat_safe_trim(char *s, int size)
{
    int len;
    if (!s || !*s)
	return;
    len = strlen(s);
    if (len >= size)
	s[size-1] = 0;
    DBCS_safe_trim(s);
}

/* ----------------------------------------------------- */
/* acct_load for check acct                              */
/* ----------------------------------------------------- */

int
acct_load(ACCT *acct, char *userid)
{
    int id;
#ifdef SELFTEST
    memset(acct, 0, sizeof(ACCT));
    acct->userlevel |= PERM_BASIC|PERM_CHAT;
    if(random()%4==0) acct->userlevel |= PERM_CHATROOM;
    if(random()%8==0) acct->userlevel |= PERM_SYSOP;
    return atoi(userid);
#endif
    if((id=searchuser(userid, NULL))<0)
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

int
chkpasswd(const char *passwd, const char *test)
{
    char *pw;
    char pwbuf[PASSLEN];

    strlcpy(pwbuf, test, PASSLEN);
    pw = fcrypt(pwbuf, passwd);
    return (!strncmp(pw, passwd, PASSLEN));
}

/* ----------------------------------------------------- */
/* operation log and debug information                   */
/* ----------------------------------------------------- */


static int flog;                /* log file descriptor */

#ifdef CHAT_MSG_LOGFILE
static int mlog;		/* main room logs */
#endif

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
#ifdef CHAT_MSG_LOGFILE
    {
	const char *dtstr = Cdate(&boot_time);
	const char *prefix = "\n- xchatd restart: ";
	mlog = open(CHAT_MSG_LOGFILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
	write(mlog, prefix, strlen(prefix));
	write(mlog, dtstr, strlen(dtstr));
	write(mlog, "\n", 1);
    }
#endif
}


static void
log_close()
{
    close(flog);
#ifdef CHAT_MSG_LOGFILE
    close(mlog);
#endif
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
str_equal(const char *s1, const char *s2)
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
str_match(const char *s1, const char *s2)
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


static int
list_add_id(UserList **list, const char *id)
{
    UserList *node;
    int uid = 0;
    if (!id[0])
	return 0;

    uid = searchuser(id, NULL);
    if (uid < 1 || uid >= MAX_USERS)
	return 0;

    if((node = (UserList *) malloc(sizeof(UserList)))) {
	/* Thor: 防止空間不夠 */
	strlcpy(node->userid, id, sizeof(node->userid));
	node->userno = uid;
	node->next = *list;
	*list = node;
	return 1;
    }
    return 0;
}

static void
list_add(UserList **list, ChatUser *user)
{
    UserList *node;

    if((node = (UserList *) malloc(sizeof(UserList)))) {
	/* Thor: 防止空間不夠 */
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
	list = &node->next;         /* Thor: list要跟著前進 */
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

static int
list_belong_id(UserList *list, const char *userid)
{
    while (list)
    {
	if (strcasecmp(list->userid, userid) == 0)
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
    zerotv.tv_usec = 16384;  /* Ptt: 改成16384 避免不按時for loop吃cpu time
				16384 約每秒64次 */
#ifdef SELFTEST
    zerotv.tv_usec = 0;
#endif

    sr = select(nfds + 1, NULL, wset, NULL, &zerotv);

    /* FIXME 若 select() timeout, 或有的 write ready 有的沒有. 則可能會漏接 msg? */
    if (sr > 0)
    {
	register int len;

	len = strlen(msg) + 1;
	while (nfds >= 0)
	{
	    if (FD_ISSET(nfds, wset))
	    {
		send(nfds, msg, len, 0);/* Thor: 如果buffer滿了, 仍會 block */
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

    if (number != MSG_MESSAGE && number)
	return;

    FD_ZERO(wptr = &wset);
    max = -1;

    for (cu = mainuser; cu; cu = cu->unext)
    {
	if (room == cu->room || room == ROOM_ALL)
	{
	    if (!userno || !list_belong(cu->ignore, userno))
	    {
		sock = cu->sock;
		FD_SET(sock, wptr);
		if (max < sock)
		    max = sock;
	    }
	}
    }

    if (max < 0)
	return;

#ifdef CHAT_MSG_LOGFILE
    if (room && room->name && 
	strcmp(room->name, MAIN_NAME) == 0)
    {
	time4_t now = time(0);
	const char *dtstr = Cdate_mdHM(&now);
	write(mlog, dtstr, strlen(dtstr));
	write(mlog, " ", 1);
	write(mlog, msg, strlen(msg));
	write(mlog, "\n", 1);
    }
#endif

    Xdo_send(max, wptr, msg);
}


static void
send_to_user(ChatUser *user, char *msg, int userno, int number)
{
    // BBS clients does not take non MSG_MESSAGE
    if (number && number != MSG_MESSAGE)
	return;

    if (!userno || !list_belong(user->ignore, userno))
    {
	fd_set wset, *wptr;
	int sock;

	sock = user->sock;
	FD_ZERO(wptr = &wset);
	FD_SET(sock, wptr);

	Xdo_send(sock, wptr, msg);
    }
}

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

		sprintf(chatbuf, "◆ %s (%s) 離開了 ...", chatid, user->userid);
		if (msg && *msg)
		{
		    strcat(chatbuf, ": ");
		    strncat(chatbuf, msg, 80);
		}
		break;

	    case EXIT_LOSTCONN:

		sprintf(chatbuf, "◆ %s (%s) 成了斷線的風箏囉", chatid, user->userid);
		break;

	    case EXIT_KICK:

		sprintf(chatbuf, "◆ 哈哈！%s (%s) 被踢出去了", chatid, user->userid);
		break;
	}
	if (!CLOAK(user))         /* Thor: 聊天室隱身術 */
	    send_to_room(room, chatbuf, user->userno, MSG_MESSAGE);

	if (list_belong(room->invite, user->userno)) {
	    list_delete(&(room->invite), user->userid);
	}

	sprintf(chatbuf, "- %s", user->userid);
	send_to_room(room, chatbuf, user->userno, MSG_USERNOTIFY);
	room_changed(room);

	return;
    }

    /* Now, room->occupants==0 */
    if (room != &mainroom)
    {                           /* Thor: 人數為0時,不是mainroom才free */
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
/* (.ACCT) 使用者帳號 (account) subroutines              */
/* ----------------------------------------------------- */

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
	send_to_user(cu, "※ 請指定話題", 0, MSG_MESSAGE);
	return;
    }

    room = cu->room;
    assert(room);
    topic = room->topic;
    strlcpy(topic, msg, sizeof(room->topic));
    DBCS_safe_trim(topic);

    sprintf(chatbuf, "/t%s", topic);
    send_to_room(room, chatbuf, 0, 0);

    room_changed(room);

    sprintf(chatbuf, "◆ %s 將話題改為 " ANSI_COLOR(1;32) "%s" ANSI_RESET, cu->chatid, topic);
    if (!CLOAK(cu))               /* Thor: 聊天室隱身術 */
	send_to_room(room, chatbuf, 0, MSG_MESSAGE);
}


static void
chat_version(ChatUser *cu, char *msg)
{
    sprintf(chatbuf, "%d %d", XCHAT_VERSION_MAJOR, XCHAT_VERSION_MINOR);
    send_to_user(cu, chatbuf, 0, MSG_VERSION);
}

static void
chat_xinfo(ChatUser *cu, char *msg)
{
    // report system information
    time4_t uptime = time(0) - boot_time;
    int dd =  uptime / DAY_SECONDS, 
	hh = (uptime % DAY_SECONDS) / 3600,
	mm = (uptime % 3600) / 60;
    sprintf(chatbuf, "⊙ 系統資訊: XCHAT 版本 %d.%02d - " __DATE__ 
	    "，已執行 %d 天 %d 小時 %d 分",
	    XCHAT_VERSION_MAJOR, XCHAT_VERSION_MINOR, dd, hh, mm);
    send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
}

static void
chat_nick(ChatUser *cu, char *msg)
{
    char *chatid;
    ChatUser *xuser;

    chatid = nextword(&msg);
    chat_safe_trim(chatid, sizeof(cu->chatid));

    if (!valid_chatid(chatid))
    {
	send_to_user(cu, "※ 這個聊天代號是不正確的", 0, MSG_MESSAGE);
	return;
    }

    xuser = cuser_by_chatid(chatid);
    if (xuser != NULL && xuser != cu)
    {
	send_to_user(cu, "※ 已經有人捷足先登囉", 0, MSG_MESSAGE);
	return;
    }

    snprintf(chatbuf, sizeof(chatbuf), "※ %s 將聊天代號改為 " ANSI_COLOR(1;33) "%s" ANSI_RESET, cu->chatid, chatid);
    if (!CLOAK(cu))               /* Thor: 聊天室隱身術 */
	send_to_room(cu->room, chatbuf, cu->userno, MSG_MESSAGE);

    strlcpy(cu->chatid, chatid, sizeof(cu->chatid));
    user_changed(cu);

    snprintf(chatbuf, sizeof(chatbuf), "/n%s", chatid);
    send_to_user(cu, chatbuf, 0, 0);
}

static void
chat_list_rooms(ChatUser *cuser, char *msg)
{
    ChatRoom *cr;

    if (RESTRICTED(cuser))
    {
	send_to_user(cuser, "※ 您沒有權限列出現有的聊天室", 0, MSG_MESSAGE);
	return;
    }

    if (common_client_command)
	send_to_user(cuser, "", 0, MSG_ROOMLISTSTART);
    else
	send_to_user(cuser, ANSI_COLOR(7) " 聊天室名稱  │人數│話題 "
		// 48-4 spaces for max topic length
		"                                        "  
		ANSI_RESET, 0, MSG_MESSAGE);

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
		snprintf(chatbuf, sizeof(chatbuf), " %-12s│%4d│%s", cr->name, cr->occupants, cr->topic);
		if (LOCKED(cr))
		    strcat(chatbuf, " [鎖住]");
		if (SECRET(cr))
		    strcat(chatbuf, " [秘密]");
		if (OPENTOPIC(cr))
		    strcat(chatbuf, " [話題]");
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
	send_to_user(cu, ANSI_COLOR(7) " 聊天代號│使用者代號  │聊天室      " ANSI_RESET, 0, MSG_MESSAGE);

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

	if (CLOAK(user))            /* Thor: 隱身術 */
	    continue;


	curr++;
	if (start && curr < start)
	    continue;
	else if (stop && (curr > stop))
	    break;

	if (common_client_command)
	{
	    if (!room)
		continue;               /* Xshadow: 還沒進入任何房間的就不列出 */

	    snprintf(chatbuf, sizeof(chatbuf), "%s %s %s %s", user->chatid, user->userid, room->name, user->lasthost);
	    if (ROOMOP(user))
		strcat(chatbuf, " Op");
	}
	else
	{
	    snprintf(chatbuf, sizeof(chatbuf), " %-8s│%-12s│%s", user->chatid, user->userid, room ? room->name : "[在門口徘徊]");
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
	    snprintf(chatbuf, sizeof(chatbuf), "※ 沒有 [%s] 這個聊天室", roomstr);
	    send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	    return;
	}

	if (whichroom != cu->room && SECRET(whichroom) && !CHATSYSOP(cu))
	{                           /* Thor: 要不要測同一room雖SECRET但可以列?
				     * Xshadow: 我改成同一 room 就可以列 */
	    send_to_user(cu, "※ 無法列出在秘密聊天室的使用者", 0, MSG_MESSAGE);
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
	send_to_user(cu, "批踢踢茶藝館 4 21", 0, MSG_CHATROOM);
}

static void
chat_map_chatids(ChatUser *cu, ChatRoom *whichroom) /* Thor: 還沒有作不同間的 */
{
    int c;
    ChatRoom *myroom, *room;
    ChatUser *user;

    myroom = whichroom;
    send_to_user(cu,
		 ANSI_COLOR(7) " 聊天代號 使用者代號  │ 聊天代號 使用者代號  │ 聊天代號 使用者代號 " ANSI_RESET, 0, MSG_MESSAGE);

    c = 0;

    for (user = mainuser; user; user = user->unext)
    {
	room = user->room;
	if (whichroom != ROOM_ALL && whichroom != room)
	    continue;
	if (myroom != room)
	{
	    if (RESTRICTED(cu) ||     /* Thor: 要先check room 是不是空的 */
		(room && SECRET(room) && !CHATSYSOP(cu)))
		continue;
	}
	if (CLOAK(user))            /* Thor:隱身術 */
	    continue;
	sprintf(chatbuf + (c * 24), " %-8s%c%-12s%s",
		user->chatid, ROOMOP(user) ? '*' : ' ',
		user->userid, (c < 2 ? "│" : "  "));
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
		     "※ 請指定狀態: {[+(設定)][-(取消)]}{[l(鎖住)][s(秘密)][t(開放話題)}", 0, MSG_MESSAGE);
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
	    fstr = "鎖住";
	    break;

	case 's':
	case 'S':
	    flag = ROOM_SECRET;
	    fstr = "秘密";
	    break;

	case 't':
	case 'T':
	    flag = ROOM_OPENTOPIC;
	    fstr = "開放話題";
	    break;
	case 'h':
	case 'H':
	    flag = ROOM_HANDUP;
	    fstr = "舉手發言";
	    break;

	default:
	    sprintf(chatbuf, "※ 狀態錯誤：[%c]", *modestr);
	    send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	}

	/* Thor: check room 是不是空的, 應該不是空的 */
	if (flag && (room->rflag & flag) != sign * flag)
	{
	    room->rflag ^= flag;
	    sprintf(chatbuf, "※ 本聊天室被 %s %s [%s] 狀態",
		    chatid, sign ? "設定為" : "取消", fstr);
	    if (!CLOAK(cu))           /* Thor: 聊天室隱身術 */
		send_to_room(room, chatbuf, 0, MSG_MESSAGE);
	}
	modestr++;
    }
    room_changed(room);
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
    {                             /* Thor.0724: 用 userid也可傳悄悄話 */
	xuser = cuser_by_userid(recipient);
    }

    if (xuser == NULL)
    {
	sprintf(chatbuf, msg_no_such_id, recipient);
    }
    else if (xuser == FUZZY_USER)
    {                             /* ambiguous */
	strcpy(chatbuf, "※ 請指明聊天代號");
    }
    else if (*msg)
    {
	userno = cu->userno;

	// XXX valid size: 對方輸入時要在 NICK: /m MYNICK MSG
	// 所以輸入的 MSG 最大為 80-NICKLEN(8)*2-5
	// prefix 的字串要小心不能過長

	sprintf(chatbuf, COLOR_PRIVATEMSG "*%s (%s)*" ANSI_RESET " ", cu->chatid, cu->userid);
	strlcat(chatbuf, msg, sizeof(chatbuf));
	send_to_user(xuser, chatbuf, userno, MSG_MESSAGE);

	sprintf(chatbuf, COLOR_PRIVATEMSG "%s" ANSI_RESET "> ", xuser->chatid);
	strlcat(chatbuf, msg, sizeof(chatbuf));
    }
    else
    {
	sprintf(chatbuf, "※ 您想對 %s 說什麼話呢？", xuser->chatid);
    }
    send_to_user(cu, chatbuf, userno, MSG_MESSAGE);       /* Thor: userno 要改成 0
							   * 嗎? */
}

static void
chat_query(ChatUser *cu, char *msg)
{
    char *recipient;
    ChatUser *xuser;

    recipient = nextword(&msg);
    xuser = (ChatUser *) fuzzy_cuser_by_chatid(recipient);
    if (xuser == NULL)
	xuser = cuser_by_userid(recipient);
    if (xuser == NULL)
	sprintf(chatbuf, msg_no_such_id, recipient);
    else if (xuser == FUZZY_USER)
	strcpy(chatbuf, "※ 請清楚指明對方的聊天代號"); // ambiguous
    else 
    {
	snprintf(chatbuf, sizeof(chatbuf),
		"※ 聊天暱稱: %s ，" BBSMNAME " ID: %s (%s)",
		xuser->chatid, xuser->userid, xuser->nickname);
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	snprintf(chatbuf, sizeof(chatbuf),
		"   " STR_LOGINDAYS " %d " STR_LOGINDAYS_QTY "，"
		"發表過 %d 篇文章，最近從 %s 上站",
		xuser->numlogindays, xuser->numposts, xuser->lasthost);
    }
    send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
}


static void
chat_cloak(ChatUser *cu, char *msg)
{
    if (CHATSYSOP(cu))
    {
	cu->uflag ^= PERM_CLOAK;
	sprintf(chatbuf, "◆ %s", CLOAK(cu) ? MSG_CLOAKED : MSG_UNCLOAK);
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
    }
}



/* ----------------------------------------------------- */


static void
arrive_room(ChatUser *cuser, ChatRoom *room)
{
    char *rname;

    /* Xshadow: 不必送給自己, 反正換房間就會重新 build user list */
    snprintf(chatbuf, sizeof(chatbuf), "+ %s %s %s %s", cuser->userid, cuser->chatid, room->name, cuser->lasthost);
    if (ROOMOP(cuser))
	strcat(chatbuf, " Op");
    send_to_room(room, chatbuf, 0, MSG_USERNOTIFY);

    cuser->room = room;
    room->occupants++;
    rname = room->name;

    room_changed(room);

    sprintf(chatbuf, "/r%s", rname);
    send_to_user(cuser, chatbuf, 0, 0);
    sprintf(chatbuf, "/t%s", room->topic);
    send_to_user(cuser, chatbuf, 0, 0);

    sprintf(chatbuf, "※ " ANSI_COLOR(1;32) "%s (%s)" ANSI_RESET " 進入 "
	    ANSI_COLOR(1;33) "[%s]" ANSI_RESET " 包廂",
	    cuser->chatid, cuser->userid, rname);
    if (!CLOAK(cuser))            /* Thor: 聊天室隱身術 */
	send_to_room(room, chatbuf, cuser->userno, MSG_MESSAGE);
}


static int
enter_room(ChatUser *cuser, char *rname, char *msg)
{
    ChatRoom *room;
    int create;

    create = 0;

    // truncate names
    chat_safe_trim(rname, sizeof(room->name));

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
	    send_to_user(cuser, "※ 無法再新闢包廂了", 0, MSG_MESSAGE);
	    return 0;
	}

	memset(room, 0, sizeof(ChatRoom));
	strlcpy(room->name, rname, sizeof(room->name));
	strcpy(room->topic, "這是一個新天地");

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
	    sprintf(chatbuf, "※ 您本來就在 [%s] 聊天室囉 :)", rname);
	    send_to_user(cuser, chatbuf, 0, MSG_MESSAGE);
	    return 0;
	}

	if (!CHATSYSOP(cuser) && 
	    (list_belong(room->ban, cuser->userno) ||
	     (LOCKED(room) && !list_belong(room->invite, cuser->userno))))
	{
	    send_to_user(cuser, "※ 內有惡犬，非請莫入", 0, MSG_MESSAGE);
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

#if 0   /* Thor: 也許不差這一個 */
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

    number = MSG_MESSAGE;

    // welcome message here.
    sprintf(chatbuf, "⊙ 歡迎光臨【批踢踢聊天室】，目前開了 " 
	    ANSI_COLOR(1;31) "%d" ANSI_RESET " 間包廂。", roomc);
    send_to_user(cuser, chatbuf, 0, number);

    sprintf(chatbuf, "⊙ 線上有 " ANSI_COLOR(1;36) "%d" ANSI_RESET " 人", userc);
    if (suserc)
	sprintf(chatbuf + strlen(chatbuf), " [%d 人在秘密聊天室]", suserc);
    send_to_user(cuser, chatbuf, 0, number);
}


static int
login_user(ChatUser *cu, char *msg)
{
    int utent;

    char *passwd;
    char *userid;
    char *chatid;

    // deprecated: we don't lookup real host now.
#if 0
    struct sockaddr_in from;
    unsigned int fromlen;
    struct hostent *hp;
#endif

    ACCT acct;
    int level;

    /*
     * Thor.0819: SECURED_CHATROOM : /! userid chatid passwd , userno
     * el 在check完passwd後取得
     */
    /* Xshadow.0915: common client support : /-! userid chatid password */

    /* 傳參數：userlevel, userid, chatid */

    /* client/server 版本依據 userid 抓 .PASSWDS 判斷 userlevel */

    userid = nextword(&msg);
    chatid = nextword(&msg);

    chat_safe_trim(chatid, sizeof(cu->chatid));

#ifdef  DEBUG
    logit("ENTER", userid);
#endif
    /* Thor.0730: parse space before passwd */
    passwd = msg;

    /* Thor.0813: 跳過一空格即可, 因為反正如果chatid有空格, 密碼也不對 */
    /* 就算密碼對, 也不會怎麼樣:p */
    /* 可是如果密碼第一個字是空格, 那跳太多空格會進不來... */
    if (*passwd == ' ')
	passwd++;

    /* Thor.0729: load acct */
    if (!*userid || (acct_load(&acct, userid) < 0))
    {

#ifdef  DEBUG
	logit("noexist", chatid);
#endif

	send_to_user(cu, CHAT_LOGIN_INVALID, 0, 0);

	return -1;
    }
    else if(strncmp(passwd, acct.passwd, PASSLEN) &&
	    !chkpasswd(acct.passwd, passwd))
    {
#ifdef  DEBUG
	logit("fake", chatid);
#endif

	send_to_user(cu, CHAT_LOGIN_INVALID, 0, 0);
	return -1;
    }

    /* Thor.0729: if ok, read level.  */
    level = acct.userlevel;
    /* Thor.0819: read userno for client/server bbs */
#ifdef SELFTEST
    utent = atoi(userid)+1;
#else
    utent = searchuser(acct.userid, NULL);
#endif
    assert(utent);

    if (!valid_chatid(chatid))
    {
#ifdef  DEBUG
	logit("enter", chatid);
#endif

	send_to_user(cu, CHAT_LOGIN_INVALID, 0, 0);
	return 0;
    }

    if (cuser_by_chatid(chatid) != NULL)
    {
	/* chatid in use */

#ifdef  DEBUG
	logit("enter", "duplicate");
#endif

	send_to_user(cu, CHAT_LOGIN_EXISTS, 0, 0);
	return 0;
    }

    cu->userno = utent;
    cu->uflag = level & ~(PERM_ROOMOP | PERM_CLOAK | PERM_HANDUP | PERM_SAY);
    /* Thor: 進來先清空ROOMOP(同PERM_CHAT), CLOAK */
    strcpy(cu->userid, userid);
    strlcpy(cu->chatid, chatid, sizeof(cu->chatid));
    // fill user information from acct
    strlcpy(cu->nickname, acct.nickname, sizeof(cu->nickname));
    cu->numposts = acct.numposts;
    cu->numlogindays = acct.numlogindays;
    strlcpy(cu->lasthost, acct.lasthost, sizeof(cu->lasthost));

    // deprecated: let's use BBS lasthost
#if 0
    /* Xshadow: 取得 client 的來源 */
    fromlen = sizeof(from);
    if (!getpeername(cu->sock, (struct sockaddr *) & from, &fromlen))
    {
	if ((hp = gethostbyaddr((char *) &from.sin_addr, sizeof(struct in_addr), from.sin_family)))
	    strlcpy(cu->lasthost, hp->h_name, sizeof(cu->lasthost));
	else
	    strlcpy(cu->lasthost, (char *) inet_ntoa(from.sin_addr), sizeof(cu->lasthost));
    }
    else
	strcpy(cu->lasthost, "[外太空]");
#endif

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
	sprintf(chatbuf, "%s " ANSI_COLOR(36) "%s" ANSI_RESET, cu->chatid, msg);
	send_to_room(cu->room, chatbuf, cu->userno, MSG_MESSAGE);
    }
}


static void
chat_ignore(ChatUser *cu, char *msg)
{

    if (RESTRICTED(cu))
    {
	strcpy(chatbuf, "※ 您沒有 ignore 別人的權利");
    }
    else
    {
	char *ignoree;

	ignoree = nextword(&msg);
	if (*ignoree)
	{
	    ChatUser *xuser;
	    xuser = cuser_by_userid(ignoree);
	    if (!xuser) xuser = cuser_by_chatid(ignoree);

	    if (list_belong_id(cu->ignore, ignoree))
	    {
		sprintf(chatbuf, "※ %s 已經被凍結了", ignoree);
	    } 
	    else if (xuser == NULL)
	    {
		// try more harder to see if this user can be
		// pre-ignored. Do not use xuser!
		if (list_add_id(&(cu->ignore), ignoree))
		    sprintf(chatbuf, "※ %s 已經被凍結了", ignoree);
		else
		    sprintf(chatbuf, msg_no_such_uid, ignoree);
	    }
	    else if (xuser == cu || CHATSYSOP(xuser) ||
		     (ROOMOP(xuser) && (xuser->room == cu->room)))
	    {
		sprintf(chatbuf, "◆ 不可以忽略 %s (%s)", 
			xuser->chatid, xuser->userid);
	    }
	    else
	    {

		if (list_belong(cu->ignore, xuser->userno))
		{
		    sprintf(chatbuf, "※ %s (%s) 已經被凍結了", 
			    xuser->chatid, xuser->userid);
		}
		else
		{
		    list_add(&(cu->ignore), xuser);
		    sprintf(chatbuf, "◆ 將 [%s] 打入冷宮了 :p", xuser->chatid);
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

		send_to_user(cu, "◆ 這些人被打入冷宮了：", 0, MSG_MESSAGE);
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
		strcpy(chatbuf, "◆ 您目前並沒有 ignore 任何人");
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
		"◆ [%s] 不再被你冷落了" :
		"◆ 您並未忽略 [%s]，請用 /ignore 檢查列表。", ignoree);
    }
    else
    {
	strcpy(chatbuf, "◆ 請指明使用者 ID");
    }
    send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
}

static void
chat_unban(ChatUser *cu, char *msg)
{
    char *unban;
    UserList **list;

    if (!ROOMOP(cu))
    {
	send_to_user(cu, msg_not_op, 0, MSG_MESSAGE);
	return;
    }

    unban = nextword(&msg);
    list = &(cu->room->ban);

    if (*unban)
    {
	sprintf(chatbuf, (list_delete(list, unban)) ?
		"◆ [%s] 不再被列為黑名單" :
		"◆ [%s] 並不在黑名單中，請用 /ban 檢查列表", unban);
    }
    else
    {
	strcpy(chatbuf, "◆ 請指明使用者 ID");
    }
    send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
}


static void
chat_join(ChatUser *cu, char *msg)
{
    if (RESTRICTED(cu))
    {
	send_to_user(cu, "※ 您沒有加入其他聊天室的權限", 0, MSG_MESSAGE);
    }
    else
    {
	char *roomid = nextword(&msg);

	if (*roomid)
	    enter_room(cu, roomid, msg);
	else
	    send_to_user(cu, "※ 請指定聊天室的名字", 0, MSG_MESSAGE);
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
    {                             /* Thor: 聊天室隱身術 */
	sprintf(chatbuf, msg_not_here, twit);
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	return;
    }

    if (CHATSYSOP(xuser))
    {                             /* Thor: 踢不走 CHATSYSOP */
	sprintf(chatbuf, "◆ 不可以 kick [%s]", twit);
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
    int wasop = 0;

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
	sprintf(chatbuf, "※ 您已經是管理員(Op)了，無法改變自己的權限");
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	return;
    }

    room = cu->room;

    if (room != xuser->room || CLOAK(xuser))
    {                             /* Thor: 聊天室隱身術 */
	sprintf(chatbuf, msg_not_here, xuser->chatid);
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	return;
    }

    wasop = ROOMOP(xuser) ? 1 : 0;
    xuser->uflag ^= PERM_ROOMOP;
    user_changed(xuser);

    if (wasop == (ROOMOP(xuser) ? 1 : 0))
    {
	// 動不了他
	sprintf(chatbuf, "※ 無法改變對方的管理員(Op)權限。");
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	return;
    }

    sprintf(chatbuf, "※ %s %s %s 的管理員(Op)權限",
	    cu->chatid, 
	    ROOMOP(xuser) ? "提昇了" : "解除了",
	    xuser->chatid);

    if (!CLOAK(cu))               /* Thor: 聊天室隱身術 */
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
	sprintf(chatbuf, "※ %s 已經接受過邀請了", xuser->chatid);
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	return;
    }
    list_add(list, xuser);

    sprintf(chatbuf, "※ %s 邀請您到 [%s] 聊天室",
	    cu->chatid, room->name);
    send_to_user(xuser, chatbuf, 0, MSG_MESSAGE); /* Thor: 要不要可以 ignore? */
    sprintf(chatbuf, "※ %s 收到您的邀請了", xuser->chatid);
    send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
}

static void
chat_ban(ChatUser *cu, char *msg)
{
    char *banned;
    ChatUser *xuser;
    ChatRoom *room;
    UserList **list;
    int unum = 0;

    banned = nextword(&msg);

    if (!banned || !*banned)
    {
	// list all
	UserList *list;
	if(!(list = cu->room->ban))
	{
	    strcpy(chatbuf, "◆ 目前黑名單是空的");
	} 
	else 
	{
	    int len;
	    char buf[16];

	    send_to_user(cu, "◆ 黑名單列表：", 0, MSG_MESSAGE);
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
	    chatbuf[len] = 0;
	}
	if (chatbuf[0])
	    send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	return;
    }

    if (!ROOMOP(cu))
    {
	send_to_user(cu, msg_not_op, 0, MSG_MESSAGE);
	return;
    }
    xuser = cuser_by_chatid(banned);

    if (!xuser)
	unum = searchuser(banned, NULL);
    else
	unum = xuser->userno;

    if (!unum)
    {
	sprintf(chatbuf, msg_no_such_id, banned);
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	return;
    }

    room = cu->room;
    assert(room);
    list = &(room->ban);

    if (list_belong(*list, unum))
    {
	sprintf(chatbuf, "※ %s 已經在黑名單內了", banned);
	send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
	return;
    }

    if (xuser)
    {
	list_add(list, xuser);
	sprintf(chatbuf, "※ %s (%s) 已被加入黑名單", xuser->chatid, xuser->userid);
    }
    else
    {
	list_add_id(list, banned);
	sprintf(chatbuf, "※ %s 已被加入黑名單", banned);
    }

    send_to_user(cu, chatbuf, 0, MSG_MESSAGE);
}


static void
chat_broadcast(ChatUser *cu, char *msg)
{
    if (!CHATSYSOP(cu))
    {
	send_to_user(cu, "※ 您沒有在聊天室廣播的權力!", 0, MSG_MESSAGE);
	return;
    }
    if (*msg == '\0')
    {
	send_to_user(cu, "※ 請指定廣播內容", 0, MSG_MESSAGE);
	return;
    }
    sprintf(chatbuf, ANSI_COLOR(1) "※ " BBSNAME "聊天室廣播中 [%s]....." ANSI_RESET,
	    cu->chatid);
    send_to_room(ROOM_ALL, chatbuf, 0, MSG_MESSAGE);
    sprintf(chatbuf, "◆ %s", msg);
    send_to_room(ROOM_ALL, chatbuf, 0, MSG_MESSAGE);
}


static void
chat_goodbye(ChatUser *cu, char *msg)
{
    exit_room(cu, EXIT_LOGOUT, msg);
    /* Thor: 要不要加 logout_user(cu) ? */
}


/* --------------------------------------------- */
/* MUD-like social commands : action             */
/* --------------------------------------------- */

struct ChatAction
{
    char *verb;                   /* 動詞 */
    char *chinese;                /* 中文翻譯 */
    char *part1_msg;              /* 介詞 */
    char *part2_msg;              /* 動作 */
};


static ChatAction party_data[] =
{
    {"aluba", "阿魯巴", "把", "架上柱子阿魯巴!!"},
    {"adore", "景仰", "對", "的景仰有如滔滔江水,連綿不絕……"},
    {"bearhug", "熱擁", "熱情的擁抱", ""},
    {"blade", "一刀", "一刀啟程把", "送上西天"},
    {"bless", "祝福", "祝福", "心想事成"},
    {"board", "主機板", "把", "抓去跪主機板"},
    {"bokan", "氣功\", "雙掌微合，蓄勢待發……突然間，電光乍現，對", "使出了Ｂo--Ｋan！"},
  {"bow", "鞠躬", "畢躬畢敬的向", "鞠躬"},
  {"box", "幕之內", "開始輪擺\式移位，對", "作肝臟攻擊"},
  {"boy", "平底鍋", "從背後拿出了平底鍋，把", "敲昏了"},
  {"bye", "掰掰", "向", "說掰掰!!"},
  {"call", "呼喚", "大聲的呼喚,啊~~", "啊~~~你在哪裡啊啊啊啊~~~~"},
  {"caress", "輕撫", "輕輕的撫摸著", ""},
  {"clap", "鼓掌", "向", "熱烈鼓掌"},
  {"claw", "抓抓", "從貓咪樂園借了隻貓爪，把", "抓得死去活來"},
  {"comfort", "安慰", "溫言安慰", ""},
  {"cong", "恭喜", "從背後拿出了拉炮，呯！呯！恭喜", ""},
  {"cpr", "口對口", "對著", "做口對口人工呼吸"},
  {"cringe", "乞憐", "向", "卑躬屈膝，搖尾乞憐"},
  {"cry", "大哭", "向", "嚎啕大哭"},
  {"dance", "跳舞", "拉了", "的手翩翩起舞"  },
  {"destroy", "毀滅", "祭起了『極大毀滅咒文』，轟向", ""},
  {"dogleg", "狗腿", "對", "狗腿"},
  {"drivel", "流口水", "對著", "流口水"},
  {"envy", "羨慕", "向", "流露出羨慕的眼光"},
  {"eye", "送秋波", "對", "頻送秋波"},
  {"fire", "銬問", "拿著火紅的鐵棒走向", ""},
  {"forgive", "原諒", "接受道歉，原諒了", ""},
  {"french", "法式吻", "把舌頭伸到", "喉嚨裡～～～哇！一個浪漫的法國氏深吻"},
  {"giggle", "傻笑", "對著", "傻傻的呆笑"},
  {"glue", "補心", "用三秒膠，把", "的心黏了起來"},
  {"goodbye", "告別", "淚\眼汪汪的向", "告別"},
  {"grin", "奸笑", "對", "露出邪惡的笑容"},
  {"growl", "咆哮", "對", "咆哮不已"},
  {"hand", "握手", "跟", "握手"},
  {"hide", "躲", "躲在", "背後"},
  {"hospitl", "送醫院", "把", "送進醫院"},
  {"hug", "擁抱", "輕輕地擁抱", ""},
  {"hrk", "昇龍拳", "沉穩了身形，匯聚了內勁，對", "使出了一記Ｈo--Ｒyu--Ｋan！！！"},
  {"jab", "戳人", "溫柔的戳著", ""},
  {"judo", "過肩摔", "抓住了", "的衣襟，轉身……啊，是一記過肩摔！"},
  {"kickout", "踢", "用大腳把", "踢到山下去了"},
  {"kick", "踢人", "把", "踢的死去活來"},
  {"kiss", "輕吻", "輕吻", "的臉頰"},
  {"laugh", "嘲笑", "大聲嘲笑", ""},
  {"levis", "給我", "說：給我", "！其餘免談！"},
  {"lick", "舔", "狂舔", ""},
  {"lobster", "壓制", "施展逆蝦形固定，把", "壓制在地板上"},
  {"love", "表白", "對", "深情的表白"},
  {"marry", "求婚", "捧著九百九十九朵玫瑰向", "求婚"},
  {"no", "不要啊", "拼命對著", "搖頭~~~~不要啊~~~~"},
  {"nod", "點頭", "向", "點頭稱是"},
  {"nudge", "頂肚子", "用手肘頂", "的肥肚子"},
  {"pad", "拍肩膀", "輕拍", "的肩膀"},
  {"pettish", "撒嬌", "跟", "嗲聲嗲氣地撒嬌"},
  {"pili", "霹靂", "使出 君子風 天地根 般若懺 三式合一打向", "~~~~~~"},
  {"pinch", "擰人", "用力的把", "擰的黑青"},
  {"roll", "打滾", "放出多爾袞的音樂,", "在地上滾來滾去"},
  {"protect", "保護", "保護著", ""},
  {"pull", "拉", "死命地拉住", "不放"},
  {"punch", "揍人", "狠狠揍了", "一頓"},
  {"rascal", "耍賴", "跟", "耍賴"},
  {"recline", "入懷", "鑽到", "的懷裡睡著了……"},
  {"respond", "負責", "安慰", "說：『不要哭，我會負責的……』"},
  {"shrug", "聳肩", "無奈地向", "聳了聳肩膀"},
  {"sigh", "歎氣", "對", "歎了一口氣"},
  {"slap", "打耳光", "啪啪的巴了", "一頓耳光"},
  {"smooch", "擁吻", "擁吻著", ""},
  {"snicker", "竊笑", "嘿嘿嘿..的對", "竊笑"},
  {"sniff", "不屑", "對", "嗤之以鼻"},
  {"spank", "打屁屁", "用巴掌打", "的臀部"},
  {"squeeze", "緊擁", "緊緊地擁抱著", ""},
  {"sysop", "召喚", "叫出了批踢踢，把", "踩扁了！"},
  {"thank", "感謝", "向", "感謝得五體投地"},
  {"tickle", "搔癢", "咕嘰!咕嘰!搔", "的癢"},
  {"wake", "搖醒", "輕輕地把", "搖醒"},
  {"wave", "揮手", "對著", "拼命的搖手"},
  {"welcome", "歡迎", "歡迎", "進來八卦一下"},
  {"what", "什麼", "說：『", "哩公瞎密哇隴聽某?？?﹖?』"},
  {"whip", "鞭子", "手上拿著蠟燭，用鞭子痛打", ""},
  {"wink", "眨眼", "對", "神秘的眨眨眼睛"},
  {"zap", "猛攻", "對", "瘋狂的攻擊"},
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
	    party = "大家";
	else
	{
	    ChatUser *xuser;

	    xuser = fuzzy_cuser_by_chatid(party);
	    if (xuser == NULL)
	    {                       /* Thor.0724: 用 userid也嘛通 */
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
		sprintf(chatbuf, "※ 請指明聊天代號");
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
	sprintf(chatbuf, ANSI_COLOR(1;32) "%s " ANSI_COLOR(31) "%s" ANSI_COLOR(33) " %s " ANSI_COLOR(31) "%s" ANSI_RESET,
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
    { "ask", "詢問", "問", NULL },
    { "chant", "歌頌", "高聲歌頌", NULL },
    { "cheer", "喝采", "喝采", NULL },
    { "chuckle", "輕笑", "輕笑", NULL },
    { "curse", "暗幹", "暗幹", NULL },
    { "demand", "要求", "要求", NULL },
    { "frown", "皺眉頭", "蹙眉", NULL },
    { "groan", "呻吟", "呻吟", NULL },
    { "grumble", "發牢騷", "發牢騷", NULL },
    { "guitar", "彈唱", "邊彈著吉他，邊唱著", NULL },
    { "hum", "喃喃", "喃喃自語", NULL },
    { "moan", "怨嘆", "怨嘆", NULL },
    { "notice", "強調", "強調", NULL },
    { "order", "命令", "命令", NULL },
    { "ponder", "沈思", "沈思", NULL },
    { "pout", "噘嘴", "噘著嘴說", NULL },
    { "pray", "祈禱", "祈禱", NULL },
    { "request", "懇求", "懇求", NULL },
    { "shout", "大罵", "大罵", NULL },
    { "sing", "唱歌", "唱歌", NULL },
    { "smile", "微笑", "微笑", NULL },
    { "smirk", "假笑", "假笑", NULL },
    { "swear", "發誓", "發誓", NULL },
    { "tease", "嘲笑", "嘲笑", NULL },
    { "whimper", "嗚咽", "嗚咽的說", NULL },
    { "yawn", "哈欠", "邊打哈欠邊說", NULL },
    { "yell", "大喊", "大喊", NULL },
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
	sprintf(chatbuf, ANSI_COLOR(1;32) "%s " ANSI_COLOR(31) "%s：" ANSI_COLOR(33) " %s" ANSI_RESET,
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
    { "applaud", "拍手", "啪啪啪啪啪啪啪....", NULL },
    { "ayo", "唉呦喂", "唉呦喂~~~", NULL },
    { "back", "坐回來", "回來坐正繼續奮戰", NULL },
    { "blood", "在血中", "倒在血泊之中", NULL },
    { "blush", "臉紅", "臉都紅了", NULL },
    { "broke", "心碎", "的心破碎成一片一片的", NULL },
    { "careles", "沒人理", "嗚～～都沒有人理我 :~~~~", NULL },
    { "chew", "嗑瓜子", "很悠閒的嗑起瓜子來了", NULL },
    { "climb", "爬山", "自己慢慢爬上山來……", NULL },
    { "cold", "感冒了", "感冒了,媽媽不讓我出去玩 :~~~(", NULL },
    { "cough", "咳嗽", "咳了幾聲", NULL },
    { "die", "暴斃", "當場暴斃", NULL },
    { "faint", "昏倒", "當場昏倒", NULL },
    { "flop", "香蕉皮", "踩到香蕉皮... 滑倒！", NULL },
    { "fly", "飄飄然", "飄飄然", NULL },
    { "gold", "拿金牌", "唱著：『金ㄍㄠˊ金ㄍㄠˊ  出國比賽! 得冠軍，拿金牌，光榮倒鄧來！』", NULL },
    { "gulu", "肚子餓", "的肚子發出咕嚕~~~咕嚕~~~的聲音", NULL },
    { "haha", "哇哈哈", "哇哈哈哈.....^o^", NULL },
    { "helpme", "求救", "大喊~~~救命啊~~~~", NULL },
    { "hoho", "呵呵笑", "呵呵呵笑個不停", NULL },
    { "happy", "高興", "高興得在地上打滾", NULL },
    { "idle", "呆住了", "呆住了", NULL },
    { "jacky", "晃晃", "痞子般的晃來晃去", NULL },
    { "luck", "幸運", "哇！福氣啦！", NULL },
    { "macarn", "一種舞", "開始跳起了ＭaＣaＲeＮa～～～～", NULL },
    { "miou", "喵喵", "喵喵口苗口苗～～～～～", NULL },
    { "mouth", "扁嘴", "扁嘴中!!", NULL },
    { "nani", "怎麼會", "：奈ㄝ啊捏??", NULL },
    { "nose", "流鼻血", "流鼻血", NULL },
    { "puke", "嘔吐", "嘔吐中", NULL },
    { "rest", "休息", "休息中，請勿打擾", NULL },
    { "reverse", "翻肚", "翻肚", NULL },
    { "room", "開房間", "r-o-O-m-r-O-Ｏ-Mmm-rRＲ........", NULL },
    { "shake", "搖頭", "搖了搖頭", NULL },
    { "sleep", "睡著", "趴在鍵盤上睡著了，口水流進鍵盤，造成當機！", NULL },
    { "so", "就醬子", "就醬子!!", NULL },
    { "sorry", "道歉", "嗚啊!!我對不起大家,我對不起國家社會~~~~~~嗚啊~~~~~", NULL },
    { "story", "講古", "開始講古了", NULL },
    { "strut", "搖擺\走", "大搖大擺\地走", NULL },
    { "suicide", "自殺", "自殺", NULL },
    { "tea", "泡茶", "泡了壺好茶", NULL },
    { "think", "思考", "歪著頭想了一下", NULL },
    { "tongue", "吐舌", "吐了吐舌頭", NULL },
    { "wall", "撞牆", "跑去撞牆", NULL },
    { "wawa", "哇哇", "哇哇哇~~~~~!!!!!  ~~~>_<~~~", NULL },
    { "www", "汪汪", "汪汪汪!!!", NULL },
    { "zzz", "打呼", "呼嚕~~~~ZZzZzｚＺZZzzZzzzZZ...", NULL },
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
	    sprintf(chatbuf, ANSI_COLOR(1;32) "%s " ANSI_COLOR(31) "%s" ANSI_RESET,
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
    ANSI_COLOR(1;37) "【 Verb + Nick：   動詞 + 對方名字 】" ANSI_COLOR(36) "   例：//kick piggy" ANSI_RESET,
    ANSI_COLOR(1;37) "【 Verb + Message：動詞 + 要說的話 】" ANSI_COLOR(36) "   例：//sing 天天天藍" ANSI_RESET,
    ANSI_COLOR(1;37) "【 Verb：動詞 】    ↑↓：舊話重提" ANSI_RESET, NULL
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

    sprintf(chatbuf, "3 動作  交談  狀態");
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

    /* Xshadow: 只有 condition 才是 immediate mode */
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
view_action_verb(ChatUser *cu, char cmd)       /* Thor.0726: 新加動詞分類顯示 */
{
    register int i;
    register char *p, *q, *data, *expn;
    register ChatAction *cap;

    send_to_user(cu, "/c", 0, MSG_CLRSCR);

    data = chatbuf;

    if (cmd < '1' || cmd > '3')
    {                             /* Thor.0726: 寫得不好, 想辦法改進... */
	for (i = 0; (p = dscrb[i]); i++)
	{
	    sprintf(data, "  [//]help %d          - MUD-like 社交動詞   第 %d 類", i + 1, i + 1);
	    send_to_user(cu, data, 0, MSG_MESSAGE);
	    send_to_user(cu, p, 0, MSG_MESSAGE);
	    send_to_user(cu, " ", 0, MSG_MESSAGE);    /* Thor.0726: 換行, 需要 " "
						       * 嗎? */
	}
    }
    else
    {
	send_to_user(cu, dscrb[cmd-'1'], 0, MSG_MESSAGE);

	expn = chatbuf + 100;       /* Thor.0726: 應該不會overlap吧? */

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
		send_to_user(cu, expn, 0, MSG_MESSAGE); /* Thor.0726: 顯示中文註解 */
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
    /* send_to_user(cu, " ",0); *//* Thor.0726: 換行, 需要 " " 嗎? */
}

/* ----------------------------------------------------- */
/* chat user service routines                            */
/* ----------------------------------------------------- */


static ChatCmd chatcmdlist[] =
{
    {"act", chat_act, 0},
    {"ban", chat_ban, 0},
    {"bye", chat_goodbye, 0},
    {"chatroom", chat_chatroom, 1}, /* Xshadow: for common client */
    {"cloak", chat_cloak, 2},
    {"flags", chat_setroom, 0},
    {"ignore", chat_ignore, 1},
    {"invite", chat_invite, 0},
    {"join", chat_join, 0},
    {"kick", chat_kick, 1},
    {"msg", chat_private, 0},
    {"nick", chat_nick, 0},
    {"operator", chat_makeop, 0},
    {"party", chat_party, 1},       /* Xshadow: party data for common client */
    {"partyinfo", chat_partyinfo, 1},       /* Xshadow: party info for common * client */
    {"query", chat_query, 0},

    {"room", chat_list_rooms, 0},
    {"unignore", chat_unignore, 1},
    {"unban", chat_unban, 1},
    {"whoin", chat_list_by_room, 1},
    {"wall", chat_broadcast, 2},

    {"who", chat_map_chatids_thisroom, 0},
    {"list", chat_list_users, 0},
    {"topic", chat_topic, 0},
    {"version", chat_version, 1},
    {"xinfo", chat_xinfo, 1},

    {NULL, NULL, 0}
};

/* Thor: 0 不用 exact, 1 要 exactly equal, 2 秘密指令 */


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
	    return login_user(cu, msg+2);
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
	if (!CLOAK(cu))           /* Thor: 聊天室隱身術 */
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
	    /* Thor.0726: 動詞分類 */
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
	sprintf(chatbuf, "◆ 指令錯誤：/%s", cmd);
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

    /* Thor: 萬一server尚未接受connection, 就回去的話, client 第一次會進入失敗 */
    /* 所以移至 listen 後 */

    /* --------------------------------------------------- */
    /* detach daemon process                               */
    /* --------------------------------------------------- */

    close(0);
    close(1);
    close(2);

#ifndef SELFTEST
    if (fork())
	exit(0);
#endif

    chdir(BBSHOME);

    setsid();

#ifndef SELFTEST
    attach_SHM();
#endif
    /* --------------------------------------------------- */
    /* adjust the resource limit                           */
    /* --------------------------------------------------- */

    getrlimit(RLIMIT_NOFILE, &limit);
    limit.rlim_cur = limit.rlim_max;
    setrlimit(RLIMIT_NOFILE, &limit);

    fd = open(CHAT_PIDFILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0)
    {
	/* sprintf(buf, "%5d\n", value); */
	sprintf(buf, "%5d\n", (int)getpid());
	write(fd, buf, 6);
	close(fd);
    }

    value = 1;
    fd = tobind(XCHATD_ADDR);

    ld.l_onoff = ld.l_linger = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_LINGER, (char *) &ld, sizeof(ld)) == -1)
    {
	perror("setsockopt");
	exit(-1);
    }

    return fd;
}


static void
free_resource(int fd)
{
    static int loop = 0;
    register ChatUser *user;
    register int sock, num;

    /* 重新計算 maxfd */
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

#ifdef SELFTESTER
#define MAXTESTUSER 20

int selftest_connect(void)
{
    struct sockaddr_in sin;
    struct hostent *h;
    int cfd;

    if (!(h = gethostbyname("localhost"))) {
	perror("gethostbyname");
	return -1;
    }
    memset(&sin, 0, sizeof sin);
#ifdef __FreeBSD__
    sin.sin_len = sizeof(sin);
#endif
    sin.sin_family = PF_INET;
    memcpy(&sin.sin_addr, h->h_addr, h->h_length);
    sin.sin_port = htons(NEW_CHATPORT);
    cfd = socket(sin.sin_family, SOCK_STREAM, 0);
    if (connect(cfd, (struct sockaddr *) & sin, sizeof sin) != 0) {
	perror("connect");
	return -1;
    }
    return cfd;
}

static int
selftest_send(int fd, char *buf)
{
    int             len;
    char            genbuf[200];

    snprintf(genbuf, sizeof(genbuf), "%s\n", buf);
    len = strlen(genbuf);
    return (send(fd, genbuf, len, 0) == len);
}
void selftest_testing(void)
{
    int cfd;
    char userid[IDLEN+1];
    char inbuf[1024], buf[1024];

    cfd=selftest_connect();
    if(cfd<0) exit(1);
    while(1) {
	snprintf(userid, sizeof(userid), "%ld", random()%(MAXTESTUSER*2));
	sprintf(buf, "/%s! %s %s %s", random()%4==0?"-":"",userid, userid, "passwd");
	selftest_send(cfd, buf);
	if (recv(cfd, inbuf, 3, 0) != 3) {
	    close(cfd);
	    return;
	}
	if (!strcmp(inbuf, CHAT_LOGIN_OK))
	    break;
    }

    if(random()%4!=0) {
	sprintf(buf, "/j %d", random()%5);
	selftest_send(cfd, buf);
    }

    while(1) {
	int i;
	int r;
	int inlen;
	fd_set rset,xset;
	struct timeval zerotv;

	FD_ZERO(&rset);
	FD_SET(cfd, &rset);
	FD_ZERO(&xset);
	FD_SET(cfd, &xset);
	zerotv.tv_sec=0;
	zerotv.tv_usec=0;
	select(cfd+1, &rset, NULL, &xset, &zerotv);
	if(FD_ISSET(cfd, &rset)) {
	    inlen=read(cfd, inbuf, sizeof(inbuf));
	    if(inlen<0) break;
	}
	if(FD_ISSET(cfd, &xset)) {
	    inlen=read(cfd, inbuf, sizeof(inbuf));
	    if(inlen<0) break;
	}


	if(random()%10==0) {
	    switch(random()%4) {
		case 0:
		    r=random()%(sizeof(party_data)/sizeof(party_data[0])-1);
		    sprintf(buf, "//%s",party_data[r].verb);
		    break;
		case 1:
		    r=random()%(sizeof(speak_data)/sizeof(speak_data[0])-1);
		    sprintf(buf, "//%s",speak_data[r].verb);
		    break;
		case 2:
		    r=random()%(sizeof(condition_data)/sizeof(condition_data[0])-1);
		    sprintf(buf, "//%s",condition_data[r].verb);
		    break;
		case 3:
		    sprintf(buf, "blah");
		    break;
	    }
	} else {
		r=random()%(sizeof(chatcmdlist)/sizeof(chatcmdlist[0])-1);
		sprintf(buf, "/%s",chatcmdlist[r].cmdstr);
		if(strncmp("/flag",buf,5)==0) {
		    if(random()%2)
			strcat(buf," +");
		    else
			strcat(buf," -");
		    strcat(buf,random()%2?"l":"L");
		    strcat(buf,random()%2?"h":"H");
		    strcat(buf,random()%2?"s":"S");
		    strcat(buf,random()%2?"t":"T");
		} else if(strncmp("/bye",buf,4)==0) {
		    switch(random()%10) {
			case 0: strcpy(buf,"//"); break;
			case 1: strcpy(buf,"//1"); break;
			case 2: strcpy(buf,"//2"); break;
			case 3: strcpy(buf,"//3"); break;
			case 4: strcpy(buf,"/bye"); break;
			case 5: strcpy(buf,"/help op"); break;
			case 6: close(cfd); return; break;
			case 7: strcpy(buf,"/help"); break;
			case 8: strcpy(buf,"/help"); break;
			case 9: strcpy(buf,"/help"); break;
		    }
		}
	}
	for(i=random()%3; i>0; i--) {
	    char tmp[1024];
	    sprintf(tmp," %ld", random()%(MAXTESTUSER*2));
	    strcat(buf, tmp);
	}

	selftest_send(cfd, buf);
    }
    close(cfd);
}

void selftest_test(void)
{
    while(1) {
	fprintf(stderr,".");
	selftest_testing();
	usleep(1000);
    }
}

void selftest(void)
{
    int i;
    pid_t pid;

    pid=fork();
    if(pid<0) exit(1);
    if(pid) return;
    sleep(1);

    for(i=0; i<MAXTESTUSER; i++) {
	if(fork()==0)
	    selftest_test();
	sleep(1);
	random();
    }

    exit(0);
}
#endif

int
main(int argc, char *argv[])
{
    register int msock, csock, nfds;
    register ChatUser *cu, *cunext;
    register fd_set *rptr, *xptr;
    fd_set rset, xset;
    struct timeval tv;
    time4_t uptime, tmaintain;

#ifdef SELFTESTER
    if(argc>1) {
	Signal(SIGPIPE, SIG_IGN);
      selftest();
      return 0;
    }
#endif
    msock = start_daemon();

    setgid(BBSGID);
    setuid(BBSUID);

    boot_time = time(0);
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

	    /* client/server 版本利用 ping-pong 方法判斷 user 是不是還活著 */
	    /* 如果 client 已經結束了，就釋放其 resource */

	    free_resource(msock);
#ifdef SELFTEST
	    break;
#endif
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

	for (cu = mainuser; cu && nfds>0; cu = cunext) {
	    /* logout_user() 會 free(cu); 先把 cu->next 記下來 */
	    /* FIXME 若剛好 cu 在 main room /kick cu->next, 則 cu->next 會被 free 掉 */
	    cunext = cu->unext;
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
    return 0;
}
