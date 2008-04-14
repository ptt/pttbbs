// $Id$
#ifndef _VISIO_H
#define _VISIO_H

/*
 * visio.c
 * High-level virtual screen input output control
 */

#include "bbs.h"
#include "ansi.h"   // we need it.

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

// DATATYPE DEFINITION -------------------------------------------------
typedef void *	VSOREF;	    // generic visio object reference

// API DEFINITION ----------------------------------------------------
// int  vans(char *prompt);	// prompt at bottom and return y/n in lower case.
// void vs_bar(char *title);    // like stand_title
void vpad   (int n, const char *pattern);
void vbarf  (const char *s, ...)  GCC_CHECK_FORMAT(1,2);
void vbarlr (const char *l, const char *r);
int  vmsgf  (const char *fmt,...) GCC_CHECK_FORMAT(1,2);
int  vmsg   (const char *msg);
int  vansf  (const char *fmt,...) GCC_CHECK_FORMAT(1,2);
int  vans   (const char *msg);
void vshowmsg(const char *msg);

// vs_*: formatted and themed virtual screen layout
// you cannot use ANSI escapes in these APIs.
void vs_header	(const char *title,   const char *mid, const char *right);	// vs_head, showtitle
void vs_hdr	(const char *title);						// vs_bar,  stand_title
void vs_footer	(const char *caption, const char *prompt);

// compatible macros
#define stand_title vs_hdr

// VSOREF API
VSOREF	vscr_save   (void);
void	vscr_restore(VSOREF);
VSOREF  vcur_save   (void);
void	vcur_restore(VSOREF);

#endif // _VISIO_H
