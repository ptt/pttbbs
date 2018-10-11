#include "cmpttui/pttui_lua_bbs.h"

const char * const luaKeywords[] = {
    "and",   "break", "do",  "else", "elseif",
    "end",   "for",   "if",  "in",   "not",  "or",
    "repeat","return","then","until","while",
    NULL
};

const char * const luaDataKeywords[] = {
    "false", "function", "local", "nil", "true",
    NULL
};

const char * const luaFunctions[] = {
    "assert", "print", "tonumber", "tostring", "type",
    NULL
};

const char * const luaMath[] = {
    "abs", "acos", "asin", "atan", "atan2", "ceil", "cos", "cosh", "deg",
    "exp", "floor", "fmod", "frexp", "ldexp", "log", "log10", "max", "min",
    "modf", "pi", "pow", "rad", "random", "randomseed", "sin", "sinh",
    "sqrt", "tan", "tanh",
    NULL
};

const char * const luaTable[] = {
    "concat", "insert", "maxn", "remove", "sort",
    NULL
};

const char * const luaString[] = {
    "byte", "char", "dump", "find", "format", "gmatch", "gsub", "len",
    "lower", "match", "rep", "reverse", "sub", "upper", NULL
};

const char * const luaBbs[] = {
    "ANSI_COLOR", "ANSI_RESET", "ESC", "addstr", "clear", "clock",
    "clrtobot", "clrtoeol", "color", "ctime", "getch","getdata",
    "getmaxyx", "getstr", "getyx", "interface", "kball", "kbhit", "kbreset",
    "move", "moverel", "now", "outs", "pause", "print", "rect", "refresh",
    "setattr", "sitename", "sleep", "strip_ansi", "time", "title",
    "userid", "usernick",
    NULL
};

const char * const luaToc[] = {
    "author", "date", "interface", "latestref",
    "notes", "title", "version",
    NULL
};

const char * const luaBit[] = {
    "arshift", "band", "bnot", "bor", "bxor", "cast", "lshift", "rshift",
    NULL
};

const char * const luaStore[] = {
    "USER", "GLOBAL", "iolimit", "limit", "load", "save",
    NULL
};

const char * const luaLibs[] = {
    "bbs", "bit", "math", "store", "string", "table", "toc",
    NULL
};

const char* const * const luaLibAPI[] = {
    luaBbs, luaBit, luaMath, luaStore, luaString, luaTable, luaToc,
    NULL
};
