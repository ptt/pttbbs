/* $Id: uflags.h  $ */
/* PTT BBS User Flags */

#ifndef INCLUDE_UFLAGS_H
#define INCLUDE_UFLAGS_H

/* -------------------- userec_t.uflag (unsigned int) */

/* UNKNOWN */

/* TRADITIONAL BBS UFLAG */
//#define UNKNOWN_FLAG  0x00000001 // deprecated ?
//#define UNKNOWN_FLAG2 0x00000002 // deprecated ?
#define PAGER_FLAG	0x00000004 /* true if pager was OFF last session */
#define CLOAK_FLAG      0x00000008 /* true if cloak was ON last session */
#define FRIEND_FLAG     0x00000010 /* true if show friends only */
#define BRDSORT_FLAG    0x00000020 /* true if the boards sorted alphabetical */
#define MOVIE_FLAG      0x00000040 /* true if show movie */
/* deprecated flag */
//#define COLOR_FLAG    0x00000080 /* true if the color mode open */
//#define MIND_FLAG     0x00000100 /* true if mind search mode open <-Heat*/

/* DBCS CONFIG */
#define DBCSAWARE_FLAG	0x00000200 /* true if DBCS-aware enabled. */
/* please keep this even if you don't have DBCSAWARE features turned on */
// #define DBCS__???	0x00000400
// #define DBCS__???	0x00000800

/* Modification Mark (~) */
#define NO_MODMARK_FLAG	0x00001000 /* true if modified files are NOT marked */
#define COLORED_MODMARK	0x00002000 /* true if mod-mark is coloured. */

/* NEW ENTRY HERE */
// #define ??__???	0x00010000

/* -------------------- userec_t.uflag2 (unsigned int) */

#define WATER_ORIG      0x00000000
#define WATER_NEW       0x00000001
#define WATER_OFO       0x00000002
#define WATERMODE(mode) ((cuser.uflag2 & WATER_MASK) == mode)
#define WATER_MASK      0x00000003 /* water mask */
// #define WATER_???	0x00000004
// #define WATER_???	0x00000008

/* MYFAV */
#define FAVNOHILIGHT    0x00000010  /* false if hilight favorite */
#define FAVNEW_FLAG     0x00000020  /* true if add new board into one's fav */
// #define FAV_???	0x00000040
// #define FAV_???	0x00000080

/* MISC */
#define FOREIGN         0x00000100  /* true if a foreign */
#define LIVERIGHT       0x00000200  /* true if get "liveright" already */
#define REJ_OUTTAMAIL   0x00000400  /* true if don't accept outside mails */
#define REJECT_OUTTAMAIL (cuser.uflag2 & REJ_OUTTAMAIL)

/* ANGEL */
#define REJ_QUESTION    0x00000800 /* true if don't want to be angel for a while */
#define ANGEL_R_MAEL    0x00001000 /* true if reject male */
#define ANGEL_R_FEMAEL  0x00002000 /* true if reject female */
#define ANGEL_MASK      0x00003000
#define ANGEL_STATUS()  ((cuser.uflag2 & ANGEL_MASK) >> 12)
#define ANGEL_SET(X)    (cuser.uflag2 = (cuser.uflag2 & ~ANGEL_MASK) | \
                          (((X) & 3) << 12))
// #define ANGEL_???    0x00004000
// #define ANGEL_???    0x00008000

/* NEW ENTRY HERE */
// #define ???_???	0x00010000

#endif //  INCLUDE_UFLAGS_H
