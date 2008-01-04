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
//  3. remove i/o libraries
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

BLAPI_PROTO
bl_igetch(lua_State* L)
{
	int c = igetch();
	lua_pushinteger(L, c);
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


int
bbslua(const char *fpath)
{
	int r = 0;
	lua_State *L = lua_open();
	myluaL_openlibs(L);
	luaL_openlib(L, "bbs", lib_bbslua, 0);

	grayout(0, b_lines, GRAYOUT_DARK);
	move(b_lines, 0); clrtoeol();
	outs("Loading BBS-Lua " BBSLUA_VERSION_STR " ..."); refresh();

	r = luaL_dofile(L, fpath);
	lua_close(L);

	grayout(0, b_lines, GRAYOUT_DARK);
	move(b_lines, 0); clrtoeol();
	vmsgf("BBS-Lua: %s", r ? "FAILED" : "OK");
	clear();

	return 0;
}


// vim:ts=4:sw=4
