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
#define VCLR_FOOTER_CAPTION     ANSI_COLOR(0;34;46)
#define VCLR_FOOTER             ANSI_COLOR(0;30;47)
#define VCLR_FOOTER_QUOTE       ANSI_COLOR(0;31;47)

// API DEFINITION ----------------------------------------------------
// int  vmsg(char *msg);
// int  vmsgf(const char *fmt,...);
// int  vans(char *prompt);	// prompt at bottom and return y/n in lower case.
// void vs_bar(char *title);   e// like stand_title
void vfooter(const char *caption, const char *prompt);
void vbarf  (const char *s, ...);
void vbar   (const char *l, const char *r);

#endif // _VISIO_H
