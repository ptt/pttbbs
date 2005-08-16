#define STRLEN   80             /* Length of most string data */
#define BRC_STRLEN 15           /* Length of boardname */
#define BTLEN    48             /* Length of board title */
#define NAMELEN  40             /* Length of username/realname */
#define FNLEN    33             /* Length of filename  */
                                /* XXX Ptt 說這裡有bug*/
#define IDLEN    12             /* Length of bid/uid */
#define PASSLEN  14             /* Length of encrypted passwd field */
#define REGLEN   38             /* Length of registration data */



typedef unsigned char	uschar;	/* length = 1 */
typedef unsigned short	ushort;	/* length = 2 */
typedef unsigned long	uslong;	/* length = 4 */
typedef unsigned int	usint;	/* length = 4 */

/* ----------------------------------------------------- */
/* .PASSWDS struct : 512 bytes                           */
/* ----------------------------------------------------- */
struct sobuserec
{
  char userid[IDLEN + 1];         /* 使用者名稱  13 bytes */
  char realname[20];              /* 真實姓名    20 bytes */
  char username[24];              /* 暱稱        24 bytes */
  char passwd[PASSLEN];           /* 密碼        14 bytes */
  uschar uflag;                   /* 使用者選項   1 byte  */
  usint userlevel;                /* 使用者權限   4 bytes */
  ushort numlogins;               /* 上站次數     2 bytes */
  ushort numposts;                /* POST次數     2 bytes */
  time4_t firstlogin;              /* 註冊時間     4 bytes */
  time4_t lastlogin;               /* 前次上站     4 bytes */
  char lasthost[24];              /* 上站地點    24 bytes */
  char vhost[24];                 /* 虛擬網址    24 bytes */
  char email[50];                 /* E-MAIL      50 bytes */
  char address[50];               /* 地址        50 bytes */
  char justify[REGLEN + 1];       /* 註冊資料    39 bytes */
  uschar month;                   /* 出生月份     1 byte  */
  uschar day;                     /* 出生日期     1 byte  */
  uschar year;                    /* 出生年份     1 byte  */
  uschar sex;                     /* 性別         1 byte  */
  uschar state;                   /* 狀態??       1 byte  */
  usint habit;                    /* 喜好設定     4 bytes */
  uschar pager;                   /* 心情顏色     1 bytes */
  uschar invisible;               /* 隱身模式     1 bytes */
  usint exmailbox;                /* 信箱封數     4 bytes */
  usint exmailboxk;               /* 信箱K數      4 bytes */
  usint toquery;                  /* 好奇度       4 bytes */
  usint bequery;                  /* 人氣度       4 bytes */
  char toqid[IDLEN + 1];          /* 前次查誰    13 bytes */
  char beqid[IDLEN + 1];          /* 前次被誰查  13 bytes */
  uslong totaltime;		  /* 上線總時數   8 bytes */
  usint sendmsg;                  /* 發訊息次數   4 bytes */
  usint receivemsg;               /* 收訊息次數   4 bytes */
  usint goldmoney;                /* 風塵金幣     8 bytes */
  usint silvermoney;              /* 銀幣         8 bytes */
  usint exp;                      /* 經驗值       8 bytes */
  time4_t dtime;                   /* 存款時間     4 bytes */
  int scoretimes;                 /* 評分次數     4 bytes */
  uschar rtimes;                  /* 填註冊單次數 1 bytes */
  int award;                      /* 獎懲判斷     4 bytes */
  int pagermode;                  /* 呼叫器門號   4 bytes */
  char pagernum[7];               /* 呼叫器號碼   7 bytes */
  char feeling[5];                /* 心情指數     5 bytes */
  char title[20];                 /* 稱謂(封號)  20 bytes */
  usint five_win;
  usint five_lost;
  usint five_draw;
  char pad[91];                  /* 空著填滿至512用      */
};

typedef struct sobuserec sobuserec;

