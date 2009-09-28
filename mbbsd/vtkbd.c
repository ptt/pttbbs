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
 *  PuTTY Source < terminal.c, term_key() >
 *  Termcap
 * ---------------------------------------------------------------------------
 * * BS/DEL Rules
 *   The BackSpace, Erase(<X]), and Delete keys are special due to history...
 *    - on vt220,       BS=0x7F, Delete=ESC[3~  (screen/xterm/PuTTY)
 *    - on vt100,       BS=0x7F
 *    - on vt100/xterm, BS=0x08, Delete=0x7F    (Windows/VMX/DOS telnet)
 *   So we define 
 *      KEY_BS  = BACKSPACE/ERASE = 0x08, 0x7F
 *      KEY_DEL = DELETE          = ESC[3~
 *
 * * CR/LF  Rules
 *   The traditional newline maps to following combination...
 *   - UNIX, LF
 *   - Win,  CR+LF
 *   - Mac,  CR
 *   When it comes to terminal,CR LF should be treated as one. 
 *   There were reports that some users getting double ENTERs if we take LF
 *   as ENTER, so we decided reject LF. We are not sure if there is any 
 *   clients sending LF only, but according the the compatibility test, 
 *   most modern clients send only CR (or CR+LF).
 *   So we define
 *      KEY_CR  = CR, CR+LF
 *      KEY_LF  = ignored.
 * ---------------------------------------------------------------------------
 * * The complete list to support:
 *   - Up/Down/Right/Left:          <Esc> [ <A-D>       | <Esc> O <A-D> (app)
 *   - Home/Ins/Del/End/PgUp/PgDn:  <Esc> [ <1~6> ~
 *   - Shift-TAB:                   <Esc> [ Z           | <Esc> [ 0 Z
 *   - F1~F4:                       <Esc> [ 1 <1234> ~  | <Esc> O <PQRS>  
 *   - F5:                          <Esc> [ 1 <5> ~ 
 *   - F6-F8:                       <Esc> [ 1 <789> ~
 *   - F9-F12:                      <Esc> [ 2 <0134> ~
 *   - Num 0-9 *+,-./=ENTER:        <Esc> O <pqrstuvwxyjklmnoXM>
 *
 *   Note: we don't support some rare terms like <Esc> O <TUVWXYZA> described 
 *   in Dell 2650 in order to prevent confusion. 
 *   Num pad is also always converted to digits.
 */

#include "bbs.h"

/* VtkbdCtx.state */
typedef enum {
    VKSTATE_NORMAL = 0,
    VKSTATE_ESC,        // <Esc>
    VKSTATE_ESC_APP,    // <Esc> O
    VKSTATE_ESC_QUOTE,  // <Esc> [
    VKSTATE_ZERO,       // <Esc> [ 0    (wait Z)
    VKSTATE_ONE,        // <Esc> [ <1> 
    VKSTATE_TWO,        // <Esc> [ <2> 
    VKSTATE_TLIDE,      // <Esc> [ *    (wait ~, return esc_arg)
}   VKSTATES;

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
                case 0x7F:
                case 0x08:
                    return KEY_BS;
            }
            return c;

        case VKSTATE_ESC:       // <Esc>
            switch (c) {
                case '[':
                    ctx->state = VKSTATE_ESC_QUOTE;
                    return KEY_INCOMPLETE;

                case 'O':
                    ctx->state = VKSTATE_ESC_APP;
                    return KEY_INCOMPLETE;

                case '0':
                    ctx->state = VKSTATE_ZERO;
                    return KEY_INCOMPLETE;
            }

            // XXX should we map this into another section of KEY_ESC_* ?
            ctx->esc_arg = c;
            ctx->state = VKSTATE_NORMAL;
            return KEY_ESC;

        case VKSTATE_ZERO:  // <Esc> 0
            if (c != 'Z')
                break;

            ctx->state = VKSTATE_NORMAL;
            return KEY_STAB;

        case VKSTATE_ESC_APP:   // <Esc> O

            switch (c) {
                case 'A':
                case 'B':
                case 'C':
                case 'D':
                    ctx->state = VKSTATE_NORMAL;
                    return KEY_UP + (c - 'A');

                case 'P':
                case 'Q':
                case 'R':
                case 'S':
                    ctx->state = VKSTATE_NORMAL;
                    return KEY_F1 + (c - 'P');

                    // Num pads: always convert to NumLock=ON
                case 'p': case 'q': case 'r': case 's':
                case 't': case 'u': case 'v': case 'w':
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
                        assert( c >= 'j' && (c-'j') < strlen(numx));
                        ctx->state = VKSTATE_NORMAL;
                        return numx[c-'j'];
                    }
            }
            break;

        case VKSTATE_ESC_QUOTE:     // <Esc> [

            switch(c) {
                case 'A':
                case 'B':
                case 'C':
                case 'D':
                    ctx->state = VKSTATE_NORMAL;
                    return KEY_UP + (c - 'A');

                case '3':
                case '4':
                case '5':
                case '6':
                    ctx->state = VKSTATE_TLIDE;
                    ctx->esc_arg = KEY_DEL + (c - '3');
                    return KEY_INCOMPLETE;

                case 'Z':
                    ctx->state = VKSTATE_NORMAL;
                    return KEY_STAB;

                case '1':
                    ctx->state = VKSTATE_ONE;
                    return KEY_INCOMPLETE;
                case '2':
                    ctx->state = VKSTATE_TWO;
                    return KEY_INCOMPLETE;
            }
            break;

        case VKSTATE_ONE:   // <Esc> [ 1
            if (c == '~')
            {
                ctx->state = VKSTATE_NORMAL;
                return KEY_HOME;
            }

            switch(c) {
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                    ctx->state = VKSTATE_TLIDE;
                    ctx->esc_arg = KEY_F1 + c - '1'; // F1 .. F5
                    return KEY_INCOMPLETE;

                case '7':
                case '8':
                case '9':
                    ctx->state = VKSTATE_TLIDE;
                    ctx->esc_arg = KEY_F6 + c - '7'; // F6 .. F8
                    return KEY_INCOMPLETE;
            }
            break;

        case VKSTATE_TWO:   // <Esc> [ 2
            if (c == '~')
            {
                ctx->state = VKSTATE_NORMAL;
                return KEY_INS;         // HOME+1
            }

            switch(c) {
                case '0':
                case '1':
                    ctx->state = VKSTATE_TLIDE;
                    ctx->esc_arg = KEY_F9 + c - '0'; // F9 .. F10
                    return KEY_INCOMPLETE;

                case '3':
                case '4':
                    ctx->state = VKSTATE_TLIDE;
                    ctx->esc_arg = KEY_F11 + c - '3'; // F11 .. F12
                    return KEY_INCOMPLETE;
            }
            break;

        case VKSTATE_TLIDE:   // Esc [ <12> <0-9> ~
            if (c != '~')
                break;

            ctx->state = VKSTATE_NORMAL;
            return ctx->esc_arg;

        default:
            assert(!"unknown vkstate");
            break;
    }

    // what to do now?
    ctx->state = VKSTATE_NORMAL;
    return KEY_UNKNOWN;
}

// vim:sw=4:sw=4:et
