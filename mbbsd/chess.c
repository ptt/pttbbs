/* $Id$ */
#include "bbs.h"
#include "chess.h"
#include <setjmp.h>

#define assert_not_reached() assert(!"Should never be here!!!")
#define dim(x)               (sizeof(x) / sizeof(x[0]))

#define CHESS_HISTORY_INITIAL_BUFFER_SIZE 300
#define CHESS_HISTORY_BUFFER_INCREMENT     50

#define CHESS_DRAWING_SIDE_ROW           7
#define CHESS_DRAWING_REAL_TURN_ROW      8
#define CHESS_DRAWING_REAL_STEP_ROW      9
#define CHESS_DRAWING_REAL_TIME_ROW1    10
#define CHESS_DRAWING_REAL_TIME_ROW2    11
#define CHESS_DRAWING_REAL_WARN_ROW     13
#define CHESS_DRAWING_MYWIN_ROW         17
#define CHESS_DRAWING_HISWIN_ROW        18
#define CHESS_DRAWING_PHOTOED_STEP_ROW  18
#define CHESS_DRAWING_PHOTOED_TURN_ROW  19
#define CHESS_DRAWING_PHOTOED_TIME_ROW1 20
#define CHESS_DRAWING_PHOTOED_TIME_ROW2 21
#define CHESS_DRAWING_PHOTOED_WARN_ROW  22

#define CONNECT_PEER() add_io(info->sock, 0)
#define IGNORE_PEER()  add_io(0, 0)

#define DO_WITHOUT_PEER(TIMEOUT,ACT,ELSE) \
    do {                                  \
	void (*orig_alarm_handler)(int) = \
	    Signal(SIGALRM, &SigjmpEnv);  \
	IGNORE_PEER();                    \
	if(sigsetjmp(sigjmpEnv, 1))       \
	    ELSE;                         \
	else {                            \
	    alarm(TIMEOUT);               \
	    ACT;                          \
	}                                 \
	CONNECT_PEER();                   \
	Signal(SIGALRM, orig_alarm_handler); \
    } while(0)

static const char * const ChessHintStr[] = {
    "  q      認輸離開",
    "  p      要求和棋",
    "方向鍵   移動遊標",
    "Enter    選擇/移動"
};

static const struct {
    const char*  name;
    int          name_len;
    ChessInfo* (*func)(FILE* fp);
} ChessReplayMap[] = {
    { "gomoku", 6, &gomoku_replay },
    { "chc",    3, &chc_replay },
    { "go",     2, &gochess_replay },
    { "reversi",7, &reversi_replay },
    { NULL }
};

static ChessInfo * CurrentPlayingGameInfo;
static sigjmp_buf sigjmpEnv;

/* XXX: This is a BAD way to pass information.
 *      Fix this by handling chess request ourselves.
 */
static ChessTimeLimit * _current_time_limit;

static void SigjmpEnv(int sig) { siglongjmp(sigjmpEnv, 1); }

#define CHESS_HISTORY_ENTRY(INFO,N) \
    ((INFO)->history.body + (N) * (INFO)->constants->step_entry_size)
static void
ChessHistoryInit(ChessHistory* history, int entry_size)
{
    history->size = CHESS_HISTORY_INITIAL_BUFFER_SIZE;
    history->used = 0;
    history->body =
	calloc(CHESS_HISTORY_INITIAL_BUFFER_SIZE,
		entry_size);
}

const void*
ChessHistoryRetrieve(ChessInfo* info, int n)
{
    assert(n >= 0 && n < info->history.used);
    return CHESS_HISTORY_ENTRY(info, n);
}

void
ChessHistoryAppend(ChessInfo* info, void* step)
{
    if (info->history.used == info->history.size)
	info->history.body = realloc(info->history.body,
		(info->history.size += CHESS_HISTORY_BUFFER_INCREMENT)
		* info->constants->step_entry_size);

    memmove(CHESS_HISTORY_ENTRY(info, info->history.used),
	    step, info->constants->step_entry_size);
    info->history.used++;
}

static void
ChessBroadcastListInit(ChessBroadcastList* list)
{
    list->head.next = NULL;
}

static void
ChessBroadcastListClear(ChessBroadcastList* list)
{
    ChessBroadcastListNode* p = list->head.next;
    while (p) {
	ChessBroadcastListNode* t = p->next;
	close(p->sock);
	free(p);
	p = t;
    }
}

static ChessBroadcastListNode*
ChessBroadcastListInsert(ChessBroadcastList* list)
{
    ChessBroadcastListNode* p =
	(ChessBroadcastListNode*) malloc(sizeof(ChessBroadcastListNode));

    p->next = list->head.next;
    list->head.next = p;
    return p;
}

static void
ChessDrawHelpLine(const ChessInfo* info)
{
    const static char* const HelpStr[] =
    {
	/* CHESS_MODE_VERSUS, 對奕 */
	ANSI_COLOR(1;33;42) " 下棋 "
	ANSI_COLOR(;31;47) " (←↑↓→)" ANSI_COLOR(30) " 移動 "
	ANSI_COLOR(31) "(空白鍵/ENTER)" ANSI_COLOR(30) " 下子 "
	ANSI_COLOR(31) "(q)" ANSI_COLOR(30) "認輸 "
	ANSI_COLOR(31) "(p)" ANSI_COLOR(30) "虛手/和棋 "
	ANSI_COLOR(31) "(u)" ANSI_COLOR(30) "悔棋 "
	ANSI_RESET,

	/* CHESS_MODE_WATCH, 觀棋 */
	ANSI_COLOR(1;33;42) " 觀棋 "
	ANSI_COLOR(;31;47) " (←→)" ANSI_COLOR(30) " 前後一步   "
	ANSI_COLOR(31) "(↑↓)" ANSI_COLOR(30) " 前後十步  "
	ANSI_COLOR(31) "(PGUP/PGDN)" ANSI_COLOR(30) " 最初/目前盤面  "
	ANSI_COLOR(31) "(q)" ANSI_COLOR(30) "離開 "
	ANSI_RESET,

	/* CHESS_MODE_PERSONAL, 打譜 */
	ANSI_COLOR(1;33;42) " 打譜 "
	ANSI_COLOR(;31;47) " (←↑↓→)" ANSI_COLOR(30) " 移動 "
	ANSI_COLOR(31) "(空白鍵/ENTER)" ANSI_COLOR(30) " 下子 "
	ANSI_COLOR(31) "(q)" ANSI_COLOR(30) "離開 "
	ANSI_COLOR(31) "(u)" ANSI_COLOR(30) "悔棋 "
	ANSI_RESET,

	/* CHESS_MODE_REPLAY, 看譜 */
	ANSI_COLOR(1;33;42) " 看譜 "
	ANSI_COLOR(;31;47) " (←→)" ANSI_COLOR(30) " 前後一步  "
	ANSI_COLOR(31) "(↑↓)" ANSI_COLOR(30) " 前後十步  "
	ANSI_COLOR(31) "(PGUP/PGDN)" ANSI_COLOR(30) " 最初/目前盤面  "
	ANSI_COLOR(31) "(q)" ANSI_COLOR(30) "離開 "
	ANSI_RESET,
    };

    mouts(b_lines, 0, HelpStr[info->mode]);
    info->actions->drawline(info, b_lines);
}

void
ChessDrawLine(const ChessInfo* info, int line)
{
#define DRAWLINE(LINE)                         \
    do {                                       \
	move((LINE), 0);                       \
	clrtoeol();                            \
	info->actions->drawline(info, (LINE)); \
    } while (0)

    if (line == b_lines) {
	ChessDrawHelpLine(info);
	return;
    } else if (line == CHESS_DRAWING_TURN_ROW)
	line = info->photo ?
	    CHESS_DRAWING_PHOTOED_TURN_ROW :
	    CHESS_DRAWING_REAL_TURN_ROW;
    else if (line == CHESS_DRAWING_TIME_ROW) {
	if(info->photo) {
	    DRAWLINE(CHESS_DRAWING_PHOTOED_TIME_ROW1);
	    DRAWLINE(CHESS_DRAWING_PHOTOED_TIME_ROW2);
	} else {
	    DRAWLINE(CHESS_DRAWING_REAL_TIME_ROW1);
	    DRAWLINE(CHESS_DRAWING_REAL_TIME_ROW2);
	}
	return;
    } else if (line == CHESS_DRAWING_WARN_ROW)
	line = info->photo ?
	    CHESS_DRAWING_PHOTOED_WARN_ROW :
	    CHESS_DRAWING_REAL_WARN_ROW;
    else if (line == CHESS_DRAWING_STEP_ROW)
	line = info->photo ?
	    CHESS_DRAWING_PHOTOED_STEP_ROW :
	    CHESS_DRAWING_REAL_STEP_ROW;

    DRAWLINE(line);

#undef DRAWLINE
}

void
ChessRedraw(const ChessInfo* info)
{
    int i;
    clear();
    for (i = 0; i <= b_lines; ++i)
	ChessDrawLine(info, i);
}

inline static int
ChessTimeCountDownCalc(ChessInfo* info, int who, int length)
{
    info->lefttime[who] -= length;

    if (!info->timelimit) /* traditional mode, only left time is considered */
	return info->lefttime[who] < 0;

    if (info->lefttime[who] < 0) { /* only allowed when in free time */
	if (info->lefthand[who])
	    return 1;
	info->lefttime[who] += info->timelimit->limit_time;
	info->lefthand[who]  = info->timelimit->limit_hand;

	return (info->lefttime[who] < 0);
    }

    return 0;
}

int
ChessTimeCountDown(ChessInfo* info, int who, int length)
{
    int result = ChessTimeCountDownCalc(info, who, length);
    ChessDrawLine(info, CHESS_DRAWING_TIME_ROW);
    return result;
}

void
ChessStepMade(ChessInfo* info, int who)
{
    if (!info->timelimit)
	info->lefttime[who] = info->constants->traditional_timeout;
    else if (
	    (info->lefthand[who] && (--(info->lefthand[who]) == 0) &&
	     info->timelimit->time_mode == CHESS_TIMEMODE_COUNTING)
	    ||
	    (info->lefthand[who] == 0 && info->lefttime[who] <= 0)
	    ) {
	info->lefthand[who] = info->timelimit->limit_hand;
	info->lefttime[who] = info->timelimit->limit_time;
    }
}

/*
 * Start of the network communication function.
 */
inline static ChessStepType
ChessRecvMove(ChessInfo* info, int sock, void *step)
{
    if (read(sock, step, info->constants->step_entry_size)
	    != info->constants->step_entry_size)
	return CHESS_STEP_FAILURE;
    return *(ChessStepType*) step;
}

inline static int
ChessSendMove(ChessInfo* info, int sock, const void *step)
{
    if (write(sock, step, info->constants->step_entry_size)
	    != info->constants->step_entry_size)
	return 0;
    return 1;
}

inline static int
ChessStepSendOpposite(ChessInfo* info, const void* step)
{
    void (*orig_handler)(int);
    int    result = 1;

    /* fd 0 is the socket to user, it means no oppisite available.
     * (Might be personal play) */
    if (info->sock == 0)
	return 1;

    orig_handler = Signal(SIGPIPE, SIG_IGN);

    if (!ChessSendMove(info, info->sock, step))
	result = 0;

    Signal(SIGPIPE, orig_handler);
    return result;
}

inline static void
ChessStepBroadcast(ChessInfo* info, const void *step)
{
    ChessBroadcastListNode *p = &(info->broadcast_list.head);
    void (*orig_handler)(int);
    
    orig_handler = Signal(SIGPIPE, SIG_IGN);

    while(p->next){
	if (!ChessSendMove(info, p->next->sock, step)) {
	    /* remove viewer */
	    ChessBroadcastListNode *tmp = p->next->next;
	    free(p->next);
	    p->next = tmp;
	} else
	    p = p->next;
    }

    Signal(SIGPIPE, orig_handler);
}

int
ChessStepSend(ChessInfo* info, const void* step)
{
    /* send to opposite... */
    if (!ChessStepSendOpposite(info, step))
	return 0;

    /* and watchers */
    ChessStepBroadcast(info, step);

    return 1;
}

int
ChessMessageSend(ChessInfo* info, ChessStepType type)
{
    return ChessStepSend(info, &type);
}

static inline int
ChessCheckAlive(ChessInfo* info)
{
    ChessStepType type = CHESS_STEP_NOP;
    return ChessStepSendOpposite(info, &type);
}

ChessStepType
ChessStepReceive(ChessInfo* info, void* step)
{
    ChessStepType result = ChessRecvMove(info, info->sock, step);

    /* automatical routing */
    if (result != CHESS_STEP_FAILURE)
	ChessStepBroadcast(info, step);

    /* and logging */
    if (result == CHESS_STEP_NORMAL || result == CHESS_STEP_PASS)
	ChessHistoryAppend(info, step);

    return result;
}

inline static void
ChessReplayUntil(ChessInfo* info, int n)
{
    const void* step;

    if (n <= info->current_step)
	return;

    while (info->current_step < n - 1) {
	info->actions->apply_step(info->board,
		ChessHistoryRetrieve(info, info->current_step));
	info->current_step++;
    }

    /* spcial for last one to maintian information correct */
    step = ChessHistoryRetrieve(info, info->current_step);
    info->turn = info->current_step++ & 1;
    info->actions->prepare_step(info, step);
    info->actions->apply_step(info->board, step);
}

static int
ChessAnswerRequest(ChessInfo* info, const char* req_name)
{
    char buf[4];
    char msg[64];

    snprintf(info->warnmsg, sizeof(info->warnmsg),
	    ANSI_COLOR(1;31) "要求%s!" ANSI_RESET, req_name);
    ChessDrawLine(info, CHESS_DRAWING_WARN_ROW);
    bell();

    snprintf(msg, sizeof(msg),
	    "對方要求%s，是否接受?(y/N)", req_name);
    DO_WITHOUT_PEER(30,
    getdata(b_lines, 0, msg, buf, sizeof(buf), DOECHO),
    buf[0] = 'n');
    ChessDrawHelpLine(info);

    info->warnmsg[0] = 0;
    ChessDrawLine(info, CHESS_DRAWING_WARN_ROW);

    if (buf[0] == 'y' || buf[0] == 'Y')
	return 1;
    else
	return 0;
}

ChessGameResult
ChessPlayFuncMy(ChessInfo* info)
{
    int last_time = now;
    int endturn = 0;
    ChessGameResult game_result = CHESS_RESULT_CONTINUE;
    int ch;
#ifdef DBCSAWARE
    int move_count = 0;
#endif

    info->pass[(int) info->turn] = 0;
    bell();

    while (!endturn) {
	ChessStepType result;

	ChessDrawLine(info, CHESS_DRAWING_TIME_ROW);
	info->actions->movecur(info->cursor.r, info->cursor.c);
	oflush();

	ch = igetch();
	if (ChessTimeCountDown(info, 0, now - last_time)) {
	    /* ran out of time */
	    game_result = CHESS_RESULT_LOST;
	    endturn = 1;
	    break;
	}
	last_time = now;

	switch (ch) {
	    case I_OTHERDATA:
		result = ChessStepReceive(info, &info->step_tmp);

		if (result == CHESS_STEP_FAILURE ||
			result == CHESS_STEP_DROP) {
		    game_result = CHESS_RESULT_WIN;
		    endturn = 1;
		} else if (result == CHESS_STEP_TIE_ACC) {
		    game_result = CHESS_RESULT_TIE;
		    endturn = 1;
		} else if (result == CHESS_STEP_TIE_REJ) {
		    strcpy(info->warnmsg, ANSI_COLOR(1;31) "求和被拒!" ANSI_RESET);
		    ChessDrawLine(info, CHESS_DRAWING_WARN_ROW);
		} else if (result == CHESS_STEP_UNDO) {
		    if (ChessAnswerRequest(info, "悔棋")) {
			ChessMessageSend(info, CHESS_STEP_UNDO_ACC);

			info->actions->init_board(info->board);
			info->current_step = 0;
			ChessReplayUntil(info, info->history.used - 1);
			info->history.used--;

			ChessRedraw(info);

			endturn = 1;
		    } else
			ChessMessageSend(info, CHESS_STEP_UNDO_REJ);
		} else if (result == CHESS_STEP_NORMAL ||
			result == CHESS_STEP_SPECIAL) {
		    info->actions->prepare_step(info, &info->step_tmp);
		    game_result =
			info->actions->apply_step(info->board,
				&info->step_tmp);
		    info->actions->drawstep(info, &info->step_tmp);
		    endturn = 1;
		    ChessStepMade(info, 0);
		}
		break;

	    case KEY_UP:
		info->cursor.r--;
		if (info->cursor.r < 0)
		    info->cursor.r = info->constants->board_height - 1;
		break;

	    case KEY_DOWN:
		info->cursor.r++;
		if (info->cursor.r >= info->constants->board_height)
		    info->cursor.r = 0;
		break;

	    case KEY_LEFT:
#ifdef DBCSAWARE
		if (!ISDBCSAWARE()) {
		    if (++move_count >= 2)
			move_count = 0;
		    else
			break;
		}
#endif /* defined(DBCSAWARE) */

		info->cursor.c--;
		if (info->cursor.c < 0)
		    info->cursor.c = info->constants->board_width - 1;
		break;

	    case KEY_RIGHT:
#ifdef DBCSAWARE
		if (!ISDBCSAWARE()) {
		    if (++move_count >= 2)
			move_count = 0;
		    else
			break;
		}
#endif /* defined(DBCSAWARE) */

		info->cursor.c++;
		if (info->cursor.c >= info->constants->board_width)
		    info->cursor.c = 0;
		break;

	    case 'q':
		{
		    char buf[4];

		    DO_WITHOUT_PEER(30,
		    getdata(b_lines, 0,
			    info->mode == CHESS_MODE_PERSONAL ?
			    "是否真的要離開?(y/N)" :
			    "是否真的要認輸?(y/N)",
			    buf, sizeof(buf), DOECHO),
		    buf[0] = 'n');
		    ChessDrawHelpLine(info);

		    if (buf[0] == 'y' || buf[0] == 'Y') {
			game_result = CHESS_RESULT_LOST;
			endturn = 1;
		    }
		}
		break;

	    case 'p':
		if (info->constants->pass_is_step) {
		    ChessStepType type = CHESS_STEP_PASS;
		    ChessHistoryAppend(info, &type); 
		    strcpy(info->last_movestr, "虛手");

		    info->pass[(int) info->turn] = 1;
		    ChessMessageSend(info, CHESS_STEP_PASS);
		    endturn = 1;
		} else if (info->mode != CHESS_MODE_PERSONAL) {
		    char buf[4];

		    DO_WITHOUT_PEER(30,
		    getdata(b_lines, 0, "是否真的要和棋?(y/N)",
			    buf, sizeof(buf), DOECHO),
		    buf[0] = 'n');
		    ChessDrawHelpLine(info);

		    if (buf[0] == 'y' || buf[1] == 'Y') {
			ChessMessageSend(info, CHESS_STEP_TIE);
			strlcpy(info->warnmsg,
				ANSI_COLOR(1;33) "要求和棋!" ANSI_RESET,
				sizeof(info->warnmsg));
			ChessDrawLine(info, CHESS_DRAWING_WARN_ROW);
			bell();
		    }
		}
		break;

	    case 'u':
		if (info->mode == CHESS_MODE_PERSONAL && info->history.used > 0) {
		    ChessMessageSend(info, CHESS_STEP_UNDO_ACC);

		    info->actions->init_board(info->board);
		    info->current_step = 0;
		    ChessReplayUntil(info, info->history.used - 1);
		    info->history.used--;

		    ChessRedraw(info);

		    endturn = 1;
		}
		break;

	    case '\r':
	    case '\n':
	    case ' ':
		endturn = info->actions->select(info, info->cursor, &game_result);
		break;

	    case I_TIMEOUT:
		break;

	    case KEY_UNKNOWN:
		break;

	    default:
		if (info->actions->process_key) {
		    DO_WITHOUT_PEER(30,
		    endturn =
			info->actions->process_key(info, ch, &game_result),
		    );
		}
	}
    }
    ChessTimeCountDown(info, 0, now - last_time);
    ChessStepMade(info, 0);
    ChessDrawLine(info, CHESS_DRAWING_TIME_ROW);
    ChessDrawLine(info, CHESS_DRAWING_STEP_ROW);
    return game_result;
}

static ChessGameResult
ChessPlayFuncHis(ChessInfo* info)
{
    int last_time = now;
    int endturn = 0;
    ChessGameResult game_result = CHESS_RESULT_CONTINUE;

    while (!endturn) {
	ChessStepType result;
	int ch;

	if (ChessTimeCountDown(info, 1, now - last_time)) {
	    info->lefttime[1] = 0;

	    /* to make him break out igetch() */
	    ChessMessageSend(info, CHESS_STEP_NOP);
	}
	last_time = now;

	ChessDrawLine(info, CHESS_DRAWING_TIME_ROW);
	move(1, 0);
	oflush();

	switch (ch = igetch()) {
	    case 'q':
		{
		    char buf[4];
		    DO_WITHOUT_PEER(30,
		    getdata(b_lines, 0, "是否真的要認輸?(y/N)",
			    buf, sizeof(buf), DOECHO),
		    buf[0] = 'n');
		    ChessDrawHelpLine(info);

		    if (buf[0] == 'y' || buf[0] == 'Y') {
			game_result = CHESS_RESULT_LOST;
			endturn = 1;
		    }
		}
		break;

	    case 'u':
		if (info->history.used > 0) {
		    strcpy(info->warnmsg, ANSI_COLOR(1;31) "要求悔棋!" ANSI_RESET);
		    ChessDrawLine(info, CHESS_DRAWING_WARN_ROW);

		    ChessMessageSend(info, CHESS_STEP_UNDO);
		}
		break;

	    case I_OTHERDATA:
		result = ChessStepReceive(info, &info->step_tmp);

		if (result == CHESS_STEP_FAILURE ||
			result == CHESS_STEP_DROP) {
		    game_result = CHESS_RESULT_WIN;
		    endturn = 1;
		} else if (result == CHESS_STEP_PASS) {
		    strcpy(info->last_movestr, "虛手");

		    info->pass[(int) info->turn] = 1;
		    endturn = 1;
		} else if (result == CHESS_STEP_TIE) {
		    if (ChessAnswerRequest(info, "和棋")) {
			ChessMessageSend(info, CHESS_STEP_TIE_ACC);

			game_result = CHESS_RESULT_TIE;
			endturn = 1;
		    } else
			ChessMessageSend(info, CHESS_STEP_TIE_REJ);
		} else if (result == CHESS_STEP_NORMAL ||
			result == CHESS_STEP_SPECIAL) {
		    info->actions->prepare_step(info, &info->step_tmp);
		    switch (info->actions->apply_step(info->board, &info->step_tmp)) {
			case CHESS_RESULT_LOST:
			    game_result = CHESS_RESULT_WIN;
			    break;

			case CHESS_RESULT_WIN:
			    game_result = CHESS_RESULT_LOST;
			    break;

			default:
			    game_result = CHESS_RESULT_CONTINUE;
		    }
		    endturn = 1;
		    info->pass[(int) info->turn] = 0;
		    ChessStepMade(info, 1);
		    info->actions->drawstep(info, &info->step_tmp);
		} else if (result == CHESS_STEP_UNDO_ACC) {
		    strcpy(info->warnmsg, ANSI_COLOR(1;31) "接受悔棋!" ANSI_RESET);

		    info->actions->init_board(info->board);
		    info->current_step = 0;
		    ChessReplayUntil(info, info->history.used - 1);
		    info->history.used--;

		    ChessRedraw(info);
		    bell();

		    endturn = 1;
		} else if (result == CHESS_STEP_UNDO_REJ) {
		    strcpy(info->warnmsg, ANSI_COLOR(1;31) "悔棋被拒!" ANSI_RESET);
		    ChessDrawLine(info, CHESS_DRAWING_WARN_ROW);
		}

	    case I_TIMEOUT:
		break;

	    case KEY_UNKNOWN:
		break;

	    default:
		if (info->actions->process_key) {
		    DO_WITHOUT_PEER(30,
		    endturn =
			info->actions->process_key(info, ch, &game_result),
		    );
		}
	}
    }
    ChessTimeCountDown(info, 1, now - last_time);
    ChessDrawLine(info, CHESS_DRAWING_TIME_ROW);
    ChessDrawLine(info, CHESS_DRAWING_STEP_ROW);
    return game_result;
}

static ChessGameResult
ChessPlayFuncWatch(ChessInfo* info)
{
    int end_watch = 0;

    while (!end_watch) {
	ChessStepType result;

	info->actions->prepare_play(info);
	if (info->sock == -1)
	    strlcpy(info->warnmsg, ANSI_COLOR(1;33) "棋局已結束" ANSI_RESET,
		    sizeof(info->warnmsg));

	ChessDrawLine(info, CHESS_DRAWING_WARN_ROW);
	ChessDrawLine(info, CHESS_DRAWING_STEP_ROW);
	move(1, 0);

	switch (igetch()) {
	    case I_OTHERDATA: /* new step */
		result = ChessStepReceive(info, &info->step_tmp);

		if (result == CHESS_STEP_FAILURE) {
		    IGNORE_PEER();
		    info->sock = -1;
		    break;
		} else if (result == CHESS_STEP_UNDO_ACC) {
		    if (info->current_step == info->history.used) {
			/* at head but redo-ed */
			info->actions->init_board(info->board);
			info->current_step = 0;
			info->turn = 1;
			ChessReplayUntil(info, info->history.used - 1);
			ChessRedraw(info);
		    }
		    info->history.used--;
		} else if (result == CHESS_STEP_NORMAL ||
			result == CHESS_STEP_SPECIAL) {
		    if (info->current_step == info->history.used - 1) {
			/* was watching up-to-date board */
			info->turn = info->current_step++ & 1;
			info->actions->prepare_step(info, &info->step_tmp);
			info->actions->apply_step(info->board, &info->step_tmp);
			info->actions->drawstep(info, &info->step_tmp);
		    }
		} else if (result == CHESS_STEP_PASS)
		    strcpy(info->last_movestr, "虛手");

		break;

	    case KEY_LEFT: /* 往前一步 */
		if (info->current_step == 0)
		    bell();
		else {
		    /* TODO: implement without re-apply all steps */
		    int current = info->current_step;

		    info->actions->init_board(info->board);
		    info->current_step = 0;

		    ChessReplayUntil(info, current - 1);
		    ChessRedraw(info);
		}
		break;

	    case KEY_RIGHT: /* 往後一步 */
		if (info->current_step == info->history.used)
		    bell();
		else {
		    const void* step =
			ChessHistoryRetrieve(info, info->current_step);
		    info->turn = info->current_step++ & 1;
		    info->actions->prepare_step(info, step);
		    info->actions->apply_step(info->board, step);
		    info->actions->drawstep(info, step);
		}
		break;

	    case KEY_UP: /* 往前十步 */
		if (info->current_step == 0)
		    bell();
		else {
		    /* TODO: implement without re-apply all steps */
		    int current = info->current_step;

		    info->actions->init_board(info->board);
		    info->current_step = 0;

		    ChessReplayUntil(info, current - 10);

		    ChessRedraw(info);
		}
		break;

	    case KEY_DOWN: /* 往後十步 */
		if (info->current_step == info->history.used)
		    bell();
		else {
		    ChessReplayUntil(info,
			    MIN(info->current_step + 10, info->history.used));
		    ChessRedraw(info);
		}
		break;

	    case KEY_PGUP: /* 起始盤面 */
		if (info->current_step == 0)
		    bell();
		else {
		    info->actions->init_board(info->board);
		    info->current_step = 0;
		    ChessRedraw(info);
		}
		break;

	    case KEY_PGDN: /* 最新盤面 */
		if (info->current_step == info->history.used)
		    bell();
		else {
		    ChessReplayUntil(info, info->history.used);
		    ChessRedraw(info);
		}
		break;

	    case 'q':
		end_watch = 1;
	}
    }

    return CHESS_RESULT_END;
}

static void
ChessWatchRequest(int sig)
{
    int sock = establish_talk_connection(&SHM->uinfo[currutmp->destuip]);
    ChessBroadcastListNode* node;

    if (sock < 0)
	return;
    
    assert(CurrentPlayingGameInfo);
    node = ChessBroadcastListInsert(&CurrentPlayingGameInfo->broadcast_list);
    node->sock = sock;

#define SEND(X) write(sock, &(X), sizeof(X))
    SEND(CurrentPlayingGameInfo->myturn);
    SEND(CurrentPlayingGameInfo->turn);

    if (!CurrentPlayingGameInfo->timelimit)
	write(sock, "T", 1);
    else {
	write(sock, "L", 1);
	SEND(*(CurrentPlayingGameInfo->timelimit));
    }

    SEND(CurrentPlayingGameInfo->history.used);
    write(sock, CurrentPlayingGameInfo->history.body,
	    CurrentPlayingGameInfo->constants->step_entry_size
	    * CurrentPlayingGameInfo->history.used);
#undef SEND
}

static void
ChessReceiveWatchInfo(ChessInfo* info)
{
    char time_mode;
#define RECV(X) read(info->sock, &(X), sizeof(X))
    RECV(info->myturn);
    RECV(info->turn);
    
    RECV(time_mode);
    if (time_mode == 'L') {
	info->timelimit = (ChessTimeLimit*) malloc(sizeof(ChessTimeLimit));
	RECV(*(info->timelimit));
    }

    RECV(info->history.used);
    for (info->history.size = CHESS_HISTORY_INITIAL_BUFFER_SIZE;
	    info->history.size < info->history.used;
	    info->history.size += CHESS_HISTORY_BUFFER_INCREMENT);
    info->history.body =
	calloc(info->history.size, info->constants->step_entry_size);
    read(info->sock, info->history.body,
	    info->history.used * info->constants->step_entry_size);
#undef RECV
}

static void
ChessGenLogGlobal(ChessInfo* info, ChessGameResult result)
{
    fileheader_t log_header;
    FILE        *fp;
    char         fname[PATHLEN];
    int          bid;

    if ((bid = getbnum(info->constants->log_board)) == 0)
	return;

    setbpath(fname, info->constants->log_board);
    stampfile(fname, &log_header);

    fp = fopen(fname, "w");
    if (fp != NULL) {
	strlcpy(log_header.owner, "[棋譜機器人]", sizeof(log_header.owner));
	snprintf(log_header.title, sizeof(log_header.title), "[棋譜] %s VS %s",
		info->user1.userid, info->user2.userid);

	fprintf(fp, "作者: %s 看板: %s\n標題: %s \n", log_header.owner, info->constants->log_board, log_header.title);
	fprintf(fp, "時間: %s\n", ctime4(&now));

	info->actions->genlog(info, fp, result);
	fclose(fp);

	setbdir(fname, info->constants->log_board);
	append_record(fname, &log_header, sizeof(log_header));

	setbtotal(bid);
    }
}

static void
ChessGenLogUser(ChessInfo* info, ChessGameResult result)
{
    fileheader_t log_header;
    FILE        *fp;
    char         fname[PATHLEN];

    sethomepath(fname, cuser.userid);
    stampfile(fname, &log_header);

    fp = fopen(fname, "w");
    if (fp != NULL) {
	info->actions->genlog(info, fp, result);
	fclose(fp);

	snprintf(log_header.owner, sizeof(log_header.owner), "[%s]",
		info->constants->chess_name);
	if(info->myturn == 0)
	    sprintf(log_header.title, "%s V.S. %s",
		    info->user1.userid, info->user2.userid);
	else
	    sprintf(log_header.title, "%s V.S. %s",
		    info->user2.userid, info->user1.userid);
	log_header.filemode = 0;

	sethomedir(fname, cuser.userid);
	append_record_forward(fname, &log_header, sizeof(log_header),
		cuser.userid);
    }
}

static void
ChessGenLog(ChessInfo* info, ChessGameResult result)
{
    if (info->mode == CHESS_MODE_VERSUS && info->myturn == 0 &&
	info->constants->log_board) {
	ChessGenLogGlobal(info, result);
    }

    if (getans("是否將棋譜寄回信箱？[N/y]") == 'y')
	ChessGenLogUser(info, result);
}

void
ChessPlay(ChessInfo* info)
{
    ChessGameResult game_result;
    void          (*old_handler)(int);
    const char*     game_result_str = 0;
    sigset_t        old_sigset;

    if (info == NULL)
	return;

    if (!ChessCheckAlive(info)) {
	if (info->sock)
	    close(info->sock);
	return;
    }

    /* XXX */
    if (!info->timelimit) {
	info->timelimit = _current_time_limit;
	_current_time_limit = NULL;
    }

    CurrentPlayingGameInfo = info;

    {
	char buf[4] = "";
	sigset_t sigset;

	if(info->mode == CHESS_MODE_VERSUS)
	    getdata(b_lines, 0, "是否接受觀棋? (Y/n)", buf, sizeof(buf), DOECHO);
	if(buf[0] == 'n' || buf[0] == 'N')
	    old_handler = Signal(SIGUSR1, SIG_IGN);
	else
	    old_handler = Signal(SIGUSR1, &ChessWatchRequest);

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGUSR1);
	sigprocmask(SIG_UNBLOCK, &sigset, &old_sigset);
    }

    if (info->mode == CHESS_MODE_WATCH) {
	int i;
	for (i = 0; i < info->history.used; ++i)
	    info->actions->apply_step(info->board,
		    ChessHistoryRetrieve(info, i));
	info->current_step = info->history.used;
    }

    /* playing initialization */
    ChessRedraw(info);
    info->turn = 1;
    info->lefttime[0] = info->lefttime[1] = info->timelimit ?
	info->timelimit->free_time : info->constants->traditional_timeout;
    info->lefthand[0] = info->lefthand[1] = 0;

    /* main loop */
    CONNECT_PEER();
    for (game_result = CHESS_RESULT_CONTINUE;
	 game_result == CHESS_RESULT_CONTINUE;
	 info->turn ^= 1) {
	if (info->actions->prepare_play(info))
	    info->pass[(int) info->turn] = 1;
	else {
	    ChessDrawLine(info, CHESS_DRAWING_TURN_ROW);
	    ChessDrawLine(info, CHESS_DRAWING_WARN_ROW);
	    game_result = info->play_func[(int) info->turn](info);
	}

	if (info->pass[0] && info->pass[1])
	    game_result = CHESS_RESULT_END;
    }

    if (game_result == CHESS_RESULT_END &&
	    info->actions->post_game &&
	    (info->mode == CHESS_MODE_VERSUS ||
	     info->mode == CHESS_MODE_PERSONAL))
	game_result = info->actions->post_game(info);

    IGNORE_PEER();

    if (info->sock)
	close(info->sock);

    /* end processing */
    if (info->mode == CHESS_MODE_VERSUS) {
	switch (game_result) {
	    case CHESS_RESULT_WIN:
		game_result_str = "對方認輸了!";
		break;

	    case CHESS_RESULT_LOST:
		game_result_str = "你認輸了!";
		break;

	    case CHESS_RESULT_TIE:
		game_result_str = "和棋";
		break;

	    default:
		assert_not_reached();
	}
    } else if (info->mode == CHESS_MODE_WATCH)
	game_result_str = "結束觀棋";
    else if (info->mode == CHESS_MODE_PERSONAL)
	game_result_str = "結束打譜";
    else if (info->mode == CHESS_MODE_REPLAY)
	game_result_str = "結束看譜";

    if (game_result_str) {
	strlcpy(info->warnmsg, game_result_str, sizeof(info->warnmsg));
	ChessDrawLine(info, CHESS_DRAWING_WARN_ROW);
    }

    info->actions->gameend(info, game_result);

    if (info->mode != CHESS_MODE_REPLAY)
	ChessGenLog(info, game_result);

    // currutmp->sig = -1;
    sigprocmask(SIG_SETMASK, &old_sigset, NULL);
    Signal(SIGUSR1, old_handler);

    CurrentPlayingGameInfo = NULL;
}

static userinfo_t*
ChessSearchUser(int sig, const char* title)
{
    char            uident[16];
    userinfo_t	   *uin;

    stand_title(title);
    CompleteOnlineUser(msg_uid, uident);
    if (uident[0] == '\0')
	return NULL;

    if ((uin = search_ulist_userid(uident)) == NULL)
	return NULL;

    if (sig >= 0)
	uin->sig = sig;
    return uin;
}

int
ChessStartGame(char func_char, int sig, const char* title)
{
    userinfo_t     *uin;
    char buf[4];
    
    if ((uin = ChessSearchUser(sig, title)) == NULL)
	return -1;
    uin->turn = 1;
    currutmp->turn = 0;
    strlcpy(uin->mateid, currutmp->userid, sizeof(uin->mateid));
    strlcpy(currutmp->mateid, uin->userid, sizeof(currutmp->mateid));

    stand_title(title);
    buf[0] = 0;
    getdata(2, 0, "使用傳統模式 (T), 限時限步模式 (L) 或是 讀秒模式 (C)? (T/l/c)",
	    buf, 3, DOECHO);

    if (buf[0] == 'l' || buf[0] == 'L' ||
	buf[0] == 'c' || buf[0] == 'C') {

	_current_time_limit = (ChessTimeLimit*) malloc(sizeof(ChessTimeLimit));
	if (buf[0] == 'l' || buf[0] == 'L')
	    _current_time_limit->time_mode = CHESS_TIMEMODE_MULTIHAND;
	else
	    _current_time_limit->time_mode = CHESS_TIMEMODE_COUNTING;

	do {
	    getdata_str(3, 0, "請設定局時 (自由時間) 以分鐘為單位:",
		    buf, 3, DOECHO, "30");
	    _current_time_limit->free_time = atoi(buf);
	} while (_current_time_limit->free_time < 0 || _current_time_limit->free_time > 90);
	_current_time_limit->free_time *= 60; /* minute -> second */

	if (_current_time_limit->time_mode == CHESS_TIMEMODE_MULTIHAND) {
	    char display_buf[128];

	    do {
		getdata_str(4, 0, "請設定步時, 以分鐘為單位:",
			buf, 3, DOECHO, "5");
		_current_time_limit->limit_time = atoi(buf);
	    } while (_current_time_limit->limit_time < 0 || _current_time_limit->limit_time > 30);
	    _current_time_limit->limit_time *= 60; /* minute -> second */

	    snprintf(display_buf, sizeof(display_buf),
		    "請設定限步 (每 %d 分鐘需走幾步):",
		    _current_time_limit->limit_time / 60);
	    do {
		getdata_str(5, 0, display_buf, buf, 3, DOECHO, "10");
		_current_time_limit->limit_hand = atoi(buf);
	    } while (_current_time_limit->limit_hand < 1);
	} else {
	    _current_time_limit->limit_hand = 1;

	    do {
		getdata_str(4, 0, "請設定讀秒, 以秒為單位",
			buf, 3, DOECHO, "60");
		_current_time_limit->limit_time = atoi(buf);
	    } while (_current_time_limit->limit_time < 0);
	}
    } else
	_current_time_limit = NULL;

    my_talk(uin, friend_stat(currutmp, uin), func_char);
    return 0;
}

int
ChessWatchGame(void (*play)(int, ChessGameMode), int game, const char* title)
{
    int 	    sock, msgsock;
    userinfo_t     *uin;

    if ((uin = ChessSearchUser(-1, title)) == NULL)
	return -1;

    if (uin->uid == currutmp->uid || uin->mode != game) {
	vmsg("無法建立連線");
	return -1;
    }

    if (getans("是否進行觀棋? [N/y]") != 'y')
	return 0;

    if ((sock = make_connection_to_somebody(uin, 10)) < 0) {
	vmsg("無法建立連線");
	return -1;
    }
#if defined(Solaris) && __OS_MAJOR_VERSION__ == 5 && __OS_MINOR_VERSION__ < 7
    msgsock = accept(sock, (struct sockaddr *) 0, 0);
#else
    msgsock = accept(sock, (struct sockaddr *) 0, (socklen_t *) 0);
#endif
    close(sock);
    if (msgsock < 0)
	return -1;

    strlcpy(currutmp->mateid, uin->userid, sizeof(currutmp->mateid));
    play(msgsock, CHESS_MODE_WATCH);
    close(msgsock);
    return 0;
}

int
ChessReplayGame(const char* fname)
{
    ChessInfo *info;
    FILE      *fp = fopen(fname, "r");
    int        found = -1;
    char       buf[256];
    screen_backup_t oldscreen;

    if(fp == NULL) {
	vmsg("檔案無法開啟, 可能被刪除了");
	return -1;
    }

    while (found == -1 && fgets(buf, sizeof(buf), fp)) {
	if (buf[0] == '<') {
	    const int line_len = strlen(buf);
	    if (strcmp(buf + line_len - 5, "log>\n") == 0) {
		int i;
		for (i = 0; ChessReplayMap[i].name; ++i)
		    if (ChessReplayMap[i].name_len == line_len - 6 &&
			    strncmp(buf + 1, ChessReplayMap[i].name,
				ChessReplayMap[i].name_len) == 0) {
			found = i;
			break;
		    }
	    }
	}
    }

    if (found == -1) {
	fclose(fp);
	return -1;
    }

    info = ChessReplayMap[found].func(fp);
    fclose(fp);

    if (info) {
	screen_backup(&oldscreen);
	ChessPlay(info);
	screen_restore(&oldscreen);

	DeleteChessInfo(info);
    }

    return 0;
}

static void
ChessInitUser(ChessInfo* info)
{
    char	      userid[2][IDLEN + 1];
    const userinfo_t* uinfo;
    userec_t          urec;

    switch (info->mode) {
	case CHESS_MODE_PERSONAL:
	    strlcpy(userid[0], cuser.userid, sizeof(userid[0]));
	    strlcpy(userid[1], cuser.userid, sizeof(userid[1]));
	    break;

	case CHESS_MODE_WATCH:
	    uinfo = search_ulist_userid(currutmp->mateid);
	    if (uinfo) {
		strlcpy(userid[0], uinfo->userid, sizeof(userid[0]));
		strlcpy(userid[1], uinfo->mateid, sizeof(userid[1]));
	    } else {
		strlcpy(userid[0], currutmp->mateid, sizeof(userid[0]));
		userid[1][0] = 0;
	    }
	    break;

	case CHESS_MODE_VERSUS:
	    strlcpy(userid[0], cuser.userid, sizeof(userid[0]));
	    strlcpy(userid[1], currutmp->mateid, sizeof(userid[1]));
	    break;

	case CHESS_MODE_REPLAY:
	    return;
    }

    uinfo = search_ulist_userid(userid[0]);
    if (uinfo)
	info->actions->init_user(uinfo, &info->user1);
    else if (getuser(userid[0], &urec))
	info->actions->init_user_rec(&urec, &info->user1);

    uinfo = search_ulist_userid(userid[1]);
    if (uinfo)
	info->actions->init_user(uinfo, &info->user2);
    else if (getuser(userid[1], &urec))
	info->actions->init_user_rec(&urec, &info->user2);
}

#ifdef CHESSCOUNTRY
static char*
ChessPhotoInitial(ChessInfo* info)
{
    char genbuf[256];
    int line;
    FILE* fp;
    static const char * const blank_photo[6] = {
	"┌──────┐",
	"│ 空         │",
	"│    白      │",
	"│       照   │",
	"│          片│",
	"└──────┘" 
    };
    char country[5], level[11];
    userec_t xuser;
    char* photo;
    int hasphoto = 0;

    if (info->mode == CHESS_MODE_REPLAY)
	return NULL;

    if(is_validuserid(info->user1.userid)) {
	sethomefile(genbuf, info->user1.userid, info->constants->photo_file_name);
	if (dashf(genbuf))
	    hasphoto++;
    }
    if(is_validuserid(info->user2.userid)) {
	sethomefile(genbuf, info->user2.userid, info->constants->photo_file_name);
	if (dashf(genbuf))
	    hasphoto++;
    }
    if(hasphoto==0)
	return NULL;

    photo = (char*) calloc(
	    CHESS_PHOTO_LINE * CHESS_PHOTO_COLUMN, sizeof(char));

    /* simulate photo as two dimensional array  */
#define PHOTO(X) (photo + (X) * CHESS_PHOTO_COLUMN)

    fp = NULL;
    if(getuser(info->user2.userid, &xuser)) {
	sethomefile(genbuf, info->user2.userid, info->constants->photo_file_name);
	fp = fopen(genbuf, "r");
    }

    if (fp == NULL) {
	strcpy(country, "無");
	level[0] = 0;
    } else {
	int i, j;
	for (line = 1; line < 8; ++line)
	    fgets(genbuf, sizeof(genbuf), fp);

	fgets(genbuf, sizeof(genbuf), fp);
	chomp(genbuf);
	strip_ansi(genbuf + 11, genbuf + 11,
		STRIP_ALL);        /* country name may have color */
	for (i = 11, j = 0; genbuf[i] && j < 4; ++i)
	    if (genbuf[i] != ' ')  /* and spaces */
		country[j++] = genbuf[i];
	country[j] = 0; /* two chinese words */

	fgets(genbuf, sizeof(genbuf), fp);
	chomp(genbuf);
	strlcpy(level, genbuf + 11, 11); /* five chinese words*/
	rewind(fp);
    }

    for (line = 0; line < 6; ++line) {
	if (fp != NULL) {
	    if (fgets(genbuf, sizeof(genbuf), fp)) {
		chomp(genbuf);
		sprintf(PHOTO(line), "%s", genbuf);
	    } else
		strcpy(PHOTO(line), "                ");
	} else
	    strcpy(PHOTO(line), blank_photo[line]);

	switch (line) {
	    case 0: sprintf(genbuf, " <代號> %s", xuser.userid);      break;
	    case 1: sprintf(genbuf, " <暱稱> %.16s", xuser.nickname); break;
	    case 2: sprintf(genbuf, " <上站> %d", xuser.numlogins);   break;
	    case 3: sprintf(genbuf, " <文章> %d", xuser.numposts);    break;
	    case 4: sprintf(genbuf, " <職位> %-4s %s", country, level);  break;
	    case 5: sprintf(genbuf, " <來源> %.16s", xuser.lasthost); break;
	    default: genbuf[0] = 0;
	}
	strcat(PHOTO(line), genbuf);
    }
    if (fp != NULL)
	fclose(fp);

    sprintf(PHOTO(6), "      %s%2.2s棋" ANSI_RESET,
	    info->constants->turn_color[(int) info->myturn ^ 1],
	    info->constants->turn_str[(int) info->myturn ^ 1]);
    strcpy(PHOTO(7), "           Ｖ.Ｓ           ");
    sprintf(PHOTO(8), "                               %s%2.2s棋" ANSI_RESET,
	    info->constants->turn_color[(int) info->myturn],
	    info->constants->turn_str[(int) info->myturn]);

    fp = NULL;
    if(getuser(info->user1.userid, &xuser)) {;
	sethomefile(genbuf, info->user1.userid, info->constants->photo_file_name);
	fp = fopen(genbuf, "r");
    }

    if (fp == NULL) {
	strcpy(country, "無");
	level[0] = 0;
    } else {
	int i, j;
	for (line = 1; line < 8; ++line)
	    fgets(genbuf, sizeof(genbuf), fp);

	fgets(genbuf, sizeof(genbuf), fp);
	chomp(genbuf);
	strip_ansi(genbuf + 11, genbuf + 11,
		STRIP_ALL);        /* country name may have color */
	for (i = 11, j = 0; genbuf[i] && j < 4; ++i)
	    if (genbuf[i] != ' ')  /* and spaces */
		country[j++] = genbuf[i];
	country[j] = 0; /* two chinese words */

	fgets(genbuf, sizeof(genbuf), fp);
	chomp(genbuf);
	strlcpy(level, genbuf + 11, 11); /* five chinese words*/
	rewind(fp);
    }

    for (line = 9; line < 15; ++line) {
	move(line, 37);
	switch (line - 9) {
	    case 0: sprintf(PHOTO(line), "<代號> %-16.16s ", xuser.userid);   break;
	    case 1: sprintf(PHOTO(line), "<暱稱> %-16.16s ", xuser.nickname); break;
	    case 2: sprintf(PHOTO(line), "<上站> %-16d ", xuser.numlogins);   break;
	    case 3: sprintf(PHOTO(line), "<文章> %-16d ", xuser.numposts);    break;
	    case 4: sprintf(PHOTO(line), "<職位> %-4s %-10s  ", country, level); break;
	    case 5: sprintf(PHOTO(line), "<來源> %-16.16s ", xuser.lasthost); break;
	}

	if (fp != NULL) {
	    if (fgets(genbuf, 200, fp)) {
		chomp(genbuf);
		strcat(PHOTO(line), genbuf);
	    } else
		strcat(PHOTO(line), "                ");
	} else
	    strcat(PHOTO(line), blank_photo[line - 9]);
    }
    if (fp != NULL)
	fclose(fp);
#undef PHOTO

    return photo;
}
#endif /* defined(CHESSCOUNTRY) */

static void
ChessInitPlayFunc(ChessInfo* info)
{
    switch (info->mode) {
	case CHESS_MODE_VERSUS:
	    info->play_func[(int) info->myturn] = &ChessPlayFuncMy;
	    info->play_func[info->myturn ^ 1]   = &ChessPlayFuncHis;
	    break;

	case CHESS_MODE_WATCH:
	case CHESS_MODE_REPLAY:
	    info->play_func[0] = info->play_func[1] = &ChessPlayFuncWatch;
	    break;

	case CHESS_MODE_PERSONAL:
	    info->play_func[0] = info->play_func[1] = &ChessPlayFuncMy;
	    break;
    }
}

ChessInfo*
NewChessInfo(const ChessActions* actions, const ChessConstants* constants,
	int sock, ChessGameMode mode)
{
    /* allocate memory for the structure and extra space for temporary
     * steping information storage (step_tmp[0]). */
    ChessInfo* info =
	(ChessInfo*) calloc(1, sizeof(ChessInfo) + constants->step_entry_size);

    if (mode == CHESS_MODE_PERSONAL)
	strcpy(currutmp->mateid, cuser.userid);

    /* compiler don't know it's actually const... */
    info->actions   = (ChessActions*)   actions;
    info->constants = (ChessConstants*) constants;
    info->mode      = mode;
    info->sock      = sock;

    if (mode == CHESS_MODE_VERSUS)
	info->myturn = currutmp->turn;
    else if (mode == CHESS_MODE_PERSONAL)
	info->myturn = 1;
    else if (mode == CHESS_MODE_REPLAY)
	info->myturn = 1;
    else if (mode == CHESS_MODE_WATCH)
	ChessReceiveWatchInfo(info);

    ChessInitUser(info);

#ifdef CHESSCOUNTRY
    info->photo = ChessPhotoInitial(info);
#endif

    if (mode != CHESS_MODE_WATCH)
	ChessHistoryInit(&info->history, constants->step_entry_size);

    ChessBroadcastListInit(&info->broadcast_list);
    ChessInitPlayFunc(info);

    return info;
}

void
DeleteChessInfo(ChessInfo* info)
{
#define NULL_OR_FREE(X) if (X) free(X); else (void) 0
    NULL_OR_FREE(info->timelimit);
    NULL_OR_FREE(info->photo);
    NULL_OR_FREE(info->history.body);

    ChessBroadcastListClear(&info->broadcast_list);
#undef NULL_OR_FREE
}

void
ChessEstablishRequest(int sock)
{
    /* XXX */
    if (!_current_time_limit)
	write(sock, "T", 1); /* traditional */
    else {
	write(sock, "L", 1); /* limited */
	write(sock, _current_time_limit, sizeof(ChessTimeLimit));
    }
}

void
ChessAcceptingRequest(int sock)
{
    /* XXX */
    char mode;
    read(sock, &mode, 1);
    if (mode == 'T')
	_current_time_limit = NULL;
    else {
	_current_time_limit = (ChessTimeLimit*) malloc(sizeof(ChessTimeLimit));
	read(sock, _current_time_limit, sizeof(ChessTimeLimit));
    }
}

void
ChessShowRequest(void)
{
    /* XXX */
    if (!_current_time_limit)
	mouts(10, 5, "使用傳統計時方式, 單步限時五分鐘");
    else if (_current_time_limit->time_mode == CHESS_TIMEMODE_MULTIHAND) {
	mouts(10, 5, "使用限時限步規則:");
	move(12, 8);
	prints("局時 (自由時間): %2d 分 %02d 秒",
		_current_time_limit->free_time / 60,
		_current_time_limit->free_time % 60);
	move(13, 8);
	prints("限時步時:        %2d 分 %02d 秒 / %2d 手",
		_current_time_limit->limit_time / 60,
		_current_time_limit->limit_time % 60,
		_current_time_limit->limit_hand);
    } else if (_current_time_limit->time_mode == CHESS_TIMEMODE_COUNTING) {
	mouts(10, 5, "使用讀秒規則:");
	move(12, 8);
	prints("局時 (自由時間): %2d 分 %02d 秒",
		_current_time_limit->free_time / 60,
		_current_time_limit->free_time % 60);
	move(13, 8);
	prints("讀秒時間:   每手 %2d 秒", _current_time_limit->limit_time);
    }
}

inline static const char*
ChessTimeStr(int second)
{
    static char buf[10];
    snprintf(buf, sizeof(buf), "%d:%02d", second / 60, second % 60);
    return buf;
}

void
ChessDrawExtraInfo(const ChessInfo* info, int line, int space)
{
    if (line == b_lines || line == 0)
	return;

    if (info->photo) {
	if (line >= 3 && line < 3 + CHESS_PHOTO_LINE) {
	    if (space > 3)
		outs(" ");
	    outs(info->photo + (line - 3) * CHESS_PHOTO_COLUMN);
	} else if (line >= CHESS_DRAWING_PHOTOED_STEP_ROW &&
		line <= CHESS_DRAWING_PHOTOED_WARN_ROW) {
	    prints("%*s", space, "");
	    if (line == CHESS_DRAWING_PHOTOED_STEP_ROW)
		outs(info->last_movestr);
	    else if (line == CHESS_DRAWING_PHOTOED_TURN_ROW)
		prints(ANSI_COLOR(1;33) "%s" ANSI_RESET,
			info->myturn == info->turn ? "輪到你下棋了" : "等待對方下棋");
	    else if (line == CHESS_DRAWING_PHOTOED_TIME_ROW1) {
		if (info->mode == CHESS_MODE_WATCH) {
		    if (!info->timelimit)
			prints("每手限時五分鐘");
		    else
			prints("局時: %5s",
				ChessTimeStr(info->timelimit->free_time));
		} else if (info->lefthand[0])
		    prints("我方剩餘時間 %s / %2d 步",
			    ChessTimeStr(info->lefttime[0]),
			    info->lefthand[0]);
		else
		    prints("我方剩餘時間 %s",
			    ChessTimeStr(info->lefttime[0]));
	    } else if (line == CHESS_DRAWING_PHOTOED_TIME_ROW2) {
		if (info->mode == CHESS_MODE_WATCH) {
		    if (info->timelimit) {
			if (info->timelimit->time_mode ==
				CHESS_TIMEMODE_MULTIHAND)
			    prints("步時: %s / %2d 步",
				    ChessTimeStr(info->timelimit->limit_time),
				    info->timelimit->limit_hand);
			else
			    prints("讀秒: %5d 秒",
				    info->timelimit->limit_time);
		    }
		} else if (info->lefthand[1])
		    prints("對方剩餘時間 %s / %2d 步",
			    ChessTimeStr(info->lefttime[1]),
			    info->lefthand[1]);
		else
		    prints("對方剩餘時間 %s",
			    ChessTimeStr(info->lefttime[1]));
	    } else if (line == CHESS_DRAWING_PHOTOED_WARN_ROW)
		outs(info->warnmsg);
	}
    } else if (line >= 3 && line <= CHESS_DRAWING_HISWIN_ROW) {
	prints("%*s", space, "");
	if (line >= 3 && line < 3 + (int)dim(ChessHintStr)) {
	    outs(ChessHintStr[line - 3]);
	} else if (line == CHESS_DRAWING_SIDE_ROW) {
	    prints(ANSI_COLOR(1) "你是%s%s" ANSI_RESET,
		    info->constants->turn_color[(int) info->myturn],
		    info->constants->turn_str[(int) info->myturn]);
	} else if (line == CHESS_DRAWING_REAL_TURN_ROW) {
	    prints(ANSI_COLOR(1;33) "%s" ANSI_RESET,
		    info->myturn == info->turn ?
		    "輪到你下棋了" : "等待對方下棋");
	} else if (line == CHESS_DRAWING_REAL_STEP_ROW && info->last_movestr) {
	    outs(info->last_movestr);
	} else if (line == CHESS_DRAWING_REAL_TIME_ROW1) {
	    if (info->lefthand[0])
		prints("我方剩餘時間 %s / %2d 步",
			ChessTimeStr(info->lefttime[0]),
			info->lefthand[0]);
	    else
		prints("我方剩餘時間 %s",
			ChessTimeStr(info->lefttime[0]));
	} else if (line == CHESS_DRAWING_REAL_TIME_ROW2) {
	    if (info->lefthand[1])
		prints("對方剩餘時間 %s / %2d 步",
			ChessTimeStr(info->lefttime[1]),
			info->lefthand[1]);
	    else
		prints("對方剩餘時間 %s",
			ChessTimeStr(info->lefttime[1]));
	} else if (line == CHESS_DRAWING_REAL_WARN_ROW) {
	    outs(info->warnmsg);
	} else if (line == CHESS_DRAWING_MYWIN_ROW) {
	    prints(ANSI_COLOR(1;33) "%12.12s    "
		    ANSI_COLOR(1;31) "%2d" ANSI_COLOR(37) "勝 "
		    ANSI_COLOR(34) "%2d" ANSI_COLOR(37) "敗 "
		    ANSI_COLOR(36) "%2d" ANSI_COLOR(37) "和" ANSI_RESET,
		    info->user1.userid,
		    info->user1.win, info->user1.lose - 1, info->user1.tie);
	} else if (line == CHESS_DRAWING_HISWIN_ROW) {
	    prints(ANSI_COLOR(1;33) "%12.12s    "
		    ANSI_COLOR(1;31) "%2d" ANSI_COLOR(37) "勝 "
		    ANSI_COLOR(34) "%2d" ANSI_COLOR(37) "敗 "
		    ANSI_COLOR(36) "%2d" ANSI_COLOR(37) "和" ANSI_RESET,
		    info->user2.userid,
		    info->user2.win, info->user2.lose, info->user2.tie);
	}
    }
}
