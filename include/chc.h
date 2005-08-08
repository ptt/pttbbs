/* 象棋
 * 雙人對戰時，雙方都會有一個 chc_act_list 的 linked-list，紀錄著每下一步
 * 棋，必須將這個訊息丟給那些人（socket）。
 * 一開始當然就是對手一個，每當一個觀棋者加入（觀棋可以從紅方或黑方的觀點
 * 進行），其中一方的下棋者的 act_list 就會多一筆記錄，之後就會將下的或收
 * 到對方下的每一步棋傳給 act_list 中所有需要的人，達到觀棋的效果。
 */

#define SIDE_ROW           7
#define REAL_TURN_ROW      8
#define STEP_ROW           9
#define REAL_TIME_ROW1    10
#define REAL_TIME_ROW2    11
#define REAL_WARN_ROW     13
#define MYWIN_ROW         17
#define HISWIN_ROW        18

#define PHOTO_TURN_ROW    19
#define PHOTO_TIME_ROW1   20
#define PHOTO_TIME_ROW2   21
#define PHOTO_WARN_ROW    22

/* virtual lines */
#define TURN_ROW         128
#define TIME_ROW         129
#define WARN_ROW         130

#define CHC_VERSUS		1	/* 雙人 */
#define CHC_WATCH		2	/* 觀棋 */
#define CHC_PERSONAL		4	/* 打譜 */
#define CHC_WATCH_PERSONAL	8	/* 觀人打譜 */

#define CHE_O(c)          ((c) >> 3)
#define CHE_P(c)          ((c) & 7)
#define RTL(myturn, x)    ((myturn)==BLK?BRD_ROW-1-((x)-3)/2:((x)-3)/2)
#define CTL(myturn, x)    ((myturn)==BLK?BRD_COL-1-(x):(x))
#define dim(x)          (sizeof(x) / sizeof(x[0]))
#define LTR(myturn, x)    ((((myturn)==BLK?BRD_ROW-1-(x):(x)) * 2) + 3)
#define CHE(a, b)         ((a) | ((b) << 3))

#define BLACK_COLOR       ANSI_COLOR(1;36)
#define RED_COLOR         ANSI_COLOR(1;31)
#define BLACK_REVERSE     ANSI_COLOR(1;37;46)
#define RED_REVERSE       ANSI_COLOR(1;37;41)
#define TURN_COLOR        ANSI_COLOR(1;33)

typedef struct chcusr_t{
    char    userid[IDLEN + 1];
    int	    win;
    int     lose;
    int     tie;
    unsigned short rating;
    unsigned short orig_rating; // 原始 rating, 因為遊戲開始先算輸一場, rating 值就跑掉了
} chcusr_t;

#define CHC_ACT_BOARD	0x1	/* set if transfered board to this sock */

typedef struct chc_act_list{
    int     sock;
    struct chc_act_list *next;
} chc_act_list;

#define BRD_ROW           10
#define BRD_COL           9

typedef int board_t[BRD_ROW][BRD_COL];
typedef int (*board_p)[BRD_COL];

