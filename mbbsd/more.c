/* $Id$ */
#include "bbs.h"

#ifndef USE_TRADITIONAL_MORE

#define USE_PIAIP_MORE

/* use new pager: piaip's more. */
int more(char *fpath, int promptend)
{
    return pmore(fpath, promptend);
}

#else

/* 把這兩個 size 調到一頁的範圍是不是能降低不必要的 IO ? */
#define MORE_BUFSIZE	4096
#define MORE_WINSIZE	4096
#define STR_ANSICODE    "[0123456789;,"

struct MorePool {
    int      base, size, head;
    unsigned char more_pool[MORE_BUFSIZE];
};

static const char    * const more_help[] = {
    "\0閱\讀文章功\能鍵使用說明",
    "\01游標移動功\能鍵",
    "(↑)                  上捲一行",
    "(↓)(Enter)           下捲一行",
    "(^B)(PgUp)(BackSpace) 上捲一頁",
    "(→)(PgDn)(Space)     下捲一頁",
    "(0)(g)(Home)          檔案開頭",
    "($)(G) (End)          檔案結尾",
    "\01其他功\能鍵",
    "(/)                   搜尋字串",
    "(n/N)                 重複正/反向搜尋",
    "(TAB)                 URL連結",
    "(Ctrl-T)              存到暫存檔",
    "(:/f/b)               跳至某頁/下/上篇",
    "(a/A)                 跳至同一作者下/上篇",
    "([-/]+)               主題式閱\讀 上/下",
    "(t)                   主題式循序閱\讀",
    "(q)(←)               結束",
    "(h)(H)(?)             輔助說明畫面",
    NULL
};

#if DEPRECATED__UNUSED
int             beep = 0;
#endif

static void
more_goto(int fd, struct MorePool *mp, off_t off)
{
    int             base = mp->base;

    if (off < base || off >= base + mp->size) {
	mp->base = base = off & (-MORE_WINSIZE);
	lseek(fd, base, SEEK_SET);
	mp->size = read(fd, mp->more_pool, MORE_BUFSIZE);
    }
    mp->head = off - base;
}

static int
more_readln(int fd, struct MorePool *mp, unsigned char *buf)
{
    int             ch;

    unsigned char  *data, *tail, *cc;
    int             len, bytes, in_ansi, in_big5;
    int             size, head, ansilen;

    len = bytes = in_ansi = in_big5 = ansilen = 0;
    tail = buf + ANSILINELEN - 1;
    size = mp->size;
    head = mp->head;
    data = &mp->more_pool[head];

    do {
	if (head >= size) {
	    mp->base += size;
	    data = mp->more_pool;
	    mp->size = size = read(fd, data, MORE_BUFSIZE);
	    if (size == 0)
		break;
	    head = 0;
	}
	ch = *data++;
	head++;
	bytes++;
	if (ch == '\n') {
	    break;
	}
	if (ch == '\t') {
	    do {
		*buf++ = ' ';
	    }
	    while ((++len & 7) && len < t_columns);
	} else if (ch == ESC_CHR) {
	    if (atoi((char *)(data + 1)) > 47) {
		if ((cc = (unsigned char *)strchr((char *)(data + 1), 'm')) != NULL) {
		    ch = cc - data + 1;

		    data += ch;
		    head += ch;
		    bytes += ch;
		}
	    } else {
		*buf++ = ch;
		in_ansi = 1;
	    }
	} else if (in_ansi) {
	    *buf++ = ch;
	    if (!strchr(STR_ANSICODE, ch))
		in_ansi = 0;
	} else if (in_big5) {
	    ++len;
	    *buf++ = ch;
	    in_big5 = 0;
	} else if (isprint2(ch)) {
	    len++;
	    *buf++ = ch;
	    if (ch >= 0xa1 && ch <= 0xf9) /* first byte of big5 encoding */
		in_big5 = 1;
	}
    } while (len < t_columns && buf < tail);

    if (in_big5 && len >= t_columns) {
	--buf;
	--head;
	--data;
	--bytes;
    }

    if (len == t_columns && head < size && *data == '\n') {
      /* XXX: not handle head==size, should read data */
      /* no extra newline dirty hack for exact 80byte line */
      data++; bytes++; head++;
    }
    *buf = '\0';
    mp->head = head;
    return bytes;
}

int
more(char *fpath, int promptend)
{
    char    *head[4] = {"作者", "標題", "時間", "轉信"};
    char           *ptr, *word = NULL, buf[ANSILINELEN + 1];
    struct stat     st;

    int             fd, fsize;

    unsigned int    pagebreak[MAX_PAGES], pageno, lino = 0;
    int             line, ch, viewed, pos, numbytes;
    int             header = 0;
    int             local = 0;
    char            search_char0 = 0;
    static char     search_str[81] = "";
    typedef char   *(*FPTR) ();
    static FPTR     fptr;
    int             searching = 0;
    int             scrollup = 0;
    char           *printcolor[3] = {"44", "33;45", "0;34;46"}, color = 0;
    struct MorePool mp;
    /* Ptt */

    STATINC(STAT_MORE);
    memset(pagebreak, 0, sizeof(pagebreak));
    if (*search_str)
	search_char0 = *search_str;
    *search_str = 0;

    fd = open(fpath, O_RDONLY, 0600);
    if (fd < 0)
	return -1;

    if (fstat(fd, &st) || ((fsize = st.st_size) <= 0) || S_ISDIR(st.st_mode)) {
	close(fd);
	//Ptt
	    return -1;
    }
    pagebreak[0] = pageno = viewed = line = pos = 0;
    clear();

    /* rocker */

    mp.base = mp.head = mp.size = 0;

    while ((numbytes = more_readln(fd, &mp, (unsigned char *)buf)) || (line == t_lines)) {
	if (scrollup) {
	    rscroll();
	    move(0, 0);
	}
	if (numbytes) {		/* 一般資料處理 */
	    if (!viewed) {	/* begin of file */
		if (!strncmp(buf, str_author1, LEN_AUTHOR1)) {
		    line = 3;
		    word = buf + LEN_AUTHOR1;
		    local = 1;
		} else if (!strncmp(buf, str_author2, LEN_AUTHOR2)) {
		    line = 4;
		    word = buf + LEN_AUTHOR2;
		}
		while (pos < line) {
		    if (!pos && ((ptr = strstr(word, str_post1)) ||
				(ptr = strstr(word, str_post2)))) {
			ptr[-1] = '\0';
			prints(ANSI_COLOR(47;34) " %s " ANSI_COLOR(44;37) "%-53.53s"
				ANSI_COLOR(47;34) " %.4s " ANSI_COLOR(44;37) "%-13s" ANSI_RESET "\n",
				head[0], word, ptr, ptr + 5);
		    } else if (pos < line)
			prints(ANSI_COLOR(47;34) " %s " ANSI_COLOR(44;37) "%-72.72s"
				ANSI_RESET "\n", head[pos], word);

		    viewed += numbytes;
		    numbytes = more_readln(fd, &mp, (unsigned char *)buf);

		    /* 第一行太長了 */
		    if (!pos && viewed > 79) {
			/* 第二行不是 [標....] */
			if (memcmp(buf, head[1], 2)) {
			    /* 讀下一行進來處理 */
			    viewed += numbytes;
			    numbytes = more_readln(fd, &mp, (unsigned char *)buf);
			}
		    }
		    pos++;
		}
		if (pos) {
		    header = 1;

		    prints(ANSI_COLOR(36) "%s" ANSI_RESET "\n", msg_seperator);
		    ++line;
		    ++pos;
		}
		lino = pos;
		word = NULL;
	    }
	    /* ※處理引用者 & 引言 */
	    if ((buf[0] == ':' || buf[0] == '>') && (buf[1] == ' '))
		word = ANSI_COLOR(36);
	    else if (!strncmp(buf, "※", 2) || !strncmp(buf, "==>", 3))
		word = ANSI_COLOR(32);

	    if (word)
		outs(word);
	    {
		char            msg[500], *pos;

		if (*search_str && (pos = (*fptr)(buf, search_str))) {
		    char            SearchStr[81];
		    char            buf1[100], *pos1;
		    int             search_str_len = strlen(search_str);

		    strlcpy(SearchStr, pos, search_str_len + 1);
		    searching = 0;
		    snprintf(msg, sizeof(msg),
			     "%.*s" ANSI_COLOR(7) "%s" ANSI_RESET, (int)(pos - buf), buf,
			     SearchStr);
		    while ((pos = (*fptr)(pos1 = pos + search_str_len,
				       search_str))) {
			snprintf(buf1, sizeof(buf1),
				 "%.*s" ANSI_COLOR(7) "%s" ANSI_RESET, (int)(pos - pos1),
				 pos1, SearchStr);
			strlcat(msg, buf1, sizeof(msg));
		    }
		    strlcat(msg, pos1, sizeof(msg));
		    outs(Ptt_prints(msg, sizeof(msg), NO_RELOAD));
		} else
		    outs(Ptt_prints(buf, sizeof(buf), NO_RELOAD));
	    }
	    if (word) {
		outs(ANSI_RESET);
		word = NULL;
	    }
	    outc('\n');

#if DEPRECATED__UNUSED
	    if (beep) {
		bell();
		beep = 0;
	    }
#endif
	    if (line < b_lines)	/* 一般資料讀取 */
		line++;

	    if (line == b_lines && searching == -1) {
		if (pageno > 0)
		    more_goto(fd, &mp, viewed = pagebreak[--pageno]);
		else
		    searching = 0;
		lino = pos = line = 0;
		clear();
		continue;
	    }
	    if (scrollup) {
		move(line = b_lines, 0);
		clrtoeol();
		for (pos = 1; pos < b_lines; pos++)
		    viewed += more_readln(fd, &mp, (unsigned char *)buf);
	    } else if (pos == b_lines)	/* 捲動螢幕 */
		scroll();
	    else
		pos++;

	    if (!scrollup                   &&
		++lino >= (unsigned)b_lines &&
		pageno < MAX_PAGES - 1         ) {
		pagebreak[++pageno] = viewed;
		lino = 1;
	    }
	    if (scrollup) {
		lino = scrollup;
		scrollup = 0;
	    }
	    viewed += numbytes;	/* 累計讀過資料 */
	} else
	    line = b_lines;	/* end of END */

	if (promptend &&
	    ((!searching && line == b_lines) || viewed == fsize)) {
	    /* Kaede 剛好 100% 時不停 */
	    move(b_lines, 0);
	    if (viewed == fsize) {
		if (searching == 1)
		    searching = 0;
		color = 0;
	    } else if (pageno == 1 && lino == 1) {
		if (searching == -1)
		    searching = 0;
		color = 1;
	    } else
		color = 2;

	    prints(ANSI_RESET ANSI_COLOR(%s) "  瀏覽 P.%d(%d%%)  %s  %-30.30s%s",
		   printcolor[(int)color],
		   pageno,
		   (int)((viewed * 100) / fsize),
		   ANSI_COLOR(31;47),
		   "(h)" ANSI_COLOR(30) "求助  " ANSI_COLOR(31) "→↓[PgUp][",
		   "PgDn][Home][End]" ANSI_COLOR(30) "游標移動  " ANSI_COLOR(31) "←[q]" ANSI_COLOR(30) "結束   " ANSI_RESET);


	    while (line == b_lines || (line > 0 && viewed == fsize)) {
		switch ((ch = igetch())) {
		case ':':
		    {
			char            buf[10];
			int             i = 0;

			getdata(b_lines - 1, 0, "Goto Page: ", buf, 5, DOECHO);
			sscanf(buf, "%d", &i);
			if (0 < i && i < MAX_PAGES && (i == 1 || pagebreak[i - 1]))
			    pageno = i - 1;
			else if (pageno)
			    pageno--;
			lino = line = 0;
			break;
		    }
		case '/':
		    {
			char            ans[4] = "n";

			*search_str = search_char0;
			getdata_buf(b_lines - 1, 0, "[搜尋]關鍵字:", search_str,
				    40, DOECHO);
			if (*search_str) {
			    searching = 1;
			    if (getdata(b_lines - 1, 0, "區分大小寫(Y/N/Q)? [N] ",
				   ans, sizeof(ans), LCECHO) && *ans == 'y')
				fptr = &strstr;
			    else
				fptr = &strcasestr;
			}
			if (*ans == 'q')
			    searching = 0;
			if (pageno)
			    pageno--;
			lino = line = 0;
			break;
		    }
		case 'n':
		    if (*search_str) {
			searching = 1;
			if (pageno)
			    pageno--;
			lino = line = 0;
		    }
		    break;
		case 'N':
		    if (*search_str) {
			searching = -1;
			if (pageno)
			    pageno--;
			lino = line = 0;
		    }
		    break;
		case 'r': // Ptt: put all reply/recommend function here
		case 'R':
		case 'Y':
		case 'y':
		    close(fd);
		    return 999;
		case 'X':
		    close(fd);
		    return 998;
		case 'A':
		    close(fd);
		    return AUTHOR_PREV;
		case 'a':
		    close(fd);
		    return AUTHOR_NEXT;
		case 'F':
		case 'f':
		    close(fd);
		    return READ_NEXT;
		case 'B':
		case 'b':
		    close(fd);
		    return READ_PREV;
		case KEY_LEFT:
		case 'q':
		    close(fd);
		    return FULLUPDATE;
		case ']':	/* Kaede 為了主題閱讀方便 */
		case '+':
		    close(fd);
		    return RELATE_NEXT;
		case '[':	/* Kaede 為了主題閱讀方便 */
		case '-':
		    close(fd);
		    return RELATE_PREV;
		case '=':	/* Kaede 為了主題閱讀方便 */
		    close(fd);
		    return RELATE_FIRST;
		case Ctrl('F'):
		case KEY_PGDN:
		    line = 1;
		    break;
		case 't':
		    if (viewed == fsize) {
			close(fd);
			return RELATE_NEXT;
		    }
		    line = 1;
		    break;
		case ' ':
		    if (viewed == fsize) {
			close(fd);
			return READ_NEXT;
		    }
		    line = 1;
		    break;
		case KEY_RIGHT:
		    if (viewed == fsize) {
			close(fd);
			return 0;
		    }
		    line = 1;
		    break;
		case '\r':
		case '\n':
		case KEY_DOWN:
		    if (viewed == fsize ||
			(promptend == 2 && (ch == '\r' || ch == '\n'))) {
			close(fd);
			return READ_NEXT;
		    }
		    line = t_lines - 2;
		    break;
		case '$':
		case 'G':
		case KEY_END:
		    line = t_lines;
		    break;
		case '0':
		case 'g':
		case KEY_HOME:
		    pageno = line = 0;
		    break;
		case 'h':
		case 'H':
		case '?':
		    /* Kaede Buggy ... */
		    show_help(more_help);
		    if (pageno)
			pageno--;
		    lino = line = 0;
		    break;
		case 'E':
		    if (HasUserPerm(PERM_SYSOP) && strcmp(fpath, "etc/ve.hlp")) {
			close(fd);
			vedit(fpath, NA, NULL);
			return 0;
		    }
		    break;

		case Ctrl('T'):
		    getdata(b_lines - 2, 0, "把這篇文章收入到暫存檔？[y/N] ",
			    buf, 4, LCECHO);
		    if (buf[0] == 'y') {
			setuserfile(buf, ask_tmpbuf(b_lines - 1));
                        Copy(fpath, buf);
		    }
		    if (pageno)
			pageno--;
		    lino = line = 0;
		    break;
		case KEY_UP:
		    line = -1;
		    break;
		case Ctrl('B'):
		case KEY_PGUP:
		    if (pageno > 1) {
			if (lino < 2)
			    pageno -= 2;
			else
			    pageno--;
			lino = line = 0;
		    } else if (pageno && lino > 1)
			pageno = line = 0;
		    break;
		case Ctrl('H'):
		    if (pageno > 1) {
			if (lino < 2)
			    pageno -= 2;
			else
			    pageno--;
			lino = line = 0;
		    } else if (pageno && lino > 1)
			pageno = line = 0;
		    else {
			close(fd);
			return READ_PREV;
		    }
		}
	    }

	    if (line > 0) {
		move(b_lines, 0);
		clrtoeol();
		refresh();
	    } else if (line < 0) {	/* Line scroll up */
		if (pageno <= 1) {
		    if (lino == 1 || !pageno) {
			close(fd);
			return READ_PREV;
		    }
		    if (header && lino <= 5) {
			more_goto(fd, &mp, viewed = pagebreak[scrollup = lino =
							 pageno = 0] = 0);
			clear();
		    }
		}
		if (pageno && (int)lino > 1 + local) {
		    line = (lino - 2) - local;
		    if (pageno > 1 && viewed == fsize)
			line += local;
		    scrollup = lino - 1;
		    more_goto(fd, &mp, viewed = pagebreak[pageno - 1]);
		    while (line--)
			viewed += more_readln(fd, &mp, (unsigned char *)buf);
		} else if (pageno > 1) {
		    scrollup = b_lines - 1;
		    line = (b_lines - 2) - local;
		    more_goto(fd, &mp, viewed = pagebreak[--pageno - 1]);
		    while (line--)
			viewed += more_readln(fd, &mp, (unsigned char *)buf);
		}
		line = pos = 0;
	    } else {
		pos = 0;
		more_goto(fd, &mp, viewed = pagebreak[pageno]);
		move(0, 0);
		clear();
	    }
	}
    }

    close(fd);
    if (promptend) {
	pressanykey();
	clear();
    } else
	outs(reset_color);
    return 0;
}
#endif
