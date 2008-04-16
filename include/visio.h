// $Id$
#ifndef _VISIO_H
#define _VISIO_H

/*
 * visio.c
 * High-level virtual screen input output control
 */

#include "bbs.h"
#include "ansi.h"   // we need it.
#include <limits.h>

// THEME DEFINITION ----------------------------------------------------
#define VCLR_HEADER		ANSI_COLOR(1;37;46)	// was: TITLE_COLOR
#define VCLR_HEADER_MID		ANSI_COLOR(1;33;46)
#define VCLR_HEADER_RIGHT	ANSI_COLOR(1;37;46)
#define VCLR_HDR		ANSI_COLOR(1;37;46)
#define VCLR_FOOTER_CAPTION     ANSI_COLOR(0;34;46)
#define VCLR_FOOTER             ANSI_COLOR(0;30;47)
#define VCLR_FOOTER_QUOTE       ANSI_COLOR(0;31;47)
#define VCLR_MSG_FLOAT		ANSI_COLOR(1;33;46)
#define VCLR_MSG		ANSI_COLOR(1;36;44)
#define VCLR_PAUSE_PAD		ANSI_COLOR(1;34;44)
#define VCLR_PAUSE		ANSI_COLOR(1;37;44)

#define VMSG_PAUSE		" 請按任意鍵繼續 "
#define VMSG_PAUSE_PAD		"▄"
#define VMSG_MSG_FLOAT		" [按任意鍵繼續]"
#define VMSG_MSG_PREFIX		" ◆ "
#define VMSG_HDR_PREFIX		"【 "
#define VMSG_HDR_POSTFIX	" 】"

// CONSTANT DEFINITION -------------------------------------------------
#define VCOL_MAXW		(INT16_MAX)

#define VFILL_DEFAULT		(0x00)
#define VFILL_NO_ANSI		VFILL_DEFAULT
#define VFILL_HAS_ANSI		(0x01)
#define VVILL_LEFT_ALIGN	VFILL_DEFAULT
#define VFILL_RIGHT_ALIGN	(0x02)
#define VFILL_HAS_BORDER	VFILL_DEFAULT
#define VFILL_NO_BORDER		(0x08)

// DATATYPE DEFINITION -------------------------------------------------
typedef void *	VREFSCR;
typedef long	VREFCUR;

typedef short	VCOLW;
typedef struct {
    char *attr;	    // default attribute
    VCOLW minw;	    // minimal width
    VCOLW maxw;	    // max width

    struct {
	char has_ansi;	    // field data have ANSI escapes
	char right_align;   // align output to right side
	char usewhole;	    // draw entire column and prevent borders
    }   flags;

} VCOL;

// API DEFINITION ----------------------------------------------------

// curses flavor
void prints(const char *fmt, ...) GCC_CHECK_FORMAT(1,2);
void mvouts(int y, int x, const char *str);

// v*: primitive rendering
void vpad   (int n, const char *pattern);	    /// pad n fields by pattern
 int vgety  (void);				    /// return cursor position (y)
void vfill  (int n, int flags, const char *s);	    /// fill n-width space with s
void vfillf (int n, int flags, const char *s, ...) GCC_CHECK_FORMAT(3,4); // formatted version of vfill
void vbarlr (const char *l, const char *r);	    /// draw a left-right expanded bar with (l,r)
void vbarf  (const char *s, ...)  GCC_CHECK_FORMAT(1,2); /// vbarlr with formatted input (\t splits (l,r)
void vshowmsg(const char *msg);			    /// draw standard pause/message

// v*: input widgets
// int  vans(char *prompt);	// prompt at bottom and return y/n in lower case.
 int vmsg   (const char *msg);				    /// draw standard pause/message and return input
 int vmsgf  (const char *fmt,...) GCC_CHECK_FORMAT(1,2);    /// formatted input of vmsg
 int vans   (const char *msg);				    /// prompt and return (lowercase) single byte input
 int vansf  (const char *fmt,...) GCC_CHECK_FORMAT(1,2);    /// formatted input of vans


// vs_*: formatted and themed virtual screen layout
// you cannot use ANSI escapes in these APIs.
void vs_header	(const char *title,   const char *mid, const char *right);	// vs_head, showtitle
void vs_hdr	(const char *title);						// vs_bar,  stand_title
void vs_footer	(const char *caption, const char *prompt);

// columned output
void vs_cols_layout (const VCOL* cols, VCOLW *ws, int n);	/// calculate VCOL to fit current screen in ws
void vs_cols	    (const VCOL* cols, const VCOLW *ws, int n, ...);

// VREF: save and storing temporary objects (restore will also free object).
VREFSCR	vscr_save   (void);
void	vscr_restore(VREFSCR);
VREFCUR vcur_save   (void);
void	vcur_restore(VREFCUR);

#endif // _VISIO_H
