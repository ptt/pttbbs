/* 象棋 */
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
} chcusr_t;

#define CHC_ACT_BOARD	0x1	/* set if transfered board to this sock */

typedef struct chc_act_list{
    int     sock;
    struct chc_act_list *next;
} chc_act_list;

