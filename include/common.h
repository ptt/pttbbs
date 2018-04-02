/* $Id$ */
#ifndef INCLUDE_COMMON_H
#define INCLUDE_COMMON_H

#define STR_GUEST	"guest"	    // guest account
#define STR_REGNEW	"new"	    // �Ψӫطs�b�����W��

#define DEFAULT_BOARD   str_sysop

// BBS Configuration Files
#define FN_CONF_EDITABLE	"etc/editable"	    // ���ȥi�s�誺�t���ɮצC��
#define FN_CONF_RESERVED_ID	"etc/reserved.id"   // �O�d�t�ΥεL�k���U�� ID
#define FN_CONF_BINDPORTS	"etc/bindports.conf"   // �w�]�n���ѳs�u�A�Ȫ� port �C��
#define FN_CONF_BANIP           BBSHOME "/etc/banip.conf"    // �T��s�u�� IP �C��

// BBS Data File Names
#define FN_PASSWD       BBSHOME "/.PASSWDS"      /* User records */
#define FN_CHICKEN	"chicken"
#define FN_USSONG       "ussong"        /* �I�q�έp */
#define FN_POST_NOTE    "post.note"     /* po�峹�Ƨѿ� */
#define FN_POST_BID     "post.bid"
#define FN_MONEY        "etc/money"
#define FN_OVERRIDES    "overrides"
#define FN_REJECT       "reject"
#define FN_WATER        "water"         // �¤���
#define FN_BANNED       "banned"        // �s����
#define FN_BANNED_HISTORY "banned.history"  // �s�������v�O��
#define FN_BADPOST_HISTORY "badpost.history"  // �H����v�O��
#define FN_CANVOTE      "can_vote"
#define FN_VISABLE      "visable"	// �����D�O�֫������A�N���N���a...
#define FN_ALOHAED      "alohaed"       // �W���n�q���ڪ��W�� (�s���)
#define FN_ALOHA        "aloha"         // �ڤW���n�q�����W�� (�۰ʲ���)
#define FN_USIES        "usies"         /* BBS log */
#define FN_DIR		".DIR"
#define FN_DIR_BOTTOM   ".DIR.bottom"
#define FN_BOARD        ".BRD"          /* board list */
#define FN_USEBOARD     "usboard"       /* �ݪO�έp */
#define FN_TOPSONG      "etc/topsong"
#define FN_OVERRIDES    "overrides"
#define FN_TICKET       "ticket"
#define FN_TICKET_END   "ticket.end"
#define FN_TICKET_LOCK  "ticket.end.lock"
#define FN_TICKET_ITEMS "ticket.items"
#define FN_TICKET_RECORD "ticket.data"
#define FN_TICKET_USER    "ticket.user"  
#define FN_TICKET_OUTCOME "ticket.outcome"
#define FN_TICKET_BRDLIST "boardlist"
#define FN_BRDLISTHELP	"etc/boardlist.help"
#define FN_BOARDHELP	"etc/board.help"
#define FN_MAIL_ACCOUNT_SYSOP "etc/mail_account_sysop"  // �b�������H�c�C��
#define FN_MAIL_ACCOUNT_SYSOP_DESC "etc/mail_account_sysop_desc"  // �b�������H�c����
#define FN_USERMEMO	"memo.txt"	// �ϥΪ̭ӤH�O�ƥ�
#define FN_BADLOGIN	"logins.bad"	// in BBSHOME & user directory
#define FN_RECENTLOGIN	"logins.recent"	// in user directory
#define FN_FORWARD      ".forward"      /* auto forward */
#ifndef SZ_RECENTLOGIN
#define SZ_RECENTLOGIN	(16000)		// size of max recent log before rotation
#endif
#define FN_RECENTPAY    "money.recent"
#ifndef SZ_RECENTPAY
#define SZ_RECENTPAY    (16000)
#endif

// �ۭq�R���峹�ɥX�{�����D�P�ɮ�
#ifndef FN_SAFEDEL
#ifdef USE_EDIT_HISTORY
// For edit_history, the new file name must be as short as possible so that so we can restore it later.
#define FN_SAFEDEL	".d"
#define FN_SAFEDEL_PREFIX_LEN (2) // must match FN_SAFEDEL
#else
#define FN_SAFEDEL	".deleted"
#endif // USE_EDIT_HISTORY
#endif // FN_SAFEDEL
#ifndef STR_SAFEDEL_TITLE
#define STR_SAFEDEL_TITLE   "(����w�Q�R��)"
#endif 
#define FN_EDITHISTORY  ".history"

#ifndef SAFE_ARTICLE_DELETE_NUSER
#define SAFE_ARTICLE_DELETE_NUSER (2)
#endif

#define MSG_DEL_CANCEL  "�����R��"
#define MSG_BIG_BOY     "�ڬO�j�ӭ�! ^o^Y"
#define MSG_BIG_GIRL    "�@���j���k *^-^*"
#define MSG_LITTLE_BOY  "�ڬO���}��... =)"
#define MSG_LITTLE_GIRL "�̥i�R������! :>"
#define MSG_MAN         "����Ҩ��� (^O^)"
#define MSG_WOMAN       "�s�ڤp����!! /:>"
#define MSG_PLANT       "�Ӫ��]���ʧO��.."
#define MSG_MIME        "�q���`�S�ʧO�F�a"

#define MSG_CLOAKED     "�w�i�J���μҦ�(���C��ϥΪ̦W��W)"
#define MSG_UNCLOAK     "�w���}���μҦ�(���}��ϥΪ̦W��W)"

#define MSG_WORKING     "�B�z���A�еy��..."

#define MSG_CANCEL      "�����C"
#define MSG_USR_LEFT    "�ϥΪ̤w�g���}�F"
#define MSG_NOBODY      "�ثe�L�H�W�u"

#define MSG_DEL_OK      "�R������"
#define MSG_DEL_CANCEL  "�����R��"
#define MSG_DEL_ERROR   "�R�����~"
#define MSG_DEL_NY      "�нT�w�R��(Y/N)?[N] "

#define MSG_FWD_OK      "�峹��H����!"
#define MSG_FWD_ERR1    "��H���~: �t�ο��~ system error"
#define MSG_FWD_ERR2    "��H���~: ��}���~ address error"

#define MSG_SURE_NY     "�бz�T�w(Y/N)�H[N] "
#define MSG_SURE_YN     "�бz�T�w(Y/N)�H[Y] "

#define MSG_BID         "�п�J�ݪO�W��: "
#define MSG_UID         "�п�J�ϥΪ̥N��: "
#define MSG_PASSWD      "�п�J�z���K�X: "

#define MSG_BIG_BOY     "�ڬO�j�ӭ�! ^o^Y"
#define MSG_BIG_GIRL    "�@���j���k *^-^*"
#define MSG_LITTLE_BOY  "�ڬO���}��... =)"
#define MSG_LITTLE_GIRL "�̥i�R������! :>"
#define MSG_MAN         "����Ҩ��� (^O^)"
#define MSG_WOMAN       "�s�ڤp����!! /:>"
#define MSG_PLANT       "�Ӫ��]���ʧO��.."
#define MSG_MIME        "�q���`�S�ʧO�F�a"

#define ERR_BOARD_OPEN  ".BOARD �}�ҿ��~"
#define ERR_BOARD_UPDATE        ".BOARD ��s���~"
#define ERR_PASSWD_OPEN ".PASSWDS �}�ҿ��~"

#define ERR_BID         "�A�d���F�աI�S���o�ӪO��I"
#define ERR_UID         "�o�̨S���o�ӤH�աI"
#define ERR_PASSWD      "�K�X�����I���ˬd�b���αK�X�j�p�g���L��J���~�C"
#define ERR_FILENAME    "�ɦW�����T�I"

#define TN_ANNOUNCE	"[���i]"

#define STR_AUTHOR1     "�@��:"
#define STR_AUTHOR2     "�o�H�H:"
#define STR_POST1       "�ݪO:"
#define STR_POST2       "����:"

#define STR_LOGINDAYS	"�n�J����"
#define STR_LOGINDAYS_QTY "��"

/* AIDS */
#define AID_DISPLAYNAME	"�峹�N�X(AID)"
/* end of AIDS */

/* QUERY_ARTICLE_URL */
#define URL_DISPLAYNAME "�峹���}"
/* end of QUERY_ARTICLE_URL */

/* LONG MESSAGES */
#define MSG_SELECT_BOARD ANSI_COLOR(7) "�i ��ܬݪO �j" ANSI_RESET "\n" \
			"�п�J�ݪO�W��(���ť���۰ʷj�M): "

#define MSG_SEPARATOR \
"�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w"

/* Flags to getdata input function */
#define NOECHO       0
#define DOECHO       1
#define LCECHO       2
#define NUMECHO	     4
#define GCARRY	     8	// (from M3) do not empty input buffer. 
#define PASSECHO    0x10 

#define YEA  1		       /* Booleans  (Yep, for true and false) */
#define NA   0

#define EQUSTR 0	/* for strcmp */

/* �n�����Y */
#define IRH 1   /* I reject him.		*/
#define HRM 2   /* He reject me.		*/
#define IBH 4   /* I am board friend of him.	*/
#define IFH 8   /* I friend him (He is one of my friends). */
#define HFM 16  /* He friends me (I am one of his friends). */
#define ST_FRIEND  (IBH | IFH | HFM)
#define ST_REJECT  (IRH | HRM)       

#define QCAST           int (*)(const void *, const void *)
#define chartoupper(c)  ((c >= 'a' && c <= 'z') ? c+'A'-'a' : c)

#define LEN_AUTHOR1     5
#define LEN_AUTHOR2     7

/* the title of article will be truncate to PROPER_TITLE_LEN */
#define PROPER_TITLE_LEN	42


/* ----------------------------------------------------- */
/* ���y�Ҧ� ��ɩw�q                                     */
/* ----------------------------------------------------- */
#define WB_OFO_USER_TOP		7
#define WB_OFO_USER_BOTTOM	11
#define WB_OFO_USER_NUM		((WB_OFO_USER_BOTTOM) - (WB_OFO_USER_TOP) + 1)
#define WB_OFO_USER_LEFT	28
#define WB_OFO_MSG_TOP		15
#define WB_OFO_MSG_BOTTOM	22
#define WB_OFO_MSG_LEFT		4

/* ----------------------------------------------------- */
/* ���D����                                              */
/* ----------------------------------------------------- */
#define SUBJECT_NORMAL      0
#define SUBJECT_REPLY       1
#define SUBJECT_FORWARD     2
#define SUBJECT_LOCKED      3

/* ----------------------------------------------------- */
/* �s�զW��Ҧ�   Ptt                                    */
/* ----------------------------------------------------- */
#define FRIEND_OVERRIDE 0
#define FRIEND_REJECT   1
#define FRIEND_ALOHA    2
// #define FRIEND_POST     3	    // deprecated
#define FRIEND_SPECIAL  4
#define FRIEND_CANVOTE  5
#define BOARD_WATER     6
#define BOARD_VISABLE   7 

#define LOCK_THIS   1    // lock�o�u���୫�ƪ�
#define LOCK_MULTI  2    // lock�Ҧ��u���୫�ƪ�   

#define MAX_MODES	(127)
#define MAX_RECOMMENDS  (100)
#define MAX_REPLY_COUNTS  (65500)

#define STR_CURSOR      ">"
#define STR_CURSOR2     "��"
#define STR_UNCUR       " "
#define STR_UNCUR2      "  "

#define NOTREPLYING     -1
#define REPLYING        0
#define RECVINREPLYING  1

/* ----------------------------------------------------- */
/* �s�边�ﶵ                                            */
/* ----------------------------------------------------- */
#define EDITFLAG_TEXTONLY   (0x00000001)
#define EDITFLAG_UPLOAD	    (0x00000002)
#define EDITFLAG_ALLOWLARGE (0x00000004)
#define EDITFLAG_ALLOWTITLE (0x00000008)
// #define EDITFLAG_ANONYMOUS  (0x00000010)
#define EDITFLAG_KIND_NEWPOST	(0x00000010)
#define EDITFLAG_KIND_REPLYPOST	(0x00000020)
#define EDITFLAG_KIND_SENDMAIL	(0x00000040)
#define EDITFLAG_KIND_MAILLIST	(0x00000080)
#define EDITFLAG_WARN_NOSELFDEL	(0x00000100)
// #define EDITFLAG_ALLOW_LOCAL    (0x00000200)
#define EDIT_ABORTED	-1

/* ----------------------------------------------------- */
/* ��ѫǱ`�� (xchatd)                                   */
/* ----------------------------------------------------- */
#define EXIT_LOGOUT     0
#define EXIT_LOSTCONN   -1
#define EXIT_CLIERROR   -2
#define EXIT_TIMEDOUT   -3
#define EXIT_KICK       -4

#define CHAT_LOGIN_OK       "OK"
#define CHAT_LOGIN_EXISTS   "EX"
#define CHAT_LOGIN_INVALID  "IN"
#define CHAT_LOGIN_BOGUS    "BG"

/* ----------------------------------------------------- */
/* Grayout Levels                                        */
/* ----------------------------------------------------- */
#define GRAYOUT_COLORBOLD (-2)
#define GRAYOUT_BOLD (-1)
#define GRAYOUT_DARK (0)
#define GRAYOUT_NORM (1)
#define GRAYOUT_COLORNORM (+2)

/* Typeahead */
#define TYPEAHEAD_NONE	(-1)
#define TYPEAHEAD_STDIN	(0)

/* ----------------------------------------------------- */
/* Constants                                             */
/* ----------------------------------------------------- */
#define DAY_SECONDS	    (86400)
#define MONTH_SECONDS	    (DAY_SECONDS*30)
#define MILLISECONDS	    (1000)  // milliseconds of one second

#ifndef SHM_HUGETLB
#define SHM_HUGETLB	04000	/* segment is mapped via hugetlb */
#endif

/* ----------------------------------------------------- */
/* Macros                                                */
/* ----------------------------------------------------- */

#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 96)
    #define __builtin_expect(exp,c) (exp)
#endif

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

#ifndef MIN
#define	MIN(a,b)	(((a)<(b))?(a):(b))
#endif
#ifndef MAX
#define	MAX(a,b)	(((a)>(b))?(a):(b))
#endif

#define toSTR(x)	__toSTR(x)
#define __toSTR(x)	#x
#define char_lower(c)  ((c >= 'A' && c <= 'Z') ? c|32 : c)

#define LOG_IF(x, y)    { if ((x)) { y; } else {} }


#endif
