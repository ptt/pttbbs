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

#ifndef VTKBD_H
#define VTKBD_H

#include <sys/types.h>
#include "cmsys.h"

/* Mouse tracking mode definitions (XTerm DECSET / DECRST) */
#define MOUSE_MODE_NONE         (0)
#define MOUSE_MODE_CLICK        (1000)  /* Normal mouse tracking: press and release */
#define MOUSE_MODE_DRAG         (1002)  /* Button-event mouse tracking: press, release, drag */
#define MOUSE_MODE_TRACK        (1003)  /* Any-event mouse tracking: all motion, press, release */
#define MOUSE_MODE_LOCATION     MOUSE_MODE_TRACK

/* Mouse button definitions */
#define MOUSE_BTN_LEFT          (0)
#define MOUSE_BTN_MIDDLE        (1)
#define MOUSE_BTN_RIGHT         (2)
#define MOUSE_BTN_WHEEL_UP      (64)
#define MOUSE_BTN_WHEEL_DOWN    (65)
#define MOUSE_WHEEL_UP          MOUSE_BTN_WHEEL_UP
#define MOUSE_WHEEL_DOWN        MOUSE_BTN_WHEEL_DOWN

/* Mouse event definitions */
typedef struct {
    int     x;          /* 0-based column */
    int     y;          /* 0-based row */
    int     button;     /* MOUSE_BTN_LEFT, MOUSE_BTN_MIDDLE, MOUSE_BTN_RIGHT,
                           MOUSE_BTN_WHEEL_UP, MOUSE_BTN_WHEEL_DOWN */
    int     is_release; /* 1 if release ('m'), 0 if press/motion ('M') */
    int     is_motion;  /* 1 if motion/hover */
    int     flags;      /* modifier flags (shift=4, meta=8, ctrl=16) */
} vtkbd_mouse_t;

#define VTKBD_MAX_PARAMS 8
#define VTKBD_MAX_INTERMEDIATE 4

/* context definition */
typedef struct {
    int     state;
    int     esc_arg;
    vtkbd_mouse_t mouse;
    /* ECMA-48 / CSI parsing state */
    int     csi_prefix;         /* Leading parameter char: '<', '?', '=', '>', etc. */
    int     csi_params[VTKBD_MAX_PARAMS];
    int     csi_param_count;
    int     csi_has_param;      /* 1 if current param has digits */
    int     csi_inter_count;
    char    csi_intermediate[VTKBD_MAX_INTERMEDIATE];
    int     csi_len;            /* Total sequence length to prevent overflow */
    int     mb_buf;
    utf8_ctx utf8;
} VtkbdCtx;

/* vtkbd API */
int     vtkbd_process(int c, VtkbdCtx *ctx);
ssize_t vtkbd_ignore_dbcs_evil_repeats(const unsigned char *buf, ssize_t len);
const vtkbd_mouse_t *vtkbd_get_mouse(const VtkbdCtx *ctx);

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

/*
 * Virtual key codes are placed in the intersection of:
 *   1. UTF-16 Surrogates (0xD800 .. 0xDFFF, never valid Unicode scalar values)
 *   2. Invalid Big5 low bytes (lo = 0x00 .. 0x3F, never valid Big5 trailing bytes)
 * so they never collide with ASCII, Big5 16-bit words, or Unicode codepoints.
 */
#define KEY_SPECIAL_BASE    0xD800
#define IS_SPECIAL_KEY(c)   (((c) & 0xF800) == KEY_SPECIAL_BASE)
#define vkey_isprint(c)     (isascii(c) ? isprint(c) : ((c) > 0 && !IS_SPECIAL_KEY(c)))

/* arrow keys (must follow vt100 ordering) */
#define KEY_UP          0xD901
#define KEY_DOWN        0xD902
#define KEY_RIGHT       0xD903
#define KEY_LEFT        0xD904

#define KEY_STAB        0xD909  /* shift-tab */

/* 6 extended keys (must follow vt220 ordering) */
#define KEY_HOME        0xDA01
#define KEY_INS         0xDA02
#define KEY_DEL         0xDA03
#define KEY_END         0xDA04
#define KEY_PGUP        0xDA05
#define KEY_PGDN        0xDA06

/* PFn/Fn function keys */
#define KEY_F1          0xDB01
#define KEY_F2          0xDB02
#define KEY_F3          0xDB03
#define KEY_F4          0xDB04
#define KEY_F5          0xDB05
#define KEY_F6          0xDB06
#define KEY_F7          0xDB07
#define KEY_F8          0xDB08
#define KEY_F9          0xDB09
#define KEY_F10         0xDB0A
#define KEY_F11         0xDB0B
#define KEY_F12         0xDB0C

// XXX TODO use 0x0?00 as 'META(alt)', for example 0x0?41 = META-A instead of esc_arg

/* vtkbd meta keys */
#define KEY_INCOMPLETE  0xDC20  /* 0x?20 to prevent accident usage */
#define KEY_UNKNOWN     0xDF20  /* unknown sequence */

/* mouse keys */
#define KEY_MOUSE       0xDD01  /* mouse event (press, motion, wheel) */
#define KEY_MOUSE_RELEASE 0xDD02 /* mouse button release */

/* vkey special data for additional fd to listen (ref: vkey_attach) */
#define I_TIMEOUT       0xDD3D /* additional fd timeout for select (replaced by vkey_poll */
#define I_OTHERDATA     0xDD3E /* data arrived in additional fd */

#endif // _VTKBD_H

// vim:ts=4:sw=4:et
