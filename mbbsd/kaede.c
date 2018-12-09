#include "bbs.h"

// TODO move stuff to libbbs(or util)/string.c, ...
// this file can be removed (or not?)

/* Jaky */
void
out_lines(const char *str, int line, int col)
{
    int y = vgety();
    move(y, col);
    while (*str && line) {
        if (*str == '\n')
        {
            move(++y, col);
            line--;
        } else
        {
            outc(*str);
        }
        str++;
    }
}

void
outmsg(const char *msg)
{
    move(b_lines - msg_occupied, 0);
    clrtoeol();
    outs(msg);
}

// vim:ts=4:expandtab
