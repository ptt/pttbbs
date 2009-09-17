/* $Id$ */
#ifndef INCLUDE_MODES_H
#define INCLUDE_MODES_H

/* common return values of read.c */
#define DONOTHING       0       /* Read menu command return states */
#define FULLUPDATE      1       /* Entire screen was destroyed in this oper */
#define PARTUPDATE      2       /* Only the top three lines were destroyed */
#define DOQUIT          3       /* Exit read menu was executed */
#define NEWDIRECT       4       /* Directory has changed, re-read files */
#define READ_NEXT       5       /* Direct read next file */
#define READ_PREV       6       /* Direct read prev file */
#define DIRCHANGED      8       /* Index file was changed */
#define READ_REDRAW     9
#define PART_REDRAW     10
#define TITLE_REDRAW    11
#define READ_SKIP       12      
#define HEADERS_RELOAD  13      

// values returned by pager
#define RET_DOREPLY	    (999)
#define RET_DORECOMMEND	    (998)
#define RET_DOQUERYINFO	    (997)
#define RET_DOSYSOPEDIT	    (996)
#define RET_DOCHESSREPLAY   (995)
#define RET_DOBBSLUA	    (994)
#define RET_COPY2TMP	    (993)
#define RET_SELECTBRD	    (992)
#define RET_DOREPLYALL	    (991)

/* user 操作狀態與模式 */
#define IDLE            0
#define MMENU           1       /* menu mode */
#define ADMIN           2
#define MAIL            3
#define TMENU           4
#define UMENU           5
#define XMENU           6
#define CLASS           7
#define PMENU           8
#define NMENU           9
#define PSALE           10
#define POSTING         11      /* boards & class */
#define READBRD         12
#define READING         13
#define READNEW         14
#define SELECT          15
#define RMAIL           16      /* mail menu */
#define SMAIL           17
#define CHATING         18      /* talk menu */
#define XMODE           19
#define FRIEND          20
#define LAUSERS         21
#define LUSERS          22
#define MONITOR         23
#define PAGE            24
#define TQUERY          25
#define TALK            26
#define EDITPLAN        27      /* user menu */
#define EDITSIG         28
#define VOTING          29
#define XINFO           30
#define MSYSOP          31
#define WWW             32
#define BIG2            33
#define REPLY           34
#define HIT             35
#define DBACK           36
#define NOTE            37
#define EDITING         38
#define MAILALL         39
#define MJ              40
#define P_FRIEND        41
#define LOGIN           42      /* main menu */
#define DICT            43
#define BRIDGE          44
#define ARCHIE          45
#define GOPHER          46
#define NEWS            47
#define LOVE            48
#define EDITEXP         49
#define IPREG           50
#define NADM            51
#define DRINK           52
#define CAL             53
#define PROVERB         54
#define ANNOUNCE        55      /* announce */
#define EDNOTE          56 
#define CDICT           57
#define LOBJ            58
#define OSONG           59
#define CHICKEN         60
#define TICKET 	    	61
#define GUESSNUM        62
#define AMUSE           63
#define OTHELLO		64
#define DICE            65
#define VICE            66
#define BBCALL          67
#define VIOLATELAW      68
#define M_FIVE          69
#define JACK_CARD       70
#define TENHALF         71
#define CARD_99         72
#define RAIL_WAY        73
#define SREG            74
#define CHC             75      /* Chinese chess */
#define DARK            76      /* 中國暗棋 */
#define TMPJACK         77
#define JCEE		78
#define REEDIT		79
#define BLOGGING        80
#define CHESSWATCHING	81
#define UMODE_GO        82
#define DEBUGSLEEPING	83
#define UMODE_CONN6	84
#define REVERSI		85
#define UMODE_BBSLUA	86
#define UMODE_ASCIIMOVIE 87
#define MODE_MAX        88      /* 所有其他選單動態須在此之前 */

/* menu.c 中的模式 */
#define QUIT    0x666           /* Return value to abort recursive functions */
#define XEASY   0x333           /* Return value to un-redraw screen */

/* for currmode */
#define MODE_STARTED     0x0001    /* 是否已經進入系統 */
#define MODE_POST        0x0002    /* 是否可以在 currboard 發表文章 */
#define MODE_POSTCHECKED 0x0004    /* 是否已檢查在 currboard 發表文章的權限 */
#define MODE_BOARD       0x0008    /* 是否可以在 currboard 刪除、mark文章 */
#define MODE_GROUPOP     0x0010    /* 是否為小組長 (可以在 MENU 開板) */
#define MODE_DIGEST      0x0020    /* 是否為 digest mode */
#define MODE_SELECT      0x0080    /* 搜尋使用者標題 */
#define MODE_DIRTY       0x0100    /* 是否更動過 userflag */

/* for curredit */
#define EDIT_MAIL       1       /* 目前是 mail/board ? */
#define EDIT_LIST       2       /* 是否為 mail list ? */
#define EDIT_BOTH       4       /* both reply to author/board ? */

/* read.c 中的模式 */
#define TAG_NIN         0       /* 不屬於 TagList */
#define TAG_TOGGLE      1       /* 切換 Taglist */
#define TAG_INSERT      2       /* 加入 TagList */


#define RS_FORWARD      0x01    /* backward */
#define RS_TITLE        0x02    /* author/title */
#define RS_KEYWORD      0x04
#define RS_FIRST        0x08    /* find first article */
#define RS_CURRENT      0x10    /* match current read article */
#define RS_MARK         0x20    /* search by 'm' mark */
#define RS_AUTHOR       0x40    /* search author's article */
#define RS_NEWPOST	0x80	/* search new posts */
#define RS_KEYWORD_EXCLUDE	0x100	/* exclude keyword */
#define RS_RECOMMEND	0x200	/* search by recommends */
#define RS_MONEY	0x400	/* search by money */
#define RS_SOLVED	0x800	/* search by 's' mark */

#define CURSOR_FIRST    (RS_TITLE | RS_FIRST)
#define CURSOR_NEXT     (RS_TITLE | RS_FORWARD)
#define CURSOR_PREV     (RS_TITLE)
#define RELATE_FIRST    (RS_TITLE | RS_FIRST | RS_CURRENT)
#define RELATE_NEXT     (RS_TITLE | RS_FORWARD | RS_CURRENT)
#define RELATE_PREV     (RS_TITLE | RS_CURRENT)
#define NEWPOST_NEXT    (RS_NEWPOST | RS_FORWARD)
#define NEWPOST_PREV    (RS_NEWPOST)
#define AUTHOR_NEXT     (RS_AUTHOR | RS_FORWARD)
#define AUTHOR_PREV     (RS_AUTHOR)

#define SIG_PK          0
#define SIG_TALK        1
#define SIG_BROADCAST   2
#define SIG_GOMO        3
#define SIG_CHC         4
#define SIG_DARK        5
#define SIG_GO          6
#define SIG_REVERSI	7
#define SIG_CONN6	8

/* talk.c 中的模式 */
#define WATERBALL_GENERAL 0
#define WATERBALL_PREEDIT 1
#define WATERBALL_ALOHA   2
#define WATERBALL_SYSOP   3
#define WATERBALL_CONFIRM 4
#ifdef PLAY_ANGEL
#define WATERBALL_ANGEL   5
#define WATERBALL_ANSWER  6
#define WATERBALL_CONFIRM_ANGEL 7
#define WATERBALL_CONFIRM_ANSWER 8
#endif

/* chat.c, talk.c: pager modes */
#define PAGER_OFF	(0)
#define PAGER_ON	(1)
#define PAGER_DISABLE	(2)
#define PAGER_ANTIWB	(3)
#define PAGER_FRIENDONLY	(4)

#define PAGER_MODES 	(5)

#define PAGER_UI_ORIG	    0x00000000	// was: WATER_ORIG
#define PAGER_UI_NEW	    0x00000001	// was: WATER_NEW
#define PAGER_UI_OFO	    0x00000002	// was: WATER_OFO
#define PAGER_UI_IS(uitype) ((cuser.pager_ui_type%PAGER_UI_TYPES) == (uitype))
#define PAGER_UI_TYPES	    0x00000003	// the types that we really support
#define PAGER_UI_TYPES_USER 0x00000003	// the types we allow user to select

/* stuff.c: show_file */
#define SHOWFILE_RAW	     (0x00)
#define SHOWFILE_ALLOW_COLOR (0x01) // ESC [ ... m
#define SHOWFILE_ALLOW_MOVE  (0x02) // ESC [ ... H
#define SHOWFILE_ALLOW_STAR  (0x04) // ESC * ...
#define SHOWFILE_ALLOW_ALL   (SHOWFILE_ALLOW_COLOR | SHOWFILE_ALLOW_MOVE | SHOWFILE_ALLOW_STAR)

#endif
