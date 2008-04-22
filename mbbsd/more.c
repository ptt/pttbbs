/* $Id$ */
#include "bbs.h"

/*
 * more.c
 * a mini pager in 130 lines of code, or stub of the huge pager pmore.
 * Author: Hung-Te Lin (piaip), April 2008.
 *
 * Copyright (c) 2008 Hung-Te Lin <piaip@csie.ntu.edu.tw>
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

#ifdef USE_PMORE
/* use new pager: piaip's more. */
int more(const char *fpath, int promptend)
{
    int r = pmore(fpath, promptend);

    switch(r)
    {

	case RET_DOSYSOPEDIT:
	    r = FULLUPDATE;

	    if (!HasUserPerm(PERM_SYSOP) ||
		    strcmp(fpath, "etc/ve.hlp") == 0)
		break;

#ifdef BN_SECURITY
	    if (strcmp(currboard, BN_SECURITY) == 0)
		break;
#endif // BN_SECURITY

	    log_filef("log/security", LOG_CREAT,
		    "%u %24.24s %d %s admin edit file=%s\n", 
		    (int)now, ctime4(&now), getpid(), cuser.userid, fpath);

	    // no need to allow anything...
	    // at least, no need to change title.
	    vedit2(fpath, NA, NULL, 0);
	    break;

	case RET_SELECTBRD:
	    r = FULLUPDATE;
	    if (HasUserPerm(PERM_BASIC))
	    {
		if (currstat == READING)
		    return Select();
	    }
	    break;

	case RET_COPY2TMP:
	    r = FULLUPDATE;
	    if (HasUserPerm(PERM_BASIC))
	    {
		char buf[PATHLEN];
		getdata(b_lines - 1, 0, "把這篇文章收入到暫存檔？[y/N] ",
			buf, 4, LCECHO);
		if (buf[0] != 'y')
		    break;
		setuserfile(buf, ask_tmpbuf(b_lines - 1));
		Copy(fpath, buf);
	    }
	    break;

	case RET_DOCHESSREPLAY:
	    r = FULLUPDATE;
	    if (HasUserPerm(PERM_BASIC))
	    {
		ChessReplayGame(fpath);
	    }
	    break;

#if defined(USE_BBSLUA)
	case RET_DOBBSLUA:
	    r = FULLUPDATE;
	    if (HasUserPerm(PERM_BASIC))
	    {
		bbslua(fpath);
	    }
	    break;
#endif
    }

    return r;
}

#else  // !USE_PMORE

// minimore: a mini pager in exactly 130 lines
#define PAGER_MAXLINES (2048)
int more(const char *fpath, int promptend)
{
    FILE *fp = fopen(fpath, "rt");
    int  lineno = 0, lines = 0, oldlineno = -1;
    int  i = 0, abort = 0, showall = 0, colorize = 0;
    int  lpos[PAGER_MAXLINES] = {0}; // line position
    char buf [ANSILINELEN];

    if (!fp) return -1; 
    clear();

    if (promptend == NA) {	    // quick print one page
	for (i = 0; i < t_lines-1; i++)
	    if (!fgets(buf, sizeof(buf), fp)) 
		break; 
	    else 
		outs(buf);
	fclose(fp); 
	return 0;
    }
    // YEA mode: pre-read
    while (lines < PAGER_MAXLINES-1 && 
	   fgets(buf, sizeof(buf), fp) != NULL)
	lpos[++lines] = ftell(fp);
    rewind(fp);

    while (!abort) 
    {
	if (oldlineno != lineno)    // seek and print
	{ 
	    clear(); 
	    showall = 0; 
	    oldlineno = lineno;
	    fseek(fp, lpos[lineno], SEEK_SET);

	    for (i = 0, buf[0] = 0; i < t_lines-1; i++, buf[0] = 0) 
	    {
		if (!showall) 
		{
		    fgets(buf, sizeof(buf), fp);
		    if (lineno + i == 0 && 
			(strncmp(buf, STR_AUTHOR1, strlen(STR_AUTHOR1))==0 ||
			 strncmp(buf, STR_AUTHOR2, strlen(STR_AUTHOR2))==0))
			colorize = 1;
		}

		if (!buf[0]) 
		{
		    outs("\n");
		    showall = 1;
		} else {
		    // dirty code to render heeader
		    if (colorize && lineno+i < 4 && *buf && 
			    *buf != '\n' && strchr(buf, ':'))
		    {
			char *q1 = strchr(buf, ':');
			int    l = t_columns - 2 - strlen(buf);
			char *q2 = strstr(buf, STR_POST1);

			chomp(buf); 
			if (q2 == NULL) q2 = strstr(buf, STR_POST2);
			if (q2)	    { *(q2-1) = 0; q2 = strchr(q2, ':'); }
			else q2 = q1;

			*q1++ = 0;	*q2++ = 0; 
			if (q1 == q2)	 q2 = NULL;

			outs(ANSI_COLOR(34;47) " ");
			outs(buf); outs(" " ANSI_REVERSE); 
			outs(q1);  prints("%*s", l, ""); q1 += strlen(q1);

			if (q2) {
			    outs(ANSI_COLOR(0;34;47) " ");  outs(q1+1);
			    outs(" " ANSI_REVERSE);	    outs(q2);
			}
			outs(ANSI_RESET"\n");
		    } else 
			outs(buf);
		}
	    }
	    if (lineno + i >= lines) 
		showall = 1;

	    // print prompt bar
	    snprintf(buf, sizeof(buf),
		    "  瀏覽 P.%d  ", 1 + (lineno / (t_lines-2)));
	    vs_footer(buf, 
	    " (→↓[PgUp][PgDn][Home][End])游標移動\t(←/q)結束");
	}
	// process key
	switch(vkey()) {
	    case KEY_UP: case 'j': case Ctrl('P'):
		if (lineno == 0) abort = READ_PREV;
		lineno--;		    
		break;

	    case KEY_PGUP: case Ctrl('B'):
		if (lineno == 0) abort = READ_PREV;
		lineno -= t_lines-2;	    
		break;

	    case KEY_PGDN: case Ctrl('F'): case ' ':
	    case KEY_RIGHT:
		if (showall) return READ_NEXT;
		lineno += t_lines-2;	    
		break;

	    case KEY_DOWN: case 'k': case Ctrl('N'):
		if (showall) return READ_NEXT;
		lineno++;		    
		break;
	    case KEY_HOME: case Ctrl('A'):
		lineno = 0;		    
		break;
	    case KEY_END: case Ctrl('E'):
		lineno = lines - (t_lines-1); 
		break;
	    case KEY_LEFT: case 'q':
		abort = -1;		    
		break;
	}
	if (lineno + (t_lines-1) >= lines)
	    lineno = lines-(t_lines-1);
	if (lineno < 0)
	    lineno = 0;
    }
    fclose(fp);
    return abort > 0 ? abort : 0;
}

#endif // !USE_PMORE
