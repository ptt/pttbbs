/* This file contains non-standard modules which
 * are fundamental in BBSLua framework.
 */

/* Bitwise operations library */
/* (c) Reuben Thomas 2000-2007 */
/*
                          bitlib release 24
                          -----------------
                   by Reuben Thomas <rrt@sc3d.org>
                 http://luaforge.net/projects/bitlib


bitlib is a C library for Lua 5.x that provides bitwise operations. It
is copyright Reuben Thomas 2000-2007, and is released under the MIT
license, like Lua (see http://www.lua.org/copyright.html; it's
basically the same as the BSD license). There is no warranty.

Please report bugs and make suggestions to the email address above, or
use the LuaForge trackers.

Thanks to John Passaniti for his bitwise operations library, some of
whose ideas I used, to Shmuel Zeigerman for the test suite, to
Thatcher Ulrich for portability fixes, and to John Stiles for a bug
fix.
*/

#include <inttypes.h>
#include <lauxlib.h>
#include <lua.h>

typedef int32_t Integer;
typedef uint32_t UInteger;

#define checkUInteger(L, n) ((UInteger)luaL_checknumber((L), (n)))

#define TDYADIC(name, op, type1, type2) \
  static int bit_ ## name(lua_State* L) { \
    lua_pushnumber(L, (Integer)((type1)checkUInteger(L, 1) op (type2)checkUInteger(L, 2))); \
    return 1; \
  }

#define MONADIC(name, op, type) \
  static int bit_ ## name(lua_State* L) { \
    lua_pushnumber(L, (Integer)(op (type)checkUInteger(L, 1))); \
    return 1; \
  }

#define VARIADIC(name, op, type) \
  static int bit_ ## name(lua_State *L) { \
    int n = lua_gettop(L), i; \
    Integer w = (type)checkUInteger(L, 1); \
    for (i = 2; i <= n; i++) \
      w op (type)checkUInteger(L, i);      \
    lua_pushnumber(L, (Integer)w);         \
    return 1; \
  }

MONADIC(cast,    +,  Integer)
MONADIC(bnot,    ~,  Integer)
VARIADIC(band,   &=, Integer)
VARIADIC(bor,    |=, Integer)
VARIADIC(bxor,   ^=, Integer)
TDYADIC(lshift,  <<, Integer, UInteger)
TDYADIC(rshift,  >>, UInteger, UInteger)
TDYADIC(arshift, >>, Integer, UInteger)

static const struct luaL_reg bitlib[] = {
  {"cast",    bit_cast},
  {"bnot",    bit_bnot},
  {"band",    bit_band},
  {"bor",     bit_bor},
  {"bxor",    bit_bxor},
  {"lshift",  bit_lshift},
  {"rshift",  bit_rshift},
  {"arshift", bit_arshift},
  {NULL, NULL}
};

LUALIB_API int luaopen_bit (lua_State *L) {
  luaL_openlib(L, "bit", bitlib, 0);
  return 1;
}
