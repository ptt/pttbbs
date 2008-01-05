//////////////////////////////////////////////////////////////////////////
// BBS Lua Project
//
// Author: Hung-Te Lin(piaip), Jan. 2008. 
// <piaip@csie.ntu.edu.tw>
// This source is released in MIT License.
//
// TODO:
//  1. add quick key/val conversion
//  2. add key values (UP/DOWN/...)
//  3. remove i/o libraries [done]
//  4. add system break key (Ctrl-C?)
//////////////////////////////////////////////////////////////////////////

#include "bbs.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

//////////////////////////////////////////////////////////////////////////
// DEFINITION
//////////////////////////////////////////////////////////////////////////

#define BBSLUA_VERSION		(1)
#define BBSLUA_VERSION_STR  "1.0"
#define BLAPI_PROTO		int

//////////////////////////////////////////////////////////////////////////
// GLOBAL VARIABLES
//////////////////////////////////////////////////////////////////////////
static int abortBBSLua = 0;

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
bl_move(lua_State* L)
{
	int n = lua_gettop(L);
	int y = 0, x = 0;
	if (n > 0)
		y = lua_tointeger(L, 1);
	if (n > 1)
		x = lua_tointeger(L, 2);
	move(y, x);
	return 0;
}

BLAPI_PROTO
bl_clear(lua_State* L)
{
	clear();
	return 0;
}

BLAPI_PROTO
bl_clrtoeol(lua_State* L)
{
	clrtoeol();
	return 0;
}

BLAPI_PROTO
bl_clrtobot(lua_State* L)
{
	clrtobot();
	return 0;
}

BLAPI_PROTO
bl_refresh(lua_State* L)
{
	refresh();
	return 0;
}

BLAPI_PROTO
bl_redrawwin(lua_State* L)
{
	redrawwin();
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

void
bl_k2s(lua_State* L, int v)
{
	if (v <= 0)
		lua_pushnil(L);
	else if (v == KEY_TAB)
		lua_pushstring(L, "TAB");
	else if (v == '\b' || v == 0x7F)
		lua_pushstring(L, "BS");
	else if (v == '\n')
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
bl_igetch(lua_State* L)
{
	int c = igetch();
	if (c == Ctrl('C'))
	{
		abortBBSLua = 1;
		return lua_yield(L, 0);
	}
	bl_k2s(L, c);
	return 1;
}


BLAPI_PROTO
bl_getdata(lua_State* L)
{
	int y, x;
	char buf[PATHLEN] = "";
	int len = 2, echo = 1;
	int n = lua_gettop(L);

	if (n > 0)
		len = lua_tointeger(L, 1);
	if (n > 2)
		echo = lua_tointeger(L, 2);

	if (len < 2)
		len = 2;
	if (len >= sizeof(buf))
		len = sizeof(buf)-1;

	// TODO process Ctrl-C here
	getyx(&y, &x);
	len = getdata(y, x, NULL, buf, len, echo);
	if (len)
		lua_pushstring(L, buf);
	return len ? 1 : 0;
}

BLAPI_PROTO
bl_vmsg(lua_State* L)
{
	int n = lua_gettop(L);
	const char *s = NULL;
	if (n > 0)
		s = lua_tostring(L, 1);

	n = vmsg(s);
	if (n == Ctrl('C'))
	{
		abortBBSLua = 1;
		return lua_yield(L, 0);
	}
	lua_pushinteger(L, n);
	return 1;
}

BLAPI_PROTO
bl_stand_title(lua_State* L)
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

//////////////////////////////////////////////////////////////////////////
// BBSLUA LIBRARY
//////////////////////////////////////////////////////////////////////////

static const struct luaL_reg lib_bbslua [] = {
	/* curses output */
	{ "getyx",		bl_getyx },
	{ "move",		bl_move },
	{ "clear",		bl_clear },
	{ "clrtoeol",	bl_clrtoeol },
	{ "clrtobot",	bl_clrtobot },
	{ "refresh",	bl_refresh },
	{ "redrawwin",	bl_redrawwin },
	{ "addch",		bl_addstr },
	{ "addstr",		bl_addstr },
	{ "outc",		bl_addstr },
	{ "outs",		bl_addstr },
	/* input */
	{ "getch",		bl_igetch },
	{ "igetch",		bl_igetch },
	{ "getdata",	bl_getdata },
	/* BBS utilities */
	{ "vmsg",		bl_vmsg },
	{ "pause",		bl_vmsg },
	{ "stand_title",bl_stand_title },
	/* ANSI helpers */
	{ "ANSI_COLOR",	bl_ansi_color },
	{ NULL, NULL},
};

static const luaL_Reg mylualibs[] = {
  {"", luaopen_base},

  // {LUA_LOADLIBNAME, luaopen_package},
  {LUA_TABLIBNAME, luaopen_table},
  // {LUA_IOLIBNAME, luaopen_io},
  // {LUA_OSLIBNAME, luaopen_os},
  {LUA_STRLIBNAME, luaopen_string},
  {LUA_MATHLIBNAME, luaopen_math},
  // {LUA_DBLIBNAME, luaopen_debug},

  {NULL, NULL}
};


LUALIB_API void myluaL_openlibs (lua_State *L) {
  const luaL_Reg *lib = mylualibs;
  for (; lib->func; lib++) {
    lua_pushcfunction(L, lib->func);
    lua_pushstring(L, lib->name);
    lua_call(L, 1, 0);
  }
}

static void
bbsluaRegConst(lua_State *L, const char *globName)
{
	lua_getglobal(L, globName);

	lua_pushstring(L, "ESC"); lua_pushstring(L, ESC_STR);
	lua_settable(L, -3);

	lua_pushstring(L, "ANSI_RESET"); lua_pushstring(L, ANSI_RESET);
	lua_settable(L, -3);

}

static void
bbsluaHook(lua_State *L, lua_Debug* ar)
{
	// vmsg("bbslua HOOK!");
	if (ar->event == LUA_HOOKCOUNT)
		lua_yield(L, 0);
}

int
bbslua(const char *fpath)
{
	int r = 0;
	lua_State *L = lua_open();

	abortBBSLua = 0;
	myluaL_openlibs(L);
	luaL_openlib(L,   "bbs", lib_bbslua, 0);
	bbsluaRegConst(L, "bbs");

	grayout(0, b_lines, GRAYOUT_DARK);
	move(b_lines, 0); clrtoeol();
	outs("Loading BBS-Lua " BBSLUA_VERSION_STR " ..."); refresh();

	if ((r = luaL_loadfile(L, fpath)) == 0)
	{
		// ready for running
		lua_sethook(L, bbsluaHook, LUA_MASKCOUNT, 100 );

		while (!abortBBSLua && lua_resume(L, 0) == LUA_YIELD)
		{
			if (input_isfull())
				drop_input();

			refresh();

			// check if input key is system break key.
			if (peek_input(0.1, Ctrl('C')))
			{
				drop_input();
				abortBBSLua = 1;
				break;
			}
		}
	}
	lua_close(L);

	grayout(0, b_lines, GRAYOUT_DARK);
	move(b_lines, 0); clrtoeol();
	vmsgf("BBS-Lua: %s", abortBBSLua ? "USER ABORT" : r ? "FAILED" : "OK");
	clear();

	return 0;
}


// vim:ts=4:sw=4
