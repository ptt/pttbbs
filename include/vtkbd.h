/*
 * vtkbd.c
 * Virtual Terminal Keyboard
 *
 * piaip's new re-implementation of xterm/VT100/220/ANSI key input 
 * escape sequence parser for BBS
 *
 * Author: Hung-Te Lin (piaip)
 * Create: Wed Sep 23 15:06:43 CST 2009
 * ---------------------------------------------------------------------------
 * Copyright (c) 2009 Hung-Te Lin <piaip@csie.org>
 * All rights reserved.
 * Distributed under BSD license (GPL compatible).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 */

#ifndef _VTKBD_H
#define _VTKBD_H

#include <sys/types.h>

/* context definition */
typedef struct {
    int     state;
    int     esc_arg;
} VtkbdCtx;

/* vtkbd API */
int     vtkbd_process(int c, VtkbdCtx *ctx);
ssize_t vtkbd_ignore_dbcs_evil_repeats(const unsigned char *buf, ssize_t len);

/* key code macro */
#define Ctrl(c)         (c & 0x1F)

/* common ASCII compatible keys definition */
#define KEY_TAB         9
#define KEY_ESC         27
#define KEY_CR          ('\r')      // Ctrl('M'), 0x0D
#define KEY_LF          ('\n')      // Ctrl('J'), 0x0A, will be ignored.
#define KEY_ENTER       KEY_CR      // for backward compatibility

/* BS/ERASE/DEL, see vtkbd.c for the rules */
#define KEY_BS          (0x08)      // see vtkbd.c for BS/DEL Rules

/* arrow keys (must follow vt100 ordering) */
#define KEY_UP          0x0101
#define KEY_DOWN        0x0102
#define KEY_RIGHT       0x0103
#define KEY_LEFT        0x0104

#define KEY_STAB        0x0109  /* shift-tab */

/* 6 extended keys (must follow vt220 ordering) */
#define KEY_HOME        0x0201
#define KEY_INS         0x0202
#define KEY_DEL         0x0203
#define KEY_END         0x0204
#define KEY_PGUP        0x0205
#define KEY_PGDN        0x0206

/* PFn/Fn function keys */
#define KEY_F1          0x0301
#define KEY_F2          0x0302
#define KEY_F3          0x0303
#define KEY_F4          0x0304
#define KEY_F5          0x0305
#define KEY_F6          0x0306
#define KEY_F7          0x0307
#define KEY_F8          0x0308
#define KEY_F9          0x0309
#define KEY_F10         0x030A
#define KEY_F11         0x030B
#define KEY_F12         0x030C

// XXX TODO use 0x0?00 as 'META(alt)', for example 0x0?41 = META-A instead of esc_arg

/* vtkbd meta keys */
#define KEY_INCOMPLETE  0x0420  /* 0x?20 to prevent accident usage */
#define KEY_UNKNOWN     0x0F20  /* unknown sequence */

/* vkey special data for additional fd to listen (ref: vkey_attach) */
#define I_TIMEOUT       0x05fd /* additional fd timeout for select (replaced by vkey_poll */
#define I_OTHERDATA     0x05fe /* data arrived in additional fd */

#endif // _VTKBD_H

// vim:ts=4:sw=4:et
