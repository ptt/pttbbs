/* $Id$ */

#ifndef INCLUDE_CHESS_H
#define INCLUDE_CHESS_H

#define CHESS_DRAWING_TIME_ROW   128
#define CHESS_DRAWING_TURN_ROW   129
#define CHESS_DRAWING_WARN_ROW   130
#define CHESS_DRAWING_STEP_ROW   131
#define CHESS_DRAWING_HELP_ROW   132

#define CHESS_PHOTO_LINE      15
#define CHESS_PHOTO_COLUMN    (256 + 25)

struct ChessBroadcastList;
struct ChessActions;
struct ChessConstants;

#define const
#define static
#define private

typedef struct {
    char    userid[IDLEN + 1];
    int	    win;
    int     lose;
    int     tie;
    unsigned short rating;
    unsigned short orig_rating; // 原始 rating, 因為遊戲開始先算輸一場, rating 值就跑掉了
} ChessUser;

private typedef struct {
    int   used;
    int   size;
    void *body;
} ChessHistory;

/*   棋類觀戰
 *
 * 雙人對戰時，雙方都會有一個 broadcast_list 的 linked-list，紀錄著每下一
 * 步棋，必須將這個訊息丟給那些人（sock）。
 * 每當一個觀棋者加入（觀棋可以從紅方或黑方的觀點進行），其中一方的下棋者
 * 的 broadcast_list 就會多一筆記錄，之後就會將下的或收到對方下的每一步棋
 * 傳給 broadcast_list 中所有需要的人，達到觀棋的效果。
 */
private typedef struct ChessBroadcastListNode {
    int    sock;
    struct ChessBroadcastListNode *next;
} ChessBroadcastListNode;

private typedef struct ChessBroadcastList {
    struct ChessBroadcastListNode head; /* dummy node */
} ChessBroadcastList;


typedef struct {
    int     limit_hand;
    int     limit_time;
    int     free_time;
    enum {
	CHESS_TIMEMODE_MULTIHAND, /* 限時限步 */
	CHESS_TIMEMODE_COUNTING   /* 讀秒 */
    } time_mode;
} ChessTimeLimit;

typedef enum {
    CHESS_MODE_VERSUS,   /* 對奕 */
    CHESS_MODE_WATCH,    /* 觀棋 */
    CHESS_MODE_PERSONAL, /* 打譜 */
    CHESS_MODE_REPLAY    /* 看譜 */
} ChessGameMode;

typedef enum {
    CHESS_RESULT_CONTINUE,
    CHESS_RESULT_WIN,
    CHESS_RESULT_LOST,
    CHESS_RESULT_TIE,
    CHESS_RESULT_END   /* watching or replaying */
} ChessGameResult;

typedef struct ChessInfo {
    private const static
	struct ChessActions* actions; /* vtable */
    private const static
	struct ChessConstants* constants;

    rc_t cursor;

    /* 計時用, [0] = mine, [1] = his */
    int	    lefttime[2];
    int     lefthand[2]; /* 限時限步時用, = 0 表為自由時間或非限時限步模式 */
    const ChessTimeLimit* timelimit;

    const ChessGameMode mode;
    const ChessUser user1;
    const ChessUser user2;
    const char myturn;   /* 我方顏色 */

    char       turn;
    char       ipass, hepass;
    char       warnmsg[64];
    char       last_movestr[36];
    char      *photo;

    void      *board;
    void      *tag;

    private int    sock;
    private ChessHistory history;
    private ChessBroadcastList broadcast_list;
    private ChessGameResult (*play_func[2])(struct ChessInfo* info);

    private int  current_step;  /* used by watch and replay */
    private char step_tmp[0];
} ChessInfo;

#undef const
#undef static
#undef private

typedef struct ChessActions {
    /* initial */
    void (*init_user)   (const userinfo_t* uinfo, ChessUser* user);
    void (*init_user_rec)(const userec_t* uinfo, ChessUser* user);
    void (*init_board)  (void* board);

    /* playing */
    void (*drawline)    (const ChessInfo* info, int line);
    void (*movecur)     (int r, int c);
    void (*prepare_play)(ChessInfo* info);
    int  (*select)      (ChessInfo* info, rc_t location,
	    ChessGameResult* result);
    void (*prepare_step)(ChessInfo* info, const void* step);
    ChessGameResult (*apply_step)  (void* board, const void* step);
    void (*drawstep)    (ChessInfo* info, const void* step);

    /* ending */
    void (*gameend)    (ChessInfo* info, ChessGameResult result);
    void (*genlog)     (ChessInfo* info, FILE* fp, ChessGameResult result);
} ChessActions;

typedef struct ChessConstants {
    int   step_entry_size;
    int   traditional_timeout;
    int   board_height;
    int   board_width;
    const char *chess_name;
    const char *photo_file_name;
    const char *log_board;
    const char *turn_color[2];
    const char *turn_str[2];
} ChessConstants;

typedef enum {
    CHESS_STEP_NORMAL, CHESS_STEP_PASS,
    CHESS_STEP_DROP, CHESS_STEP_FAILURE,
    CHESS_STEP_NOP, /* for wake up */
    CHESS_STEP_UNDO, /* undo request */
    CHESS_STEP_UNDO_ACC,  /* accept */
    CHESS_STEP_UNDO_REJ,  /* reject */
} ChessStepType;


int ChessTimeCountDown(ChessInfo* info, int who, int length);
void ChessStepMade(ChessInfo* info, int who);

ChessStepType ChessStepReceive(ChessInfo* info, void* step);
int ChessStepSendOpposite(ChessInfo* info, const void* step);
void ChessStepBroadcast(ChessInfo* info, const void* step);
int ChessStepSend(ChessInfo* info, const void* step);

void ChessHistoryAppend(ChessInfo* info, void* step);
const void* ChessHistoryRetrieve(ChessInfo* info, int n);

void ChessPlay(ChessInfo* info);
int ChessStartGame(char func_char, int sig, const char* title);
int ChessWatchGame(void (*play)(int, ChessGameMode),
	int game, const char* title);
int ChessReplayGame(const char* fname);

ChessInfo* NewChessInfo(const ChessActions* actions,
	const ChessConstants* constants, int sock, ChessGameMode mode);
void DeleteChessInfo(ChessInfo* info);

void ChessEstablishRequest(int sock);
void ChessAcceptingRequest(int sock);
void ChessShowRequest(void);

void ChessDrawLine(const ChessInfo* info, int line);
void ChessDrawExtraInfo(const ChessInfo* info, int line);

#endif /* INCLUDE_CHESS_H */
