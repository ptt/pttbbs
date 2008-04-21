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
//  7. syntax highlight in editor [done]
//  8. prevent loadfile, dofile [done]
//  9. provide local storage
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
#include "fnv_hash.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

//////////////////////////////////////////////////////////////////////////
// CONST DEFINITION
//////////////////////////////////////////////////////////////////////////

#define BBSLUA_INTERFACE_VER    0.119 // (0.201)
#define BBSLUA_SIGNATURE        "--#BBSLUA"

// BBS-Lua script format:
// $BBSLUA_SIGNATURE
// -- Interface: $interface
// -- Title: $title
// -- Notes: $notes
// -- Author: $author <email@domain>
// -- Version: $version
// -- Date: $date
// -- LatestRef: #AID board
//  [... script ...]
// $BBSLUA_SIGNATURE

//////////////////////////////////////////////////////////////////////////
// CONFIGURATION VARIABLES
//////////////////////////////////////////////////////////////////////////
#define BLAPI_PROTO     int

#define BLCONF_BREAK_KEY    Ctrl('C')
#define BLCONF_EXEC_COUNT   (5000)
#define BLCONF_PEEK_TIME    (0.01)
#define BLCONF_KBHIT_TMIN   (BLCONF_PEEK_TIME)
#define BLCONF_KBHIT_TMAX   (60*10)
#define BLCONF_SLEEP_TMIN   (BLCONF_PEEK_TIME)
#define BLCONF_SLEEP_TMAX   (BLCONF_KBHIT_TMAX)
#define BLCONF_U_SECOND     (1000000L)
#define BLCONF_PRINT_TOC_INDEX (2)

#define BLCONF_MMAP_ATTACH
#define BLCONF_CURRENT_USERID   cuser.userid
#define BLCONF_CURRENT_USERNICK cuser.nickname

// BBS-Lua Storage
enum {
    BLS_INVALID= 0,
    BLS_GLOBAL = 1,
    BLS_USER,
};

// #define BLSCONF_ENABLED
#define BLSCONF_BN_VAL  "global"
#define BLSCONF_USER_VAL    "user"
#define BLSCONF_GMAXSIZE    (16*1024)   // should be aligned to block size
#define BLSCONF_UMAXSIZE    (16*1024)   // should be aligned to block size
#define BLSCONF_GPATH       BBSHOME "/luastore"
#define BLSCONF_UPATH       ".luastore"
#define BLSCONF_PREFIX      "v1_"
#define BLSCONF_MAXIO       32          // prevent bursting system

// #define BBSLUA_USAGE

#ifdef _WIN32
# undef  BLCONF_MMAP_ATTACH
# undef  BLCONF_CURRENT_USERID
# define BLCONF_CURRENT_USERID    "guest"
# undef  BLCONF_CURRENT_USERNICK
# define BLCONF_CURRENT_USERNICK  "測試帳號"
#endif

//////////////////////////////////////////////////////////////////////////
// GLOBAL VARIABLES
//////////////////////////////////////////////////////////////////////////
typedef struct {
    char running;   // prevent re-entrant
    char abort;     // system break key hit
    char iocounter; // prevent bursting i/o
    Fnv32_t storename;  // storage filename
} BBSLuaRT;

// runtime information
// static 
BBSLuaRT blrt = {0};

#define BL_INIT_RUNTIME() { \
    memset(&blrt, 0, sizeof(blrt)); \
    blrt.storename = FNV1_32_INIT; \
}

#define BL_END_RUNTIME() { \
    memset(&blrt, 0, sizeof(blrt)); \
}

#ifdef BBSLUA_USAGE
static int bbslua_count;
#endif

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

static int
bl_peekbreak(float f)
{
    if (input_isfull())
        drop_input();
    if (peek_input(f, BLCONF_BREAK_KEY))
    {
        drop_input();
        blrt.abort = 1;
        return 1;
    }
    return 0;
}

static void
bl_k2s(lua_State* L, int v)
{
    if (v <= 0)
        lua_pushnil(L);
    else if (v == KEY_TAB)
        lua_pushstring(L, "TAB");
    else if (v == '\b' || v == 0x7F)
        lua_pushstring(L, "BS");
    else if (v == KEY_ENTER)
        lua_pushstring(L, "ENTER");
    else if (v < ' ')
        lua_pushfstring(L, "^%c", v-1+'A');
    else if (v < 0x100)
        lua_pushfstring(L, "%c", v);
    else if (v >= KEY_F1 && v <= KEY_F12)
        lua_pushfstring(L, "F%d", v - KEY_F1 +1);
    else switch(v)
    {
        case KEY_UP:    lua_pushstring(L, "UP");    break;
        case KEY_DOWN:  lua_pushstring(L, "DOWN");  break;
        case KEY_RIGHT: lua_pushstring(L, "RIGHT"); break;
        case KEY_LEFT:  lua_pushstring(L, "LEFT");  break;
        case KEY_HOME:  lua_pushstring(L, "HOME");  break;
        case KEY_END:   lua_pushstring(L, "END");   break;
        case KEY_INS:   lua_pushstring(L, "INS");   break;
        case KEY_DEL:   lua_pushstring(L, "DEL");   break;
        case KEY_PGUP:  lua_pushstring(L, "PGUP");  break;
        case KEY_PGDN:  lua_pushstring(L, "PGDN");  break;
        default:        lua_pushnil(L);             break;
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
        blrt.abort = 1;
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
    const char *pmsg = NULL;

    if (n > 0)
        len = lua_tointeger(L, 1);
    if (n > 1)
        echo = lua_tointeger(L, 2);
    if (n > 2)
        pmsg = lua_tostring(L, 3);

    if (len < 2)
        len = 2;
    if (len >= sizeof(buf))
        len = sizeof(buf)-1;
    /*
     * this part now done in getdata_str
    if (pmsg && *pmsg)
    {
        strlcpy(buf, pmsg, sizeof(buf));
    }
    */

    // TODO process Ctrl-C here
    getyx(&y, &x);
    if (!pmsg) pmsg = "";
    len = getdata_str(y, x, NULL, buf, len, echo, pmsg);
    if (len <= 0)
    {
        len = 0;
        // check if we got Ctrl-C? (workaround in getdata)
        // TODO someday write 'ungetch()' in io.c to prevent
        // such workaround.
        if (buf[1] == Ctrl('C'))
        {
            drop_input();
            blrt.abort = 1;
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
    if (bl_peekbreak(BLCONF_PEEK_TIME))
        return lua_yield(L, 0);

    drop_input();
    return 0;
}

BLAPI_PROTO
bl_sleep(lua_State *L)
{
    int n = lua_gettop(L);
    double us = 0, nus = 0;

    // update screen first.
    bl_refresh(L);

    if (n > 0)
        us = lua_tonumber(L, 1);
    if (us < BLCONF_SLEEP_TMIN) us = BLCONF_SLEEP_TMIN;
    if (us > BLCONF_SLEEP_TMAX) us = BLCONF_SLEEP_TMAX;
    nus = us;

#ifdef _WIN32

    Sleep(us * 1000);

#else // !_WIN32
    {
        struct timeval tp, tdest;

        gettimeofday(&tp, NULL);

        // nus is the destination time
        nus = bl_tv2double(&tp) + us;
        bl_double2tv(nus, &tdest);

        while ( (tp.tv_sec < tdest.tv_sec) ||
                ((tp.tv_sec == tdest.tv_sec) && (tp.tv_usec < tdest.tv_usec)))
        {
            // calculate new peek time
            us = nus - bl_tv2double(&tp);

            // check if input key is system break key.
            if (bl_peekbreak(us))
                return lua_yield(L, 0);

            // check time
            gettimeofday(&tp, NULL);
        }
    }
#endif // !_WIN32

    return 0;
}

BLAPI_PROTO
bl_kball(lua_State *L)
{
    // first, sleep by given seconds
    int r = 0, oldr = 0, i = 0;

    r = bl_sleep(L);
    if (blrt.abort)
        return r;

    // pop all arguments
    lua_pop(L, lua_gettop(L));


#ifdef _WIN32
    while (peekch(0))
    {
        bl_k2s(L, igetch());
        i++;
    }
#else
    // next, collect all input and return.
    if (num_in_buf() < 1)
        return 0;

    oldr = num_in_buf() +1;
    i = 0;

    while (  i < LUA_MINSTACK &&
            (r = num_in_buf()) > 0 && oldr > r)
    {
        oldr = r;
        bl_k2s(L, igetch());
        i++;
    }
#endif

    return i;
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
        blrt.abort = 1;
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

    vs_hdr(s);
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

//////////////////////////////////////////////////////////////////////////
// BBS-Lua Storage System
//////////////////////////////////////////////////////////////////////////
static int
bls_getcat(const char *s)
{
    if (!s || !*s)
        return BLS_INVALID;
    if (strcmp(s,       BLSCONF_BN_VAL) == 0)
        return          BLS_GLOBAL;
    else if (strcmp(s,  BLSCONF_USER_VAL) == 0)
        return          BLS_USER;
    return BLS_INVALID;
}

static int 
bls_getlimit(const char *p)
{
    switch(bls_getcat(p))
    {
        case BLS_GLOBAL:
            return  BLSCONF_GMAXSIZE;
        case BLS_USER:
            return  BLSCONF_UMAXSIZE;
    }
    return 0;
}

static int 
bls_setfn(char *fn, size_t sz, const char *p)
{
    *fn = 0;
    switch(bls_getcat(p))
    {
        case BLS_GLOBAL:
            snprintf(fn, sz, "%s/" BLSCONF_PREFIX "U%08x", 
                    BLSCONF_GPATH, blrt.storename);
            return  1;

        case BLS_USER:
            setuserfile(fn, BLSCONF_UPATH);
            mkdir(fn, 0755);
            assert(strlen(fn) +8 <= sz);
            snprintf(fn + strlen(fn),
                    sz - strlen(fn),
                    "/" BLSCONF_PREFIX "G%08x", blrt.storename);
            return  1;
    }
    return 0;
}

BLAPI_PROTO
bls_iolimit(lua_State *L)
{
    lua_pushinteger(L, BLSCONF_MAXIO);
    return 1;
}

BLAPI_PROTO
bls_limit(lua_State *L)
{
    int n = lua_gettop(L);
    const char *s = NULL;
    if (n > 0)
        s = lua_tostring(L, 1);
    lua_pushinteger(L, bls_getlimit(s));
    return 1;
}

BLAPI_PROTO
bls_load(lua_State *L)
{
    int n = lua_gettop(L);
    const char *cat = NULL;
    char fn[PATHLEN];

    if (blrt.iocounter >= BLSCONF_MAXIO)
    {
        lua_pushnil(L);
        return 1;
    }

    if (n != 1)
    {
        lua_pushnil(L);
        return 1;
    }
    cat = lua_tostring(L, 1); // category
    if (!cat)
    { 
        lua_pushnil(L);
        return 1; 
    }

    blrt.iocounter++;
    // read file!
    if (bls_setfn(fn, sizeof(fn), cat))
    {
        int fd = open(fn, O_RDONLY);
        if (fd >= 0)
        {
            char buf[2048];
            luaL_Buffer b;

            luaL_buffinit(L, &b);
            while ((n = read(fd, buf, sizeof(buf))) > 0)
                luaL_addlstring(&b, buf, n);
            close(fd);
            luaL_pushresult(&b);
        } else {
            lua_pushnil(L);
        }
    } else {
            lua_pushnil(L);
    }
    return 1;
}

BLAPI_PROTO
bls_save(lua_State *L)
{
    int n = lua_gettop(L), fd = -1;
    int limit = 0, slen = 0;
    const char *s = NULL, *cat = NULL;
    char fn[PATHLEN] = "", ret = 0;

    if (blrt.iocounter >= BLSCONF_MAXIO)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (n != 2) { lua_pushboolean(L, 0); return 1; }

    cat = lua_tostring(L, 1); // category
    s   = lua_tostring(L, 2); // data
    limit = bls_getlimit(cat);

    if (!cat || !s || limit < 1) 
    { 
        lua_pushboolean(L, 0); 
        return 1; 
    }

    slen = lua_objlen(L, 2);
    if (slen >= limit) slen = limit;

    blrt.iocounter++;
    // write file!
    if (bls_setfn(fn, sizeof(fn), cat))
    {
        fd = open(fn, O_WRONLY|O_CREAT, 0644);
        if (fd >= 0)
        {
            write(fd, s, slen);
            close(fd);
            ret = 1;
        }
    }

    lua_pushboolean(L, ret);
    return 1;
}

//////////////////////////////////////////////////////////////////////////
// BBSLUA LIBRARY
//////////////////////////////////////////////////////////////////////////

static const struct luaL_reg lib_bbslua [] = {
    /* curses output */
    { "getyx",      bl_getyx },
    { "getmaxyx",   bl_getmaxyx },
    { "move",       bl_move },
    { "moverel",    bl_moverel },
    { "clear",      bl_clear },
    { "clrtoeol",   bl_clrtoeol },
    { "clrtobot",   bl_clrtobot },
    { "refresh",    bl_refresh },
    { "addstr",     bl_addstr },
    { "outs",       bl_addstr },
    { "print",      bl_print },
    /* input */
    { "getch",      bl_getch },
    { "getdata",    bl_getstr },
    { "getstr",     bl_getstr },
    { "kbhit",      bl_kbhit },
    { "kbreset",    bl_kbreset },
    { "kball",      bl_kball },
    /* advanced output */
    { "rect",       bl_rect },
    /* BBS utilities */
    { "pause",      bl_pause },
    { "title",      bl_title },
    /* time */
    { "time",       bl_time },
    { "now",        bl_time },
    { "clock",      bl_clock },
    { "ctime",      bl_ctime },
    { "sleep",      bl_sleep },
    /* ANSI helpers */
    { "ANSI_COLOR", bl_ansi_color },
    { "color",      bl_attrset },
    { "attrset",    bl_attrset },
    { "strip_ansi", bl_strip_ansi },
    { NULL, NULL},
};

static const struct luaL_reg lib_store [] = {
    { "load",       bls_load },
    { "save",       bls_save },
    { "limit",      bls_limit },
    { "iolimit",    bls_iolimit },
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
    {"ESC",         ESC_STR},
    {"ANSI_RESET",  ANSI_RESET},
    {"sitename",    BBSNAME},
    {NULL,          NULL},
};

static const  bbsluaL_RegNum bbsluaNums[] = {
    {"interface",   BBSLUA_INTERFACE_VER},
    {NULL,          0},
};

static void
bbsluaRegConst(lua_State *L)
{
    int i = 0;

    // unbind unsafe API
    lua_pushnil(L); lua_setglobal(L, "dofile");
    lua_pushnil(L); lua_setglobal(L, "loadfile");

    // global
    lua_pushcfunction(L, bl_print);
    lua_setglobal(L, "print");

    // bbs.*
    lua_getglobal(L, "bbs");
    for (i = 0; bbsluaStrs[i].name; i++)
    {
        lua_pushstring(L, bbsluaStrs[i].val);
        lua_setfield(L, -2, bbsluaStrs[i].name);
    }

    for (i = 0; bbsluaNums[i].name; i++)
    {
        lua_pushnumber(L, bbsluaNums[i].val);
        lua_setfield(L, -2, bbsluaNums[i].name);
    }
    // dynamic info
    lua_pushstring(L, BLCONF_CURRENT_USERID);
    lua_setfield(L, -2, "userid");
    lua_pushstring(L, BLCONF_CURRENT_USERNICK);
    lua_setfield(L, -2, "usernick");

    lua_pop(L, 1);

#ifdef BLSCONF_ENABLED
    // store.*
    lua_getglobal(L, "store");
    lua_pushstring(L, BLSCONF_USER_VAL);
    lua_setfield(L, -2, "USER");
    lua_pushstring(L, BLSCONF_BN_VAL);
    lua_setfield(L, -2, "GLOBAL");
    lua_pop(L, 1);
#endif // BLSCONF_ENABLED
}

//////////////////////////////////////////////////////////////////////////
// BBSLUA ENGINE UTILITIES
//////////////////////////////////////////////////////////////////////////

static void
bbsluaHook(lua_State *L, lua_Debug* ar)
{
    // vmsg("bbslua HOOK!");
    if (blrt.abort)
    {
        drop_input();
        lua_yield(L, 0);
        return;
    }

    if (ar->event != LUA_HOOKCOUNT)
        return;
#ifdef BBSLUA_USAGE
    bbslua_count += BLCONF_EXEC_COUNT;
#endif

    // now, peek and check
    if (input_isfull())
        drop_input();

    // refresh();
    
    // check if input key is system break key.
    if (bl_peekbreak(BLCONF_PEEK_TIME))
    {
        lua_yield(L, 0);
        return;
    }
}

static char * 
bbslua_attach(const char *fpath, size_t *plen)
{
    char *buf = NULL;

#ifdef BLCONF_MMAP_ATTACH
    struct stat st;
    int fd = open(fpath, O_RDONLY);

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

//////////////////////////////////////////////////////////////////////////
// BBSLUA TOC Processing
//////////////////////////////////////////////////////////////////////////

static const char *bbsluaTocTags[] =
{
    "interface",
    "latestref",

    // BLCONF_PRINT_TOC_INDEX
    "title",
    "notes",
    "author",
    "version",
    "date",
    NULL
};

static const char *bbsluaTocPrompts[] = 
{
    "界面版本",
    "最新版本",

    // BLCONF_PRINT_TOC_INDEX
    "名稱",
    "說明",
    "作者",
    "版本",
    "日期",
    NULL
};

int
bbslua_load_TOC(lua_State *L, const char *bs, const char *be, char **ppc)
{
    unsigned char *ps = NULL, *pe = NULL;
    int i = 0;

    lua_newtable(L);
    *ppc = NULL;

    while (bs < be)
    {
        // find stripped line start, end
        ps = pe = (unsigned char *) bs;
        while (pe < (unsigned char*)be && *pe != '\n' && *pe != '\r')
            pe ++;
        bs = (char*)pe+1;
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

    if (ps >= (unsigned char*)bs && ps < (unsigned char*)be)
        *ppc = (char*)ps;
    lua_setglobal(L, "toc");
    return 0;
}

static void 
fullmsg(const char *s)
{
    clrtoeol();
    prints("%-*.*s\n", t_columns-1, t_columns-1, s);
}

void
bbslua_logo(lua_State *L)
{
    int y, by = b_lines -1; // print - back from bottom
    int i = 0;
    double tocinterface = 0;
    int tocs = 0;
    char msg[STRLEN];

    // get toc information
    lua_getglobal(L, "toc");
    lua_getfield(L, -1, bbsluaTocTags[0]);
    tocinterface = lua_tonumber(L, -1); lua_pop(L, 1);

    // query print-able toc tags
    for (i = BLCONF_PRINT_TOC_INDEX; bbsluaTocTags[i]; i++)
    {
        lua_getfield(L, -1, bbsluaTocTags[i]);
        if (lua_tostring(L, -1))
            tocs++;
        lua_pop(L, 1);
    }

    // prepare logo window
    grayout(0, b_lines, GRAYOUT_DARK);

    // print compatibility test
    // now (by) is the base of new information
    if (tocinterface == 0)
    {
        by -= 4; y = by+2;
        move(y-1, 0);
        outs(ANSI_COLOR(0;31;47));
        fullmsg("");
        fullmsg(" ▲ 此程式缺少相容性資訊，您可能無法正常執行");
        fullmsg("    若執行出現錯誤，請向原作者取得新版");
        fullmsg("");
    } 
    else if (tocinterface > BBSLUA_INTERFACE_VER)
    {
        by -= 4; y = by+2;
        move(y-1, 0); 
        outs(ANSI_COLOR(0;1;37;41));
        fullmsg("");
        snprintf(msg, sizeof(msg),
                " ▲ 此程式使用新版的 BBS-Lua 規格 (%0.3f)，您可能無法正常執行",
                tocinterface);
        fullmsg(msg);
        fullmsg("   若執行出現錯誤，建議您重新登入 BBS 後再重試");
        fullmsg("");
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

    // print toc, if any.
    if (tocs)
    {
        y = by - 1 - tocs;
        by = y-1;

        move(y, 0); outs(ANSI_COLOR(0;1;30;47));
        fullmsg("");

        // now try to print all TOC infos
        for (i = BLCONF_PRINT_TOC_INDEX; bbsluaTocTags[i]; i++)
        {
            lua_getfield(L, -1, bbsluaTocTags[i]);
            if (!lua_isstring(L, -1))
            {
                lua_pop(L, 1);
                continue;
            }
            move(++y, 0); 
            snprintf(msg, sizeof(msg), "  %s: %-.*s",
                    bbsluaTocPrompts[i], STRLEN-12, lua_tostring(L, -1));
            msg[sizeof(msg)-1] = 0;
            fullmsg(msg);
            lua_pop(L, 1);
        }
        fullmsg("");
    }

    // print caption
    move(by-2, 0); outc('\n');
    outs(ANSI_COLOR(0;1;37;44));
    snprintf(msg, sizeof(msg),
            " ■ BBS-Lua %.03f  (Build " __DATE__ " " __TIME__") ",
            (double)BBSLUA_INTERFACE_VER);
    fullmsg(msg);

    // system break key prompt
    {
        int sz = t_columns -1;
        const char 
            *prompt1 = "    提醒您執行中隨時可按 ",
            *prompt2 = "[Ctrl-C]",
            *prompt3 = " 強制中斷 BBS-Lua 程式";
        sz -= strlen(prompt1);
        sz -= strlen(prompt2);
        sz -= strlen(prompt3);
        outs(ANSI_COLOR(22;37));  outs(prompt1);
        outs(ANSI_COLOR(1;31));   outs(prompt2);
        outs(ANSI_COLOR(0;37;44));outs(prompt3);
        prints("%*s", sz, "");
        outs(ANSI_RESET);
    }
    lua_pop(L, 1);
}

//////////////////////////////////////////////////////////////////////////
// BBSLUA Script Loader
//////////////////////////////////////////////////////////////////////////

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

void bbslua_loadLatest(lua_State *L, 
        char **pbs, char **pps, char **ppe, char **ppc, int *psz,
        int *plineshift, char *bfpath, const char **pfpath)
{
    int r = 1; // max redirect for one article only.

    do {
        char *xbs = NULL, *xps = NULL, *xpe = NULL, *xpc = NULL;
        int xlineshift = 0;
        size_t xsz;
        const char *lastref = NULL;
        char loadnext = 0, isnewver = 0;

        // detect file
        xbs = bbslua_attach(bfpath, &xsz);
        if (!xbs)
            break;

        xps = xbs;
        xpe = xps + xsz;

        if(!bbslua_detect_range(&xps, &xpe, &xlineshift))
        {
            // not detected
            bbslua_detach(xbs, xsz);
            break;
        }

        // detect and verify TOC meta data
        lua_getglobal(L, "toc");    // stack 1
        if (lua_istable(L, -1))
        {
            const char *oref, *otitle, *nref, *ntitle;
            // load and verify tocinfo
            lua_getfield(L, -1, "latestref");   // stack 2
            oref = lua_tostring(L, -1);
            lua_getfield(L, -2, "title");       // stack 3
            otitle = lua_tostring(L, -1);
            bbslua_load_TOC(L, xps, xpe, &xpc);
            lua_getglobal(L, "toc");            // stack 4
            lua_getfield(L, -1, "latestref");   // stack 5
            nref = lua_tostring(L, -1);
            lua_getfield(L, -2, "title");       // stack 6
            ntitle = lua_tostring(L, -1);

            if (oref && nref && otitle && ntitle &&
                    strcmp(oref, nref) == 0 &&
                    strcmp(otitle, ntitle) == 0)
                isnewver = 1;

            // pop all
            lua_pop(L, 5);              // stack = 1 (old toc)
            if (!isnewver)
                lua_setglobal(L, "toc");
            else
                lua_pop(L, 1);
        } else {
            lua_pop(L, 1);
            bbslua_load_TOC(L, xps, xpe, &xpc);
        }

        if (*pbs && !isnewver)
        {
            // different.
            bbslua_detach(xbs, xsz);
            break;
        }

        // now, apply current buffer
        if (*pbs)
        {
            bbslua_detach(*pbs, *psz);
            // XXX fpath=bfpath, supporting only one level redirection now.
            *pfpath = bfpath;
        }
        *pbs = xbs; *pps = xps; *ppe = xpe; *psz = xsz; 
        *ppc = xpc;
        *plineshift = xlineshift;

#ifdef AID_DISPLAYNAME
        // quick exit
        if (r <= 0)
            break;

        // LatestRef only works if system supports AID.
        // try to load next reference.
        lua_getglobal(L, "toc");            // stack 1
        lua_getfield(L, -1, "latestref");   // stack 2
        lastref = lua_tostring(L, -1);

        while (lastref && *lastref)
        {
            // try to load by AID
            char bn[IDLEN+1] = "";
            aidu_t aidu = 0;
            unsigned char *p = (unsigned char*)bn;

            if (*lastref == '#') lastref++;
            aidu = aidc2aidu((char*)lastref);
            if (aidu <= 0) break;

            while (*lastref > ' ') lastref ++;              // lastref points to zero of space
            while (*lastref && *lastref <= ' ') lastref++;  // lastref points to zero or board name
            if (*lastref == '(') lastref ++;

            if (!*lastref) break;
            strlcpy(bn, lastref, sizeof(bn));
            // truncate board name
            // (not_alnum(ch) && ch != '_' && ch != '-' && ch != '.')
            while (*p && 
                (isalnum(*p) || *p == '_' || *p == '-' || *p == '.')) p++;
            *p = 0;
            if (bn[0])
            {
                bfpath[0] = 0;
                setaidfile(bfpath, bn, aidu);
            }

            if (bfpath[0])
                loadnext = 1;
            break;
        }
        lua_pop(L, 2);

        if (loadnext) continue;
#endif // AID_DISPLAYNAME
        break;

    } while (r-- > 0);
}

//////////////////////////////////////////////////////////////////////////
// BBSLUA Hash
//////////////////////////////////////////////////////////////////////////

#if 0
static int 
bbslua_hashWriter(lua_State *L, const void *p, size_t sz, void *ud)
{
    Fnv32_t *phash = (Fnv32_t*) ud;
    *phash = fnv_32_buf(p, sz, *phash);
    return 0;
}

static Fnv32_t 
bbslua_f2hash(lua_State *L)
{
    Fnv32_t fnvseed = FNV1_32_INIT;
    lua_dump(L, bbslua_hashWriter, &fnvseed);
    return fnvseed;
}

static Fnv32_t 
bbslua_str2hash(const char *str)
{
    if (!str)
        return 0;
    return fnv_32_str(str, FNV1_32_INIT);
}
#endif

static Fnv32_t 
bbslua_path2hash(const char *path)
{
    Fnv32_t seed = FNV1_32_INIT;
    if (!path)
        return 0;
    if (path[0] != '/') // relative, append BBSHOME
        seed = fnv_32_str(BBSHOME "/", seed);
    return fnv_32_str(path, seed);
}

//////////////////////////////////////////////////////////////////////////
// BBSLUA Main
//////////////////////////////////////////////////////////////////////////

int
bbslua(const char *fpath)
{
    int r = 0;
    lua_State *L;
    char *bs = NULL, *ps = NULL, *pe = NULL, *pc = NULL;
    char bfpath[PATHLEN] = "";
    int sz = 0;
    int lineshift = 0;
    AllocData ad;
#ifdef BBSLUA_USAGE
    struct rusage rusage_begin, rusage_end;
    struct timeval lua_begintime, lua_endtime;
    gettimeofday(&lua_begintime, NULL);
    getrusage(0, &rusage_begin);
#endif

#ifdef UMODE_BBSLUA
    unsigned int prevmode = getutmpmode();
#endif

    // re-entrant not supported!
    if (blrt.running)
        return 0;

    // initialize runtime
    BL_INIT_RUNTIME();

#ifdef BBSLUA_USAGE
    bbslua_count = 0;
#endif

    // init lua
    alloc_init(&ad);
    L = lua_newstate(allocf, &ad);
    if (!L)
        return 0;
    lua_atpanic(L, &panic);

    strlcpy(bfpath, fpath, sizeof(bfpath));

    // load file
    bbslua_loadLatest(L, &ps, &ps, &pe, &pc, &sz, &lineshift, bfpath, &fpath);

    if (!ps || !pe || ps >= pe)
    {
        if (bs)
            bbslua_detach(bs, sz);
        lua_close(L);
        vmsg("BBS-Lua 載入錯誤: 未含 BBS-Lua 程式");
        return 0;
    }

    // init library
    bbsluaL_openlibs(L);
    luaL_openlib(L,   "bbs",    lib_bbslua, 0);

#ifdef BLSCONF_ENABLED
    luaL_openlib(L,   "store",  lib_store, 0);
#endif // BLSCONF_ENABLED

    bbsluaRegConst(L);

    // load script
    r = bbslua_loadbuffer(L, ps, pe-ps, "BBS-Lua", lineshift);
    
    // build hash or store name
    blrt.storename = bbslua_path2hash(fpath);
    // vmsgf("BBS-Lua Hash: %08X", blrt.storename);

    // unmap
    bbslua_detach(bs, sz);

    if (r != 0)
    {
        const char *errmsg = lua_tostring(L, -1);
        outs(ANSI_RESET);
        move(b_lines-3, 0); clrtobot();
        outs("\n");
        outs(errmsg);
        lua_close(L); // delay closing because we need to print out error message
        vmsg("BBS-Lua 載入錯誤: 請通知作者修正程式碼。");
        return 0;
    }

#ifdef UMODE_BBSLUA
    setutmpmode(UMODE_BBSLUA);
#endif

    bbslua_logo(L);
    vmsgf("提醒您執行中隨時可按 [Ctrl-C] 強制中斷 BBS-Lua 程式");

    // ready for running
    clear();
    blrt.running =1;
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
    blrt.running =0;
    drop_input();
#ifdef BBSLUA_USAGE
    {
        double cputime;
        double load;
        double walltime;
        getrusage(0, &rusage_end);
        gettimeofday(&lua_endtime, NULL);
        cputime = bl_tv2double(&rusage_end.ru_utime) - bl_tv2double(&rusage_begin.ru_utime);
        walltime = bl_tv2double(&lua_endtime) - bl_tv2double(&lua_begintime);
        load = cputime / walltime;
        log_filef("log/bbslua.log", LOG_CREAT,
                "maxalloc=%d leak=%d op=%d cpu=%.3f Mop/s=%.1f load=%f file=%s\n",
                (int)ad.max_alloc_size, (int)ad.alloc_size,
                bbslua_count, cputime, bbslua_count / cputime / 1000000.0, load * 100,
                fpath);
    }
#endif

    // grayout(0, b_lines, GRAYOUT_DARK);
    move(b_lines, 0); clrtoeol();
    vmsgf("BBS-Lua 執行結束%s。", 
            blrt.abort ? " (使用者中斷)" : r ? " (程式錯誤)" : "");
    BL_END_RUNTIME();
    clear();

#ifdef UMODE_BBSLUA
    setutmpmode(prevmode);
#endif

    return 0;
}

// vim:ts=4:sw=4:expandtab
