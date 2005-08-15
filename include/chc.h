
#define CHE_O(c)          ((c) >> 3)
#define CHE_P(c)          ((c) & 7)
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

#define BRD_ROW           10
#define BRD_COL           9

typedef int board_t[BRD_ROW][BRD_COL];
typedef int (*board_p)[BRD_COL];

