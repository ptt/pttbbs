//////////////////////////////////////////////////////////////////////////
// BBS-Lua Project
//
// Author: Hung-Te Lin(piaip), Jan. 2008. 
// <piaip@csie.ntu.edu.tw>
// Create: 2008-01-04 22:02:58
// $Id$
//
// This source is released in MIT License, same as Lua 5.0
// http://www.lua.org/license.html
//
// Copyright 2008 Hung-Te Lin <piaip@csie.ntu.edu.tw>
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE. 
//
// TODO:
//  BBSLUA 1.0
//  1. add quick key/val conversion [deprecated]
//  2. add key values (UP/DOWN/...) [done]
//  3. remove i/o libraries [done]
//  4. add system break key (Ctrl-C) [done]
//  5. add version string and script tags [done]
//  6. standalone w32 sdk [done]
//  7. syntax highlight in editor [drop?]
//  8. provide local storage
//  9. deal with loadfile, dofile
//  ?. modify bbs user data (eg, money)
//  ?. os.date(), os.exit(), abort(), os.time()
//  ?. memory free issue in C library level?
//  ?. add digital signature
//
//  BBSLUA 2.0
//  1. 2 people communication
//  
//  BBSLUA 3.0
//  1. n people communication
//////////////////////////////////////////////////////////////////////////

#include "bbs.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

//////////////////////////////////////////////////////////////////////////
// CONST DEFINITION
//////////////////////////////////////////////////////////////////////////

#define BBSLUA_INTERFACE_VER	(0.116)
#define BBSLUA_SIGNATURE		"--#BBSLUA"

// BBS-Lua script format:
// $BBSLUA_SIGNATURE
// -- Interface: $interface
// -- Title: $title
// -- Notes: $notes
// -- Author: $author <email@domain>
// -- Version: $version
// -- Date: $date
// -- X-Category: $category
//  [... script ...]
// $BBSLUA_SIGNATURE

//////////////////////////////////////////////////////////////////////////
// CONFIGURATION VARIABLES
//////////////////////////////////////////////////////////////////////////
#define BLAPI_PROTO		int

#define BLCONF_BREAK_KEY	Ctrl('C')
#define BLCONF_EXEC_COUNT	(5000)
#define BLCONF_PEEK_TIME	(0.01)
#define BLCONF_KBHIT_TMIN	(0.05f)
#define BLCONF_KBHIT_TMAX	(60*10)
#define BLCONF_SLEEP_TMIN	(BLCONF_PEEK_TIME)
#define BLCONF_SLEEP_TMAX	(BLCONF_KBHIT_TMAX)
#define BLCONF_U_SECOND		(1000000L)
#define BLCONF_MMAP_ATTACH
#define BLCONF_CURRENT_USERID cuser.userid

#ifdef _WIN32
# undef  BLCONF_MMAP_ATTACH
# undef	 BLCONF_CURRENT_USERID
# define BLCONF_CURRENT_USERID "guest"
#endif

//////////////////////////////////////////////////////////////////////////
// GLOBAL VARIABLES
//////////////////////////////////////////////////////////////////////////
static int abortBBSLua   = 0;
static int runningBBSLua = 0; // prevent re-entrant

//////////////////////////////////////////////////////////////////////////
// UTILITIES
//////////////////////////////////////////////////////////////////////////

static void
bl_double2tv(double d, struct timeval *tv)
{
	tv->tv_sec = d;
	tv->tv_usec = (d - tv->tv_sec) * BLCONF_U_SECOND;
}

static double
bl_tv2double(const struct timeval *tv)
{
	double d = tv->tv_sec;
	d += tv->tv_usec / (double)BLCONF_U_SECOND;
	return d;
}

void
bl_k2s(lua_State* L, int v)
{
	if (v <= 0)
		lua_pushnil(L);
	else if (v == KEY_TAB)
		lua_pushstring(L, "TAB");
	else if (v == '\b' || v == 0x7F)
		lua_pushstring(L, "BS");
	else if (v == '\n' || v == '\r' || v == Ctrl('M'))
		lua_pushstring(L, "ENTER");
	else if (v < ' ')
		lua_pushfstring(L, "^%c", v-1+'A');
	else if (v < 0x100)
		lua_pushfstring(L, "%c", v);
	else if (v >= KEY_F1 && v <= KEY_F12)
		lua_pushfstring(L, "F%d", v - KEY_F1 +1);
	else switch(v)
	{
		case KEY_UP:	lua_pushstring(L, "UP");	break;
		case KEY_DOWN:	lua_pushstring(L, "DOWN");	break;
		case KEY_RIGHT: lua_pushstring(L, "RIGHT"); break;
		case KEY_LEFT:	lua_pushstring(L, "LEFT");	break;
		case KEY_HOME:	lua_pushstring(L, "HOME");	break;
		case KEY_END:	lua_pushstring(L, "END");	break;
		case KEY_INS:	lua_pushstring(L, "INS");	break;
		case KEY_DEL:	lua_pushstring(L, "DEL");	break;
		case KEY_PGUP:	lua_pushstring(L, "PGUP");	break;
		case KEY_PGDN:	lua_pushstring(L, "PGDN");	break;
		default:		lua_pushnil(L);				break;
	}
}

BLAPI_PROTO
bl_newwin(int rows, int cols, const char *title)
{
	// input: (rows, cols, title)
	int y = 0, x = 0, n = 0;
	int oy = 0, ox = 0;

	if (rows <= 0 || cols <= 0)
		return 0;

	getyx(&oy, &ox);
	// now, draw the window
	newwin(rows, cols, oy, ox);

	if (!title || !*title)
		return 0;

	// draw center-ed title
	n = strlen_noansi(title);
	x = ox + (cols - n)/2;
	y = oy + (rows)/2;
	move(y, x);
	outs(title);

	move(oy, ox);
	return 0;
}

//////////////////////////////////////////////////////////////////////////
// BBSLUA API IMPLEMENTATION
//////////////////////////////////////////////////////////////////////////

BLAPI_PROTO
bl_getyx(lua_State* L)
{
	int y, x;
	getyx(&y, &x);
	lua_pushinteger(L, y);
	lua_pushinteger(L, x);
	return 2;
}

BLAPI_PROTO
bl_getmaxyx(lua_State* L)
{
	lua_pushinteger(L, t_lines);
	lua_pushinteger(L, t_columns);
	return 2;
}

BLAPI_PROTO
bl_move(lua_State* L)
{
	int n = lua_gettop(L);
	int y = 0, x = 0;
	if (n > 0)
		y = lua_tointeger(L, 1);
	if (n > 1)
		x = lua_tointeger(L, 2);
	move_ansi(y, x);
	return 0;
}

BLAPI_PROTO
bl_moverel(lua_State* L)
{
	int n = lua_gettop(L);
	int y = 0, x = 0;
	getyx(&y, &x);
	if (n > 0)
		y += lua_tointeger(L, 1);
	if (n > 1)
		x += lua_tointeger(L, 2);
	move(y, x);
	getyx(&y, &x);
	lua_pushinteger(L, y);
	lua_pushinteger(L, x);
	return 2;
}

BLAPI_PROTO
bl_clear(lua_State* L)
{
	(void)L;  /* to avoid warnings */
	clear();
	return 0;
}

BLAPI_PROTO
bl_clrtoeol(lua_State* L)
{
	(void)L;  /* to avoid warnings */
	clrtoeol();
	return 0;
}

BLAPI_PROTO
bl_clrtobot(lua_State* L)
{
	(void)L;  /* to avoid warnings */
	clrtobot();
	return 0;
}

BLAPI_PROTO
bl_refresh(lua_State* L)
{
	(void)L;  /* to avoid warnings */
	// refresh();
	// Seems like that most people don't understand the relationship
	// between refresh() and input queue, so let's force update here.
	doupdate();
	return 0;
}

BLAPI_PROTO
bl_addstr(lua_State* L)
{
	int n = lua_gettop(L);
	int i = 1;
	for (i = 1; i <= n; i++)
	{
		const char *s = lua_tostring(L, i);
		if(s)
			outs(s);
	}
	return 0;
}

BLAPI_PROTO
bl_rect(lua_State *L)
{
	// input: (rows, cols, title)
	int rows = 1, cols = 1;
	int n = lua_gettop(L);
	const char *title = NULL;

	if (n > 0)
		rows = lua_tointeger(L, 1);
	if (n > 1)
		cols = lua_tointeger(L, 2);
	if (n > 2)
		title = lua_tostring(L, 3);
	if (rows <= 0 || cols <= 0)
		return 0;

	// now, draw the rectangle
	bl_newwin(rows, cols, title);
	return 0;
}

BLAPI_PROTO
bl_print(lua_State* L)
{
	bl_addstr(L);
	outc('\n');
	return 0;
}

BLAPI_PROTO
bl_getch(lua_State* L)
{
	int c = igetch();
	if (c == BLCONF_BREAK_KEY)
	{
		drop_input();
		abortBBSLua = 1;
		return lua_yield(L, 0);
	}
	bl_k2s(L, c);
	return 1;
}


BLAPI_PROTO
bl_getstr(lua_State* L)
{
	int y, x;
	// TODO not using fixed length here?
	char buf[PATHLEN] = "";
	int len = 2, echo = 1;
	int n = lua_gettop(L);

	if (n > 0)
		len = lua_tointeger(L, 1);
	if (n > 1)
		echo = lua_tointeger(L, 2);

	if (len < 2)
		len = 2;
	if (len >= sizeof(buf))
		len = sizeof(buf)-1;

	// TODO process Ctrl-C here
	getyx(&y, &x);
	len = getdata(y, x, NULL, buf, len, echo);
	if (len <= 0)
	{
		len = 0;
		// check if we got Ctrl-C? (workaround in getdata)
		// TODO someday write 'ungetch()' in io.c to prevent
		// such workaround.
		if (buf[1] == Ctrl('C'))
		{
			drop_input();
			abortBBSLua = 1;
			return lua_yield(L, 0);
		}
		lua_pushstring(L, "");
	} 
	else
	{
		lua_pushstring(L, buf);
	}
	// return len ? 1 : 0;
	return 1;
}

BLAPI_PROTO
bl_kbhit(lua_State *L)
{
	int n = lua_gettop(L);
	double f = 0.1f;
	
	if (n > 0)
		f = (double)lua_tonumber(L, 1);

	if (f < BLCONF_KBHIT_TMIN) f = BLCONF_KBHIT_TMIN;
	if (f > BLCONF_KBHIT_TMAX) f = BLCONF_KBHIT_TMAX;

	refresh();
	if (num_in_buf() || wait_input(f, 0))
		lua_pushboolean(L, 1);
	else
		lua_pushboolean(L, 0);
	return 1;
}

BLAPI_PROTO
bl_kbreset(lua_State *L)
{
	// peek input queue first!
	if (peek_input(BLCONF_PEEK_TIME, BLCONF_BREAK_KEY))
	{
		drop_input();
		abortBBSLua = 1;
		return lua_yield(L, 0);
	}

	drop_input();
	return 0;
}

BLAPI_PROTO
bl_sleep(lua_State *L)
{
	int n = lua_gettop(L);
	double us = 0, nus = 0;

	if (n > 0)
		us = lua_tonumber(L, 1);
	if (us < BLCONF_SLEEP_TMIN)
		us = BLCONF_SLEEP_TMIN;
	if (us > BLCONF_SLEEP_TMAX)
		us = BLCONF_SLEEP_TMAX;
	nus = us;

	refresh();

#ifdef _WIN32

	Sleep(us * 1000);

#else // !_WIN32
	{
		struct timeval tp, tdest;

		gettimeofday(&tp, NULL);

		// nus is the destination time
		nus = bl_tv2double(&tp) + us;
		bl_double2tv(nus, &tdest);

		// use peek_input
		while ( (tp.tv_sec < tdest.tv_sec) ||
				((tp.tv_sec == tdest.tv_sec) && (tp.tv_usec < tdest.tv_usec)))
		{
			// calculate new peek time
			us = nus - bl_tv2double(&tp);

			// check if input key is system break key.
			if (peek_input(us, BLCONF_BREAK_KEY))
			{
				drop_input();
				abortBBSLua = 1;
				return lua_yield(L, 0);
			}

			// check time
			gettimeofday(&tp, NULL);
		}
	}
#endif // !_WIN32

	return 0;
}


BLAPI_PROTO
bl_pause(lua_State* L)
{
	int n = lua_gettop(L);
	const char *s = NULL;
	if (n > 0)
		s = lua_tostring(L, 1);

	n = vmsg(s);
	if (n == BLCONF_BREAK_KEY)
	{
		drop_input();
		abortBBSLua = 1;
		return lua_yield(L, 0);
	}
	bl_k2s(L, n);
	return 1;
}

BLAPI_PROTO
bl_title(lua_State* L)
{
	int n = lua_gettop(L);
	const char *s = NULL;
	if (n > 0)
		s = lua_tostring(L, 1);
	if (s == NULL)
		return 0;

	stand_title(s);
	return 0;
}

BLAPI_PROTO
bl_ansi_color(lua_State *L)
{
	char buf[PATHLEN] = ESC_STR "[";
	char *p = buf + strlen(buf);
	int i = 1;
	int n = lua_gettop(L);
	if (n >= 10) n = 10;
	for (i = 1; i <= n; i++)
	{
		if (i > 1) *p++ = ';';
		sprintf(p, "%d", (int)lua_tointeger(L, i));
		p += strlen(p);
	}
	*p++ = 'm';
	*p   = 0;
	lua_pushstring(L, buf);
	return 1;
}

BLAPI_PROTO
bl_strip_ansi(lua_State *L)
{
	int n = lua_gettop(L);
	const char *s = NULL;
	char *s2 = NULL;
	size_t os2 = 0;

	if (n < 1 || (s = lua_tostring(L, 1)) == NULL ||
			*s == 0)
	{
		lua_pushstring(L, "");
		return 1;
	}


	os2 = strlen(s)+1;
	s2 = (char*) lua_newuserdata(L, os2);
	strip_ansi(s2, s, STRIP_ALL);
	lua_pushstring(L, s2);
	lua_remove(L, -2);
	return 1;
}

BLAPI_PROTO
bl_attrset(lua_State *L)
{
	char buf[PATHLEN] = ESC_STR "[";
	char *p = buf + strlen(buf);
	int i = 1;
	int n = lua_gettop(L);
	if (n == 0)
		outs(ANSI_RESET);
	for (i = 1; i <= n; i++)
	{
		sprintf(p, "%dm",(int)lua_tointeger(L, i)); 
		outs(buf);
	}
	return 0;
}

BLAPI_PROTO
bl_time(lua_State *L)
{
	syncnow();
	lua_pushinteger(L, now);
	return 1;
}

BLAPI_PROTO
bl_ctime(lua_State *L)
{
	syncnow();
	lua_pushstring(L, ctime4(&now));
	return 1;
}

BLAPI_PROTO
bl_clock(lua_State *L)
{
	double d = 0;

#ifdef _WIN32

	// XXX this is a fast hack because we don't want to do 64bit calculation.
	SYSTEMTIME st;
	GetSystemTime(&st);
	syncnow();
	// XXX the may be some latency between our GetSystemTime and syncnow.
	// So build again the "second" part.
	d = (int)((now / 60) * 60);
	d += st.wSecond;
	d += (st.wMilliseconds / 1000.0f);

#else // !_WIN32

	struct timeval tp;
	gettimeofday(&tp, NULL);
	d = bl_tv2double(&tp);

#endif // !_WIN32

	lua_pushnumber(L, d);
	return 1;
}

BLAPI_PROTO
bl_userid(lua_State *L)
{
	lua_pushstring(L, BLCONF_CURRENT_USERID);
	return 1;
}

//////////////////////////////////////////////////////////////////////////
// BBSLUA LIBRARY
//////////////////////////////////////////////////////////////////////////

static const struct luaL_reg lib_bbslua [] = {
	/* curses output */
	{ "getyx",		bl_getyx },
	{ "getmaxyx",	bl_getmaxyx },
	{ "move",		bl_move },
	{ "moverel",	bl_moverel },
	{ "clear",		bl_clear },
	{ "clrtoeol",	bl_clrtoeol },
	{ "clrtobot",	bl_clrtobot },
	{ "refresh",	bl_refresh },
	{ "addstr",		bl_addstr },
	{ "outs",		bl_addstr },
	{ "print",		bl_print },
	/* input */
	{ "getch",		bl_getch },
	{ "getdata",	bl_getstr },
	{ "getstr",		bl_getstr },
	{ "kbhit",		bl_kbhit },
	{ "kbreset",	bl_kbreset },
	/* advanced output */
	{ "rect",		bl_rect },
	/* BBS utilities */
	{ "pause",		bl_pause },
	{ "title",		bl_title },
	{ "userid",		bl_userid },
	/* time */
	{ "time",		bl_time },
	{ "now",		bl_time },
	{ "clock",		bl_clock },
	{ "ctime",		bl_ctime },
	{ "sleep",		bl_sleep },
	/* ANSI helpers */
	{ "ANSI_COLOR",	bl_ansi_color },
	{ "color",		bl_attrset },
	{ "attrset",	bl_attrset },
	{ "strip_ansi", bl_strip_ansi },
	{ NULL, NULL},
};

// non-standard modules in bbsluaext.c
LUALIB_API int luaopen_bit (lua_State *L);

static const luaL_Reg bbslualibs[] = {
  // standard modules
  {"", luaopen_base},

  // {LUA_LOADLIBNAME, luaopen_package},
  {LUA_TABLIBNAME, luaopen_table},
  // {LUA_IOLIBNAME, luaopen_io},
  // {LUA_OSLIBNAME, luaopen_os},
  {LUA_STRLIBNAME, luaopen_string},
  {LUA_MATHLIBNAME, luaopen_math},
  // {LUA_DBLIBNAME, luaopen_debug},
  
  // bbslua-ext modules
  {"bit", luaopen_bit},

  {NULL, NULL}
};


LUALIB_API void bbsluaL_openlibs (lua_State *L) {
  const luaL_Reg *lib = bbslualibs;
  for (; lib->func; lib++) {
    lua_pushcfunction(L, lib->func);
    lua_pushstring(L, lib->name);
    lua_call(L, 1, 0);
  }
}


// Constant registration

typedef struct bbsluaL_RegStr {
  const char *name;
  const char *val;
} bbsluaL_RegStr;

typedef struct bbsluaL_RegNum {
  const char *name;
  lua_Number val;
} bbsluaL_RegNum;

static const bbsluaL_RegStr bbsluaStrs[] = {
	{"ESC",			ESC_STR},
	{"ANSI_RESET",	ANSI_RESET},
	{"sitename",	BBSNAME},
	{NULL,			NULL},
};

static const  bbsluaL_RegNum bbsluaNums[] = {
	{"interface",	BBSLUA_INTERFACE_VER},
	{NULL,			0},
};

static void
bbsluaRegConst(lua_State *L, const char *globName)
{
	int i = 0;

	// section
	lua_getglobal(L, globName);

	for (i = 0; bbsluaStrs[i].name; i++)
	{
		lua_pushstring(L, bbsluaStrs[i].val);
		lua_setfield(L, -3, bbsluaStrs[i].name);
	}

	for (i = 0; bbsluaNums[i].name; i++)
	{
		lua_pushnumber(L, bbsluaNums[i].val);
		lua_setfield(L, -3, bbsluaNums[i].name);
	}
	lua_pop(L, 1);

	// global
	lua_pushcfunction(L, bl_print);
	lua_setglobal(L, "print");
}

static void
bbsluaHook(lua_State *L, lua_Debug* ar)
{
	// vmsg("bbslua HOOK!");
	if (abortBBSLua)
	{
		drop_input();
		lua_yield(L, 0);
		return;
	}

	if (ar->event != LUA_HOOKCOUNT)
		return;

	// now, peek and check
	if (input_isfull())
		drop_input();

	// refresh();
	
	// check if input key is system break key.
	if (peek_input(BLCONF_PEEK_TIME, BLCONF_BREAK_KEY))
	{
		drop_input();
		abortBBSLua = 1;
		lua_yield(L, 0);
		return;
	}
}

static char * 
bbslua_attach(const char *fpath, int *plen)
{
	char *buf = NULL;

#ifdef BLCONF_MMAP_ATTACH
    struct stat st;
	int fd = open(fpath, O_RDONLY, 0600);

	*plen = 0;

	if (fd < 0) return buf;
    if (fstat(fd, &st) || ((*plen = st.st_size) < 1) || S_ISDIR(st.st_mode))
    {
		close(fd);
		return buf;
    }
	*plen = *plen +1;

    buf = mmap(NULL, *plen, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);

	if (buf == NULL || buf == MAP_FAILED) 
	{
		*plen = 0;
		return  NULL;
	}
    madvise(buf, *plen, MADV_SEQUENTIAL);

#else // !BLCONF_MMAP_ATTACH

	FILE *fp = fopen(fpath, "rt");
	*plen = 0;
	if (!fp)
		return NULL;
	fseek(fp, 0, SEEK_END);
	*plen = ftell(fp);
	buf = (char*) malloc (*plen);
	rewind(fp);
	fread(buf, *plen, 1, fp);

#endif // !BLCONF_MMAP_ATTACH

	return buf;
}

static void
bbslua_detach(char *p, int len)
{
#ifdef BLCONF_MMAP_ATTACH
	munmap(p, len);
#else // !BLCONF_MMAP_ATTACH
	free(p);
#endif // !BLCONF_MMAP_ATTACH
}

int
bbslua_isHeader(const char *ps, const char *pe)
{
	int szsig = strlen(BBSLUA_SIGNATURE);
	if (ps + szsig > pe)
		return 0;
	if (strncmp(ps, BBSLUA_SIGNATURE, szsig) == 0)
		return 1;
	return 0;
}

static int
bbslua_detect_range(char **pbs, char **pbe, int *lineshift)
{
	int szsig = strlen(BBSLUA_SIGNATURE);
	char *bs, *be, *ps, *pe;
	int line = 0;

	bs = ps = *pbs;
	be = pe = *pbe;

	// find start
	while (ps + szsig < pe)
	{
		if (strncmp(ps, BBSLUA_SIGNATURE, szsig) == 0)
			break;
		// else, skip to next line
		while (ps + szsig < pe && *ps++ != '\n');
		line++;
	}
	*lineshift = line;

	if (!(ps + szsig < pe))
		return 0;

	*pbs = ps;
	*pbe = be;

	// find tail by SIGNATURE
	pe = ps + 1;
	while (pe + szsig < be)
	{
		if (strncmp(pe, BBSLUA_SIGNATURE, szsig) == 0)
			break;
		// else, skip to next line
		while (pe + szsig < be && *pe++ != '\n');
	}

	if (pe + szsig < be)
	{
		// found sig, end at such line.
		pe--;
		*pbe = pe;
	} else {
		// abort.
		*pbe = NULL;
		*pbs = NULL;
		return 0;
	}

	// prevent trailing zeros
	pe = *pbe;
	while (pe > ps && !*pe)
		pe--;
	*pbe = pe;

	return 1;
}

static const char *bbsluaTocTags[] =
{
	"interface",
	"title",
	"notes",
	"author",
	"version",
	"date",
	NULL
};

static const char *bbsluaTocPrompts[] = 
{
	"界面",
	"名稱",
	"說明",
	"作者",
	"版本",
	"日期",
	NULL
};

int
bbslua_load_TOC(lua_State *L, const char *pbs, const char *pbe)
{
	unsigned char *ps = NULL, *pe = NULL;
	int i = 0;

	lua_newtable(L);

	while (pbs < pbe)
	{
		// find stripped line start, end
		ps = pe = (unsigned char *) pbs;
		while (pe < (unsigned char*)pbe && *pe != '\n' && *pe != '\r')
			pe ++;
		pbs = (char*)pe+1;
		while (ps < pe && *ps <= ' ') ps++;
		while (pe > ps && *(pe-1) <= ' ') pe--;
		// at least "--"
		if (pe < ps+2)
			break;
		if (*ps++ != '-' || *ps++ != '-')
			break;
		while (ps < pe && *ps <= ' ') ps++;
		// empty entry?
		if (ps >= pe)
			continue;
		// find pattern
		for (i = 0; bbsluaTocTags[i]; i++)
		{
			int l = strlen(bbsluaTocTags[i]);
			if (ps + l > pe)
				continue;
			if (strncasecmp((char*)ps, bbsluaTocTags[i], l) != 0)
				continue;
			ps += l;
			// found matching pattern, now find value
			while (ps < pe && *ps <= ' ') ps++;
			if (ps >= pe || *ps++ != ':') 
				break;
			while (ps < pe && *ps <= ' ') ps++;
			// finally, (ps, pe) is the value!
			if (ps >= pe)
				break;

			lua_pushlstring(L, (char*)ps, pe-ps);

			// accept only real floats for interface ([0])
			if (i == 0 && lua_tonumber(L, -1) <= 0)
			{
				lua_pop(L, 1);
			}
			else
			{
				lua_setfield(L, -2, bbsluaTocTags[i]);
			}
			break;
		}
	}

	lua_setglobal(L, "toc");
	return 0;
}

void
bbslua_logo(lua_State *L)
{
	int y, by = b_lines -1; // print - back from bottom
	int i = 0;
	double tocinterface = 0;
	int tocs = 0;

	// get toc information
	lua_getglobal(L, "toc");
	lua_getfield(L, -1, bbsluaTocTags[0]);
	tocinterface = lua_tonumber(L, -1); lua_pop(L, 1);

	for (i = 1; bbsluaTocTags[i]; i++)
	{
		lua_getfield(L, -1, bbsluaTocTags[i]);
		if (lua_tostring(L, -1))
			tocs++;
		lua_pop(L, 1);
	}

	// prepare logo window
	grayout(0, b_lines, GRAYOUT_DARK);
	outs(ANSI_COLOR(30;47));

	// print compatibility test
	// now (by) is the base of new information
	if (tocinterface == 0)
	{
		by -= 4; y = by+2;
		outs(ANSI_COLOR(0;31;47));
		move(y-1, 0); bl_newwin(4, t_columns-1, NULL);
		move(y, 0);
		outs(" ▲此程式缺少相容性資訊，您可能無法正常執行");
		move(++y, 0);
		outs(" 若執行出現錯誤，請向原作者取得新版");
	} 
	else if (tocinterface > BBSLUA_INTERFACE_VER)
	{
		by -= 4; y = by+2;
		outs(ANSI_COLOR(0;1;37;41));
		move(y-1, 0); bl_newwin(4, t_columns-1, NULL);
		move(y, 0);
		prints(" ▲此程式使用新版的 BBS-Lua 規格 (%0.3f)，您可能無法正常執行",
				tocinterface);
		move(++y, 0);
		outs(" 若執行出現錯誤，建議您重新登入 BBS 後再重試");
	}
	else if (tocinterface == BBSLUA_INTERFACE_VER)
	{
		// do nothing!
	} 
	else 
	{
		// should be comtaible
		// prints("相容 (%.03f)", tocinterface);
	}


	// print toc, ifany.
	if (tocs)
	{
		y = by - 1 - tocs;
		by = y-1;
		outs(ANSI_COLOR(0;1;30;47));
		move(y, 0);
		bl_newwin(tocs+2, t_columns-1, NULL);
		// now try to print all TOC infos
		for (i = 1; bbsluaTocTags[i]; i++)
		{
			lua_getfield(L, -1, bbsluaTocTags[i]);
			if (!lua_isstring(L, -1))
			{
				lua_pop(L, 1);
				continue;
			}
			move(++y, 2); 
			outs(bbsluaTocPrompts[i]);
			outs(": ");
			outns(lua_tostring(L, -1), t_columns-10); 
			lua_pop(L, 1);
		}
	}
	outs(ANSI_COLOR(0;1;37;44));
	move(by-2, 0); outc('\n');
	bl_newwin(2, t_columns-1, NULL);
	prints(" ■ BBS-Lua %.03f  (Build " __DATE__ " " __TIME__") ",
			(double)BBSLUA_INTERFACE_VER);
	move(by, 0);
	outs(ANSI_COLOR(22;37) "    提醒您執行中隨時可按 "
			ANSI_COLOR(1;31) "[Ctrl-C] " ANSI_COLOR(0;37;44) 
			"強制中斷 BBS-Lua 程式");
	outs(ANSI_RESET);
	lua_pop(L, 1);
}

typedef struct LoadS {
	const char *s;
	size_t size;
	int lineshift;
} LoadS;

static const char* bbslua_reader(lua_State *L, void *ud, size_t *size)
{
	LoadS *ls = (LoadS *)ud;
	(void)L;
	if (ls->size == 0) return NULL;
	if (ls->lineshift > 0) {
		const char *linefeed = "\n";
		*size = 1;
		ls->lineshift--;
		return linefeed;
	}
	*size = ls->size;
	ls->size = 0;
	return ls->s;
}

static int bbslua_loadbuffer (lua_State *L, const char *buff, size_t size,
		const char *name, int lineshift) {
	LoadS ls;
	ls.s = buff;
	ls.size = size;
	ls.lineshift = lineshift;
	return lua_load(L, bbslua_reader, &ls, name);
}

typedef struct AllocData {
	size_t alloc_size;
	size_t max_alloc_size;
	size_t alloc_limit;
} AllocData;

static void alloc_init(AllocData *ad)
{
	memset(ad, 0, sizeof(*ad));
	// real limit is not determined yet, just assign a big value
	ad->alloc_limit = 20*1048576;
}

static void *allocf (void *ud, void *ptr, size_t osize, size_t nsize) {
	/* TODO use our own allocator, for better memory control, avoid fragment and leak */
	AllocData *ad = (AllocData*)ud;

	if (ad->alloc_size + nsize - osize > ad->alloc_limit) {
		return NULL;
	}
	ad->alloc_size += nsize - osize;
	if (ad->alloc_size > ad->max_alloc_size) {
		ad->max_alloc_size = ad->alloc_size;
	}
	if (nsize == 0) {
		free(ptr);
		return NULL;
	}
	else
		return realloc(ptr, nsize);
}
static int panic (lua_State *L) {
	(void)L;  /* to avoid warnings */
	fprintf(stderr, "PANIC: unprotected error in call to Lua API (%s)\n",
			lua_tostring(L, -1));
	return 0;
}

int
bbslua(const char *fpath)
{
	int r = 0;
	lua_State *L;
	char *bs, *ps, *pe;
	int sz = 0;
	int lineshift;
	AllocData ad;

	alloc_init(&ad);
	L = lua_newstate(allocf, &ad);
	if (!L)
		return 0;
	lua_atpanic(L, &panic);

#ifdef UMODE_BBSLUA
	unsigned int prevmode = getutmpmode();
#endif

	// re-entrant not supported!
	if (runningBBSLua)
		return 0;

	abortBBSLua = 0;

	// detect file
	bs = bbslua_attach(fpath, &sz);
	if (!bs)
		return 0;
	ps = bs;
	pe = ps + sz;

	if(!bbslua_detect_range(&ps, &pe, &lineshift))
	{
		// not detected
		bbslua_detach(bs, sz);
		return 0;
	}

	// init library
	bbsluaL_openlibs(L);
	luaL_openlib(L,   "bbs", lib_bbslua, 0);
	bbsluaRegConst(L, "bbs");

	// detect TOC meta data
	bbslua_load_TOC(L, ps, pe);

	// load script
	r = bbslua_loadbuffer(L, ps, pe-ps, "BBS-Lua", lineshift);
	
	// unmap
	bbslua_detach(bs, sz);

	if (r != 0)
	{
		const char *errmsg = lua_tostring(L, -1);
		move(b_lines-3, 0); clrtobot();
		outs("\n");
		outs(errmsg);
		vmsg("BBS-Lua 載入錯誤: 請通知作者修正程式碼。");
		lua_close(L);
		return 0;
	}

#ifdef UMODE_BBSLUA
	setutmpmode(UMODE_BBSLUA);
#endif

	bbslua_logo(L);
	vmsgf("提醒您執行中隨時可按 [Ctrl-C] 強制中斷 BBS-Lua 程式");

	// ready for running
	clear();
	runningBBSLua =1;
	lua_sethook(L, bbsluaHook, LUA_MASKCOUNT, BLCONF_EXEC_COUNT );

	refresh();
	// check is now done inside hook
	r = lua_resume(L, 0);

	// even if r == yield, let's abort - you cannot yield main thread.

	if (r != 0)
	{
		const char *errmsg = lua_tostring(L, -1);
		move(b_lines-3, 0); clrtobot();
		outs("\n");
		if (errmsg) outs(errmsg);
	}

	lua_close(L);
	runningBBSLua =0;
	drop_input();

	// grayout(0, b_lines, GRAYOUT_DARK);
	move(b_lines, 0); clrtoeol();
	vmsgf("BBS-Lua 執行結束%s。", 
			abortBBSLua ? " (使用者中斷)" : r ? " (程式錯誤)" : "");
	clear();

#ifdef UMODE_BBSLUA
	setutmpmode(prevmode);
#endif

	return 0;
}

// vim:ts=4:sw=4
