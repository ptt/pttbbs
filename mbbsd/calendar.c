/* $Id$ */
#include "bbs.h"

typedef struct event_t {
    int             year, month, day, days;
    int             color;
    char           *content;
    struct event_t *next;
}               event_t;

static int
MonthDay(int m, int leap)
{
    int      day[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    return leap && m == 2 ? 29 : day[m - 1];
}

static int
IsLeap(int y)
{
    if (y % 400 == 0 || (y % 4 == 0 && y % 100 != 0))
	return 1;
    else
	return 0;
}

static int
Days(int y, int m, int d)
{
    int             i, w;

    w = 1 + 365 * (y - 1)
	+ ((y - 1) / 4) - ((y - 1) / 100) + ((y - 1) / 400)
	+ d - 1;
    for (i = 1; i < m; i++)
	w += MonthDay(i, IsLeap(y));
    return w;
}

static int
ParseDate(char *date, event_t * t)
{
    char           *y, *m, *d;

    y = strtok(date, "/");
    m = strtok(NULL, "/");
    d = strtok(NULL, "");
    if (!y || !m || !d)
	return 1;

    t->year = atoi(y);
    t->month = atoi(m);
    t->day = atoi(d);
    if (t->year < 1 || t->month < 1 || t->month > 12 ||
	t->day < 1 || t->day > 31)
	return 1;
    t->days = Days(t->year, t->month, t->day);
    return 0;
}

static int
ParseColor(char *color)
{
    struct {
	char           *str;
	int             val;
    }               c[] = {
	{
	    "black", 0
	},
	{
	    "red", 1
	},
	{
	    "green", 2
	},
	{
	    "yellow", 3
	},
	{
	    "blue", 4
	},
	{
	    "magenta", 5
	},
	{
	    "cyan", 6
	},
	{
	    "white", 7
	}
    };
    int             i;

    for (i = 0; (unsigned)i < sizeof(c) / sizeof(c[0]); i++)
	if (strcasecmp(color, c[i].str) == 0)
	    return c[i].val;
    return 7;
}

static void
InsertEvent(event_t * head, event_t * t)
{
    event_t        *p;

    for (p = head; p->next && p->next->days < t->days; p = p->next);
    t->next = p->next;
    p->next = t;
}

static void
FreeEvent(event_t * e)
{
    event_t        *n;

    while (e) {
	n = e->next;
	free(e->content); /* from strdup() */
	free(e);
	e = n;
    }
}

static event_t *
ReadEvent(int today)
{
    FILE           *fp;
    char            buf[256];
    static event_t  head;

    head.next = NULL;
    sethomefile(buf, cuser.userid, "calendar");
    fp = fopen(buf, "r");
    if (fp) {
	while (fgets(buf, sizeof(buf), fp)) {
	    char           *date, *color, *content;
	    event_t        *t;

	    if (buf[0] == '#')
		continue;

	    date = strtok(buf, " \t\n");
	    color = strtok(NULL, " \t\n");
	    content = strtok(NULL, "\n");
	    if (!date || !color || !content)
		continue;

	    t = malloc(sizeof(event_t));
	    if (ParseDate(date, t) || t->days < today) {
		free(t);
		continue;
	    }
	    t->color = ParseColor(color) + 30;
	    for (; *content == ' ' || *content == '\t'; content++);
	    t->content = strdup(content);
	    InsertEvent(&head, t);
	}
	fclose(fp);
    }
    return head.next;
}

static char   **
AllocCalBuffer(int line, int len)
{
    int             i;
    char          **p;

    p = malloc(sizeof(char *) * line);
    p[0] = malloc(sizeof(char) * line * len);
    for (i = 1; i < line; i++)
	p[i] = p[i - 1] + len;
    return p;
}

static void
FreeCalBuffer(char **buf)
{
    free(buf[0]);
    free(buf);
}

#define CALENDAR_COLOR  "\33[0;30;47m"
#define HEADER_COLOR    "\33[1;44m"
#define HEADER_SUNDAY_COLOR    "\33[31m"
#define HEADER_DAY_COLOR       "\33[33m"

static int
GenerateCalendar(char **buf, int y, int m, int today, event_t * e)
{
    char    *week_str[7] = {"日", "一", "二", "三", "四", "五", "六"};
    char    *month_color[12] = {
	"\33[1;32m", "\33[1;33m", "\33[1;35m", "\33[1;36m",
	"\33[1;32m", "\33[1;33m", "\33[1;35m", "\33[1;36m",
	"\33[1;32m", "\33[1;33m", "\33[1;35m", "\33[1;36m"
    };
    char    *month_str[12] = {
	"一月  ", "二月  ", "三月  ", "四月  ", "五月  ", "六月  ",
	"七月  ", "八月  ", "九月  ", "十月  ", "十一月", "十二月"
    };

    char           *p, attr1[16], *attr2;
    int             i, d, w, line = 0, first_day = Days(y, m, 1);


    /* week day banner */
    p = buf[line];
    p += sprintf(p, "    %s%s%s%s", HEADER_COLOR, HEADER_SUNDAY_COLOR,
		 week_str[0], HEADER_DAY_COLOR);
    for (i = 1; i < 7; i++)
	p += sprintf(p, " %s", week_str[i]);
    p += sprintf(p, "\33[m");

    /* indent for first line */
    p = buf[++line];
    p += sprintf(p, "    %s", CALENDAR_COLOR);
    for (i = 0, w = first_day % 7; i < w; i++)
	p += sprintf(p, "   ");

    /* initial event */
    for (; e && e->days < first_day; e = e->next);

    d = MonthDay(m, IsLeap(y));
    for (i = 1; i <= d; i++, w = (w + 1) % 7) {
	attr1[0] = 0;
	attr2 = "";
	while (e && e->days == first_day + i - 1) {
	    sprintf(attr1, "\33[1;%dm", e->color);
	    attr2 = CALENDAR_COLOR;
	    e = e->next;
	}
	if (today == first_day + i - 1) {
	    strlcpy(attr1, "\33[1;37;42m", sizeof(attr1));
	    attr2 = CALENDAR_COLOR;
	}
	p += sprintf(p, "%s%2d%s", attr1, i, attr2);

	if (w == 6) {
	    p += sprintf(p, "\33[m");
	    p = buf[++line];
	    /* show month */
	    if (line >= 2 && line <= 4)
		p += sprintf(p, "%s%2.2s\33[m  %s", month_color[m - 1],
			     month_str[m - 1] + (line - 2) * 2,
			     CALENDAR_COLOR);
	    else if (i < d)
		p += sprintf(p, "    %s", CALENDAR_COLOR);
	} else
	    *p++ = ' ';
    }

    /* fill up the last line */
    if (w) {
	for (w = 7 - w; w; w--)
	    p += sprintf(p, w == 1 ? "  " : "   ");
	p += sprintf(p, "\33[m");
    }
    return line + 1;
}

int
calendar()
{
    char          **buf;
    struct tm       snow;
    int             i, y, m, today, lines = 0;
    event_t        *head = NULL, *e = NULL;

    /* initialize date */
    memcpy(&snow, localtime(&now), sizeof(struct tm));
    today = Days(snow.tm_year + 1900, snow.tm_mon + 1, snow.tm_mday);
    y = snow.tm_year + 1900, m = snow.tm_mon + 1;

    /* read event */
    head = e = ReadEvent(today);

    /* generate calendar */
    buf = AllocCalBuffer(22, 256);
    for (i = 0; i < 22; i++)
	sprintf(buf[i], "%24s", "");
    for (i = 0; i < 3; i++) {
	lines += GenerateCalendar(buf + lines, y, m, today, e) + 1;
	if (m == 12)
	    y++, m = 1;
	else
	    m++;
    }

    /* output */
    clear();
    outc('\n');
    for (i = 0; i < 22; i++) {
	outs(buf[i]);
	if (i == 0) {
	    prints("\t\33[1;37m現在是 %d.%02d.%02d %2d:%02d:%02d%cm\33[m",
		   snow.tm_year + 1900, snow.tm_mon + 1, snow.tm_mday,
		   (snow.tm_hour == 0 || snow.tm_hour == 12) ?
		   12 : snow.tm_hour % 12, snow.tm_min, snow.tm_sec,
		   snow.tm_hour >= 12 ? 'p' : 'a');
	} else if (i >= 2 && e) {
	    prints("\t\33[1;37m(\33[%dm%3d\33[37m)\33[m %02d/%02d %s",
		   e->color, e->days - today,
		   e->month, e->day, e->content);
	    e = e->next;
	}
	outc('\n');
    }
    FreeEvent(head);
    FreeCalBuffer(buf);
    pressanykey();
    return 0;
}
