/* $Id */
#ifndef INCLUDE_ANSI_H
#define INCLUDE_ANSI_H

/* This is a more elegant style which is first introduced by pmore(piaip)
 * and then promoted by  Rong-en Fan (rafan). */

/* to help pmore know that we've already included this. */
#define PMORE_STYLE_ANSI 

// Escapes.
#define ESC_NUM (0x1b)
#define ESC_STR "\x1b"
#define ESC_CHR '\x1b'

// Common ANSI commands.
#define ANSI_RESET  ESC_STR "[m"
#define ANSI_COLOR(x) ESC_STR "[" #x "m"
#define ANSI_MOVETO(y,x) ESC_STR "[" #y ";" #x "H"
#define ANSI_CLRTOEND ESC_STR "[K"

#endif	/* INCLUDE_ANSI_H */

/* vim:sw=4
 */
