/**
 * edit.c, 用來提供 bbs上的文字編輯器, 即 ve.
 *
 * 目前每一行都只 allocate 需要的記憶體，只有編輯中的行有獨立的 buffer,
 * 移到別行時看 dirty flag 決定是否要寫回。
 * 另外若定義 DEBUG, 在 textline_t 結構中將加入 mlength, 表示該行實際佔的
 * 記憶體大小. 以方便測試結果.
 *
 * XXX 由於各種操作都不會 maintain 區塊標記模式的 pointer/state,
 * 因此對於會造成增刪 line 的功能, 都得自動取消標記模式(call block_cancel()).
 *
 * 20071230 piaip
 * BBSmovie 有人作出了 1.9G 的檔案, 看來要分 hard limit 跟 soft limit
 * [第 7426572/7426572 頁 (100%)  目前顯示: 第 163384551~163384573 行]
 * 當日調查 BBSmovie 看板與精華區，平均檔案皆在 5M 以下
 * 最大的為 16M 的 Haruhi OP (avi 轉檔 with massive ANSI)
 * [第 2953/2953 頁 (100%)  目前顯示: 第 64942~64964 行]
 * 另外互動迷宮的大小為
 * [第 1408/1408 頁 (100%)  目前顯示: 第 30940~30962 行]
 * 是以定義:
 * 32M 為 size limit
 * 1M 為 line limit
 * 又，忽然發現之前 totaln 之類都是 short... 所以 65536 就夠了?
 * 後註: 似乎是用 announce 的 append 作出來的，有看到 > --- <- mark。
 *
 * FIXME screen resize 造成 b_lines 改變, 可能會造成 state 錯亂. 尤其是 tty
 * mode 隨時都可能 resize. TODO editor_internal_t 多個欄位記 screen size,
 * 當操作到一段落之後才 check b_lines 是否有改變.
 */
#include "bbs.h"
#ifdef ALL_REEDIT_LOG
# define HAS_ALL_REEDIT_LOG 1
#else
# define HAS_ALL_REEDIT_LOG 0
#endif

#define EDIT_SIZE_LIMIT (32768*1024)
#define EDIT_LINE_LIMIT (65530) // (1048576)

#ifndef POST_MONEY_RATIO
#define POST_MONEY_RATIO (0.5f)
#endif

#ifndef ENTROPY_RATIO
#define ENTROPY_RATIO	(0.25f)
#endif

#define ENTROPY_MAX	(MAX_POST_MONEY/ENTROPY_RATIO)

// #define DEBUG

/**
 * data 欄位的用法:
 * 每次 allocate 一個 textline_t 時，會配給他 (sizeof(textline_t) + string
 * length - 1) 的大小。如此可直接存取 data 而不需額外的 malloc。
 */
typedef struct textline_t {
    struct textline_t *prev;
    struct textline_t *next;
    short           len;
    short           alloc_len;
#ifdef DEBUG
    short           mlength;
#endif
    char            data[1];
}               textline_t;

#define KEEP_EDITING    -2

enum {
    NOBODY, MANAGER, SYSOP
};

/**
 * 這個說明會將整個 edit.c 運作的概念帶過，主要會從 editor_internal_t 的
 * data structure 談起。對於每一個 data member 的詳細功能，請見 sturcture
 * 中的註解。
 *
 * 文章的內容 (以下稱 content) 以「行」為單位，主要以 firstline, lastline,
 * totaln 來記錄。
 *
 * User 在畫面中看到的畫面，置於一個「 window 」中，這個 window 會在
 * content 上移動，window 裡面的範圍即為要 show 出來的範圍。它用了不少
 * 欄位來記錄，包括 currline, top_of_win, currln, currpnt, curr_window_line,
 * edit_margin。顯示出來的效果當然不只是靠這幾個資料，還會跟其他欄位有交互
 * 作用，例如 彩色編輯模式、特殊符號編輯 等等。其中最複雜的部分是在選取 block
 * （見後）的時候。
 *
 * editor 的使用上目前有五種 inclusive 的 mode：
 *   insert mode:
 *     插入/取代
 *   ansi mode:
 *     彩色編輯
 *   indent mode:
 *     自動縮排
 *   phone mode:
 *     特殊符號編輯
 *   raw mode:
 *     ignore Ctrl('S'), Ctrl('Q'), Ctrl('T')
 *     贊曰: 這有什麼用? 看起來是 modem 上傳用 (沒人在用這個了吧)
 *     拿來當 dbcs option 吧
 *
 * editor 支援了區塊選擇的功能（多行選取 或 單行中的片段），對於一個 selected
 * block，可以 cut, copy, cancel, 或者存到暫取檔，甚至是往左/右 shift。詳見
 * block_XXX。
 *
 * 用 Ctrl('Y') 刪除的那一行會被記到 deleted_line 這個欄位中。undelete_line()
 * 可以做 undelete 的動作。 deleted_line 初始值為 NULL，每次只允許存一行，所以
 * 在存下來時，發現值不為 NULL 會先做 free 的動作。editor 結束時，在
 * edit_buffer_destructor() 中如果發現有 deleted_line，會在這邊釋放掉。其他地
 * 方也有相同的作法，例如 searched_string。searched_string 是在搜尋文章內容
 * 時，要尋找的 key word。
 *
 * 還有一個有趣的特點，「括號匹配」！行為就如同在 vim 裡面一樣。呃，當然沒那
 * 麼強啦，但至少在含有 c-style comment 跟 c-style string 時是對的喔。這個動
 * 作定義於 match_paren() 中。
 *
 * 另外，如果有需要新增新的欄位，請將初始化（有需要的話）的動作寫在
 * edit_buffer_constructor 中。當然也有個 edit_buffer_destructor 可以使用。
 *
 * 此外，為了提供一個 reentrant 的 editor，prev 指向前一個 editor 的
 * editor_internal_t。enter_edit_buffer 跟 exit_edit_buffer 提供進出的介面，
 * 裡面分別會呼叫 constructor 跟 destructor。
 *
 * Victor Hsieh <victor@csie.org>
 * Thu, 03 Feb 2005 15:18:00 +0800
 */
typedef struct editor_internal_t {

    textline_t *firstline;	/* first line of the article. */
    textline_t *lastline;	/* last line of the article. */

    textline_t *currline;	/* current line of the article(window). */
    textline_t *blockline;	/* the first selected line of the block. */
    textline_t *top_of_win;	/* top line of the article in the window. */

    textline_t *deleted_line;	/* deleted line. Just keep one deleted line. */
    textline_t *oldcurrline;

    struct editor_internal_t *prev;

    int   flags;                /* editor flags. */
    int   currln;		/* current line of the article. */
    int   totaln;		/* last line of the article. */
    int   curr_window_line;	/* current line to the window. */
    int   blockln;		/* the row you started to select block. */
    short currpnt;		/* current column of the article. */
    short last_margin;
    short edit_margin;		/* when the cursor moves out of range (say,
				   t_columns), shift this length of the string
				   so you won't see the first edit_margin-th
				   character. */
    short lastindent;
    char last_phone_mode;

    unsigned char redraw_everything	:1;
    unsigned char ifuseanony	:1;

    unsigned char insert_mode	:1;
    unsigned char ansimode	:1;
    unsigned char indent_mode	:1;
    unsigned char phone_mode	:1;
    unsigned char raw_mode	:1;
    char synparser;		// syntax parser

    char *searched_string;
    char *sitesig_string;
    char *(*substr_fp)(const char *, const char *);

} editor_internal_t;
// } __attribute__ ((packed))

static editor_internal_t *curr_buf = NULL;

static const char * const fp_bak = "bak";

// forward declare
static int has_block_selection(void);
static textline_t * alloc_line(short length);
static void block_cancel(void);

static const char * const BIG5[13] = {
  "，；：、﹑。？！‧﹗（）〝〞‵′",
  "▁▂▃▄▅▆▇█▏▎▍▌▋▊▉▓ ",
  "○⊙◎●☆★□■▼▲▽△◇◆♀♂",
  "﹌﹏\︴¯＿—∥∣▕／＼╳╱╲∕﹨",
  "＋－×÷√±＝≡≠≒≦≧＜＞∵∴",
  "∞～∩∪∫∮＆⊥∠∟⊿﹢﹣﹤﹥﹦",
  "↑↓←→↖↗↙↘",
  "【】「」『』〈〉《》〔〕｛｝︵︶",
  "︹︺︷︸︻︼︿﹀︽︾﹁﹂﹃﹄",
  "◢◣◥◤﹡＊※§＠⊕㊣…‥﹉﹍",
  "α\βγδεζηθικλμνξοπ",
  "ρστυφχψωΔΘΛΠΣΦΨΩ",
  "ⅠⅡⅢⅣⅤⅥⅦⅧⅨⅩ"
};

static const char * const BIG_mode[13] = {
  "標點",
  "圖塊",
  "標記",
  "標線",
  "數一",
  "數二",
  "箭頭",
  "括一",
  "括二",
  "其他",
  "希一",
  "希二",
  "數字"
};

static const char *table[8] = {
  "│─└┴┘├┼┤┌┬┐",
  "║═╚╩╝╠╬╣╔╦╗",
  "║─╙╨╜╟╫╢╓╥╖",
  "│═╘╧╛╞╪╡╒╤╕",
  "│─╰┴╯├┼┤╭┬╮",
  "║═╰╩╯╠╬╣╭╦╮",
  "║─╙╨╜╟╫╢╓╥╖",
  "│═╘╧╛╞╪╡╒╤╕"
};

static const char *table_mode[6] = {
  "直角",
  "彎弧",
  "┼",
  "╬",
  "╫",
  "╪"
};

static char mbcs_mode		=1;

#define IS_BIG5_HI(x) (0x81 <= (x) && (x) <= 0xfe)
#define IS_BIG5_LOS(x) (0x40 <= (x) && (x) <= 0x7e)
#define IS_BIG5_LOE(x) (0x80 <= (x) && (x) <= 0xfe)
#define IS_BIG5_LO(x) (IS_BIG5_LOS(x) || IS_BIG5_LOE(x))
#define IS_BIG5(hi,lo) (IS_BIG5_HI(hi) && IS_BIG5_LO(lo))

static int
mb_count(const char *str)
{
    int count = 0, w;
    while ((w = mb_bytes(str)) > 0) {
	str += w;
	count++;
    }
    return count;
}

#define FC_RIGHT (0)
#define FC_LEFT  (1)

/* Return the cursor position aligned to the beginning of a character.
 * If `pos' is in the middle of a character, the alignment determines on `dir':
 *     FC_LEFT: aligned to the current character.
 *     FC_RIGHT: aligned to the next character.
 */
static int
fix_cursor(char *str, int pos, int dir)
{
    int newpos = 0, w = 0;
    assert(dir == FC_RIGHT || dir == FC_LEFT);
    assert(pos >= 0);

    while (*str != '\0' && newpos < pos) {
	w = mb_bytes(str);
	str += w;
	newpos += w;
    }
    if (dir == FC_LEFT && newpos > pos)
	newpos -= w;

    return newpos;
}


/* 記憶體管理與編輯處理 */
static void
edit_buffer_constructor(editor_internal_t *buf)
{
    /* all unspecified columns are 0 */
    buf->blockln = -1;
    buf->insert_mode = 1;
    buf->redraw_everything = 1;
    buf->lastindent = -1;

    buf->oldcurrline = buf->currline = buf->top_of_win =
	buf->firstline = buf->lastline = alloc_line(WRAPMARGIN);

}

static void
enter_edit_buffer(void)
{
    editor_internal_t *p = curr_buf;
    curr_buf = (editor_internal_t *)malloc(sizeof(editor_internal_t));
    memset(curr_buf, 0, sizeof(editor_internal_t));
    curr_buf->prev = p;
    edit_buffer_constructor(curr_buf);
}

static void
free_line(textline_t *p)
{
    if (p == curr_buf->oldcurrline)
	curr_buf->oldcurrline = NULL;
    p->next = (textline_t*)0x12345678;
    p->prev = (textline_t*)0x87654321;
    p->len = -12345;
    free(p);
}

static void
edit_buffer_destructor(void)
{
    textline_t *p, *pnext;
    for (p = curr_buf->firstline; p; p = pnext) {
	pnext = p->next;
	free_line(p);
    }
    if (curr_buf->deleted_line != NULL)
	free_line(curr_buf->deleted_line);

    if (curr_buf->searched_string != NULL)
	free(curr_buf->searched_string);
    if (curr_buf->sitesig_string != NULL)
	free(curr_buf->sitesig_string);
}

static void
exit_edit_buffer(void)
{
    editor_internal_t *p = curr_buf;

    edit_buffer_destructor();
    curr_buf = p->prev;
    free(p);
}

/**
 * transform position ansix in an ansi string of textline_t to the same
 * string without escape code.
 * @return position in the string without escape code.
 */
static int
line_col_to_pos(const textline_t *line, int col, int strip_esc)
{
    const char *data = line->data;
    const char *tmp = data;
    char ch;

    while (*tmp) {
	if (strip_esc && *tmp == KEY_ESC) {
	    while ((ch = *tmp) && !isalpha((unsigned char)ch))
		tmp++;
	    if (ch)
		tmp++;
	    continue;
	}
	if (col <= 0)
	    break;
	int w = mb_bytes(tmp);
	int cw = mb_width(tmp);
	if (col < cw) {
	    if (!mbcs_mode)
		tmp += col;
	    break;
	}
	tmp += w;
	col -= cw;
    }
    return (int)(tmp - data);
}

static short
line_pos_to_col(const textline_t *line, int nx, int strip_esc)
{
    short col = 0;
    const char *tmp = line->data;
    const char *nxp = tmp + nx;
    char ch;

    while (*tmp) {
	if (strip_esc && *tmp == KEY_ESC) {
	    while ((ch = *tmp) && !isalpha((unsigned char)ch))
		tmp++;
	    if (ch)
		tmp++;
	    continue;
	}
	if (tmp >= nxp)
	    break;
	int w = mb_bytes(tmp);
	int cw = mb_width(tmp);
	if (tmp + w > nxp) {
	    col += (short)(nxp - tmp);
	    break;
	}
	tmp += w;
	col += cw;
    }
    return col;
}

static int
ansi2n(int ansix, textline_t * line)
{
    return line_col_to_pos(line, ansix, 1);
}

/**
 * opposite to ansi2n, according to given textline_t.
 * @return position in the string with escape code.
 */
static short
n2ansi(short nx, textline_t * line)
{
    return line_pos_to_col(line, nx, 1);
}

/* 螢幕處理：輔助訊息、顯示編輯內容 */

static void
show_phone_mode_panel(void)
{
    int i;

    move(b_lines - 1, 0);
    clrtoeol();

    if (curr_buf->last_phone_mode < 20) {
	int len;
	prints(ANSI_COLOR(1;46) "【%s輸入】 ", BIG_mode[curr_buf->last_phone_mode - 1]);
	const char *p = BIG5[curr_buf->last_phone_mode - 1];
	len = mb_count(p);
	for (i = 0; i < len; i++, p += mb_bytes(p))
	    prints(ANSI_COLOR(37) "%c" ANSI_COLOR(34) "%.*s",
		    i + 'A', mb_bytes(p), p);
	for (i = 0; i < 16 - len; i++)
	    outs("   ");
	outs(ANSI_COLOR(37) " `1~9-=切換 Z表格" ANSI_RESET);
    }
    else {
	prints(ANSI_COLOR(1;46) "【表格繪製】 /=%s *=%s形   ",
		table_mode[(curr_buf->last_phone_mode - 20) / 4],
		table_mode[(curr_buf->last_phone_mode - 20) % 4 + 2]);
	const char *p = table[curr_buf->last_phone_mode - 20];
	for (i = 0; i < 11 && *p; i++, p += mb_bytes(p))
	    prints(ANSI_COLOR(37) "%c" ANSI_COLOR(34) "%.*s", i ? i + '/' : '.',
		    mb_bytes(p), p);
	outs(ANSI_COLOR(37) "          Z內碼 " ANSI_RESET);
    }
}

/**
 * Show the bottom status/help bar, and BIG5/table in phone_mode.
 */
static void
edit_msg(void)
{
    int n = line_pos_to_col(curr_buf->currline, curr_buf->currpnt,
			    curr_buf->ansimode);

    if (curr_buf->phone_mode)
	show_phone_mode_panel();

    vs_footer(" 編輯文章 ",
	    TEMPFORMAT(STRLEN, " (^Z/F1)說明 (^P/^G)插入符號/範本 (^X/^Q)離開\t"
		"║%s│%c%c%c%c║%3d:%3d",
		curr_buf->insert_mode ? "插入" : "取代",
		curr_buf->ansimode ? 'A' : 'a',
		curr_buf->indent_mode ? 'I' : 'i',
		curr_buf->phone_mode ? 'P' : 'p',
		curr_buf->raw_mode ? 'R' : 'r',
		curr_buf->currln + 1, n + 1));
}

static const char *
get_edit_kind_prompt(int flags)
{
    int has_reply_post = (flags & EDITFLAG_KIND_REPLYPOST),
	has_reply_mail = (flags & EDITFLAG_KIND_SENDMAIL),
	has_new_post   = (flags & EDITFLAG_KIND_NEWPOST);

    if (has_reply_post && has_reply_mail)
	return ANSI_COLOR(0;1;37;45)
	    "注意：您即將 回信給原作者 並同時 回覆文章至看板上" ANSI_RESET;
    if (has_reply_mail)
	return ANSI_COLOR(0;1;31) "注意：您即將 寄出 私人信件" ANSI_RESET;
    if (has_reply_post)
	return ANSI_COLOR(0;1;33) "注意：您即將 回覆 文章至看板上" ANSI_RESET;
    if (has_new_post)
	return ANSI_COLOR(0;1;32) "注意：您即將 發表 新文章至看板上" ANSI_RESET;
    return NULL;
}

static const char *
get_edit_warn_prompt(int flags) {
    int no_self_sel    = (flags & EDITFLAG_WARN_NOSELFDEL);

    // FIXME not always on boards, maybe
    if (no_self_sel)
        return ANSI_COLOR(0;1;31)
            "注意: 此看板禁止自刪文章，發出後將只有板主以上才能刪文!!!" ANSI_RESET;

    return NULL;
}

//#define SLOW_CHECK_DETAIL
static void
edit_buffer_check_healthy(textline_t *line)
{
    assert(line);

    if (line->next)
	assert(line->next->prev == line);
    if (line->prev)
	assert(line->prev->next == line);
    assert(0 <= line->len);
    assert(line->len <= WRAPMARGIN);
#ifdef DEBUG
    assert(line->len <= line->mlength);
#endif
#ifdef SLOW_CHECK_DETAIL
    assert(strlen(line->data) == line->len);
#endif
}

static int visible_window_height(void);

static void
edit_check_healthy()
{
#ifdef SLOW_CHECK_DETAIL
    int i;
    textline_t *p;
#endif

    edit_buffer_check_healthy(curr_buf->firstline);
    assert(curr_buf->firstline->prev == NULL);

    edit_buffer_check_healthy(curr_buf->lastline);
    assert(curr_buf->lastline->next == NULL);

    if (curr_buf->oldcurrline)
	edit_buffer_check_healthy(curr_buf->oldcurrline);

    // currline
    edit_buffer_check_healthy(curr_buf->currline);
    assert(0 <= curr_buf->currpnt && curr_buf->currpnt <= curr_buf->currline->len);

    if (curr_buf->deleted_line) {
	edit_buffer_check_healthy(curr_buf->deleted_line);
	assert(curr_buf->deleted_line->next == NULL);
	assert(curr_buf->deleted_line->prev == NULL);
	assert(curr_buf->deleted_line != curr_buf->firstline);
	assert(curr_buf->deleted_line != curr_buf->lastline);
	assert(curr_buf->deleted_line != curr_buf->currline);
	assert(curr_buf->deleted_line != curr_buf->blockline);
	assert(curr_buf->deleted_line != curr_buf->top_of_win);
    }

    // lines
    assert(0 <= curr_buf->currln);
    assert(curr_buf->currln <= curr_buf->totaln);

    // window
    assert(curr_buf->curr_window_line < visible_window_height());

#ifdef SLOW_CHECK_DETAIL
    // firstline -> currline (0 -> currln)
    p = curr_buf->firstline;
    for (i = 0; i < curr_buf->currln; i++) {
	edit_buffer_check_healthy(p);
	p = p->next;
    }
    assert(p == curr_buf->currline);

    // currline -> lastline (currln -> totaln)
    p = curr_buf->currline;
    for (i = 0; i < curr_buf->totaln - curr_buf->currln; i++) {
	edit_buffer_check_healthy(p);
	p = p->next;
    }
    assert(p == curr_buf->lastline);

    // top_of_win -> currline (curr_window_line)
    p = curr_buf->top_of_win;
    for (i = 0; i < curr_buf->curr_window_line; i++)
	p = p->next;
    assert(p == curr_buf->currline);

#endif

    // block
    assert(curr_buf->blockln < 0 || curr_buf->blockline);
    if (curr_buf->blockline) {
	edit_buffer_check_healthy(curr_buf->blockline);
#ifdef SLOW_CHECK_DETAIL
	p = curr_buf->firstline;
	for (i = 0; i < curr_buf->blockln; i++)
	    p = p->next;
	assert(p == curr_buf->blockline);
#endif
    }
}

/**
 * return the middle line of the window.
 */
static int
middle_line(void)
{
    return p_lines / 2 + 1;
}

/**
 * Return the previous 'num' line.  Stop at the first line if there's
 * not enough lines.
 */
static textline_t *
back_line(textline_t * pos, int num, bool changeln)
{
    while (num-- > 0) {
	textline_t *item;

	if (pos && (item = pos->prev)) {
	    pos = item;
	    if (changeln)
		curr_buf->currln--;
	}
	else
	    break;
    }
    return pos;
}

static int
visible_window_height(void)
{
    if (curr_buf->phone_mode)
	return b_lines - 1;
    else
	return b_lines;
}

/**
 * Return the next 'num' line.  Stop at the last line if there's not
 * enough lines.
 */
static textline_t *
forward_line(textline_t * pos, int num, bool changeln)
{
    while (num-- > 0) {
	textline_t *item;

	if (pos && (item = pos->next)) {
	    pos = item;
	    if (changeln)
		curr_buf->currln++;
	}
	else
	    break;
    }
    return pos;
}

/**
 * move the cursor to the next line with ansimode fixed.
 */
static void
cursor_to_next_line(void)
{
    short pos;

    if (curr_buf->currline->next == NULL)
	return;

    curr_buf->currline = curr_buf->currline->next;
    curr_buf->curr_window_line++;
    curr_buf->currln++;

    if (curr_buf->ansimode) {
	pos = n2ansi(curr_buf->currpnt, curr_buf->currline->prev);
	curr_buf->currpnt = ansi2n(pos, curr_buf->currline);
    }
    else {
	curr_buf->currpnt = line_col_to_pos(curr_buf->currline,
					    curr_buf->lastindent, 0);
    }
}

/**
 * opposite to cursor_to_next_line.
 */
static void
cursor_to_prev_line(void)
{
    short pos;

    if (curr_buf->currline->prev == NULL)
	return;

    curr_buf->curr_window_line--;
    curr_buf->currln--;
    curr_buf->currline = curr_buf->currline->prev;

    if (curr_buf->ansimode) {
	pos = n2ansi(curr_buf->currpnt, curr_buf->currline->next);
	curr_buf->currpnt = ansi2n(pos, curr_buf->currline);
    }
    else {
	curr_buf->currpnt = line_col_to_pos(curr_buf->currline,
					    curr_buf->lastindent, 0);
    }
}

static void
edit_window_adjust(void)
{
    int offset = 0;
    if (curr_buf->curr_window_line < 0) {
	offset = curr_buf->curr_window_line;
	curr_buf->curr_window_line = 0;
	curr_buf->top_of_win = curr_buf->currline;
    }

    if (curr_buf->curr_window_line >= visible_window_height()) {
	offset = curr_buf->curr_window_line - visible_window_height() + 1;
	curr_buf->curr_window_line = visible_window_height() - 1;
	curr_buf->top_of_win = back_line(curr_buf->currline, visible_window_height() - 1, false);
    }

    if (offset == -1)
	rscroll();
    else if (offset == 1) {
	move(visible_window_height(), 0);
	clrtoeol();
	scroll();
    } else if (offset != 0) {
	curr_buf->redraw_everything = YEA;
    }
}

static void
edit_window_adjust_middle(void)
{
    if (curr_buf->currln < middle_line()) {
	curr_buf->top_of_win = curr_buf->firstline;
	curr_buf->curr_window_line = curr_buf->currln;
    } else {
	int i;
	textline_t *p = curr_buf->currline;
	curr_buf->curr_window_line = middle_line();
	for (i = curr_buf->curr_window_line; i; i--)
	    p = p->prev;
	curr_buf->top_of_win = p;
    }
    curr_buf->redraw_everything = YEA;
}

/**
 * Get the current line number in the window now.
 */
static int
get_lineno_in_window(void)
{
    int             cnt = 0;
    textline_t     *p = curr_buf->currline;

    while (p && (p != curr_buf->top_of_win)) {
	cnt++;
	p = p->prev;
    }
    return cnt;
}

/**
 * shift given raw data s with length len to left by one byte.
 */
static void
raw_shift_left(char *s, int len)
{
    int i;
    for (i = 0; i < len && s[i] != 0; ++i)
	s[i] = s[i + 1];
}

/**
 * shift given raw data s with length len to right by one byte.
 */
static void
raw_shift_right(char *s, int len)
{
    int i;
    for (i = len - 1; i >= 0; --i)
	s[i + 1] = s[i];
}

/**
 * Return the pointer to the next non-space position.
 */
static char *
next_non_space_char(char *s)
{
    while (*s == ' ')
	s++;
    return s;
}

/**
 * allocate a textline_t with length length.
 */
static textline_t *
alloc_line(short length)
{
    textline_t *p;

    if ((p = (textline_t *) malloc(length + sizeof(textline_t)))) {
	p->prev = NULL;
	p->next = NULL;
	p->len = 0;
	p->alloc_len = length;
	p->data[0] = '\0';
#ifdef DEBUG
	p->mlength = length;
#endif
	return p;
    }
    assert(p);
    abort_bbs(0);
    return NULL;
}

/**
 * clone a textline_t
 */
static textline_t *
clone_line(const textline_t *line)
{
    textline_t *p;

    p = alloc_line(line->len);
    p->len = line->len;
    strlcpy(p->data, line->data, p->len + 1);

    return p;
}

/**
 * Insert p after line in list. Keeps up with last line
 */
static void
insert_line(textline_t *line, textline_t *p)
{
    textline_t *n;

    if ((p->next = n = line->next))
	n->prev = p;
    else
	curr_buf->lastline = p;
    line->next = p;
    p->prev = line;
}

/**
 * delete_line deletes 'line' from the line list.
 * @param saved  true if you want to keep the line in deleted_line
 */
static void
delete_line(textline_t * line, int saved)
{
    textline_t *p = line->prev;
    textline_t *n = line->next;

    if (!p && !n) {
	line->data[0] = line->len = 0;
	return;
    }
    assert(line != curr_buf->top_of_win);
    if (n)
	n->prev = p;
    else
	curr_buf->lastline = p;
    if (p)
	p->next = n;
    else
	curr_buf->firstline = n;

    curr_buf->totaln--;

    if (saved) {
	if  (curr_buf->deleted_line != NULL)
	    free_line(curr_buf->deleted_line);
	if (line == curr_buf->oldcurrline)
	    curr_buf->oldcurrline = NULL;
	if (line->alloc_len > line->len) {
	    line = (textline_t *) realloc(line, line->len + sizeof(textline_t));
	    assert(line);
	    line->alloc_len = line->len;
#ifdef DEBUG
	    line->mlength = line->len;
#endif
	}
	curr_buf->deleted_line = line;
	curr_buf->deleted_line->next = NULL;
	curr_buf->deleted_line->prev = NULL;
    }
    else {
	free_line(line);
    }
}

/**
 * Return the indent space number according to CURRENT line and the FORMER
 * line. It'll be the first line contains non-space character.
 * @return space number from the beginning to the first non-space character,
 *         return 0 if non or not in indent mode.
 */
static int
indent_space(void)
{
    textline_t     *p;
    int             spcs;

    if (!curr_buf->indent_mode)
	return 0;

    for (p = curr_buf->currline; p; p = p->prev) {
	for (spcs = 0; p->data[spcs] == ' '; ++spcs);
	    /* empty loop */
	if (p->data[spcs])
	    return spcs;
    }
    return 0;
}

/**
 * adjustline(oldp, len);
 * 用來將 oldp 指到的那一行, 透過 realloc() 重新調整為 len 長度,
 * 並在指標位址改變時同步修正相關全域與串列指標.
 */
static textline_t *
adjustline(textline_t *oldp, short len)
{
    textline_t *newp;

    assert(0 <= oldp->len && oldp->len <= WRAPMARGIN);
    assert(oldp != curr_buf->deleted_line);
    assert(oldp->len <= len);

    if (oldp->alloc_len == len)
	return oldp;

    newp = (textline_t *) realloc(oldp, len + sizeof(textline_t));
    if (!newp) {
	assert(newp);
	abort_bbs(0);
	return NULL;
    }
    newp->alloc_len = len;
    newp->data[newp->len] = '\0';
#ifdef DEBUG
    newp->mlength = len;
#endif
    if (oldp != newp) {
	if (oldp == curr_buf->firstline)   curr_buf->firstline   = newp;
	if (oldp == curr_buf->lastline)    curr_buf->lastline    = newp;
	if (oldp == curr_buf->currline)    curr_buf->currline    = newp;
	if (oldp == curr_buf->blockline)   curr_buf->blockline   = newp;
	if (oldp == curr_buf->top_of_win)  curr_buf->top_of_win  = newp;
	if (oldp == curr_buf->oldcurrline) curr_buf->oldcurrline = newp;
	if (newp->prev != NULL) newp->prev->next = newp;
	if (newp->next != NULL) newp->next->prev = newp;
    }
    if (curr_buf->oldcurrline == NULL && len == WRAPMARGIN)
	curr_buf->oldcurrline = curr_buf->currline;
    return newp;
}

/**
 * split 'line' right before the character pos
 *
 * @return the upper line after splitting
 */
static textline_t *
split(textline_t * line, int pos, int indent)
{
    if (mbcs_mode && pos > 0 && pos < line->len)
	pos = fix_cursor(line->data, pos, FC_LEFT);
    if (pos <= line->len) {
	char  *ptr;
	int    spcs = indent;
	int    tail_len, new_len;

	curr_buf->totaln++;

	ptr = line->data + pos;
	if (curr_buf->indent_mode) {
	    ptr = next_non_space_char(ptr);
	    tail_len = strlen(ptr);
	} else {
	    tail_len = line->len - pos;
	}
	new_len = tail_len + spcs;

	if (line == curr_buf->currline && pos <= curr_buf->currpnt) {
	    /* Save offset: adjustline() below may realloc and move line->data. */
	    int actual_pos = (int)(ptr - line->data);

	    /* Allocate exact-size node for the upper half, reuse line (WRAPMARGIN) for currline */
	    textline_t *head = alloc_line(pos);
	    head->len = pos;
	    memcpy(head->data, line->data, pos);
	    head->data[pos] = '\0';

	    if (line->alloc_len < WRAPMARGIN)
		line = adjustline(line, WRAPMARGIN);
	    ptr = line->data + actual_pos;

	    head->prev = line->prev;
	    head->next = line;
	    if (line->prev)
		line->prev->next = head;
	    else
		curr_buf->firstline = head;
	    line->prev = head;

	    if (curr_buf->top_of_win == line)
		curr_buf->top_of_win = head;
	    if (curr_buf->blockline == line)
		curr_buf->blockline = head;

	    memmove(line->data + spcs, ptr, tail_len + 1);
	    if (spcs > 0)
		memset(line->data, ' ', spcs);
	    line->len = new_len;

	    if (curr_buf->currpnt <= actual_pos)
		curr_buf->currpnt = spcs;
	    else {
		curr_buf->currpnt = curr_buf->currpnt - actual_pos + spcs;
		// In indent_mode, the length may be shorter.
		if (curr_buf->currpnt > curr_buf->currline->len)
		    curr_buf->currpnt = curr_buf->currline->len;
	    }
	    curr_buf->curr_window_line++;
	    curr_buf->currln++;

	    /* split may cause cursor hit bottom */
	    edit_window_adjust();
	    line = head;
	} else {
	    /* Allocate exact-size node for the lower half directly */
	    textline_t *p = alloc_line(new_len);
	    p->len = new_len;
	    if (spcs > 0)
		memset(p->data, ' ', spcs);
	    memcpy(p->data + spcs, ptr, tail_len + 1);
	    line->len = pos;
	    line->data[pos] = '\0';
	    insert_line(line, p);
	}
	curr_buf->redraw_everything = YEA;
	edit_buffer_check_healthy(line);
	edit_buffer_check_healthy(line->next);
    }
    return line;
}

/**
 * Insert a character ch to current line.
 *
 * The line will be split if the length is >= WRAPMARGIN.  It'll be split
 * from the last space if any, or start a new line after the last character.
 */
static void delete_char(void);

static void
del_currchar(void)
{
    if (curr_buf->ansimode)
	curr_buf->currpnt = ansi2n(n2ansi(curr_buf->currpnt, curr_buf->currline), curr_buf->currline);
    if (curr_buf->currpnt >= curr_buf->currline->len)
	return;
    if (mbcs_mode)
	curr_buf->currpnt = fix_cursor(curr_buf->currline->data, curr_buf->currpnt, FC_LEFT);
    int w = mbcs_mode ? mb_bytes(curr_buf->currline->data + curr_buf->currpnt) : 1;
    for (; w > 0; w--)
	delete_char();
}

static void
raw_insert_char(int ch)
{
    textline_t *p;
    char  *s;
    int             wordwrap = YEA;

    if (curr_buf->currline->alloc_len < WRAPMARGIN)
	curr_buf->currline = adjustline(curr_buf->currline, WRAPMARGIN);
    p = curr_buf->currline;

    assert(curr_buf->currpnt <= p->len);
#ifdef DEBUG
    assert(curr_buf->currline->mlength == WRAPMARGIN);
    assert(p->len < p->mlength);
#endif

    block_cancel();
    raw_shift_right(p->data + curr_buf->currpnt, p->len - curr_buf->currpnt + 1);
    p->data[curr_buf->currpnt++] = ch;
    ++(p->len);
    if (p->len < WRAPMARGIN)
	return;

    s = p->data + (p->len - 1);
    while (s != p->data && *s == ' ')
	s--;
    while (s != p->data && *s != ' ')
	s--;
    int split_pos = (int)(s - p->data) + 1;
    if (s == p->data || (p->len - split_pos) >= WRAPMARGIN - 1) {
	wordwrap = NA;
	s = p->data + fix_cursor(p->data, p->len - 1, FC_LEFT) - 1;
	split_pos = (int)(s - p->data) + 1;
    }

    p = split(p, split_pos, 0);

    p = p->next;
    if (wordwrap && p->len >= 1 && p->len + 1 < WRAPMARGIN &&
	!(curr_buf->currline == p && curr_buf->currpnt == p->len)) {
	if (p->alloc_len < p->len + 1)
	    p = adjustline(p, p->len + 1);
#ifdef DEBUG
	assert(p->len < p->mlength);
#endif
	if (p->data[p->len - 1] != ' ') {
	    p->data[p->len] = ' ';
	    p->data[p->len + 1] = '\0';
	    p->len++;
	}
    }
}

static void
insert_char(int ch)
{
    if (!curr_buf->insert_mode)
	del_currchar();
    raw_insert_char(ch);
    /* Thor: ansi 編輯, 可以overwrite, 不蓋到 ansi code */
    if (!curr_buf->insert_mode && curr_buf->ansimode)
	curr_buf->currpnt = ansi2n(n2ansi(curr_buf->currpnt, curr_buf->currline), curr_buf->currline);
}

/**
 * insert_char twice.
 */
static void
insert_dchar(const char *dchar)
{
    int w = mb_bytes(dchar);
    if (!curr_buf->insert_mode && !mbcs_mode) {
	/* Byte-oriented editing: del_currchar() removes 1 byte; overwrite w. */
	for (int i = 0; i < w; i++)
	    del_currchar();
    } else if (!curr_buf->insert_mode) {
	int old_len = curr_buf->currline->len;
	del_currchar();
	/* If a 2-column dchar overwrote a 1-byte ASCII char, also overwrite the next 1-byte ASCII char */
	if (old_len - curr_buf->currline->len == 1 &&
	    mb_width(dchar) >= 2 &&
	    curr_buf->currpnt < curr_buf->currline->len &&
	    mb_bytes(curr_buf->currline->data + curr_buf->currpnt) == 1 &&
	    (!curr_buf->ansimode || curr_buf->currline->data[curr_buf->currpnt] != ESC_CHR)) {
	    del_currchar();
	}
    }
    textline_t *p = curr_buf->currline;
    if (mbcs_mode && curr_buf->currpnt < p->len)
	curr_buf->currpnt = fix_cursor(p->data, curr_buf->currpnt, FC_LEFT);
    if (p->len + w >= WRAPMARGIN) {
	int max_split = (curr_buf->currpnt == p->len) ? p->len : (WRAPMARGIN - 1 - w);
	char *s = p->data + (max_split - 1);
	int wordwrap = YEA;
	while (s != p->data && *s == ' ')
	    s--;
	while (s != p->data && *s != ' ')
	    s--;
	int split_pos = (int)(s - p->data) + 1;
	int lower_extra = (curr_buf->currpnt >= split_pos) ? w : 0;
	if (s == p->data || (p->len - split_pos) + 1 + lower_extra >= WRAPMARGIN) {
	    wordwrap = NA;
	    if (curr_buf->currpnt == p->len)
		s = p->data + p->len - 1;
	    else
		s = p->data + fix_cursor(p->data, max_split, FC_LEFT) - 1;
	    split_pos = (int)(s - p->data) + 1;
	}
	p = split(p, split_pos, 0);
	p = p->next;
	if (wordwrap && p->len >= 1 && p->len + 1 + lower_extra < WRAPMARGIN &&
	    !(curr_buf->currline == p && curr_buf->currpnt == p->len)) {
	    if (p->alloc_len < p->len + 1)
		p = adjustline(p, p->len + 1);
	    if (p->data[p->len - 1] != ' ') {
		p->data[p->len] = ' ';
		p->data[p->len + 1] = '\0';
		p->len++;
	    }
	}
	if (mbcs_mode && curr_buf->currpnt < curr_buf->currline->len)
	    curr_buf->currpnt = fix_cursor(curr_buf->currline->data, curr_buf->currpnt, FC_LEFT);
    }
    for (int i = 0; i < w; i++)
	raw_insert_char(dchar[i]);
    if (!curr_buf->insert_mode && curr_buf->ansimode)
	curr_buf->currpnt = ansi2n(n2ansi(curr_buf->currpnt, curr_buf->currline), curr_buf->currline);
}

/* A key read ahead by vkey_to_mb() that did not belong to the character. */
static int edit_pending_key = KEY_INCOMPLETE;

/* Big5 lead byte inserted without its trail byte, e.g. a colored half of a
 * character ("lead ESC[1;33m trail"). The trail byte should be inserted
 * alone instead of being paired with the next lead byte. */
static int edit_dbcs_half = 0;
/* Inside the ANSI sequence of edit_dbcs_half (after ESC or ^U). */
static int edit_dbcs_half_esc = 0;

static int
edit_vkey(void)
{
    int c;
    if (edit_pending_key != KEY_INCOMPLETE) {
	c = edit_pending_key;
	edit_pending_key = KEY_INCOMPLETE;
    } else {
	c = vkey();
    }
    if (c == Ctrl('U') || c == ESC_CHR) {
	if (edit_dbcs_half)
	    edit_dbcs_half_esc = 1;
    } else if (!vkey_isprint(c)) {
	edit_dbcs_half = edit_dbcs_half_esc = 0;
    }
    return c;
}

static int
vkey_to_mb(int ch, char mb[5])
{
    if (!VKEY_IS_MB)
	return mb_from_vkey(ch, mb);
    if (edit_dbcs_half) {
	if (edit_dbcs_half_esc) {
	    /* ANSI parameters; a final byte (other than '[') ends it. */
	    if (ch >= 0x40 && ch <= 0x7E && ch != '[')
		edit_dbcs_half_esc = 0;
	    else if (ch < 0x20 || ch > 0x3F)
		edit_dbcs_half_esc = (ch == '[');
	    if (isascii(ch)) {
		mb[0] = (char)ch;
		mb[1] = '\0';
		return 1;
	    }
	    edit_dbcs_half_esc = 0;
	}
	edit_dbcs_half = 0;
	if ((ch >= 0x40 && ch <= 0x7E) || (ch >= 0x80 && ch <= 0xFE)) {
	    /* The trail byte of the pending half character. */
	    mb[0] = (char)ch;
	    mb[1] = '\0';
	    return 1;
	}
    }
    if (isascii(ch)) {
	mb[0] = (char)ch;
	mb[1] = '\0';
	return 1;
    }
    if (MB_IS_UTF8) {
	utf8_ctx uctx;
	utf8_init(&uctx);
	utf8_add_byte(&uctx, ch);
	while (utf8_pending(&uctx)) {
	    int cb = vkey();
	    if (cb < 0x80 || cb > 0xBF) {
		/* Not a continuation byte: keep it for the caller. */
		edit_pending_key = cb;
		mb[0] = '\0';
		return 0;
	    }
	    utf8_add_byte(&uctx, cb);
	}
	return utf8_to_mb(&uctx, mb);
    }
    int ch2 = vkey();
    if ((ch2 >= 0x40 && ch2 <= 0x7E) || (ch2 >= 0x80 && ch2 <= 0xFE)) {
	mb[0] = (char)ch;
	mb[1] = (char)ch2;
	mb[2] = '\0';
	return 2;
    }
    /* Not a valid trail byte: keep the lead byte as-is (like old behavior)
     * and leave the other key (e.g. Enter) for the caller. */
    edit_pending_key = ch2;
    edit_dbcs_half = 1;
    edit_dbcs_half_esc = 0;
    mb[0] = (char)ch;
    mb[1] = '\0';
    return 1;
}

static void
insert_tab(void)
{
    do {
	insert_char(' ');
	edit_buffer_check_healthy(curr_buf->currline);
    } while (line_pos_to_col(curr_buf->currline, curr_buf->currpnt, curr_buf->ansimode) & 0x7);
}

/**
 * Insert a string.
 *
 * All printable and ESC_CHR will be directly printed out.
 * '\t' will be printed to align every 8 byte.
 * '\n' will split the line.
 * The other character will be ignore.
 */
static void
insert_string(const char *str)
{
    char ch;

    block_cancel();
    while ((ch = *str++)) {
	if ((unsigned char)ch >= 0x80) {
	    int w = mb_bytes(str - 1);
	    if (w > 1) {
		insert_dchar(str - 1);
		str += w - 1;
		continue;
	    }
	}
	if (isprint2(ch) || ch == ESC_CHR)
	    insert_char(ch);
	else if (ch == '\t')
	    insert_tab();
	else if (ch == '\n')
	    split(curr_buf->currline, curr_buf->currpnt, 0);
    }
}

/**
 * undelete the deleted line.
 *
 * return NULL if there's no deleted_line, otherwise, return currline.
 */
static textline_t *
undelete_line(void)
{
    textline_t *p;

    if (!curr_buf->deleted_line)
	return NULL;

    block_cancel();
    p = clone_line(curr_buf->deleted_line);

    // insert in front of currline
    p->prev = curr_buf->currline->prev;
    p->next = curr_buf->currline;
    if (curr_buf->currline->prev)
	curr_buf->currline->prev->next = p;
    curr_buf->currline->prev = p;
    curr_buf->totaln++;

    if (curr_buf->currline->alloc_len > curr_buf->currline->len)
	curr_buf->currline = adjustline(curr_buf->currline, curr_buf->currline->len);

    // maintain special line pointer
    if (curr_buf->top_of_win == curr_buf->currline)
	curr_buf->top_of_win = p;
    if (curr_buf->firstline == curr_buf->currline)
	curr_buf->firstline = p;

    // change currline
    curr_buf->currline = p;
    curr_buf->currpnt = 0;

    curr_buf->redraw_everything = YEA;

    return curr_buf->currline;
}

/*
 * join $line and $line->next
 *
 * line: A1 A2
 * next: B1 B2
 * ....: C1 C2
 *
 * case B=empty:
 *	delete_line B
 * 	return YEA
 *
 * case A+B < WRAPMARGIN:
 * 	line: A1 A2 B1 B2
 * 	next: C1 C2
 * 	return YEA
 * 	NOTE It assumes $line has allocated WRAPMARGIN length of data buffer.
 *
 * case A+B1+B2 > WRAPMARGIN, A+B1<WRAPMARGIN
 * 	line: A1 A2 B1
 * 	next: B2
 */
static int
join(textline_t * line)
{
    textline_t *n;
    int    ovfl;

    if (!(n = line->next))
	return YEA;
    if (!*next_non_space_char(n->data)) {
	delete_line(n, 0);
	return YEA;
    }


    ovfl = line->len + n->len - WRAPMARGIN;
    if (ovfl < 0) {
	if (line->alloc_len < WRAPMARGIN)
	    line = adjustline(line, WRAPMARGIN);
	strcat(line->data, n->data);
	line->len += n->len;
	delete_line(n, 0);
	return YEA;
    } else {
	char  *s; /* the split point */

	s = n->data + n->len - ovfl - 1;
	while (s != n->data && *s == ' ')
	    s--;
	while (s != n->data && *s != ' ')
	    s--;
	if (s == n->data) {
	    // TODO don't give up
	    return YEA;
	}
	split(n, (s - n->data) + 1, 0);
	assert(line->len + line->next->len < WRAPMARGIN);
	join(line);
	return NA;
    }
}

static void
delete_char(void)
{
    int    len;

    if ((len = curr_buf->currline->len)) {
	assert(curr_buf->currpnt < len);

	raw_shift_left(curr_buf->currline->data + curr_buf->currpnt, curr_buf->currline->len - curr_buf->currpnt + 1);
	curr_buf->currline->len--;
    }
}

static char *
edit_fgets_line(char *buf, int size, FILE *fp)
{
    if (!fgets(buf, size, fp))
        return NULL;
    storage_to_mb(buf, buf, (size_t)size);
    return buf;
}

static void
edit_insert_file_line(const char *buf)
{
    insert_string(TEMP_STORAGE_TO_MB_SZ(WRAPMARGIN + 2, buf));
}

static void
edit_fprintf_line(FILE *fp, const char *line)
{
    fprintf(fp, "%s\n", TEMP_MB_TO_STORAGE_SZ(WRAPMARGIN + 2, line));
}

static void
load_file(FILE * fp, off_t offSig)
{
    char buf[WRAPMARGIN + 2];
    int indent_mode0 = curr_buf->indent_mode;
    size_t szread = 0;

    assert(fp);
    curr_buf->indent_mode = 0;
    while (fgets(buf, sizeof(buf), fp))
    {
	szread += strlen(buf);
	if (offSig < 0 || szread <= (size_t)offSig)
	{
	    edit_insert_file_line(buf);
	}
	else
	{
	    // this is the site sig
	    break;
	}
    }
    curr_buf->indent_mode = indent_mode0;
}

/* 暫存檔 */
const char           *
ask_tmpbuf(int y)
{
    static char     fp_buf[] = "buf.0";
    char     msg[] = "請選擇暫存檔 (0-9)[0]: ";
    char choice[2];

    msg[19] = fp_buf[4];
    do {
	if (!getdata(y, 0, msg, choice, sizeof(choice), DOECHO))
	    choice[0] = fp_buf[4];
    } while (choice[0] < '0' || choice[0] > '9');
    fp_buf[4] = choice[0];
    return fp_buf;
}

static void
read_tmpbuf(int n)
{
    FILE           *fp;
    char            fp_tmpbuf[80];
    char            tmpfname[] = "buf.0";
    const char     *tmpf;
    char            ans[4] = "y";

    if (curr_buf->totaln >= EDIT_LINE_LIMIT)
    {
	vmsg("檔案已超過最大限制，無法再讀入暫存檔。");
	return;
    }

    if (0 <= n && n <= 9) {
	tmpfname[4] = '0' + n;
	tmpf = tmpfname;
    } else {
	tmpf = ask_tmpbuf(3);
	n = tmpf[4] - '0';
    }

    setuserfile(fp_tmpbuf, tmpf);
    if (n != 0 && n != 5 && more(fp_tmpbuf, NA) != -1)
	getdata(b_lines - 1, 0, "確定讀入嗎(Y/N)?[Y]", ans, sizeof(ans), LCECHO);
    if (*ans != 'n' && (fp = fopen(fp_tmpbuf, "r"))) {
	load_file(fp, -1);
	fclose(fp);
    }
}

static void
write_tmpbuf(void)
{
    FILE           *fp;
    char            fp_tmpbuf[80], ans[4];
    textline_t     *p;
    off_t	    sz = 0;

    setuserfile(fp_tmpbuf, ask_tmpbuf(3));
    if (dashf(fp_tmpbuf)) {
	more(fp_tmpbuf, NA);
	getdata(b_lines - 1, 0, "暫存檔已有資料 (A)附加 (W)覆寫 (Q)取消？[A] ",
		ans, sizeof(ans), LCECHO);

	if (ans[0] == 'q')
	    return;
    }
    if (ans[0] != 'w') // 'a'
    {
	sz = dashs(fp_tmpbuf);
	if (sz > EDIT_SIZE_LIMIT)
	{
	    vmsg("暫存檔已超過大小限制，無法再附加。");
	    return;
	}
    }
    if ((fp = fopen(fp_tmpbuf, (ans[0] == 'w' ? "w" : "a+")))) {
	for (p = curr_buf->firstline; p; p = p->next) {
	    if (p->next || p->data[0])
		edit_fprintf_line(fp, p->data);
	}
	fclose(fp);
    }
}

static void
erase_tmpbuf(void)
{
    char            fp_tmpbuf[80];
    char            ans[4] = "n";

    setuserfile(fp_tmpbuf, ask_tmpbuf(3));
    if (more(fp_tmpbuf, NA) != -1)
	getdata(b_lines - 1, 0, "確定刪除嗎(Y/N)?[N]",
		ans, sizeof(ans), LCECHO);
    if (*ans == 'y')
	unlink(fp_tmpbuf);
}

/**
 * 編輯器自動備份
 *(最多備份 512 行 (?))
 */
void
auto_backup(void)
{
    if (curr_buf == NULL)
	return;

    if (curr_buf->currline) {
	FILE           *fp;
	textline_t     *p, *v;
	char            bakfile[PATHLEN];
	int             count = 0;

	curr_buf->currline = NULL;

	setuserfile(bakfile, fp_bak);
	if ((fp = fopen(bakfile, "w"))) {
	    for (p = curr_buf->firstline; p != NULL && count < 512; p = v, count++) {
		v = p->next;
		edit_fprintf_line(fp, p->data);
		free_line(p);
	    }
	    fclose(fp);
	}
    }
}

/**
 * 取回編輯器備份
 */
void
restore_backup(void)
{
    char            bakfile[80], buf[80], ans[3];

    setuserfile(bakfile, fp_bak);
    while (dashf(bakfile)) {
	vs_hdr("編輯器自動復原");
        mvouts(3, 0, "== 以下為未完成的文章部份內容 ==\n");
        show_file(bakfile, 4, 15, SHOWFILE_ALLOW_NONE);
	getdata(1, 0, "您有一篇文章尚未完成，(S)寫入暫存檔 (Q)算了？[S] ",
		buf, 4, LCECHO);
        if (*buf == 'q') {
            unlink(bakfile);
            break;
        }
        setuserfile(buf, ask_tmpbuf(2));
        if (dashs(buf) > 0) {
            vs_hdr("暫存檔已有內容");
            mvouts(2, 0, "== 以下為暫存檔部份內容 ==\n");
            show_file(buf, 3, 18, SHOWFILE_ALLOW_NONE);
            getdata(1, 0, "選定暫存檔已有下列內容，確定要覆蓋\掉它？ [y/N] ",
                    ans, sizeof(ans), LCECHO);
            if (*ans != 'y')
                continue;
        }
        Rename(bakfile, buf);
    }
}

/* 引用文章 */

static int
garbage_line(const char *str)
{
    int             qlevel = 0;

    while (*str == ':' || *str == '>') {
	if (*(++str) == ' ')
	    str++;
	if (qlevel++ >= 1)
	    return 1;
    }
    while (*str == ' ' || *str == '\t')
	str++;
    if (qlevel >= 1) {
	if (!strncmp(str, "※ ", strlen("※ ")) || !strncmp(str, "==>", 3) ||
	    mbs_strstr(str, ") 提到:\n"))
	    return 1;
    }
    return (*str == '\n');
}

static void
quote_strip_ansi_inline(unsigned char *is)
{
    unsigned char *os = is;

    while (*is)
    {
	if (*is != ESC_CHR)
	    *os++ = *is++;
	else if (is[1] == '*')
	{
	    /* ptt prints, keep it as normal */
	    *os++ = '*';
	    *os++ = '*';
	    is += 2;
	}
	else
	{
	    /* normal ansi, strip them out. */
	    is = (unsigned char *)skip_control_sequence((const char *)is);
	}
    }

    *os = 0;
}

static void
do_quote(void)
{
    int             op;
    char            buf[512];

    getdata(b_lines - 1, 0, "請問要引用原文嗎(Y/N/All/Repost)？[Y] ",
	    buf, 3, LCECHO);
    op = buf[0];

    if (op != 'n') {
	FILE           *inf;

	if ((inf = fopen(quote_file, "r"))) {
	    char           *ptr;
	    int             indent_mode0 = curr_buf->indent_mode;

	    edit_fgets_line(buf, sizeof(buf), inf);
	    if ((ptr = strrchr(buf, ')')))
		ptr[1] = '\0';
	    else if ((ptr = strrchr(buf, '\n')))
		ptr[0] = '\0';

	    if ((ptr = strchr(buf, ':'))) {
		char           *str;

		while (*(++ptr) == ' ');

		/*
		 * 順手牽羊，取得 author's address
		 * 這只有用在 do_post_article() 中偵測回信給外部貼文者。
		 * '.' 前面是什麼不重要，只要 '.' 後面是原作者信箱即可。
		 */
		if ((curr_buf->flags & EDITFLAG_KIND_SENDMAIL) &&
                    (curr_buf->flags & EDITFLAG_KIND_REPLYPOST) &&
                    (str = strchr(quote_user, '.'))) {
		    quote_user[0] = '.';
		    strlcpy(quote_user + 1, ptr, sizeof(quote_user) - 1);
		    strcpy(++str, ptr);
		    str = strchr(quote_user, ' ');
		    if (str)
			str[0] = '\0';
		}
	    } else
		ptr = quote_user;

	    curr_buf->indent_mode = 0;
	    insert_string("※ 引述《");
	    insert_string(ptr);
	    insert_string("》之銘言：\n");

	    if (op != 'a')	/* 去掉 header */
		while (edit_fgets_line(buf, sizeof(buf), inf) && buf[0] != '\n');
	    /* FIXME by MH:
	         如果 header 到內文中間沒有空行分隔，會造成 All 以外的模式
	         都引不到內文。
	     */

	    if (op == 'a')
		while (edit_fgets_line(buf, sizeof(buf), inf)) {
		    insert_char(':');
		    insert_char(' ');
		    quote_strip_ansi_inline((unsigned char *)buf);
		    insert_string(buf);
		}
	    else if (op == 'r')
		while (edit_fgets_line(buf, sizeof(buf), inf)) {
		    /* repost, keep anything */
		    // quote_strip_ansi_inline((unsigned char *)buf);
		    insert_string(buf);
		}
	    else {
                /* 去掉 mail list 之 header */
		if (curr_buf->flags & EDITFLAG_KIND_MAILLIST)
		    while (edit_fgets_line(buf, sizeof(buf), inf) && (!strncmp(buf, "※ ", strlen("※ "))));
		while (edit_fgets_line(buf, sizeof(buf), inf)) {
		    if (!strcmp(buf, "--\n"))
			break;
		    if (!garbage_line(buf)) {
			insert_char(':');
			insert_char(' ');
			quote_strip_ansi_inline((unsigned char *)buf);
			insert_string(buf);
		    }
		}
	    }
	    curr_buf->indent_mode = indent_mode0;
	    fclose(inf);
	}
    }
}

/**
 * 審查 user 引言的使用
 */
static int
check_quote(void)
{
    textline_t *p = curr_buf->firstline;
    char  *str;
    int             post_line;
    int             included_line;

    post_line = included_line = 0;
    while (p) {
	if (!strcmp(str = p->data, "--"))
	    break;
	if (((str[0] == ':') || (str[0] == '>')) && str[1] == ' ')
	    included_line++;
	else {
	    while (*str == ' ' || *str == '\t')
		str++;
	    if (*str)
		post_line++;
	}
	p = p->next;
    }

    if ((included_line >> 2) > post_line) {
	move(4, 0);
	outs("本篇文章的引言比例超過 80%，請您做些微的修正：\n\n"
	     ANSI_COLOR(1;33) "1) 增加一些文章 或  2) 刪除不必要之引言"
             ANSI_RESET "\n");
	{
	    char            ans[4];

	    getdata(12, 12, "(E)繼續編輯 (W)強制寫入？[E] ",
		    ans, sizeof(ans), LCECHO);
	    if (ans[0] == 'w')
		return 0;
	}
	return 1;
    }
    return 0;
}

/* 檔案處理：讀檔、存檔、標題、簽名檔 */
off_t loadsitesig(const char *fname);

static int
read_file(const char *fpath, int splitSig)
{
    FILE  *fp;
    off_t offSig = -1;

    if (splitSig)
	offSig = loadsitesig(fpath);

    if ((fp = fopen(fpath, "r")) == NULL) {
	int fd;
	if ((fd = creat(fpath, DEFAULT_FILE_CREATE_PERM)) >= 0) {
	    close(fd);
	    return 0;
	}

	return -1;
    }
    load_file(fp, offSig);
    fclose(fp);

    return 0;
}

void
write_header(FILE * fp,  const char *mytitle)
{
    assert(mytitle);
    // cross_post may call this without setting curr_buf.
    // TODO Isolate curr_buf so we don't need to hack around.
    char hbuf[WRAPMARGIN];
    if (curr_buf &&
        (curr_buf->flags & (EDITFLAG_KIND_MAILLIST | EDITFLAG_KIND_SENDMAIL)) &&
        !(curr_buf->flags & (EDITFLAG_KIND_NEWPOST | EDITFLAG_KIND_REPLYPOST))) {
	snprintf(hbuf, sizeof(hbuf), "%s %s (%s)", STR_AUTHOR1, cuser.userid,
		 cuser.nickname);
	edit_fprintf_line(fp, hbuf);
    } else {
	const char *ptr = mytitle;
        const char *nickname = cuser.nickname;
	struct {
	    char            author[IDLEN + 1];
	    char            board[IDLEN + 1];
	    char            title[66];
	    time4_t         date;	/* last post's date */
	    int             number;	/* post number */
	}               postlog;

	memset(&postlog, 0, sizeof(postlog));
	STRLCPY(postlog.author, cuser.userid);
	if (curr_buf)
	    curr_buf->ifuseanony = 0;
#ifdef HAVE_ANONYMOUS
	if (currbrdattr & BRD_ANONYMOUS) {
	    int defanony = (currbrdattr & BRD_DEFAULTANONYMOUS);
            char default_name[IDLEN + 1] = "";
            char ans[3];
            int use_userid = 1;

            // dirty hack here... sorry
            if (HAS_ANGEL && HasUserPerm(PERM_ANGEL) && (currbrdattr & BRD_ANGELANONYMOUS)) {
                angel_load_my_fullnick(default_name, sizeof(default_name));
            }

            do {
                getdata_str(3, 0, defanony ?
                    "請輸入你想用的ID，或直接按[Enter]暱名，或按[r][R]用真名：" :
                    "請輸入你想用的ID，也可直接按[Enter]或[r]或[R]使用原ID：",
                    real_name, sizeof(real_name), DOECHO, default_name);

                // sanity checks
                if (real_name[0] == '-') {
                    // names with '-' prefix are considered as 'deleted'.
                    mvouts(4, 0, "抱歉，請勿使用以 - 開頭的名稱。");
                    continue;
                }
                trim(real_name);
                //  defanony:  "" = Anonymous, "r" = cuser.userid.
                // !defanony:  "" "r" = cuser.userid.
                if (strcmp(real_name, "R") == 0)
                    strcpy(real_name, "r");

                if (!*real_name) {
                    if (defanony)
                        STRLCPY(real_name, "Anonymous");
                    else
                        STRLCPY(real_name, "r");
                }

                if (strcmp("r", real_name) == 0)
                    use_userid = 1;
                else
                    use_userid = 0;

                mvprints(3, 0, "使用名稱: %s",
                         use_userid ? cuser.userid : real_name);
                if (getdata(4, 0, "確定[y/N]? ", ans, sizeof(ans), LCECHO) < 1 ||
                    ans[0] != 'y') {
                    move(4, 0); clrtobot();
                    continue;
                }
                break;
            } while (1);

            if (use_userid) {
                STRLCPY(postlog.author, cuser.userid);
            } else {
                SNPRINTF(postlog.author, "%s.", real_name);
                nickname = "猜猜我是誰 ? ^o^";
                if (curr_buf)
                    curr_buf->ifuseanony = 1;
            }
	}
#endif
	STRLCPY(postlog.board, currboard);
        ptr = subject(ptr);
	STRLCPY(postlog.title, ptr);
	postlog.date = now;
	postlog.number = 1;
	append_record(".post", (fileheader_t *) &postlog, sizeof(postlog));
	snprintf(hbuf, sizeof(hbuf), "%s %s (%s) %s %s", STR_AUTHOR1,
		 postlog.author, nickname, STR_POST1, currboard);
	edit_fprintf_line(fp, hbuf);

    }
    snprintf(hbuf, sizeof(hbuf), "標題: %s", mytitle);
    edit_fprintf_line(fp, hbuf);
    snprintf(hbuf, sizeof(hbuf), "時間: %s", ctime4(&now));
    edit_fprintf_line(fp, hbuf);
}

off_t
loadsitesig(const char *fname)
{
    int fd = 0;
    off_t sz = 0, ret = -1;
    char *start, *sp;

    sz = dashs(fname);
    if (sz < 1)
	return -1;
    fd = open(fname, O_RDONLY);
    if (fd < 0)
	return -1;
    start = (char*)mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);
    if (start != MAP_FAILED)
    {
	sp = start + sz - 4 - 1; // 4 = \n--\n
	while (sp > start)
	{
	    if ((*sp == '\n' && strncmp(sp, "\n--\n", 4) == 0) ||
		(*sp == '\r' && strncmp(sp, "\r--\r", 4) == 0) )
	    {
		size_t szSig = sz - (sp-start+1);
		ret = sp - start + 1;
		// allocate string
		free(curr_buf->sitesig_string);
		curr_buf->sitesig_string = (char*) malloc (szSig + 1);
		if (curr_buf->sitesig_string)
		{
		    memcpy(curr_buf->sitesig_string, sp+1, szSig);
		    curr_buf->sitesig_string[szSig] = 0;
		}
		break;
	    }
	    sp --;
	}
	munmap(start, sz);
    }

    close(fd);
    return ret;
}

void
addforwardsignature(FILE *fp, const char *host) {
    char temp[STRLEN];

    if (!host && from_cc[0]) {
	SNPRINTF(temp, "%s %s", FROMHOST, from_cc);
        host = temp;
    } else if (!host) {
        host = FROMHOST;
    }
    char sbuf[WRAPMARGIN];
    syncnow();
    fputc('\n', fp);
    edit_fprintf_line(fp, "※ 發信站: " BBSNAME "(" MYHOSTNAME ")");
    snprintf(sbuf, sizeof(sbuf), "※ 轉錄者: %s (%s), %s",
             cuser.userid, host, Cdatelite(&now));
    edit_fprintf_line(fp, sbuf);
}

void
addsimplesignature(FILE *fp, const char *host) {
    char temp[STRLEN];
    char sbuf[WRAPMARGIN];

    if (!host && from_cc[0]) {
	SNPRINTF(temp, "%s (%s)", FROMHOST, from_cc);
        host = temp;
    } else if (!host) {
        host = FROMHOST;
    }
    fputs("\n--\n", fp);
    snprintf(sbuf, sizeof(sbuf),
             "※ 發信站: " BBSNAME "(" MYHOSTNAME "), 來自: %s", host);
    edit_fprintf_line(fp, sbuf);
}

void
addsignature(FILE * fp, int ifuseanony)
{
    FILE           *fs;
    int             i;
    int             idx_pos;
    char            buf[WRAPMARGIN + 1];
    char            fpath[STRLEN];

    char            ch;

    if (!strcmp(cuser.userid, STR_GUEST)) {
        addsimplesignature(fp, NULL);
	return;
    }
    if (!ifuseanony) {

	int browsing = 0;
	SigInfo	    si;
	memset(&si, 0, sizeof(si));

browse_sigs:
	showsignature(fpath, &idx_pos, &si);

	if (si.total > 0){
	    ch = isdigit(cuser.signature) ? cuser.signature : 'x';
	    getdata(0, 0, TEMPFORMAT(64,
		    (browsing || (si.max > si.show_max))  ?
		    "請選擇簽名檔 (1-9, 0=不加 n=翻頁 x=隨機)[%c]: ":
		    "請選擇簽名檔 (1-9, 0=不加 x=隨機)[%c]: ",
		    ch), buf, 4, LCECHO);

	    if(buf[0] == 'n')
	    {
		si.show_start = si.show_max + 1;
		if(si.show_start > si.max)
		    si.show_start = 0;
		browsing = 1;
		goto browse_sigs;
	    }

	    if (!buf[0])
		buf[0] = ch;

	    if (isdigit((int)buf[0]))
		ch = buf[0];
	    else
		ch = '1' + arc4random_uniform(si.max + 1);
	    pwcuSetSignature(buf[0]);

	    if (ch != '0') {
		fpath[idx_pos] = ch;
		do
		{
		    if ((fs = fopen(fpath, "r"))) {
			fputs("\n--\n", fp);
			for (i = 0; i < MAX_SIGLINES &&
                                    fgets(buf, sizeof(buf), fs); i++) {
                            strip_move_control_sequence(buf);
                            strip_esc_star(buf);
			    fputs(buf, fp);
                        }
			fclose(fs);
			fpath[idx_pos] = ch;
		    }
		    else
			fpath[idx_pos] = '1' + (fpath[idx_pos] - '1' + 1) % (si.max+1);
		} while (!isdigit((int)buf[0]) && si.max > 0 && ch != fpath[idx_pos]);
	    }
	}
    }
#ifdef HAVE_ORIGIN
#ifdef HAVE_ANONYMOUS
    if (ifuseanony)
        addsimplesignature(fp, "匿名天使的家");
    else
#endif
    {
        addsimplesignature(fp, NULL);
    }
#endif
}

#ifdef USE_POST_ENTROPY
static int
get_string_entropy(const char *s)
{
    int ent = 0;
    while (*s)
    {
	char c = *s++;
	if (!isascii(c) || isalnum(c))
	    ent++;
    }
    return ent;
}
#endif

#ifdef EXP_EDIT_UPLOAD
static void upload_file(void);
#endif // EXP_EDIT_UPLOAD

// return	EDIT_ABORTED	if aborted
// 		KEEP_EDITING	if keep editing
// 		0		if write ok & exit
static int
write_file(const char *fpath, int saveheader, char mytitle[STRLEN],
           int flags, int *pentropy)
{
    FILE           *fp = NULL;
    textline_t     *p;
    char            ans[TTLEN], *msg;
    int             aborted = 0;
    int             entropy = 0;

    int upload GCC_UNUSED = (flags & EDITFLAG_UPLOAD) ? 1 : 0;
    int chtitle = (flags & EDITFLAG_ALLOWTITLE) ? 1 : 0;
    const char *kind_prompt = get_edit_kind_prompt(flags);
    const char *warn_prompt = get_edit_warn_prompt(flags);

    assert(!chtitle || mytitle);
    vs_hdr("檔案處理");
    move(1,0);

#ifdef EDIT_UPLOAD_ALLOWALL
    upload = 1;
#endif // EDIT_UPLOAD_ALLOWALL

    {
        const char *msgSave = "儲存";

        if (flags & (EDITFLAG_KIND_NEWPOST |
                     EDITFLAG_KIND_REPLYPOST)) {
            msgSave = "發文";
        } else if (flags & (EDITFLAG_KIND_SENDMAIL |
                            EDITFLAG_KIND_MAILLIST)) {
            msgSave = "發信";
        }

        // common trail
        prints("[S]%s", msgSave);
    }

#ifdef EXP_EDIT_UPLOAD
    if (upload)
	outs(" (U)上傳資料");
#endif // EXP_EDIT_UPLOAD

    if (chtitle)
	outs(" (T)改標題");

    outs(" (A)放棄 (E)繼續 (R/W/D)讀寫刪暫存檔");

    // TODO FIXME what if prompt is multiline?
    if (kind_prompt)
	mvouts(4, 0, kind_prompt);
    if (warn_prompt)
	mvouts(6, 0, warn_prompt);
    getdata(2, 0, "確定要儲存檔案嗎？ ", ans, 2, LCECHO);

    // avoid lots pots
    if (ans[0] != 'a')
	sleep(1);

    switch (ans[0]) {
    case 'a':
	outs("文章" ANSI_COLOR(1) " 沒有 " ANSI_RESET "存入");
	aborted = EDIT_ABORTED;
	break;
    case 'e':
	return KEEP_EDITING;
#ifdef EXP_EDIT_UPLOAD
    case 'u':
	if (upload)
	    upload_file();
	return KEEP_EDITING;
#endif // EXP_EDIT_UPLOAD
    case 'r':
	read_tmpbuf(-1);
	return KEEP_EDITING;
    case 'w':
	write_tmpbuf();
	return KEEP_EDITING;
    case 'd':
	erase_tmpbuf();
	return KEEP_EDITING;
    case 't':
	if (!chtitle)
	    return KEEP_EDITING;
	move(3, 0);
	prints("舊標題：%s", mytitle);
	STRLCPY(ans, mytitle);
	if (getdata_buf(4, 0, "新標題：", ans, sizeof(ans), DOECHO))
	    strlcpy(mytitle, ans, STRLEN);
	return KEEP_EDITING;
    case 's':
        break;
    }

    if (!aborted) {

	if (saveheader && !(curr_buf->flags & EDITFLAG_KIND_SENDMAIL) &&
            check_quote())
	    return KEEP_EDITING;

	assert(*fpath);
	if ((fp = fopen(fpath, "w")) == NULL) {
	    assert(fp);
	    abort_bbs(0);
	}
	if (saveheader)
	    write_header(fp, mytitle);
    }
    if (!aborted) {
	for (p = curr_buf->firstline; p; p = p->next) {
	    msg = p->data;

	    if (p->next == NULL && !msg[0]) // ignore lastline is empty
		continue;

	    trim(msg);
            strip_move_control_sequence(msg);

#ifdef USE_POST_ENTROPY
	    // calculate the real content of msg
	    if (entropy < ENTROPY_MAX)
		entropy += get_string_entropy(msg);
	    else
#endif
		entropy = ENTROPY_MAX;
	    // write the message body
	    edit_fprintf_line(fp, msg);
	}
    }
    curr_buf->currline = NULL;

    *pentropy = entropy;
    if (aborted)
	return aborted;

    if (curr_buf->sitesig_string)
	fputs(curr_buf->sitesig_string, fp);

    if (currstat == POSTING || currstat == SMAIL)
    {
	addsignature(fp, curr_buf->ifuseanony);
    }
    else if (currstat == REEDIT)
    {
	if (HAS_ALL_REEDIT_LOG || strcmp(currboard, BN_SYSOP) == 0)
	{
	    char sbuf[WRAPMARGIN];
	    snprintf(sbuf, sizeof(sbuf),
		     "※ 編輯: %s (%s%s%s), %s",
		     cuser.userid, FROMHOST, from_cc[0] ? " " : "", from_cc,
		     Cdatelite(&now));
	    edit_fprintf_line(fp, sbuf);
	}
    }

    fclose(fp);
    return 0;
}

static int
has_block_selection(void)
{
    return curr_buf->blockln >= 0;
}

/**
 * a block is continual lines of the article.
 */

/**
 * stop the block selection.
 */
static void
block_cancel(void)
{
    if (has_block_selection()) {
	curr_buf->blockln = -1;
	curr_buf->blockline = NULL;
	curr_buf->redraw_everything = YEA;
    }
}

static void
setup_block_begin_end(textline_t **begin, textline_t **end)
{
    if (curr_buf->currln >= curr_buf->blockln) {
	*begin = curr_buf->blockline;
	*end = curr_buf->currline;
    } else {
	*begin = curr_buf->currline;
	*end = curr_buf->blockline;
    }
}

#define BLOCK_TRUNCATE	0
#define BLOCK_APPEND	1
/**
 * save the selected block to file 'fname.'
 * mode: BLOCK_TRUNCATE  truncate mode
 *       BLOCK_APPEND    append mode
 */
static void
block_save_to_file(const char *fname, int mode)
{
    textline_t *begin, *end;
    char fp_tmpbuf[80];
    FILE *fp;

    if (!has_block_selection())
	return;

    setup_block_begin_end(&begin, &end);

    setuserfile(fp_tmpbuf, fname);
    if ((fp = fopen(fp_tmpbuf, mode == BLOCK_APPEND ? "a+" : "w+"))) {

	textline_t *p;

	for (p = begin; p != end; p = p->next)
	    edit_fprintf_line(fp, p->data);
	edit_fprintf_line(fp, end->data);
	fclose(fp);
    }
}

/**
 * delete selected block
 */
static void
block_delete(void)
{
    textline_t *begin, *end;
    textline_t *p;

    if (!has_block_selection())
	return;

    setup_block_begin_end(&begin, &end);

    // the block region is (currln, block) or (blockln, currln).

    textline_t *pnext;
    int deleted_line_count = abs(curr_buf->currln - curr_buf->blockln) + 1;
    if (begin->prev)
	begin->prev->next = end->next;
    if (end->next)
	end->next->prev = begin->prev;

    if (curr_buf->currln > curr_buf->blockln) {
	// case (blockln, currln)
	curr_buf->currln = curr_buf->blockln;
	curr_buf->curr_window_line -= deleted_line_count - 1;
    } else {
	// case (currln, blockln)
    }
    curr_buf->totaln -= deleted_line_count;

    curr_buf->currline = end->next;
    if (curr_buf->currline == NULL) {
	curr_buf->currline = begin->prev;
	curr_buf->currln--;
	curr_buf->curr_window_line--;
    }
    if (curr_buf->currline == NULL) {
	assert(curr_buf->currln == -1);
	assert(curr_buf->totaln == -1);

	curr_buf->currline = alloc_line(WRAPMARGIN);
	curr_buf->currln++;
	curr_buf->totaln++;
    }

    // maintain special line pointer
    if (curr_buf->firstline == begin)
	curr_buf->firstline = curr_buf->currline;
    if (curr_buf->lastline == end)
	curr_buf->lastline = curr_buf->currline;
    if (curr_buf->curr_window_line <= 0) {
	curr_buf->curr_window_line = 0;
	curr_buf->top_of_win = curr_buf->currline;
    }

    // remove buffer
    end->next = NULL;
    for (p = begin; p; p = pnext) {
	pnext = p->next;
	free_line(p);
	deleted_line_count--;
    }
    assert(deleted_line_count == 0);

    curr_buf->currpnt = 0;

    block_cancel();
}

static void
block_cut(void)
{
    if (!has_block_selection())
	return;

    block_save_to_file("buf.0", BLOCK_TRUNCATE);
    block_delete();
}

static void
block_copy(void)
{
    if (!has_block_selection())
	return;

    block_save_to_file("buf.0", BLOCK_TRUNCATE);

    block_cancel();
}

static void
block_prompt(void)
{
    char fp_tmpbuf[80];
    char tmpfname[] = "buf.0";
    char mode[2] = "w";
    char choice[2];

    move(b_lines - 1, 0);
    clrtoeol();

    if (!getdata(b_lines - 1, 0, "把區塊移至暫存檔 (0:Cut, 5:Copy, 6-9, q: Cancel)[0] ", choice, sizeof(choice), LCECHO))
	choice[0] = '0';

    if (choice[0] < '0' || choice[0] > '9')
	goto cancel_block;

    if (choice[0] == '0') {
	block_cut();
	return;
    }
    else if (choice[0] == '5') {
	block_copy();
	return;
    }

    tmpfname[4] = choice[0];
    setuserfile(fp_tmpbuf, tmpfname);
    if (dashf(fp_tmpbuf)) {
	more(fp_tmpbuf, NA);
	getdata(b_lines - 1, 0, "暫存檔已有資料 (A)附加 (W)覆寫 (Q)取消？[W] ", mode, sizeof(mode), LCECHO);
	if (mode[0] == 'q')
	    goto cancel_block;
	else if (mode[0] != 'a')
	    mode[0] = 'w';
    }

    block_save_to_file(tmpfname, mode[0] == 'a' ? BLOCK_APPEND : BLOCK_TRUNCATE);

    if (vans("刪除區塊(Y/N)?[N] ") == 'y') {
	block_delete();
	return;
    }

cancel_block:
    block_cancel();
}

static void
block_select(void)
{
    curr_buf->blockln = curr_buf->currln;
    curr_buf->blockline = curr_buf->currline;
}

/////////////////////////////////////////////////////////////////////////////
// Syntax Highlight

#define PMORE_USE_ASCII_MOVIE // disable this if you don't enable ascii movie
#define ENABLE_PMORE_ASCII_MOVIE_SYNTAX // disable if you don't want rich colour syntax

#ifdef PMORE_USE_ASCII_MOVIE
// pmore movie header support
unsigned char *
    mf_movieFrameHeader(unsigned char *p, unsigned char *end);

#endif // PMORE_USE_ASCII_MOVIE

enum {
    EOATTR_NORMAL   = 0x00,
    EOATTR_SELECTED = 0x01,	// selected (reverse)
    EOATTR_MOVIECODE= 0x02,	// pmore movie
    EOATTR_BBSLUA   = 0x04,	// BBS Lua (header)
    EOATTR_COMMENT  = 0x08,	// comment syntax

};

static const char * const luaKeywords[] = {
    "and",   "break", "do",  "else", "elseif",
    "end",   "for",   "if",  "in",   "not",  "or",
    "repeat","return","then","until","while",
    NULL
};

static const char * const luaDataKeywords[] = {
    "false", "function", "local", "nil", "true",
    NULL
};

static const char * const luaFunctions[] = {
    "assert", "print", "tonumber", "tostring", "type",
    NULL
};

static const char * const luaMath[] = {
    "abs", "acos", "asin", "atan", "atan2", "ceil", "cos", "cosh", "deg",
    "exp", "floor", "fmod", "frexp", "ldexp", "log", "log10", "max", "min",
    "modf", "pi", "pow", "rad", "random", "randomseed", "sin", "sinh",
    "sqrt", "tan", "tanh",
    NULL
};

static const char * const luaTable[] = {
    "concat", "insert", "maxn", "remove", "sort",
    NULL
};

static const char * const luaString[] = {
    "byte", "char", "dump", "find", "format", "gmatch", "gsub", "len",
    "lower", "match", "rep", "reverse", "sub", "upper", NULL
};

static const char * const luaBbs[] = {
    "ANSI_COLOR", "ANSI_RESET", "ESC", "addstr", "clear", "clock",
    "clrtobot", "clrtoeol", "color", "ctime", "getch","getdata",
    "getmaxyx", "getstr", "getyx", "interface", "kball", "kbhit", "kbreset",
    "move", "moverel", "now", "outs", "pause", "print", "rect", "refresh",
    "setattr", "sitename", "sleep", "strip_ansi", "time", "title",
    "userid", "usernick",
    NULL
};

static const char * const luaToc[] = {
    "author", "date", "interface", "latestref",
    "notes", "title", "version",
    NULL
};

static const char * const luaBit[] = {
    "arshift", "band", "bnot", "bor", "bxor", "cast", "lshift", "rshift",
    NULL
};

static const char * const luaStore[] = {
    "USER", "GLOBAL", "iolimit", "limit", "load", "save",
    NULL
};

static const char * const luaLibs[] = {
    "bbs", "bit", "math", "store", "string", "table", "toc",
    NULL
};
static const char* const * const luaLibAPI[] = {
    luaBbs, luaBit, luaMath, luaStore, luaString, luaTable, luaToc,
    NULL
};

int synLuaKeyword(const char *text, int n, char *wlen)
{
    int i = 0;
    const char * const *tbl = NULL;
    if (*text >= 'A' && *text <= 'Z')
    {
	// normal identifier
	while (n-- > 0 && (isalnum(*text) || *text == '_'))
	{
	    text++;
	    (*wlen) ++;
	}
	return 0;
    }
    if (*text >= '0' && *text <= '9')
    {
	// digits
	while (n-- > 0 && (isdigit(*text) || *text == '.' || *text == 'x'))
	{
	    text++;
	    (*wlen) ++;
	}
	return 5;
    }
    if (*text == '#')
    {
	text++;
	(*wlen) ++;
	// length of identifier
	while (n-- > 0 && (isalnum(*text) || *text == '_'))
	{
	    text++;
	    (*wlen) ++;
	}
	return -2;
    }

    // ignore non-identifiers
    if (!(*text >= 'a' && *text <= 'z'))
	return 0;

    // 1st, try keywords
    for (i = 0; luaKeywords[i] && *text >= *luaKeywords[i]; i++)
    {
	int l = strlen(luaKeywords[i]);
	if (n < l)
	    continue;
	if (isalnum(text[l]))
	    continue;
	if (strncmp(text, luaKeywords[i], l) == 0)
	{
	    *wlen = l;
	    return 3;
	}
    }
    for (i = 0; luaDataKeywords[i] && *text >= *luaDataKeywords[i]; i++)
    {
	int l = strlen(luaDataKeywords[i]);
	if (n < l)
	    continue;
	if (isalnum(text[l]))
	    continue;
	if (strncmp(text, luaDataKeywords[i], l) == 0)
	{
	    *wlen = l;
	    return 2;
	}
    }
    for (i = 0; luaFunctions[i] && *text >= *luaFunctions[i]; i++)
    {
	int l = strlen(luaFunctions[i]);
	if (n < l)
	    continue;
	if (isalnum(text[l]))
	    continue;
	if (strncmp(text, luaFunctions[i], l) == 0)
	{
	    *wlen = l;
	    return 6;
	}
    }
    for (i = 0; luaLibs[i]; i++)
    {
	int l = strlen(luaLibs[i]);
	if (n < l)
	    continue;
	if (text[l] != '.' && text[l] != ':')
	    continue;
	if (strncmp(text, luaLibs[i], l) == 0)
	{
	    *wlen = l+1;
	    text += l; text ++;
	    n -= l; n--;
	    break;
	}
    }

    tbl = luaLibAPI[i];
    if (!tbl)
    {
	// calcualte wlen
	while (n-- > 0 && (isalnum(*text) || *text == '_'))
	{
	    text++;
	    (*wlen) ++;
	}
	return 0;
    }

    for (i = 0; tbl[i]; i++)
    {
	int l = strlen(tbl[i]);
	if (n < l)
	    continue;
	if (isalnum(text[l]))
	    continue;
	if (strncmp(text, tbl[i], l) == 0)
	{
	    *wlen += l;
	    return 6;
	}
    }
    // luaLib. only
    return -6;
}

void syn_pmore_render(char *os, int len, char *buf)
{
    // XXX buf should be same length as s.
    char *s = (char *)mf_movieFrameHeader((unsigned char*)os, (unsigned char*)os + len);
    char attr = 1;
    char prefix = 0;

    memset(buf, 0, len);
    if (!len || !s) return;

    // render: frame header
    memset(buf, attr++, (s - os));
    len -= s - os;
    buf += s - os;

    while (len-- > 0)
    {
	switch (*s++)
	{
	    case 'P':
	    case 'E':
		*buf++ = attr++;
		return;

	    case 'S':
		*buf++ = attr++;
		continue;

	    case '0': case '1': case '2': case '3':
	    case '4': case '5': case '6': case '7':
	    case '8': case '9': case '.':
		*buf++ = attr;
		while (len > 0 && isascii(*s) &&
			(isalnum(*s) || *s == '.') )
		{
		    *buf++ = attr;
		    len--; s++;
		}
		return;

	    case '#':
		*buf++ = attr++;
		while (len > 0)
		{
		    *buf++ = attr;
		    if (*s == '#') attr++;
		    len--; s++;
		}
		return;

	    case ':':
		*buf++ = attr;
		while (len > 0 && isascii(*s) &&
			(isalnum(*s) || *s == ':') )
		{
		    *buf++ = attr;
		    len--;
		    if (*s++ == ':') break;
		}
		attr++;
		continue;

	    case 'I':
	    case 'G':
		*buf++ = attr++;
		prefix = 1;
		while (len > 0 &&
			( (isascii(*s) && isalnum(*s)) ||
			  strchr("+-,:lpf", *s)) )
		{
		    if (prefix)
		    {
			if (!strchr(":lpf", *s))
			    break;
			prefix = 0;
		    }
		    *buf++ = attr;
		    if (*s == ',')
		    {
			attr++;
			prefix = 1;
		    }
		    s++; len--;
		}
		attr++;
		return;

	    case 'K':
		*buf++ = attr;
		if (*s != '#')
		    return;
		*buf++ = attr; s++; len--; // #
		while (len >0)
		{
		    *buf++ = attr;
		    len--;
		    if (*s++ == '#') break;
		}
		attr++;
		continue;


	    default: // unknown
		return;
	}
    }
}

/**
 * Just like outs, but print out '*' instead of 27(decimal) in the given string.
 *
 * FIXME column could not start from 0
 */

static void
edit_outs_attr_n(const char *text, int n, int attr)
{
    int    column = 0;
    unsigned char inAnsi = 0;
    unsigned char ch;
    int doReset = 0;
    const char *reset = ANSI_RESET;

    // syntax attributes
    char fComment = 0,
	 fSingleQuote = 0,
	 fDoubleQuote = 0,
	 fSquareQuote = 0,
	 fWord = 0;

    // movie syntax rendering
#ifdef ENABLE_PMORE_ASCII_MOVIE_SYNTAX
    char movie_attrs[WRAPMARGIN+10] = {0};
    char *pmattr = movie_attrs, mattr = 0;
#endif

#ifdef COLORED_SELECTION
    if ((attr & EOATTR_SELECTED) &&
	(attr & ~EOATTR_SELECTED))
    {
	reset = ANSI_COLOR(0;7;36);
	doReset = 1;
	outs(reset);
    }
    else
#endif // if not defined, color by  priority - selection first
    if (attr & EOATTR_SELECTED)
    {
	reset = ANSI_COLOR(0;7);
	doReset = 1;
	outs(reset);
    }
    else if (attr & EOATTR_MOVIECODE)
    {
	reset = ANSI_COLOR(0;36);
	doReset = 1;
	outs(reset);
#ifdef ENABLE_PMORE_ASCII_MOVIE_SYNTAX
	syn_pmore_render((char*)text, n, movie_attrs);
#endif
    }
    else if (attr & EOATTR_BBSLUA)
    {
	reset = ANSI_COLOR(0;1;31);
	doReset = 1;
	outs(reset);
    }
    else if (attr & EOATTR_COMMENT)
    {
	reset = ANSI_COLOR(0;1;34);
	doReset = 1;
	outs(reset);
    }

    /* 0 = N/A, 1 = leading byte printed, 2 = ansi in middle */
    unsigned char isDBCS = 0;
    const char *ansi_end = NULL;

    while ((ch = *text++) && (++column < t_columns) && n-- > 0)
    {
#ifdef ENABLE_PMORE_ASCII_MOVIE_SYNTAX
	mattr = *pmattr++;
#endif

	if(inAnsi == 1)
	{
	    if(ch == ESC_CHR) {
		outc('*');
		ansi_end = skip_control_sequence((const char *)(text - 1));
		if ((const char *)text >= ansi_end) {
		    inAnsi = 0;
		    outs(reset);
		}
	    }
	    else
	    {
		outc(ch);

		if ((const char *)text >= ansi_end)
		{
		    inAnsi = 0;
		    outs(reset);
		}
	    }

	}
	else if(ch == ESC_CHR)
	{
	    ansi_end = skip_control_sequence((const char *)(text - 1));
	    inAnsi = ((const char *)text < ansi_end);
	    if(isDBCS == 1)
	    {
		isDBCS = 2;
		outs(ANSI_COLOR(1;33) "?");
		outs(reset);
	    }
	    outs(ANSI_COLOR(1) "*");
	    if (!inAnsi)
		outs(reset);
	}
	else
	{
	    if (MB_IS_UTF8) {
		if (ch >= 0x80) {
		    int w = mb_bytes(text - 1);
		    int cw = mb_width(text - 1);
		    if (column + (cw - 1) >= t_columns || n < w - 1)
			break;
		    outc(ch);
		    for (int k = 1; k < w; k++) {
			outc(*text++);
			n--;
#ifdef ENABLE_PMORE_ASCII_MOVIE_SYNTAX
			pmattr++;
#endif
		    }
		    column += cw - 1;
		    continue;
		}
	    } else {
#ifdef DBCSAWARE
	    if(isDBCS == 1)
		isDBCS = 0;
	    else if (isDBCS == 2)
	    {
		/* ansi in middle. */
		outs(ANSI_COLOR(0;33) "?");
		outs(reset);
		isDBCS = 0;
		continue;
	    }
	    else
		if(IS_BIG5_HI(ch))
		{
		    isDBCS = 1;
		    // peak next char
		    if(n > 0 && *text == ESC_CHR)
			continue;
		}
#endif
	    }

	    // Lua Parser!
	    if (!attr && curr_buf->synparser && !fComment)
	    {
		// syntax highlight!
		if (fSquareQuote) {
		    if (ch == ']' && n > 0 && *(text) == ']')
		    {
			fSquareQuote = 0;
			doReset = 0;
			// directly print quotes
			outc(ch); outc(ch);
			text++, n--;
			outs(ANSI_RESET);
			continue;
		    }
		} else if (fSingleQuote) {
		    if (ch == '\'')
		    {
			fSingleQuote = 0;
			doReset = 0;
			// directly print quotes
			outc(ch);
			outs(ANSI_RESET);
			continue;
		    }
		} else if (fDoubleQuote) {
		    if (ch == '"')
		    {
			fDoubleQuote = 0;
			doReset = 0;
			// directly print quotes
			outc(ch);
			outs(ANSI_RESET);
			continue;
		    }
		} else if (ch == '-' && n > 0 && *(text) == '-') {
		    fComment = 1;
		    doReset = 1;
		    outs(ANSI_COLOR(0;1;34));
		} else if (ch == '[' && n > 0 && *(text) == '[') {
		    fSquareQuote = 1;
		    doReset = 1;
		    fWord = 0;
		    outs(ANSI_COLOR(1;35));
		} else if (ch == '\'' || ch == '"') {
		    if (ch == '"')
			fDoubleQuote = 1;
		    else
			fSingleQuote = 1;
		    doReset = 1;
		    fWord = 0;
		    outs(ANSI_COLOR(1;35));
		} else {
		    // normal words
		    if (fWord)
		    {
			// inside a word.
			if (--fWord <= 0){
			    fWord = 0;
			    doReset = 0;
			    outc(ch);
			    outs(ANSI_RESET);
			    continue;
			}
		    } else if (isalnum(tolower(ch)) || ch == '#') {
			char attr[] = ANSI_COLOR(0;1;37);
			int x = synLuaKeyword(text-1, n+1, &fWord);
			if (fWord > 0)
			    fWord --;
			if (x != 0)
			{
			    // sorry, fixed string here.
			    // 7 = *[0;1;3?
			    if (x<0) {  attr[4] = '0'; x= -x; }
			    attr[7] = '0' + x;
			    outs(attr);
			    doReset = 1;
			}
			if (!fWord)
			{
			    outc(ch);
			    outs(ANSI_RESET);
			    doReset = 0;
			    continue;
			}
		    }
		}
	    }
	    outc(ch);

#ifdef ENABLE_PMORE_ASCII_MOVIE_SYNTAX
	    // pmore Movie Parser!
	    if (attr & EOATTR_MOVIECODE)
	    {
		// only render when attribute was changed.
		if (mattr != *pmattr)
		{
		    if (*pmattr)
		    {
			prints(ANSI_COLOR(1;3%d),
				8 - ((mattr-1) % 7+1) );
		    } else {
			outs(ANSI_RESET);
		    }
		}
	    }
#endif // ENABLE_PMORE_ASCII_MOVIE_SYNTAX
	}
    }

    // this must be ANSI_RESET, not "reset".
    if(inAnsi || doReset)
	outs(ANSI_RESET);
}

static void
edit_outs_attr(const char *text, int attr)
{
    edit_outs_attr_n(text, strlen(text), attr);
}

static void
edit_ansi_outs_n(const char *str, int n, int attr GCC_UNUSED)
{
    char c;
    while (n-- > 0 && (c = *str++)) {
	if(c == ESC_CHR && *str == '*')
	{
	    // ptt prints
	    /* Because moving within ptt_prints is too hard
	     * let's just display it as-is.
	     */
	    outc('*');
	} else {
	    outc(c);
	}
    }
}

static void
edit_ansi_outs(const char *str, int attr)
{
    return edit_ansi_outs_n(str, strlen(str), attr);
}

static int
detect_attr(const char *ps, size_t len)
{
    int attr = 0;

#ifdef PMORE_USE_ASCII_MOVIE
    if (mf_movieFrameHeader((unsigned char*)ps, (unsigned char*)ps+len))
	attr |= EOATTR_MOVIECODE;
#endif
#ifdef USE_BBSLUA
    if (bbslua_isHeader(ps, ps + len))
    {
	attr |= EOATTR_BBSLUA;
	if (!curr_buf->synparser)
	{
	    curr_buf->synparser = 1;
	    // if you need indent, toggle by hotkey.
	    // enabling indent by default may cause trouble to copy pasters
	    // curr_buf->indent_mode = 1;
	}
    }
#endif
    return attr;
}

static void
display_textline_internal(textline_t *p, int i)
{
    short tmp;
    void (*output)(const char *, int)	    = edit_outs_attr;

    int attr = EOATTR_NORMAL;

    move(i, 0);
    clrtoeol();

    if (!p) {
	outc('~');
	outs(ANSI_CLRTOEND);
	return;
    }

    if (curr_buf->ansimode) {
	output = edit_ansi_outs;
    }

    tmp = curr_buf->currln - curr_buf->curr_window_line + i;

    // parse attribute of line

    // selected attribute?
    if (has_block_selection() &&
	    ( (curr_buf->blockln <= curr_buf->currln &&
	       curr_buf->blockln <= tmp && tmp <= curr_buf->currln) ||
	      (curr_buf->currln <= tmp && tmp <= curr_buf->blockln)) )
    {
	// outs(ANSI_REVERSE); // remove me when EOATTR is ready...
	attr |= EOATTR_SELECTED;
    }

    attr |= detect_attr(p->data, p->len);

    if (curr_buf->edit_margin > 0) {
	int pos = line_col_to_pos(p, curr_buf->edit_margin, 0);
	if (pos >= p->len) {
	    (*output)("", attr);
	} else {
	    int col = line_pos_to_col(p, pos, 0);
	    const char *pdata = p->data + pos;
	    if (col < curr_buf->edit_margin) {
		outs(ANSI_COLOR(1) "<" ANSI_RESET);
		pdata += mb_bytes(pdata);
	    }
	    (*output)(pdata, attr);
	}
    } else {
	(*output)(p->data, attr);
    }

    if (attr)
	outs(ANSI_RESET);

    // workaround poor terminal
    outs(ANSI_CLRTOEND);
}

static void
refresh_window(void)
{
    textline_t *p;
    int    i;

    for (p = curr_buf->top_of_win, i = 0; i < visible_window_height(); i++) {
	display_textline_internal(p, i);

	if (p)
	    p = p->next;
    }
    edit_msg();
}

static void
goto_line(int lino)
{
    if (lino > 0 && lino <= curr_buf->totaln + 1) {
	textline_t     *p;

	p = curr_buf->firstline;
	curr_buf->currln = lino - 1;

	while (--lino && p->next)
	    p = p->next;

	if (p)
	    curr_buf->currline = p;
	else {
	    curr_buf->currln = curr_buf->totaln;
	    curr_buf->currline = curr_buf->lastline;
	}

	curr_buf->currpnt = 0;

	edit_window_adjust_middle();
    }
    curr_buf->redraw_everything = YEA;
}

static void
prompt_goto_line(void)
{
    char buf[10];

    if (getdata(b_lines - 1, 0, "跳至第幾行:", buf, sizeof(buf), DOECHO))
	goto_line(atoi(buf));
}

/**
 * search string interactively.
 * @param mode 0: prompt
 *             1: forward
 *            -1: backward
 */
static void
search_str(int mode)
{
    const int max_keyword = 65;
    char *str;
    char            ans[4] = "n";

    if (curr_buf->searched_string == NULL) {
	if (mode != 0)
	    return;
	curr_buf->searched_string = (char *)malloc(max_keyword * sizeof(char));
	curr_buf->searched_string[0] = 0;
    }

    str = curr_buf->searched_string;

    if (!mode) {
	if (getdata_buf(b_lines - 1, 0, "[搜尋]關鍵字:",
			str, max_keyword, DOECHO))
	    if (*str) {
		if (getdata(b_lines - 1, 0, "區分大小寫(Y/N/Q)? [N] ",
			    ans, sizeof(ans), LCECHO) && *ans == 'y')
		    curr_buf->substr_fp = strstr;
		else
		    curr_buf->substr_fp = strcasestr;
	    }
    }
    if (*str && *ans != 'q') {
	textline_t     *p;
	char           *pos = NULL;
	int             lino;
	bool found = false;

	if (mode >= 0) {
	    for (lino = curr_buf->currln, p = curr_buf->currline; p; p = p->next, lino++) {
		int offset = (lino == curr_buf->currln ? MIN(curr_buf->currpnt + 1, curr_buf->currline->len) : 0);
		pos = (*curr_buf->substr_fp)(p->data + offset, str);
		if (pos) {
		    found = true;
		    break;
		}
	    }
	} else {
	    for (lino = curr_buf->currln, p = curr_buf->currline; p; p = p->prev, lino--) {
		pos = (*curr_buf->substr_fp)(p->data, str);
		if (pos &&
		    (lino != curr_buf->currln || pos - p->data < curr_buf->currpnt)) {
		    found = true;
		    break;
		}
	    }
	}
	if (found) {
	    /* move window */
	    curr_buf->currline = p;
	    curr_buf->currln = lino;
	    curr_buf->currpnt = pos - p->data;

	    edit_window_adjust_middle();
	}
    }
    if (!mode)
	curr_buf->redraw_everything = YEA;
}

/**
 * move the cursor from bracket to corresponding bracket.
 */
static void
match_paren(void)
{
    char           *parens = "()[]{}";
    char           *ptype;
    textline_t     *p;
    int             lino;
    int             i = 0;
    bool found = false;
    char findch;
    char quotech = '\0';
    enum MatchState {
	MATCH_STATE_NORMAL,
	MATCH_STATE_C_COMMENT,
	MATCH_STATE_QUOTE
    };
    enum MatchState state = MATCH_STATE_NORMAL;
    int dir;
    int nested = 0;

    char cursorch = curr_buf->currline->data[curr_buf->currpnt];
    if (cursorch == '\0')
	return;
    if (!(ptype = strchr(parens, cursorch)))
	return;

    dir = (ptype - parens) % 2 == 0 ? 1 : -1;
    findch = *(ptype + dir);

    p = curr_buf->currline;
    lino = curr_buf->currln;
    i = curr_buf->currpnt;
    while (p && !found) {
	// next position
	i += dir;
	while (p && i < 0) {
	    p = p->prev;
	    if (p)
		i = p->len - 1;
	    lino--;
	}
	while (p && i >= p->len) {
	    p = p->next;
	    i = 0;
	    lino++;
	}
	if (!p)
	    break;
	assert(0 <= i && i < p->len);

	// match char
	switch (state) {
	    case MATCH_STATE_NORMAL:
		if (nested == 0 && p->data[i] == findch) {
		    found = true;
		    break;
		}
		if (p->data[i] == cursorch)
		    nested++;
		else if (p->data[i] == findch)
		    nested--;
		if (p->data[i] == '\'' || p->data[i] == '"') {
		    quotech = p->data[i];
		    state = MATCH_STATE_QUOTE;
		} else if ((i+dir) >= 0 && p->data[i] == '/' && p->data[i+dir] == '*') {
		    state = MATCH_STATE_C_COMMENT;
		    i += dir;
		}
		break;
	    case MATCH_STATE_C_COMMENT:
		if ((i+dir) >= 0 && p->data[i] == '*' && p->data[i+dir] == '/') {
		    state = MATCH_STATE_NORMAL;
		    i += dir;
		}
		break;
	    case MATCH_STATE_QUOTE:
		if (p->data[i] == quotech) {
		    if (i==0 || p->data[i-1] != '\\')
			state = MATCH_STATE_NORMAL;
		}
		break;
	}
    }
    if (found) {
	int             top = curr_buf->currln - curr_buf->curr_window_line;
	int             bottom = top + visible_window_height() - 1;

	assert(p);
	curr_buf->currpnt = i;
	curr_buf->currline = p;
	curr_buf->curr_window_line += lino - curr_buf->currln;
	curr_buf->currln = lino;

	if (lino < top || lino > bottom) {
	    edit_window_adjust_middle();
	}
    }
}

static void
currline_shift_left(void)
{
    int currpnt0;

    if (curr_buf->currline->len <= 0)
	return;

    currpnt0 = curr_buf->currpnt;
    curr_buf->currpnt = 0;
    delete_char();
    curr_buf->currpnt = (currpnt0 <= curr_buf->currline->len) ? currpnt0 : currpnt0 - 1;
    if (curr_buf->ansimode)
	curr_buf->currpnt = ansi2n(n2ansi(curr_buf->currpnt, curr_buf->currline), curr_buf->currline);
}

static void
currline_shift_right(void)
{
    int currpnt0;

    if (curr_buf->currline->len >= WRAPMARGIN - 1)
	return;

    currpnt0 = curr_buf->currpnt;
    curr_buf->currpnt = 0;
    insert_char(' ');
    curr_buf->currpnt = currpnt0;
}

static void
cursor_to_next_word(void)
{
    while (curr_buf->currpnt < curr_buf->currline->len &&
	    isalnum((unsigned char)curr_buf->currline->data[curr_buf->currpnt]))
	curr_buf->currpnt++;
    while (curr_buf->currpnt < curr_buf->currline->len &&
	    !isalnum((unsigned char)curr_buf->currline->data[curr_buf->currpnt]))
	curr_buf->currpnt++;
}

static void
cursor_to_prev_word(void)
{
    while (curr_buf->currpnt > 0 &&
	    !isalnum((unsigned char)curr_buf->currline->data[curr_buf->currpnt - 1]))
	curr_buf->currpnt--;
    while (curr_buf->currpnt > 0 &&
	    isalnum((unsigned char)curr_buf->currline->data[curr_buf->currpnt - 1]))
	curr_buf->currpnt--;
}

static void
delete_current_word(void)
{
    while (curr_buf->currpnt < curr_buf->currline->len) {
	delete_char();
	if (!isalnum((unsigned char)curr_buf->currline->data[curr_buf->currpnt]))
	    break;
    }
    while (curr_buf->currpnt < curr_buf->currline->len) {
	delete_char();
	if (!isspace((unsigned char)curr_buf->currline->data[curr_buf->currpnt]))
	    break;
    }
}

/**
 * transform every "*[" in given string to KEY_ESC "["
 */
static void
transform_to_color(char *line)
{
    while (line[0] && line[1])
	if (line[0] == '*' && line[1] == '[') {
	    line[0] = KEY_ESC;
	    line += 2;
	} else
	    ++line;
}

static void
block_color(void)
{
    textline_t     *begin, *end, *p;

    setup_block_begin_end(&begin, &end);

    p = begin;
    while (1) {
	assert(p);
	transform_to_color(p->data);
	if (p == end)
	    break;
	else
	    p = p->next;
    }
    block_cancel();
}

/**
 * insert ansi code
 */
static void
insert_ansi_code(void)
{
    int ch = curr_buf->insert_mode;
    curr_buf->insert_mode = curr_buf->redraw_everything = YEA;
    if (!curr_buf->ansimode)
	insert_string(ANSI_RESET);
    else {
	char            ans[4];
	move(b_lines - 2, 55);
	outs(ANSI_COLOR(1;33;40) "B" ANSI_COLOR(41) "R" ANSI_COLOR(42) "G" ANSI_COLOR(43) "Y" ANSI_COLOR(44) "L"
		ANSI_COLOR(45) "P" ANSI_COLOR(46) "C" ANSI_COLOR(47) "W" ANSI_RESET);
	if (getdata(b_lines - 1, 0,
		    "請輸入  亮度/前景/背景[正常白字黑底][0wb]：",
		    ans, sizeof(ans), LCECHO))
	{
	    const char      t[] = "BRGYLPCW";
	    char  color[15], sbr[2]="", sfg[4]="", sbg[4]="";
            const char *tmp;
	    char  *apos = ans;
	    int   fg = -1, bg = -1;
            bool  need_sep = false;

	    if (isdigit((int)*apos)) {
                SNPRINTF(sbr, "%c", *apos++);
                need_sep = true;
            }
	    if (*apos) {
		if ((tmp = strchr(t, toupper(*(apos++)))))
		    fg = tmp - t + 30;
		else
		    fg = 37;
                SNPRINTF(sfg, "%s%d", need_sep ? ";" : "", fg);
                need_sep = true;
	    }
	    if (*apos) {
		if ((tmp = strchr(t, toupper(*(apos++)))))
		    bg = tmp - t + 40;
		else
		    bg = 40;
                SNPRINTF(sbg, "%s%d", need_sep ? ";" : "", bg);
                need_sep = true;
	    }
            SNPRINTF(color, ESC_STR "[%s%s%sm", sbr, sfg, sbg);
	    insert_string(color);
	} else
    	    insert_string(ANSI_RESET);
    }
    curr_buf->insert_mode = ch;
}

static void
phone_mode_switch(void)
{
    if (curr_buf->phone_mode)
	curr_buf->phone_mode = 0;
    else {
	curr_buf->phone_mode = 1;
	if (!curr_buf->last_phone_mode)
	    curr_buf->last_phone_mode = 2;
    }
}

/**
 * return coresponding phone char of given key c
 */
static const char*
phone_char(int c)
{
    if (!isascii(c))
	return 0;
    if (curr_buf->last_phone_mode > 0 && curr_buf->last_phone_mode < 20) {
	if (tolower(c) < 'a')
	    return 0;
	return mbs_nth(BIG5[curr_buf->last_phone_mode - 1], tolower(c) - 'a');
    }
    else if (curr_buf->last_phone_mode >= 20) {
	if (c == '.') c = '/';

	if (c < '/' || c > '9')
	    return 0;

	return mbs_nth(table[curr_buf->last_phone_mode - 20], c - '/');
    }
    return 0;
}

/**
 * When get the key for phone mode, handle it (e.g. edit_msg) and return the
 * key.  Otherwise return 0.
 */
static char
phone_mode_filter(char ch)
{
    if (!curr_buf->phone_mode)
	return 0;

    switch (ch) {
	case 'z':
	case 'Z':
	    if (curr_buf->last_phone_mode < 20)
		curr_buf->last_phone_mode = 20;
	    else
		curr_buf->last_phone_mode = 2;
	    edit_msg();
	    return ch;
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	    if (curr_buf->last_phone_mode < 20) {
		curr_buf->last_phone_mode = ch - '0' + 1;
		curr_buf->redraw_everything = YEA;
		return ch;
	    }
	    break;
	case '-':
	    if (curr_buf->last_phone_mode < 20) {
		curr_buf->last_phone_mode = 11;
		curr_buf->redraw_everything = YEA;
		return ch;
	    }
	    break;
	case '=':
	    if (curr_buf->last_phone_mode < 20) {
		curr_buf->last_phone_mode = 12;
		curr_buf->redraw_everything = YEA;
		return ch;
	    }
	    break;
	case '`':
	    if (curr_buf->last_phone_mode < 20) {
		curr_buf->last_phone_mode = 13;
		curr_buf->redraw_everything = YEA;
		return ch;
	    }
	    break;
	case '/':
	    if (curr_buf->last_phone_mode >= 20) {
		curr_buf->last_phone_mode += 4;
		if (curr_buf->last_phone_mode > 27)
		    curr_buf->last_phone_mode -= 8;
		curr_buf->redraw_everything = YEA;
		return ch;
	    }
	    break;
	case '*':
	    if (curr_buf->last_phone_mode >= 20) {
		curr_buf->last_phone_mode++;
		if ((curr_buf->last_phone_mode - 21) % 4 == 3)
		    curr_buf->last_phone_mode -= 4;
		curr_buf->redraw_everything = YEA;
		return ch;
	    }
	    break;
    }

    return 0;
}

#ifdef EXP_EDIT_UPLOAD

static void
upload_file(void)
{
    size_t szdata = 0;
    int c = 1;
    char promptmsg = 0;

    clear();
    block_cancel();
    vs_hdr("上傳文字檔案");
    move(3,0);
    outs("利用本服務您可以上傳較大的文字檔 (但不計入稿費)。\n"
	 "\n"
	 "上傳期間您打的字暫時不會出現在螢幕上，除了 Ctrl-U 會被轉換為 ANSI \n"
	 "控制碼的 ESC 外，其它特殊鍵一律沒有作用。\n"
	 "\n"
	 "請在您的電腦本機端複製好內容後貼上即可開始傳送。\n");

    do {
	if (!vkey_is_ready())
	{
	    move(10, 0); clrtobot();
	    prints("\n\n資料接收中... %u 位元組。\n", (unsigned int)szdata);
	    outs(ANSI_COLOR(1)
		    "◆全部完成後按下 End 或 ^X/^Q/^C 即可回到編輯畫面。"
		    ANSI_RESET "\n");
	    promptmsg = 0;
	}

	c = edit_vkey();
	if (vkey_isprint(c))
	{
	    char mb[5];
	    int mblen = vkey_to_mb(c, mb);
	    if (mblen > 1) {
		insert_dchar(mb);
		szdata += mblen;
	    } else if (mblen == 1) {
		insert_char(mb[0]);
		szdata ++;
	    }
	}
	else if (c == Ctrl('U') || c == ESC_CHR)
	{
	    insert_char(ESC_CHR);
	    szdata ++;
	}
	else if (c == Ctrl('I'))
	{
	    insert_tab();
	    szdata ++;
	}
	else if (c == KEY_ENTER)
	{
	    split(curr_buf->currline, curr_buf->currpnt, 0);
	    szdata ++;
	    promptmsg = 1;
	}

	if (!promptmsg)
	    promptmsg = (szdata && szdata % 1024 == 0);

	// all other keys are ignored.
    } while (c != KEY_END && c != Ctrl('X') &&
	     c != Ctrl('C') && c != Ctrl('Q') &&
	     curr_buf->totaln <= EDIT_LINE_LIMIT &&
	     szdata <= EDIT_SIZE_LIMIT);

    move(12, 0);
    prints("傳送結束: 收到 %u 位元組。", (unsigned int)szdata);
    vmsgf("回到編輯畫面");
}

#endif // EXP_EDIT_UPLOAD


/** 編輯處理：主程式、鍵盤處理
 * @param	title		NULL, 否則長度 STRLEN
 * @return	EDIT_ABORTED	abort
 * 		>= 0		編輯錢數
 * 由於各處都以 == EDIT_ABORTED 判斷, 若想傳回其他負值要注意
 */
#define KEY_EDIT_ESC(c) (0x2000 | (unsigned char)(c))

typedef struct {
    const char *fpath;
    int saveheader;
    char *title;
    int flags;
    int mode0;
    int destuid0;
    int money;
    int entropy;
    int finished;
    int retval;
    char trans_buffer[256];
} edit_ctx_t;

static int
edit_cmd_save(cmd_ctx_t *ctx)
{
    edit_ctx_t *ec = (edit_ctx_t *)ctx->priv;
    int tmp;

    block_cancel();
    tmp = write_file(ec->fpath, ec->saveheader, ec->title, ec->flags,
                     &ec->entropy);
    if (tmp != KEEP_EDITING) {
        currutmp->mode = ec->mode0;
        currutmp->destuid = ec->destuid0;

        exit_edit_buffer();

        // adjust final money
        ec->money *= POST_MONEY_RATIO;
        // money or entropy?
        if (ec->money > (ec->entropy * ENTROPY_RATIO) && ec->entropy >= 0)
            ec->money = (ec->entropy * ENTROPY_RATIO) + 1;

        ec->finished = 1;
        ec->retval = !tmp ? ec->money : tmp;
        return PSB_OK;
    }
    curr_buf->redraw_everything = YEA;
    return PSB_OK;
}

static int
edit_cmd_abort(cmd_ctx_t *ctx)
{
    edit_ctx_t *ec = (edit_ctx_t *)ctx->priv;
    int ch;

    grayout(0, b_lines - 1, GRAYOUT_DARK);
    ch = vmsg("結束但不儲存 [y/N]? ");
    if (ch == 'y' || ch == 'Y') {
        currutmp->mode = ec->mode0;
        currutmp->destuid = ec->destuid0;
        exit_edit_buffer();
        ec->finished = 1;
        ec->retval = -1;
        return PSB_OK;
    }
    curr_buf->redraw_everything = YEA;
    return PSB_OK;
}

static int
edit_cmd_goto_line(cmd_ctx_t *ctx GCC_UNUSED)
{
    prompt_goto_line();
    curr_buf->redraw_everything = YEA;
    return PSB_OK;
}

static int
edit_cmd_users(cmd_ctx_t *ctx GCC_UNUSED)
{
    t_users();
    curr_buf->redraw_everything = YEA;
    return PSB_OK;
}

static int
edit_cmd_cut(cmd_ctx_t *ctx GCC_UNUSED)
{
    block_cut();
    return PSB_OK;
}

static int
edit_cmd_ansi_code(cmd_ctx_t *ctx GCC_UNUSED)
{
    insert_ansi_code();
    return PSB_OK;
}

static int
edit_cmd_search(cmd_ctx_t *ctx GCC_UNUSED)
{
    search_str(0);
    return PSB_OK;
}

static int
edit_cmd_search_next(cmd_ctx_t *ctx GCC_UNUSED)
{
    search_str(1);
    return PSB_OK;
}

static int
edit_cmd_search_prev(cmd_ctx_t *ctx GCC_UNUSED)
{
    search_str(-1);
    return PSB_OK;
}

static int
edit_cmd_match_paren(cmd_ctx_t *ctx GCC_UNUSED)
{
    match_paren();
    return PSB_OK;
}

static int
edit_cmd_read_tmpbuf(cmd_ctx_t *ctx)
{
    read_tmpbuf(ctx->key - KEY_EDIT_ESC('0'));
    curr_buf->redraw_everything = YEA;
    return PSB_OK;
}

static int
edit_cmd_block_mark(cmd_ctx_t *ctx GCC_UNUSED)
{
    if (has_block_selection())
        block_prompt();
    else
        block_select();
    return PSB_OK;
}

static int
edit_cmd_block_cancel(cmd_ctx_t *ctx GCC_UNUSED)
{
    block_cancel();
    return PSB_OK;
}

static int
edit_cmd_block_copy(cmd_ctx_t *ctx GCC_UNUSED)
{
    block_copy();
    return PSB_OK;
}

static int
edit_cmd_undelete_line(cmd_ctx_t *ctx GCC_UNUSED)
{
    undelete_line();
    return PSB_OK;
}

static int
edit_cmd_toggle_raw(cmd_ctx_t *ctx GCC_UNUSED)
{
    mbcs_mode = !mbcs_mode;
    curr_buf->raw_mode ^= 1;
    return PSB_OK;
}

static int
edit_cmd_toggle_indent(cmd_ctx_t *ctx GCC_UNUSED)
{
    curr_buf->indent_mode ^= 1;
    return PSB_OK;
}

static int
edit_cmd_shift_left(cmd_ctx_t *ctx GCC_UNUSED)
{
    currline_shift_left();
    return PSB_OK;
}

static int
edit_cmd_shift_right(cmd_ctx_t *ctx GCC_UNUSED)
{
    currline_shift_right();
    return PSB_OK;
}

static int
edit_cmd_next_word(cmd_ctx_t *ctx GCC_UNUSED)
{
    cursor_to_next_word();
    return PSB_OK;
}

static int
edit_cmd_prev_word(cmd_ctx_t *ctx GCC_UNUSED)
{
    cursor_to_prev_word();
    return PSB_OK;
}

static int
edit_cmd_del_word(cmd_ctx_t *ctx GCC_UNUSED)
{
    delete_current_word();
    return PSB_OK;
}

static int
edit_cmd_toggle_synparser(cmd_ctx_t *ctx GCC_UNUSED)
{
    curr_buf->synparser = !curr_buf->synparser;
    return PSB_OK;
}

static int
edit_cmd_esc_char(cmd_ctx_t *ctx GCC_UNUSED)
{
    insert_char(ESC_CHR);
    return PSB_OK;
}

static int
edit_cmd_toggle_ansi(cmd_ctx_t *ctx GCC_UNUSED)
{
    curr_buf->ansimode ^= 1;
    if (curr_buf->ansimode && has_block_selection())
        block_color();
    clear();
    curr_buf->redraw_everything = YEA;
    return PSB_OK;
}

static int
edit_cmd_tab(cmd_ctx_t *ctx GCC_UNUSED)
{
    insert_tab();
    return PSB_OK;
}

static int
edit_cmd_enter(cmd_ctx_t *ctx)
{
    edit_ctx_t *ec = (edit_ctx_t *)ctx->priv;

    block_cancel();
    if (curr_buf->totaln >= EDIT_LINE_LIMIT)
    {
        vmsg("檔案已超過最大限制，無法再增加行數。");
        return PSB_OK;
    }

#ifdef MAX_EDIT_LINE
    if (curr_buf->totaln ==
            ((ec->flags & EDITFLAG_ALLOWLARGE) ?
             MAX_EDIT_LINE_LARGE : MAX_EDIT_LINE))
    {
        vmsg("已到達最大行數限制。");
        return PSB_OK;
    }
#endif
    split(curr_buf->currline, curr_buf->currpnt, indent_space());
    return PSB_OK;
}

static int
edit_cmd_editexp(cmd_ctx_t *ctx)
{
    edit_ctx_t *ec = (edit_ctx_t *)ctx->priv;
    unsigned int currstat0 = currstat;
    int mode0 = currutmp->mode;

    setutmpmode(EDITEXP);
    a_menu("編輯輔助器", "etc/editexp",
           (HasUserPerm(PERM_SYSOP) ? SYSOP : NOBODY),
           0,
           ec->trans_buffer, NULL);
    currstat = currstat0;
    currutmp->mode = mode0;

    if (ec->trans_buffer[0]) {
        FILE *fp1;
        if ((fp1 = fopen(ec->trans_buffer, "r"))) {
            int indent_mode0 = curr_buf->indent_mode;
            char buf[WRAPMARGIN + 2];

            curr_buf->indent_mode = 0;
            while (fgets(buf, sizeof(buf), fp1)) {
                if (!strncmp(buf, "作者:", 5) ||
                    !strncmp(buf, "標題:", 5) ||
                    !strncmp(buf, "時間:", 5))
                    continue;
                insert_string(buf);
            }
            fclose(fp1);
            curr_buf->indent_mode = indent_mode0;
            edit_window_adjust();
        }
    }
    curr_buf->redraw_everything = YEA;
    return PSB_OK;
}

static int
edit_cmd_phone_mode(cmd_ctx_t *ctx GCC_UNUSED)
{
    phone_mode_switch();
    curr_buf->redraw_everything = YEA;
    return PSB_OK;
}

static int
edit_cmd_help(cmd_ctx_t *ctx GCC_UNUSED)
{
    more("etc/ve.hlp", YEA);
    curr_buf->redraw_everything = YEA;
    return PSB_OK;
}

static int
edit_cmd_keys_help(cmd_ctx_t *ctx GCC_UNUSED)
{
    return PSB_NA;
}

static int
edit_cmd_redraw(cmd_ctx_t *ctx GCC_UNUSED)
{
    clear();
    curr_buf->redraw_everything = YEA;
    return PSB_OK;
}

static int
edit_cmd_left(cmd_ctx_t *ctx GCC_UNUSED)
{
    if (curr_buf->currpnt) {
        if (curr_buf->ansimode)
            curr_buf->currpnt = n2ansi(curr_buf->currpnt, curr_buf->currline);
        curr_buf->currpnt--;
        if (curr_buf->ansimode)
            curr_buf->currpnt = ansi2n(curr_buf->currpnt, curr_buf->currline);
        if (mbcs_mode)
            curr_buf->currpnt = fix_cursor(curr_buf->currline->data, curr_buf->currpnt, FC_LEFT);
    } else if (curr_buf->currline->prev) {
        curr_buf->curr_window_line--;
        curr_buf->currln--;
        curr_buf->currline = curr_buf->currline->prev;
        curr_buf->currpnt = curr_buf->currline->len;
    }
    return PSB_OK;
}

static int
edit_cmd_right(cmd_ctx_t *ctx GCC_UNUSED)
{
    if (curr_buf->currline->len != curr_buf->currpnt) {
        if (curr_buf->ansimode) {
            int cw = mb_width(curr_buf->currline->data + curr_buf->currpnt);
            curr_buf->currpnt = n2ansi(curr_buf->currpnt, curr_buf->currline) + (cw > 0 ? cw : 1);
            curr_buf->currpnt = ansi2n(curr_buf->currpnt, curr_buf->currline);
        } else {
            curr_buf->currpnt++;
        }
        if (mbcs_mode)
            curr_buf->currpnt = fix_cursor(curr_buf->currline->data, curr_buf->currpnt, FC_RIGHT);
    } else if (curr_buf->currline->next) {
        curr_buf->currpnt = 0;
        curr_buf->curr_window_line++;
        curr_buf->currln++;
        curr_buf->currline = curr_buf->currline->next;
    }
    return PSB_OK;
}

static int
edit_cmd_up(cmd_ctx_t *ctx GCC_UNUSED)
{
    cursor_to_prev_line();
    return PSB_OK;
}

static int
edit_cmd_down(cmd_ctx_t *ctx GCC_UNUSED)
{
    cursor_to_next_line();
    return PSB_OK;
}

static int
edit_cmd_pgup(cmd_ctx_t *ctx GCC_UNUSED)
{
    int col = line_pos_to_col(curr_buf->currline, curr_buf->currpnt, curr_buf->ansimode);
    curr_buf->top_of_win = back_line(curr_buf->top_of_win, visible_window_height() - 1, false);
    curr_buf->currline = back_line(curr_buf->currline, visible_window_height() - 1, true);
    curr_buf->curr_window_line = get_lineno_in_window();
    curr_buf->currpnt = line_col_to_pos(curr_buf->currline, col, curr_buf->ansimode);
    curr_buf->redraw_everything = YEA;
    return PSB_OK;
}

static int
edit_cmd_pgdn(cmd_ctx_t *ctx GCC_UNUSED)
{
    int col = line_pos_to_col(curr_buf->currline, curr_buf->currpnt, curr_buf->ansimode);
    curr_buf->top_of_win = forward_line(curr_buf->top_of_win, visible_window_height() - 1, false);
    curr_buf->currline = forward_line(curr_buf->currline, visible_window_height() - 1, true);
    curr_buf->curr_window_line = get_lineno_in_window();
    curr_buf->currpnt = line_col_to_pos(curr_buf->currline, col, curr_buf->ansimode);
    curr_buf->redraw_everything = YEA;
    return PSB_OK;
}

static int
edit_cmd_end(cmd_ctx_t *ctx GCC_UNUSED)
{
    curr_buf->currpnt = curr_buf->currline->len;
    return PSB_OK;
}

static int
edit_cmd_bof(cmd_ctx_t *ctx GCC_UNUSED)
{
    curr_buf->currline = curr_buf->top_of_win = curr_buf->firstline;
    curr_buf->currpnt = curr_buf->currln = curr_buf->curr_window_line = 0;
    curr_buf->redraw_everything = YEA;
    return PSB_OK;
}

static int
edit_cmd_eof(cmd_ctx_t *ctx GCC_UNUSED)
{
    curr_buf->top_of_win = back_line(curr_buf->lastline, visible_window_height() - 1, false);
    curr_buf->currline = curr_buf->lastline;
    curr_buf->curr_window_line = get_lineno_in_window();
    curr_buf->currln = curr_buf->totaln;
    curr_buf->redraw_everything = YEA;
    curr_buf->currpnt = 0;
    return PSB_OK;
}

static int
edit_cmd_home(cmd_ctx_t *ctx GCC_UNUSED)
{
    curr_buf->currpnt = 0;
    return PSB_OK;
}

static int
edit_cmd_toggle_insert(cmd_ctx_t *ctx GCC_UNUSED)
{
    curr_buf->insert_mode ^= 1;
    return PSB_OK;
}

static int
edit_cmd_backspace(cmd_ctx_t *ctx GCC_UNUSED)
{
    block_cancel();
    if (curr_buf->ansimode) {
        curr_buf->ansimode = 0;
        clear();
        curr_buf->redraw_everything = YEA;
    } else {
        if (curr_buf->currpnt == 0) {
            if (!curr_buf->currline->prev)
                return PSB_OK;
            curr_buf->curr_window_line--;
            curr_buf->currln--;

            if (curr_buf->currline->alloc_len > curr_buf->currline->len)
                curr_buf->currline = adjustline(curr_buf->currline, curr_buf->currline->len);
            curr_buf->currline = curr_buf->currline->prev;
            curr_buf->oldcurrline = curr_buf->currline;

            curr_buf->currpnt = curr_buf->currline->len;
            curr_buf->redraw_everything = YEA;
            if (curr_buf->currline->next == curr_buf->top_of_win) {
                curr_buf->top_of_win = curr_buf->currline;
                curr_buf->curr_window_line = 0;
            }
            join(curr_buf->currline);
            return PSB_OK;
        }
        {
            int newpnt = curr_buf->currpnt - 1;

            if (mbcs_mode)
                newpnt = fix_cursor(curr_buf->currline->data, newpnt, FC_LEFT);

            for (; curr_buf->currpnt > newpnt;)
            {
                curr_buf->currpnt--;
                delete_char();
            }
        }
    }
    return PSB_OK;
}

static int
edit_cmd_delete(cmd_ctx_t *ctx GCC_UNUSED)
{
    block_cancel();
    if (curr_buf->currline->len == curr_buf->currpnt) {
        join(curr_buf->currline);
        curr_buf->redraw_everything = YEA;
    } else {
        del_currchar();
        if (curr_buf->ansimode)
            curr_buf->currpnt = ansi2n(n2ansi(curr_buf->currpnt, curr_buf->currline), curr_buf->currline);
    }
    return PSB_OK;
}

static void
do_delete_current_line(void)
{
    textline_t *p = curr_buf->currline->next;
    if (!p) {
        p = curr_buf->currline->prev;
        if (!p) {
            curr_buf->currline->data[0] = 0;
            curr_buf->currline->len = 0;
            return;
        }
        if (curr_buf->curr_window_line > 0) {
            curr_buf->curr_window_line--;
        }
        curr_buf->currln--;
    }
    if (curr_buf->currline == curr_buf->top_of_win)
        curr_buf->top_of_win = p;

    delete_line(curr_buf->currline, 1);
    curr_buf->currline = p;
    curr_buf->redraw_everything = YEA;
}

static int
edit_cmd_del_line(cmd_ctx_t *ctx GCC_UNUSED)
{
    curr_buf->currpnt = 0;
    block_cancel();
    do_delete_current_line();
    return PSB_OK;
}

static int
edit_cmd_del_eol(cmd_ctx_t *ctx GCC_UNUSED)
{
    block_cancel();
    if (curr_buf->currline->len == 0) {
        do_delete_current_line();
        return PSB_OK;
    }
    if (curr_buf->currline->len == curr_buf->currpnt) {
        join(curr_buf->currline);
        curr_buf->redraw_everything = YEA;
        return PSB_OK;
    }
    curr_buf->currline->len = curr_buf->currpnt;
    curr_buf->currline->data[curr_buf->currpnt] = '\0';
    return PSB_OK;
}

static const cmd_t edit_cmds[] = {
    /* Footer left: always shown first (in order) */
    { Ctrl('X'), "存檔", "儲存檔案並離開編輯器", edit_cmd_save, 0, CMD_PRIO_MAX },
    { KEY_F10, NULL, NULL, edit_cmd_save },
    { KEY_EDIT_ESC('X'), NULL, NULL, edit_cmd_save },
    { Ctrl('C'), "色碼", "插入 ANSI 色彩控制碼", edit_cmd_ansi_code, 0, CMD_PRIO_MAX },
    { Ctrl('V'), "彩色", "切換 ANSI 彩色顯示模式", edit_cmd_toggle_ansi, 0, CMD_PRIO_MAX },
    { KEY_EDIT_ESC('a'), NULL, NULL, edit_cmd_toggle_ansi },
    { KEY_EDIT_ESC('A'), NULL, NULL, edit_cmd_toggle_ansi },

    /* Footer left overflow: shown if space permits (in order) */
    { Ctrl('P'), "符號", "切換內建注音/符號輸入法", edit_cmd_phone_mode, 0, CMD_PRIO_HIGH },
    { Ctrl('Q'), "放棄", "放棄修改並離開編輯器", edit_cmd_abort, 0, CMD_PRIO_HIGH },
    { KEY_EDIT_ESC('q'), NULL, NULL, edit_cmd_abort },
    { Ctrl('G'), "範本", "開啟編輯輔助器", edit_cmd_editexp, 0, CMD_PRIO_HIGH },

    /* Footer right: right-aligned tail (in order) */
    { KEY_EDIT_ESC('h'), "按鍵", "顯示編輯器按鍵列表", edit_cmd_keys_help, 0, CMD_PRIO_TOP },
    { Ctrl('Z'), "說明", "顯示編輯器操作說明", edit_cmd_help, 0, CMD_PRIO_TOP },
    { KEY_F1, NULL, NULL, edit_cmd_help },

    /* Other editor commands (help-only, prio = CMD_PRIO_NONE) */
    { Ctrl('S'), "搜尋", "搜尋指定字串", edit_cmd_search },
    { KEY_F3, NULL, NULL, edit_cmd_search },
    { KEY_EDIT_ESC('s'), NULL, NULL, edit_cmd_search },
    { KEY_EDIT_ESC('n'), "下個搜尋", "搜尋下一個相符字串", edit_cmd_search_next },
    { KEY_EDIT_ESC('p'), "上個搜尋", "搜尋上一個相符字串", edit_cmd_search_prev },
    { KEY_F5, "跳行", "跳至指定行號", edit_cmd_goto_line },
    { KEY_EDIT_ESC('L'), NULL, NULL, edit_cmd_goto_line },
    { KEY_EDIT_ESC('J'), NULL, NULL, edit_cmd_goto_line },
    { KEY_EDIT_ESC(']'), "括號配對", "跳至對應的括號", edit_cmd_match_paren },
    { KEY_F8, "線上名單", "查看線上使用者", edit_cmd_users },
    { KEY_EDIT_ESC('U'), NULL, NULL, edit_cmd_users },
    { Ctrl('U'), "插入ESC", "插入 ESC 控制字元", edit_cmd_esc_char },
    { Ctrl('W'), "區塊剪下", "剪下標記區塊", edit_cmd_cut },
    { KEY_EDIT_ESC('w'), NULL, NULL, edit_cmd_cut },
    { KEY_EDIT_ESC('W'), NULL, NULL, edit_cmd_cut },
    { KEY_EDIT_ESC('l'), "區塊標記", "開始或操作區塊標記", edit_cmd_block_mark },
    { KEY_EDIT_ESC(' '), NULL, NULL, edit_cmd_block_mark },
    { KEY_EDIT_ESC('u'), "取消標記", "取消區塊標記", edit_cmd_block_cancel },
    { KEY_EDIT_ESC('c'), "區塊複製", "複製標記區塊", edit_cmd_block_copy },
    { Ctrl('Y'), "刪除整行", "刪除游標所在整行", edit_cmd_del_line },
    { KEY_EDIT_ESC('y'), "還原刪行", "貼回最近刪除的行", edit_cmd_undelete_line },
    { Ctrl('K'), "刪至行尾", "刪除游標至行尾字元", edit_cmd_del_eol },
    { Ctrl('D'), "刪除字元", "刪除游標所在字元", edit_cmd_delete },
    { KEY_DEL, NULL, NULL, edit_cmd_delete },
    { KEY_BS, "倒退刪除", "刪除游標前一個字元", edit_cmd_backspace },
    { KEY_EDIT_ESC('d'), "刪除單字", "刪除游標所在單字", edit_cmd_del_word },
    { Ctrl('I'), "插入Tab", "插入定位空格", edit_cmd_tab },
    { KEY_ENTER, "換行分段", "在游標處換行", edit_cmd_enter },
    { Ctrl('O'), "插入模式", "切換插入/覆寫模式", edit_cmd_toggle_insert },
    { KEY_INS, NULL, NULL, edit_cmd_toggle_insert },
    { KEY_EDIT_ESC('o'), NULL, NULL, edit_cmd_toggle_insert },
    { KEY_EDIT_ESC('I'), "自動縮排", "切換自動縮排模式", edit_cmd_toggle_indent },
    { KEY_EDIT_ESC('j'), "整行左移", "將游標所在行向左縮排", edit_cmd_shift_left },
    { KEY_EDIT_ESC('k'), "整行右移", "將游標所在行向右縮排", edit_cmd_shift_right },
    { KEY_EDIT_ESC('r'), "雙位元組", "切換 DBCS/Raw 編輯模式", edit_cmd_toggle_raw },
    { KEY_EDIT_ESC('R'), NULL, NULL, edit_cmd_toggle_raw },
    { KEY_EDIT_ESC('S'), "語法上色", "切換語法高亮模式", edit_cmd_toggle_synparser },
    { KEY_EDIT_ESC('0'), "讀暫存檔", "讀取暫存檔 0~9", edit_cmd_read_tmpbuf },
    { KEY_EDIT_ESC('1'), NULL, NULL, edit_cmd_read_tmpbuf },
    { KEY_EDIT_ESC('2'), NULL, NULL, edit_cmd_read_tmpbuf },
    { KEY_EDIT_ESC('3'), NULL, NULL, edit_cmd_read_tmpbuf },
    { KEY_EDIT_ESC('4'), NULL, NULL, edit_cmd_read_tmpbuf },
    { KEY_EDIT_ESC('5'), NULL, NULL, edit_cmd_read_tmpbuf },
    { KEY_EDIT_ESC('6'), NULL, NULL, edit_cmd_read_tmpbuf },
    { KEY_EDIT_ESC('7'), NULL, NULL, edit_cmd_read_tmpbuf },
    { KEY_EDIT_ESC('8'), NULL, NULL, edit_cmd_read_tmpbuf },
    { KEY_EDIT_ESC('9'), NULL, NULL, edit_cmd_read_tmpbuf },
    { Ctrl('L'), "重繪畫面", "清除並重繪畫面", edit_cmd_redraw },
    { KEY_UP, "游標上移", "游標向上移動一行", edit_cmd_up },
    { KEY_DOWN, "游標下移", "游標向下移動一行", edit_cmd_down },
    { KEY_LEFT, "游標左移", "游標向左移動一字", edit_cmd_left },
    { KEY_RIGHT, "游標右移", "游標向右移動一字", edit_cmd_right },
    { KEY_EDIT_ESC('f'), "下一單字", "移動游標至下一個單字", edit_cmd_next_word },
    { KEY_EDIT_ESC('b'), "上一單字", "移動游標至上一個單字", edit_cmd_prev_word },
    { KEY_PGUP, "向上翻頁", "向上捲動一頁", edit_cmd_pgup },
    { Ctrl('B'), NULL, NULL, edit_cmd_pgup },
    { KEY_EDIT_ESC('v'), NULL, NULL, edit_cmd_pgup },
    { KEY_PGDN, "向下翻頁", "向下捲動一頁", edit_cmd_pgdn },
    { Ctrl('F'), NULL, NULL, edit_cmd_pgdn },
    { KEY_HOME, "移至行首", "移動游標至行首", edit_cmd_home },
    { Ctrl('A'), NULL, NULL, edit_cmd_home },
    { KEY_END, "移至行尾", "移動游標至行尾", edit_cmd_end },
    { Ctrl('E'), NULL, NULL, edit_cmd_end },
    { Ctrl(']'), "移至檔首", "移動游標至檔案開頭", edit_cmd_bof },
    { KEY_EDIT_ESC(','), NULL, NULL, edit_cmd_bof },
    { Ctrl('T'), "移至檔尾", "移動游標至檔案結尾", edit_cmd_eof },
    { KEY_EDIT_ESC('.'), NULL, NULL, edit_cmd_eof },
    {0}
};



int
vedit2(const char *fpath, int saveheader, char title[STRLEN], int flags)
{
    char            last = 0;	/* the last key you press */
    int             ch, tmp;
    int             interval = 0;
    time4_t         th = now;
    int             count = 0, tin = 0, quoted = 0;
    edit_ctx_t      ec = {0};
    const cmd_layer_t edit_layers[] = {
        { edit_cmds, &ec },
        {0}
    };

    ec.fpath = fpath;
    ec.saveheader = saveheader;
    ec.title = title;
    ec.flags = flags;
    ec.mode0 = currutmp->mode;
    ec.destuid0 = currutmp->destuid;

    STATINC(STAT_VEDIT);
    currutmp->mode = EDITING;
    currutmp->destuid = currstat;

    mbcs_mode = ISDBCSAWARE() ? 1 : 0;

    enter_edit_buffer();
    curr_buf->flags = flags;

    if (*fpath) {
	int tmp = read_file(fpath, (flags & EDITFLAG_TEXTONLY) ? 1 : 0);

	if (tmp < 0)
	    return tmp;
    }

    if (*quote_file) {
	do_quote();
	*quote_file = '\0';
	quoted = 1;
    }

    if (curr_buf->oldcurrline != curr_buf->firstline ||
	curr_buf->currline != curr_buf->firstline) {
	/* we must adjust because cursor (currentline) moved. */
	if (curr_buf->oldcurrline != NULL &&
	    curr_buf->oldcurrline->alloc_len > curr_buf->oldcurrline->len)
	    curr_buf->oldcurrline = adjustline(curr_buf->oldcurrline, curr_buf->oldcurrline->len);
	curr_buf->oldcurrline = curr_buf->currline = curr_buf->top_of_win =
           curr_buf->firstline;
    }

    /* No matter you quote or not, just start the cursor from (0,0) */
    curr_buf->currpnt = curr_buf->currln = curr_buf->curr_window_line =
    curr_buf->edit_margin = curr_buf->last_margin = 0;

    /* if quote, move to end of file. */
    if (quoted)
    {
	/* maybe do this in future. */
    }

    while (1) {
	edit_check_healthy();

	if (curr_buf->redraw_everything || has_block_selection()) {
	    refresh_window();
	    curr_buf->redraw_everything = NA;
	}
	if (curr_buf->oldcurrline != curr_buf->currline) {
	    if (curr_buf->oldcurrline != NULL &&
		curr_buf->oldcurrline->alloc_len > curr_buf->oldcurrline->len)
		curr_buf->oldcurrline = adjustline(curr_buf->oldcurrline, curr_buf->oldcurrline->len);
	    curr_buf->oldcurrline = curr_buf->currline;
	}

	if (curr_buf->ansimode)
	    ch = n2ansi(curr_buf->currpnt, curr_buf->currline);
	else
	    ch = line_pos_to_col(curr_buf->currline, curr_buf->currpnt, 0) -
		 curr_buf->edit_margin;
	move(curr_buf->curr_window_line, ch);

	ch = edit_vkey();
	/* jochang debug */
	if ((interval = (now - th))) {
	    th = now;
	    if ((char)ch != last) {
		ec.money++;
		last = (char)ch;
	    }
	}
	if (interval && interval == tin) {
	    count++;
            if (count > 60) {
                ec.money = 0;
                count = 0;
            }
	} else if (interval) {
	    count = 0;
	    tin = interval;
	}
	if (phone_mode_filter(ch))
	    continue;

	if (vkey_isprint(ch)) {
	    const char *pstr;
	    char mb[5];
	    int mblen;
            if(curr_buf->phone_mode && (pstr=phone_char(ch)))
	   	insert_dchar(pstr);
	    else if ((mblen = vkey_to_mb(ch, mb)) > 1)
		insert_dchar(mb);
	    else if (mblen == 1)
		insert_char(mb[0]);
	    curr_buf->lastindent = -1;
	} else {
	    int dispatch_key = (ch == KEY_ESC) ? KEY_EDIT_ESC(KEY_ESC_arg) : ch;
	    cmd_ctx_t cctx = { .key = dispatch_key };

	    if (ch == KEY_UP || ch == KEY_DOWN) {
		if (curr_buf->lastindent == -1)
		    curr_buf->lastindent = line_pos_to_col(
			curr_buf->currline, curr_buf->currpnt, curr_buf->ansimode);
	    } else
		curr_buf->lastindent = -1;

	    cmd_dispatch_layers(edit_layers, &cctx, "【文章編輯】");
	    if (ec.finished) {
		return ec.retval;
	    }

	    if (curr_buf->currln < 0)
		curr_buf->currln = 0;

	    edit_window_adjust();
	    if(mbcs_mode)
	      curr_buf->currpnt = fix_cursor(curr_buf->currline->data, curr_buf->currpnt, FC_LEFT);
	}

	tmp = line_pos_to_col(curr_buf->currline, curr_buf->currpnt,
			      curr_buf->ansimode);

	if (tmp < t_columns - 1)
	    curr_buf->edit_margin = 0;
	else
	    curr_buf->edit_margin = tmp / (t_columns - 8) * (t_columns - 8);

	if (!curr_buf->redraw_everything) {
	    if (curr_buf->edit_margin != curr_buf->last_margin) {
		curr_buf->last_margin = curr_buf->edit_margin;
		curr_buf->redraw_everything = YEA;
	    } else {
		move(curr_buf->curr_window_line, 0);
		clrtoeol();
		if (curr_buf->ansimode)
		    outs(curr_buf->currline->data);
		else
		{
		    int attr = EOATTR_NORMAL;
		    attr |= detect_attr(curr_buf->currline->data, curr_buf->currline->len);
		    if (curr_buf->edit_margin > 0) {
			int pos = line_col_to_pos(curr_buf->currline, curr_buf->edit_margin, 0);
			if (pos < curr_buf->currline->len) {
			    int col = line_pos_to_col(curr_buf->currline, pos, 0);
			    const char *pdata = curr_buf->currline->data + pos;
			    if (col < curr_buf->edit_margin) {
				outs(ANSI_COLOR(1) "<" ANSI_RESET);
				pdata += mb_bytes(pdata);
			    }
			    edit_outs_attr(pdata, attr);
			}
		    } else {
			edit_outs_attr(curr_buf->currline->data, attr);
		    }
		}
		outs(ANSI_RESET ANSI_CLRTOEND);
		edit_msg();
	    }
	} else {
	    curr_buf->last_margin = curr_buf->edit_margin;
	}
    } /* main event loop */

    exit_edit_buffer();
}

int
vedit(const char *fpath, int saveheader, char title[STRLEN])
{
    assert(title);
    return vedit2(fpath, saveheader, title, EDITFLAG_ALLOWTITLE);
}

/**
 * 編輯一般檔案 (非看板文章/信件).
 *
 * 因此不會有 header, title, local save 等參數.
 */
int
veditfile(const char *fpath)
{
    return vedit2(fpath, NA, NULL, 0);
}

/* vim:sw=4:nofoldenable
 */
