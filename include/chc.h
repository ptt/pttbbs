/* 象棋
 * 雙人對戰時，雙方都會有一個 chc_act_list 的 linked-list，紀錄著每下一步
 * 棋，必須將這個訊息丟給那些人（socket）。
 * 一開始當然就是對手一個，每當一個觀棋者加入（觀棋可以從紅方或黑方的觀點
 * 進行），其中一方的下棋者的 act_list 就會多一筆記錄，之後就會將下的或收
 * 到對方下的每一步棋傳給 act_list 中所有需要的人，達到觀棋的效果。
 */

#define SIDE_ROW           7
#define TURN_ROW           8
#define STEP_ROW           9
#define TIME_ROW          10
#define WARN_ROW          12
#define MYWIN_ROW         17
#define HISWIN_ROW        18

#define CHC_VERSUS		1	/* 雙人 */
#define CHC_WATCH		2	/* 觀棋 */
#define CHC_PERSONAL		4	/* 打譜 */
#define CHC_WATCH_PERSONAL	8	/* 觀人打譜 */

#define CHE_O(c)          ((c) >> 3)
#define CHE_P(c)          ((c) & 7)
#define RTL(x)            (((x) - 3) >> 1)
#define dim(x)          (sizeof(x) / sizeof(x[0]))
#define LTR(x)            ((x) * 2 + 3)
#define CHE(a, b)         ((a) | ((b) << 3))

#define BLACK_COLOR       "\033[1;36m"
#define RED_COLOR         "\033[1;31m"
#define BLACK_REVERSE     "\033[1;37;46m"
#define RED_REVERSE       "\033[1;37;41m"
#define TURN_COLOR        "\033[1;33m"

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

