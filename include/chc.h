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

#define CHE_O(c)          ((c) >> 3)
#define CHE_P(c)          ((c) & 7)
#define dim(x)          (sizeof(x) / sizeof(x[0]))
#define CHE(a, b)         ((a) | ((b) << 3))
/* TODO let user flip chessboard */
#define REDDOWN(info)     ((info)->myturn==RED)
#define RTL(info, x)      (REDDOWN(info)?((x)-3)/2:BRD_ROW-1-((x)-3)/2)
#define CTL(info, x)      (REDDOWN(info)?(x):BRD_COL-1-(x))
#define LTR(info, x)      (((REDDOWN(info)?(x):BRD_ROW-1-(x)) * 2) + 3)

#define BLACK_COLOR       ANSI_COLOR(1;36)
#define RED_COLOR         ANSI_COLOR(1;31)
#define BLACK_REVERSE     ANSI_COLOR(1;37;46)
#define RED_REVERSE       ANSI_COLOR(1;37;41)
#define TURN_COLOR        ANSI_COLOR(1;33)

#define BRD_ROW           10
#define BRD_COL           9

typedef int board_t[BRD_ROW][BRD_COL];
typedef int (*board_p)[BRD_COL];

