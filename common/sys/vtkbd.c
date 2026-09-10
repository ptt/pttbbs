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
 * ---------------------------------------------------------------------------
 * References:
 *  http://support.dell.com/support/edocs/systems/pe2650/en/ug/5g387ad0.htm
 *  http://aperiodic.net/phil/archives/Geekery/term-function-keys.html
 *  http://publib.boulder.ibm.com/infocenter/iseries/v5r3/index.jsp?topic=/rzaiw/rzaiwvt220opmode.htm
 *  http://www.rebol.com/docs/core23/rebolcore-18.html
 *  http://ascii-table.com/ansi-escape-sequences-vt-100.php
 *  http://web.mit.edu/gnu/doc/html/screen_10.html
 *  http://vt100.net/docs/vt220-rm/chapter3.html
 *  http://inwap.com/pdp10/ansicode.txt
 *  http://www.connectrf.com/Documents/vt220.html
 *  http://www.ibb.net/~anne/keyboard.html
 *  http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 *  http://invisible-island.net/xterm/xterm.faq.html
 *  http://www.vim.org/htmldoc/term.html
 *  http://wiki.archlinux.org/index.php/Why_don%27t_my_Home_and_End_keys_work_in_terminals%3F
 *  http://yz.kiev.ua/www/etc/putty/Section3.5.html
 *  PuTTY Source < terminal.c, term_key() >
 *  Termcap
 * ---------------------------------------------------------------------------
 * * BS/DEL Rules
 *   The BackSpace, Erase(<X]), and Delete keys are special due to history...
 *    - on vt220,       BS=0x7F, Delete=ESC[3~  (screen/xterm/PuTTY/PCMan)
 *    - on vt100,       BS=0x7F
 *    - on vt100/xterm, BS=0x08, Delete=0x7F    (VMX/Windows/DOS telnet/KKMan)
 *   So we define 
 *      KEY_BS  = BACKSPACE/ERASE = 0x08, 0x7F
 *      KEY_DEL = DELETE          = ESC[3~
 *
 * * CR/LF  Rules
 *   The traditional newline maps to following combination...
 *   - UNIX, LF
 *   - Win,  CR+LF
 *   - Mac,  CR
 *   When it comes to terminal, most terminal sends CR or CR+LF for ENTER key.
 *   To simply processing, we treat CR+LF as single key stroke (CR).
 *   There were reports that some users getting double ENTERs if we take LF
 *   as ENTER, so we decided reject LF. We are not sure if there is any 
 *   clients sending LF only, but according the the compatibility test, 
 *   most modern clients send only CR (or CR+LF).
 *   So we define
 *      KEY_CR  = CR, CR+LF
 *      KEY_LF  = ignored.
 *
 * * Editing Keys (Home/End/Ins/Del/PgUp/PgDn, Find/Select/Ins/Prev/Remove/Next):
 *   Some old terminals use location mapping instead of mnemonic mapping:
 *     http://invisible-island.net/xterm/xterm.faq.html#xterm_keypad
 *   Well.... I decide to follow the mnemonic mapping. Unfortunately some
 *   terminals (may include gnome-terminal, as I've heard) may get into trouble.
 * ---------------------------------------------------------------------------
 * * The complete list to support:
 *   - Up/Down/Right/Left:          <Esc> [ <A-D>       | <Esc> O <A-D> (app)
 *   - Home/Ins/Del/End/PgUp/PgDn:  <Esc> [ <1~6> ~
 *   - Shift-TAB:                   <Esc> [ Z
 *   - F1~F4:                       <Esc> [ 1 <1234> ~  | <Esc> O <PQRS>  
 *   - F5:                          <Esc> [ 1 <5> ~ 
 *   - F6-F8:                       <Esc> [ 1 <789> ~
 *   - F9-F12:                      <Esc> [ 2 <0134> ~
 *   - Num 0-9 *+,-./=ENTER:        <Esc> O <pqrstuvwxyjklmnoXM>
 *   - (SCO) End/PgDn/Home/PgUp/Ins <Esc> [ <FGHIL>
 *   - (SCO) Del                    <0x7F>
 *   - (Xterm) HOME/END             <Esc> <[O> <HF>
     - (rxvt)  HOME/END             <Esc> [ <78> ~
 *   - (putty-rxvt) HOME            <Esc> [ H
 *   - (putty-rxvt) END             <Esc> O w
 *   - (Old Term?) Home/Ins/Del/End/PgUp/PgDn:  <Esc> [ <214536> ~  // not supported
 *   - (vt220) <Esc> [ 0 Z or <Esc> 0 Z ? // not supported to prevent conflicting Esc-?
 *
 *   Note: we don't support some rare terms like <Esc> O <TUVWXYZA> described 
 *   in Dell 2650 in order to prevent confusion. 
 *   Num pad is also always converted to digits.
 */

#include <assert.h>
#include <string.h>
#include "vtkbd.h"

/* VtkbdCtx.state */
typedef enum {
    VKSTATE_NORMAL = 0,
    VKSTATE_ESC,        // <Esc>
    VKSTATE_ESC_APP,    // <Esc> O (SS3)
    VKSTATE_CSI,        // <Esc> [ (ECMA-48 Control Sequence Introducer)
}   VKSTATES;

#define VKRAW_BS    0x08    // \b = Ctrl('H')
#define VKRAW_ERASE 0x7F    // <X]

static void
vtkbd_reset_csi(VtkbdCtx *ctx)
{
    ctx->csi_prefix = 0;
    ctx->csi_param_count = 0;
    ctx->csi_has_param = 0;
    ctx->csi_inter_count = 0;
    ctx->csi_len = 0;
    memset(ctx->csi_params, 0, sizeof(ctx->csi_params));
    memset(ctx->csi_intermediate, 0, sizeof(ctx->csi_intermediate));
}

/* the processor API */
int 
vtkbd_process(int c, VtkbdCtx *ctx)
{
    switch (ctx->state)
    {
        case VKSTATE_NORMAL:    // original state
            if (c == KEY_ESC)
            {
                ctx->state = VKSTATE_ESC;
                return KEY_INCOMPLETE;
            }

            // simple mappings
            switch (c) {
                // BS/ERASE/DEL Rules
                case VKRAW_BS:
                case VKRAW_ERASE:
                    return KEY_BS;
            }
            return c;

        case VKSTATE_ESC:       // <Esc>
            switch (c) {
                case '[':
                    vtkbd_reset_csi(ctx);
                    ctx->state = VKSTATE_CSI;
                    return KEY_INCOMPLETE;

                case 'O':
                    ctx->state = VKSTATE_ESC_APP;
                    return KEY_INCOMPLETE;
            }

            // XXX should we map this into another section of KEY_ESC_* ?
            ctx->esc_arg = c;
            ctx->state = VKSTATE_NORMAL;
            return KEY_ESC;

        case VKSTATE_ESC_APP:   // <Esc> O

            switch (c) {
                case 'A':
                case 'B':
                case 'C':
                case 'D':
                    ctx->state = VKSTATE_NORMAL;
                    return KEY_UP + (c - 'A');

                    // SCO
                case 'H':
                    ctx->state = VKSTATE_NORMAL;
                    return KEY_HOME;
                case 'F':
                    ctx->state = VKSTATE_NORMAL;
                    return KEY_END;
                case 'G':
                    ctx->state = VKSTATE_NORMAL;
                    return KEY_PGDN;
                case 'I':
                    ctx->state = VKSTATE_NORMAL;
                    return KEY_PGUP;
                case 'L':
                    ctx->state = VKSTATE_NORMAL;
                    return KEY_INS;

                case 'P':
                case 'Q':
                case 'R':
                case 'S':
                    ctx->state = VKSTATE_NORMAL;
                    return KEY_F1 + (c - 'P');

                    // rxvt style DELETE
                case 'w':
                    ctx->state = VKSTATE_NORMAL;
                    return KEY_DEL;

                    // Num pads: was always converted to NumLock=ON
                    // However we let 'w' map to DEL..
                    // XXX the 'w' may be used as Delete...
                case 'p': case 'q': case 'r': case 's':
                case 't': case 'u': case 'v': 
                // case 'w':
                case 'x': case 'y':
                    ctx->state = VKSTATE_NORMAL;
                    return '0' + (c - 'p');

                case 'M':
                    ctx->state = VKSTATE_NORMAL;
                    return KEY_ENTER;

                case 'X':
                    ctx->state = VKSTATE_NORMAL;
                    return '=';

                case 'j': case 'k': case 'l': case 'm': 
                case 'n': case 'o':
                    {
                        static const char *numx = "*+,-./";
                        assert( c >= 'j' && (c-'j') < (int)strlen(numx));
                        ctx->state = VKSTATE_NORMAL;
                        return numx[c-'j'];
                    }
            }
            break;

        case VKSTATE_CSI:   // <Esc> [ P...P I...I F (ECMA-48 Control Sequence)
            if (c == KEY_ESC) {
                // Interrupted by new ESC, restart ESC state
                ctx->state = VKSTATE_ESC;
                return KEY_INCOMPLETE;
            }

            if (++ctx->csi_len > 64) {
                // Sequence too long / malformed; abort to prevent hang
                ctx->state = VKSTATE_NORMAL;
                return KEY_UNKNOWN;
            }

            if (c < 0x20 || c == 0x7F) {
                // Unexpected control character inside CSI sequence.
                // Abort sequence and process control character.
                ctx->state = VKSTATE_NORMAL;
                if (c == VKRAW_BS || c == VKRAW_ERASE)
                    return KEY_BS;
                return c;
            }

            if (c > 0x7E) {
                // Non-ASCII byte; abort sequence
                ctx->state = VKSTATE_NORMAL;
                return KEY_UNKNOWN;
            }

            // Parameter characters: 3/0 to 3/15
            // (0x30 to 0x3F: '0'-'9', ':', ';', '<', '=', '>', '?')
            if (c >= 0x30 && c <= 0x3F) {
                if (ctx->csi_inter_count > 0) {
                    // Parameters cannot follow intermediate characters in ECMA-48
                    return KEY_INCOMPLETE;
                }
                if (ctx->csi_len == 1 && (c == '<' || c == '?' || c == '=' || c == '>')) {
                    ctx->csi_prefix = c;
                    return KEY_INCOMPLETE;
                }
                if (c == ';' || c == ':') {
                    if (ctx->csi_param_count < VTKBD_MAX_PARAMS)
                        ctx->csi_param_count++;
                    ctx->csi_has_param = 0;
                    return KEY_INCOMPLETE;
                }
                if (c >= '0' && c <= '9') {
                    if (!ctx->csi_has_param) {
                        ctx->csi_has_param = 1;
                        if (ctx->csi_param_count == 0)
                            ctx->csi_param_count = 1;
                    }
                    int idx = (ctx->csi_param_count > 0) ? ctx->csi_param_count - 1 : 0;
                    if (idx < VTKBD_MAX_PARAMS) {
                        ctx->csi_params[idx] = ctx->csi_params[idx] * 10 + (c - '0');
                    }
                    return KEY_INCOMPLETE;
                }
                // Other parameter characters (e.g. private marker not at start)
                return KEY_INCOMPLETE;
            }

            // Intermediate characters: 2/0 to 2/15
            // (0x20 to 0x2F: space, '!', '"', '#', '$', '%', '&', '\'',
            // '(', ')', '*', '+', ',', '-', '.', '/')
            if (c >= 0x20 && c <= 0x2F) {
                if (ctx->csi_inter_count < VTKBD_MAX_INTERMEDIATE)
                    ctx->csi_intermediate[ctx->csi_inter_count++] = (char)c;
                return KEY_INCOMPLETE;
            }

            // Final character: 4/0 to 7/14 (0x40 to 0x7E: '@' to '~')
            if (c >= 0x40 && c <= 0x7E) {
                ctx->state = VKSTATE_NORMAL;

                // 1. XTerm SGR Mouse: ESC [ < btn ; col ; row M/m
                if (ctx->csi_prefix == '<' && (c == 'M' || c == 'm')) {
                    int btn_raw = ctx->csi_params[0];
                    int col = (ctx->csi_param_count > 1) ? ctx->csi_params[1] : 0;
                    int row = (ctx->csi_param_count > 2) ? ctx->csi_params[2] : 0;

                    ctx->mouse.x = (col > 0) ? (col - 1) : 0;
                    ctx->mouse.y = (row > 0) ? (row - 1) : 0;
                    ctx->mouse.is_release = (c == 'm');
                    ctx->mouse.is_motion = (btn_raw & 32) ? 1 : 0;
                    ctx->mouse.flags = btn_raw & (4 | 8 | 16);

                    if (btn_raw & MOUSE_BTN_WHEEL_UP) {
                        ctx->mouse.button = (btn_raw & 1) ?
                                MOUSE_BTN_WHEEL_DOWN : MOUSE_BTN_WHEEL_UP;
                    } else {
                        ctx->mouse.button = btn_raw & 3;
                    }

                    if (c == 'm')
                        return KEY_MOUSE_RELEASE;
                    return KEY_MOUSE;
                }

                // If private prefix is present (e.g. '?', '=', '>'), unhandled above -> drop
                if (ctx->csi_prefix != 0) {
                    return KEY_UNKNOWN;
                }

                // Standard sequences (no private prefix)
                switch (c) {
                    // Directions: Up, Down, Right, Left (also handles modified arrows like Ctrl-Up ESC [ 1;5A)
                    case 'A':
                    case 'B':
                    case 'C':
                    case 'D':
                        return KEY_UP + (c - 'A');

                    // SCO / XTerm cursor keys
                    case 'H':
                        return KEY_HOME;
                    case 'F':
                        return KEY_END;
                    case 'G':
                        return KEY_PGDN;
                    case 'I':
                        return KEY_PGUP;
                    case 'L':
                        return KEY_INS;

                    // Shift-TAB
                    case 'Z':
                        return KEY_STAB;

                    // Tilde sequences: ESC [ <param> ~
                    case '~': {
                        int p0 = ctx->csi_params[0];
                        switch (p0) {
                            case 1:
                                return KEY_HOME;
                            case 2:
                                return KEY_INS;
                            case 3:
                                return KEY_DEL;
                            case 4:
                                return KEY_END;
                            case 5:
                                return KEY_PGUP;
                            case 6:
                                return KEY_PGDN;
                            case 7:
                                return KEY_HOME;
                            case 8:
                                return KEY_END;

                            // F1 .. F5
                            case 11: case 12: case 13: case 14: case 15:
                                return KEY_F1 + (p0 - 11);

                            // F6 .. F8
                            case 17: case 18: case 19:
                                return KEY_F6 + (p0 - 17);

                            // F9 .. F10
                            case 20: case 21:
                                return KEY_F9 + (p0 - 20);

                            // F11 .. F12
                            case 23: case 24:
                                return KEY_F11 + (p0 - 23);

                            default:
                                return KEY_UNKNOWN;
                        }
                    }

                    default:
                        return KEY_UNKNOWN;
                }
            }

            // Fallback for any other unexpected character
            ctx->state = VKSTATE_NORMAL;
            return KEY_UNKNOWN;

        default:
            assert(!"unknown vkstate");
            break;
    }

    // what to do now?
    ctx->state = VKSTATE_NORMAL;
    return KEY_UNKNOWN;
}

const vtkbd_mouse_t *
vtkbd_get_mouse(const VtkbdCtx *ctx)
{
    return ctx ? &ctx->mouse : NULL;
}

ssize_t 
vtkbd_ignore_dbcs_evil_repeats(const unsigned char *buf, ssize_t len)
{
    // determine DBCS repeats by evil clients 
    // NOTE: this is usually invoked before vtkbd_process,
    // so we have to deal with the raw sequence.
    if (len == 2)
    {
	// XXX len==2 is dangerous. hope we are not in telnet IAC state...
	if (buf[0] != buf[1])
	    return len;

        // targest here:
        //  - VKRAW_BS
        //  - VKRAW_ERASE
        //  - Ctrl('D')     (KKMan3 also treats Ctrl('D') as DBCS DEL)
	if (buf[0] == VKRAW_BS ||
	    buf[0] == VKRAW_ERASE ||
	    buf[0] == Ctrl('D'))
	    return len/2;
    } 
    else if (len == 6)
    {
	// RIGHT:   KEY_ESC "OC" or KEY_ESC "[C"
	// LEFT:    KEY_ESC "OD" or KEY_ESC "[D"
	if (buf[2] != 'C' && buf[2] != 'D')
	    return len;

	if ( buf[0] == KEY_ESC &&
	    (buf[1] == '[' || buf[1] == 'O') &&
	     buf[0] == buf[3] &&
	     buf[1] == buf[4] &&
	     buf[2] == buf[5])
	    return len/2;
    } 
    else if (len == 8)
    {
	// DEL:	    ESC_STR "[3~" // vt220
	if (buf[0] != KEY_ESC ||
            buf[2] != '3' ||
	    buf[1] != '[' ||
	    buf[3] != '~')
            return len;

        if( buf[4] == buf[0] &&
	    buf[5] == buf[1] &&
	    buf[6] == buf[2] &&
	    buf[7] == buf[3])
	    return len/2;
    }
    return len;
}

// vim:sw=4:sw=4:et
