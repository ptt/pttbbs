#ifndef INCLUDE_PATH_HELPER_H
#define INCLUDE_PATH_HELPER_H

#include "osdep.h"
#include "cleanup.h"
#include "common.h"

extern const char * const fn_mandex;

/*
 * Automatic string path helpers with Stack SSO (Small String Optimization)
 * and temporary compound literal expressions.
 *
 * Replaces traditional set*() buffer-filling functions:
 *
 *   Declaration form (declares a local variable with automatic cleanup):
 *     AUTO_BDIR(bdir, board);                // replaces: char bdir[PATHLEN]; setbdir(bdir, board);
 *     AUTO_BFILE(bfile, board, fname);       // replaces: char bfile[PATHLEN]; setbfile(bfile, board, fname);
 *     AUTO_BPATH(bpath, board);              // replaces: char bpath[PATHLEN]; setbpath(bpath, board);
 *     AUTO_BNFILE(bnfile, board, fname, n);  // replaces: char bnfile[PATHLEN]; setbnfile(bnfile, board, fname, n);
 *     AUTO_APATH(apath, board);              // replaces: char apath[PATHLEN]; setapath(apath, board);
 *     AUTO_AFILE(afile, board, fname);       // replaces: char afile[PATHLEN]; setafile(afile, board, fname);
 *     AUTO_ADIR(adir, path);                 // replaces: char adir[PATHLEN]; setadir(adir, path);
 *     AUTO_USERFILE(ufile, fname);           // replaces: char ufile[PATHLEN]; setuserfile(ufile, fname);
 *     AUTO_HOMEFILE(hfile, userid, fname);   // replaces: char hfile[PATHLEN]; sethomefile(hfile, userid, fname);
 *     AUTO_HOMEPATH(hpath, userid);          // replaces: char hpath[PATHLEN]; sethomepath(hpath, userid);
 *     AUTO_HOMEDIR(hdir, userid);            // replaces: char hdir[PATHLEN]; sethomedir(hdir, userid);
 *     AUTO_HOMEMAN(hman, userid);            // replaces: char hman[PATHLEN]; sethomeman(hman, userid);
 *     AUTO_DIRPATH(dpath, direct, fname);    // replaces: char dpath[PATHLEN]; setdirpath(dpath, direct, fname);
 *
 *   Expression form (for passing directly to function arguments):
 *     TEMP_BDIR(board)           or AUTO_BDIR(board)
 *     TEMP_BFILE(board, fname)   or AUTO_BFILE(board, fname)
 *     TEMP_BPATH(board)          or AUTO_BPATH(board)
 *     TEMP_USERFILE(fname)       or AUTO_USERFILE(fname)
 *     TEMP_HOMEFILE(u, fname)    or AUTO_HOMEFILE(u, fname)
 *     TEMP_HOMEPATH(u)           or AUTO_HOMEPATH(u)
 *     TEMP_HOMEDIR(u)            or AUTO_HOMEDIR(u)
 *     TEMP_HOMEMAN(u)            or AUTO_HOMEMAN(u)
 *     TEMP_APATH(board)          or AUTO_APATH(board)
 *     TEMP_AFILE(board, fname)   or AUTO_AFILE(board, fname)
 *     TEMP_ADIR(path)            or AUTO_ADIR(path)
 *     TEMP_DIRPATH(dir, fname)   or AUTO_DIRPATH(dir, fname)
 */

#define _PATH_MACRO_2(_1, _2, NAME, ...) NAME
#define _PATH_MACRO_3(_1, _2, _3, NAME, ...) NAME
#define _PATH_MACRO_4(_1, _2, _3, _4, NAME, ...) NAME

/* 1. Board Directory (setbdir) */
#define TEMP_BDIR(board) ({ \
    const char *_b = (board); \
    TEMPFORMAT(PATHLEN, "boards/%c/%s/%s", _b[0], _b, \
               ((currmode & MODE_DIGEST) ? fn_mandex : FN_DIR)); \
})
#define AUTO_BDIR_DECL(var, board) \
    const char *_b_##var = (board); \
    AUTO_STR(var, "boards/%c/%s/%s", _b_##var[0], _b_##var, \
             ((currmode & MODE_DIGEST) ? fn_mandex : FN_DIR))
#define AUTO_BDIR(...) _PATH_MACRO_2(__VA_ARGS__, AUTO_BDIR_DECL, TEMP_BDIR)(__VA_ARGS__)

/* 2. Board File (setbfile) */
#define TEMP_BFILE(board, fname) ({ \
    const char *_b = (board); \
    TEMPFORMAT(PATHLEN, "boards/%c/%s/%s", _b[0], _b, (fname)); \
})
#define AUTO_BFILE_DECL(var, board, fname) \
    const char *_b_##var = (board); \
    AUTO_STR(var, "boards/%c/%s/%s", _b_##var[0], _b_##var, (fname))
#define AUTO_BFILE(...) _PATH_MACRO_3(__VA_ARGS__, AUTO_BFILE_DECL, TEMP_BFILE)(__VA_ARGS__)

/* 3. Board Path (setbpath) */
#define TEMP_BPATH(board) ({ \
    const char *_b = (board); \
    TEMPFORMAT(PATHLEN, "boards/%c/%s", _b[0], _b); \
})
#define AUTO_BPATH_DECL(var, board) \
    const char *_b_##var = (board); \
    AUTO_STR(var, "boards/%c/%s", _b_##var[0], _b_##var)
#define AUTO_BPATH(...) _PATH_MACRO_2(__VA_ARGS__, AUTO_BPATH_DECL, TEMP_BPATH)(__VA_ARGS__)

/* 4. Board N-File (setbnfile) */
#define TEMP_BNFILE(board, fname, n) ({ \
    const char *_b = (board); \
    TEMPFORMAT(PATHLEN, "boards/%c/%s/%s.%d", _b[0], _b, (fname), (n)); \
})
#define AUTO_BNFILE_DECL(var, board, fname, n) \
    const char *_b_##var = (board); \
    AUTO_STR(var, "boards/%c/%s/%s.%d", _b_##var[0], _b_##var, (fname), (n))
#define AUTO_BNFILE(...) _PATH_MACRO_4(__VA_ARGS__, AUTO_BNFILE_DECL, TEMP_BNFILE)(__VA_ARGS__)

/* 5. Announce Path (setapath) */
#define TEMP_APATH(board) ({ \
    const char *_b = (board); \
    TEMPFORMAT(PATHLEN, "man/boards/%c/%s", _b[0], _b); \
})
#define AUTO_APATH_DECL(var, board) \
    const char *_b_##var = (board); \
    AUTO_STR(var, "man/boards/%c/%s", _b_##var[0], _b_##var)
#define AUTO_APATH(...) _PATH_MACRO_2(__VA_ARGS__, AUTO_APATH_DECL, TEMP_APATH)(__VA_ARGS__)

/* 6. Announce File (setafile) */
#define TEMP_AFILE(board, fname) ({ \
    const char *_b = (board); \
    TEMPFORMAT(PATHLEN, "man/boards/%c/%s/%s", _b[0], _b, (fname)); \
})
#define AUTO_AFILE_DECL(var, board, fname) \
    const char *_b_##var = (board); \
    AUTO_STR(var, "man/boards/%c/%s/%s", _b_##var[0], _b_##var, (fname))
#define AUTO_AFILE(...) _PATH_MACRO_3(__VA_ARGS__, AUTO_AFILE_DECL, TEMP_AFILE)(__VA_ARGS__)

/* 7. Announce Directory (setadir) */
#define TEMP_ADIR(path) TEMPFORMAT(PATHLEN, "%s/%s", (path), FN_DIR)
#define AUTO_ADIR_DECL(var, path) AUTO_STR(var, "%s/%s", (path), FN_DIR)
#define AUTO_ADIR(...) _PATH_MACRO_2(__VA_ARGS__, AUTO_ADIR_DECL, TEMP_ADIR)(__VA_ARGS__)

/* 8. User File (setuserfile) */
#define TEMP_USERFILE(fname) TEMPFORMAT(PATHLEN, "home/%c/%s/%s", cuser.userid[0], cuser.userid, (fname))
#define AUTO_USERFILE_DECL(var, fname) AUTO_STR(var, "home/%c/%s/%s", cuser.userid[0], cuser.userid, (fname))
#define AUTO_USERFILE(...) _PATH_MACRO_2(__VA_ARGS__, AUTO_USERFILE_DECL, TEMP_USERFILE)(__VA_ARGS__)

/* 9. Home File (sethomefile) */
#define TEMP_HOMEFILE(userid, fname) ({ \
    const char *_u = (userid); \
    TEMPFORMAT(PATHLEN, "home/%c/%s/%s", _u[0], _u, (fname)); \
})
#define AUTO_HOMEFILE_DECL(var, userid, fname) \
    const char *_u_##var = (userid); \
    AUTO_STR(var, "home/%c/%s/%s", _u_##var[0], _u_##var, (fname))
#define AUTO_HOMEFILE(...) _PATH_MACRO_3(__VA_ARGS__, AUTO_HOMEFILE_DECL, TEMP_HOMEFILE)(__VA_ARGS__)

/* 10. Home Path (sethomepath) */
#define TEMP_HOMEPATH(userid) ({ \
    const char *_u = (userid); \
    TEMPFORMAT(PATHLEN, "home/%c/%s", _u[0], _u); \
})
#define AUTO_HOMEPATH_DECL(var, userid) \
    const char *_u_##var = (userid); \
    AUTO_STR(var, "home/%c/%s", _u_##var[0], _u_##var)
#define AUTO_HOMEPATH(...) _PATH_MACRO_2(__VA_ARGS__, AUTO_HOMEPATH_DECL, TEMP_HOMEPATH)(__VA_ARGS__)

/* 11. Home Directory (sethomedir) */
#define TEMP_HOMEDIR(userid) ({ \
    const char *_u = (userid); \
    TEMPFORMAT(PATHLEN, "home/%c/%s/%s", _u[0], _u, FN_DIR); \
})
#define AUTO_HOMEDIR_DECL(var, userid) \
    const char *_u_##var = (userid); \
    AUTO_STR(var, "home/%c/%s/%s", _u_##var[0], _u_##var, FN_DIR)
#define AUTO_HOMEDIR(...) _PATH_MACRO_2(__VA_ARGS__, AUTO_HOMEDIR_DECL, TEMP_HOMEDIR)(__VA_ARGS__)

/* 12. Home Man (sethomeman) */
#define TEMP_HOMEMAN(userid) ({ \
    const char *_u = (userid); \
    TEMPFORMAT(PATHLEN, "home/%c/%s/man", _u[0], _u); \
})
#define AUTO_HOMEMAN_DECL(var, userid) \
    const char *_u_##var = (userid); \
    AUTO_STR(var, "home/%c/%s/man", _u_##var[0], _u_##var)
#define AUTO_HOMEMAN(...) _PATH_MACRO_2(__VA_ARGS__, AUTO_HOMEMAN_DECL, TEMP_HOMEMAN)(__VA_ARGS__)

/* 13. Directory Path (setdirpath) */
static inline char *autostring_init_dirpath(autostring_t *as, const char *direct, const char *fname) {
    const char *p = strrchr(direct, '/');
    if (p) {
        int dirlen = (int)(p - direct + 1);
        return autostring_init_format(as, "%.*s%s", dirlen, direct, fname);
    }
    return autostring_init_format(as, "%s", fname);
}
#define TEMP_DIRPATH(direct, fname) ({ \
    const char *_d = (direct); \
    const char *_p = strrchr(_d, '/'); \
    _p ? TEMPFORMAT(PATHLEN, "%.*s%s", (int)(_p - _d + 1), _d, (fname)) \
       : TEMPFORMAT(PATHLEN, "%s", (fname)); \
})
#define AUTO_DIRPATH_DECL(var, direct, fname) \
    autostring_t _as_##var __attribute__((cleanup(autostring_cleanup))); \
    __attribute__((unused)) char *var = autostring_init_dirpath(&_as_##var, (direct), (fname)); \
    __attribute__((unused)) int var##_len = _as_##var.len
#define AUTO_DIRPATH(...) _PATH_MACRO_3(__VA_ARGS__, AUTO_DIRPATH_DECL, TEMP_DIRPATH)(__VA_ARGS__)

#endif /* INCLUDE_PATH_HELPER_H */
