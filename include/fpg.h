

#define STRLEN   80             /* Length of most string data */
#define BTLEN    48             /* Length of board title */
#define FTTLEN    72             /* Length of title */
#define NAMELEN  40             /* Length of username/realname */
#define FNLEN    33             /* Length of filename  */
#define IDLEN    12             /* Length of bid/uid */
#define PASSLEN  14             /* Length of encrypted passwd field */
#define REGLEN   38             /* Length of registration data */

typedef unsigned char uschar;   /* length = 1 */
typedef unsigned int  usint;    /* length = 4 */


typedef struct
{
      uschar        id;
        char          broken;
}       WEAPONARMOR;

typedef struct
{
  char          id;
  char          name[IDLEN + 1];
  char          lv;
  short         lp;
  ushort        mlp;
  short         hp;
  ushort        mhp;
  uschar        po;
  uschar        st;
  uschar        ag;
  uschar        lu;
  int           ex;
  char          emotion;
  char          moral;
  char          wisdom;
  char          will;
  char          charm;
  WEAPONARMOR   weapon;
  WEAPONARMOR   armor[4];
  uschar        flag;
  char          hungry;
  char          sick;
  char          angry;
}       PET;

typedef struct
{
  char   userid[IDLEN + 1];
  char   realname[20];
  char   username[24];
  char   passwd[PASSLEN];
  ushort uflag;
  usint  userlevel;
  ushort numlogins;
  ushort numposts;
  time_t firstlogin;
  time_t lastlogin;
  char   lasthost[16];
  char   email[50];
  char   address[50];
  char   justify[REGLEN + 1];
  uschar month;
  uschar day;
  uschar year;
  uschar sex;
  uschar state;
  ushort mailk;
  ushort keepmail;
  int    money;
  ushort totalday;
  uschar totalhour;
  uschar totalmin;
  int    market[10];
  short  locate;
  char   action;
  char   direct;
  char   speed;
  char   count;
  uschar landnum;
  uschar tool[10];
  char   NAME[IDLEN + 1];
  char   LV;
  short  HP;
  short  MHP;
  short  MP;
  short  MMP;
  short  WC;
  short  AC;
  short  PO;
  short  ST;
  short  AG;
  short  LU;
  int    EX;
  char   EVENT;
  uschar WA[6];
  uschar USE[10];
  uschar MAGIC[5];
  uschar NOWOCCUPATION;
  short  OCCUPATION[4];
  uschar cardfightnum;
  uschar cardfight[20];
  uschar dragon[5];
  PET    pet;
  char   left[40];
} ACCT;

struct fileheader
{
  char filename[FNLEN];         /* M.9876543210.A */
  char savemode;                /* file save mode */
  char owner[IDLEN + 2];        /* uid[.] */
  char date[6];                 /* [02/02] or space(5) */
  char title[FTTLEN + 1];
  uschar filemode;              /* must be last field @ boards.c */
};
typedef struct fileheader fileheader;

struct boardheader
{
  char brdname[IDLEN + 1];      /* bid */
  char title[BTLEN + 1];
  char BM[IDLEN * 3 + 3];       /* BMs' uid, token '/' */
  char group[9];		/* 看板分類 */
  char type;			/* 看板性質: 轉信？目錄？ */
  char pad[1];
  time_t bupdate;               /* note update time */
  char pad2[3];
  uschar bvote;                 /* Vote flags */
  time_t vtime;                 /* Vote close time */
  usint level;
};
typedef struct boardheader boardheader;

