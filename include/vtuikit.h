// $Id$
#ifndef VTUIKIT_H
#define VTUIKIT_H

/*
 * vtuikit.h
 * piaip's new implementation of vtuikit
 * 
 * see vtuikit.c for license, usage, and introduction.
 */

#include "bbs.h"
#include "ansi.h"   // we need it.
#include "vtkbd.h"  // we usually use it
#include <limits.h>

// THEME DEFINITION ----------------------------------------------------
#define VCLR_HEADER		ANSI_COLOR(1;37;46)	// was: TITLE_COLOR
#define VCLR_HEADER_MID		ANSI_COLOR(1;33;46)
#define VCLR_HEADER_RIGHT	ANSI_COLOR(1;37;46)
#define VCLR_HDR		ANSI_COLOR(1;37;46)
#define VCLR_HDR2_LEFT		ANSI_COLOR(1;37;46)
#define VCLR_HDR2_RIGHT		ANSI_COLOR(1;37;45)
#define VCLR_FOOTER_CAPTION     ANSI_COLOR(0;34;46)
#define VCLR_FOOTER             ANSI_COLOR(0;30;47)
#define VCLR_FOOTER_QUOTE       ANSI_COLOR(0;31;47)
#define VCLR_ZA_CAPTION		ANSI_COLOR(1;37;42)
#define VCLR_MSG_FLOAT		ANSI_COLOR(1;33;46)
#define VCLR_MSG		ANSI_COLOR(1;36;44)
#define VCLR_PAUSE_PAD		ANSI_COLOR(1;34;44)
#define VCLR_PAUSE		ANSI_COLOR(1;37;44)
#define VCLR_QPREF_TITLE        ANSI_COLOR(0;30;47)
#define VCLR_QPREF_PROMPT       ANSI_COLOR(0)
#define VCLR_QPREF_ENTRY_KEY    ANSI_COLOR(0;1;31)
#define VCLR_QPREF_ENTRY_TEXT   ANSI_COLOR(0)
#define VCLR_QPREF_ENTRY_ACTIVE ANSI_COLOR(0;1;36)

#define VCLR_INPUT_FIELD	ANSI_COLOR(0;7)

#define VMSG_PAUSE		" �Ы����N���~�� "
#define VMSG_PAUSE_PAD		"�e"
#define VMSG_MSG_FLOAT		" [�����N���~��]"
#define VMSG_MSG_PREFIX		" �� "
#define VMSG_HDR_PREFIX		"�i "
#define VMSG_HDR_POSTFIX	" �j"

#define VMSG_QPREF_TITLE        "�г]�w�U�C�ﶵ"
#define VMSG_QPREF_ENTRY        "�i�ϥοﶵ: "
#define VMSG_QPREF_PROMPT       "�Ы��Ʀr�Τ�V��վ�A�Ψ䥦���N�䵲��"

// CONSTANT DEFINITION -------------------------------------------------
#define VCOL_MAXW		(INT16_MAX)
#define VCOL_MAXPRI		(INT16_MAX)

#define VFILL_DEFAULT		(0x00)
// #define VFILL_NO_ANSI		VFILL_DEFAULT
#define VFILL_HAS_ANSI		(0x01)
// #define VVILL_LEFT_ALIGN	VFILL_DEFAULT
#define VFILL_RIGHT_ALIGN	(0x02)
// #define VFILL_HAS_BORDER	VFILL_DEFAULT
#define VFILL_NO_BORDER		(0x08)

#define VGET_DEFAULT		(0x00)
// #define VGET_DOECHO		(VGET_DEFAULT)
#define VGET_NOECHO		(0x01)
#define VGET_LOWERCASE		(0x02)
#define VGET_DIGITS		(0x04)
#define VGET_TRANSPARENT	(0x08)
#define VGET_ASCII_ONLY		(0x10)
#define VGET_NO_NAV_HISTORY	(0x20)	// disable UP/DOWN
#define VGET_NO_NAV_EDIT	(0x40)	// disable LEFT/RIGHT/HOME/END/DEL
#define VGET_PASSWORD           (0x80)  // show only '*'

#define VGETSET_DUMB_TERM		(VGET_NO_NAV_HISTORY | VGET_NO_NAV_EDIT)
#define VGETSET_NOECHO		        (VGET_NOECHO | VGET_ASCII_ONLY | VGETSET_DUMB_TERM)
#define VGETSET_PASSWORD		(VGET_PASSWORD | VGET_ASCII_ONLY | VGETSET_DUMB_TERM)

// DATATYPE DEFINITION -------------------------------------------------
typedef void *	VREFSCR;
typedef long	VREFCUR;

typedef short	VCOLW;
typedef short	VCOLPRI;

typedef struct VCOL {
    char *attr;	    // default attribute
    VCOLW minw;	    // minimal width
    VCOLW maxw;	    // max width
    VCOLPRI pri;    // priority (higher expands first)

    struct {
	char has_ansi;	    // field data have ANSI escapes
	char right_align;   // align output to right side
	char usewhole;	    // draw entire column and prevent borders
    }   flags;

} VCOL;

#define VGETCB_NONE	(0) // do nothing
#define VGETCB_NEXT	(1) // skip to next event loop 
#define VGETCB_END	(2) // finish input
#define VGETCB_ABORT	(3) // clear buffer and finish

typedef struct {
    char    *buf;
    const int len;	// callbacks should not change this.
    int	     icurr;	// cursor position
    int	     iend;	// buffer tail (= strlen(buf))
} VGET_RUNTIME;

typedef int (*VGET_FCALLBACK)(int key, VGET_RUNTIME *prt, void *instance);
typedef struct VGET_CALLBACKS {
    VGET_FCALLBACK   peek;   // called immediately after key hit
    VGET_FCALLBACK   data;   // called before inserting character data
    VGET_FCALLBACK   change; // called after buffer changed (the key may be an editing key)
    VGET_FCALLBACK   redraw; // called before drawing input box unless VGET_NOECHO
}   VGET_CALLBACKS;

// API DEFINITION ----------------------------------------------------

// curses flavor
void prints(const char *fmt, ...) GCC_CHECK_FORMAT(1,2);
void mvprints(int y, int x, const char *fmt, ...) GCC_CHECK_FORMAT(3,4);
void mvouts(int y, int x, const char *str);

// vbuf rendering
void outs_vbuf (VBUF *v);
void outns_vbuf(VBUF *v, int n);

// input history
int  InputHistoryAdd	(const char *s);
int  InputHistoryExists	(const char *s);
void InputHistoryPrev	(char *s, int sz);
void InputHistoryNext	(char *s, int sz);

// v*: primitive rendering
void vpad   (int n, const char *pattern);	    /// pad n fields by pattern
 int vgety  (void);				    /// return cursor position (y)
void vfill  (int n, int flags, const char *s);	    /// fill n-width space with s
void vfillf (int n, int flags, const char *s, ...) GCC_CHECK_FORMAT(3,4); /// formatted version of vfill
void vbarlr (const char *l, const char *r);	    /// draw a left-right expanded bar with (l,r)
void vbarf  (const char *s, ...)  GCC_CHECK_FORMAT(1,2); /// vbarlr with formatted input (\t splits (l,r)
void vshowmsg(const char *msg);			    /// draw standard pause/message

// v*: input widgets
// int  vans(char *prompt);	// prompt at bottom and return y/n in lower case.
 int vmsg   (const char *msg);				    /// draw standard pause/message and return input
 int vmsgf  (const char *fmt,...) GCC_CHECK_FORMAT(1,2);    /// formatted input of vmsg
 int vans   (const char *msg);				    /// prompt and return (lowercase) single byte input
 int vansf  (const char *fmt,...) GCC_CHECK_FORMAT(1,2);    /// formatted input of vans

// vget: (y, x, ...)
int vgets   (char *buf, int len, int flags);		    /// input with edit box control
int vgetstr (char *buf, int len, int flags, const char *str);/// input with default value
int vget    (int y, int x, const char *prompt, char *buf, int len, int mode);
int vgetstring(char *_buf, int len, int flags, const char *defstr, const VGET_CALLBACKS *pcbs, void *instance);

// vs_*: formatted and themed virtual screen layout
// you cannot use ANSI escapes in these APIs.
void vs_header	(const char *title,   const char *mid, const char *right);	// vs_head, showtitle
void vs_hdr	(const char *title);						// vs_bar,  stand_title
void vs_hdr2	(const char *left, const char *right);
void vs_hdr2f	(const char *fmt, ...);
void vs_hdr2bar	(const char *left, const char *right);
void vs_hdr2barf(const char *fmt, ...);
void vs_footer	(const char *caption, const char *prompt);

int vs_quick_pref(int default_value, const char *title, const char *entry,
                  const char *options, const char *prompt);

void vs_rectangle_simple(int l, int t, int r, int b);	// draw a single line rectangle, not filling inside interior
void vs_multi_T_table_simple(
	const char ***t_tables,   int  n_t_tables, 
	const int  *col_widths,   const int  *l_widths,
	const char *attr_caption, const char *attr_l, const char *attr_r);

// columned output
void vs_cols_layout (const VCOL* cols, VCOLW *ws, int n);	/// calculate VCOL to fit current screen in ws
void vs_cols	    (const VCOL* cols, const VCOLW *ws, int n, ...);

// VREF: save and storing temporary objects (restore will also free object).
VREFSCR	vscr_save   (void);
void	vscr_restore(VREFSCR);
VREFCUR vcur_save   (void);
void	vcur_restore(VREFCUR);

// moved from stuff
int  show_file(const char *filename, int y, int lines, int mode);
int  show_80x24_screen(const char *filename);
int  search_num(int ch, int max);
void cursor_clear(int row, int column);
void cursor_show(int row, int column);
int  cursor_key(int row, int column);
void printdash(const char *mesg, int msglen);
void show_help(const char * const helptext[]);
void show_helpfile(const char * helpfile);
void outmsg(const char *msg);

#endif // _VTUIKIT_H
