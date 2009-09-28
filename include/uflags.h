/* $Id: uflags.h  $ */
/* PTT BBS User Flags */

#ifndef INCLUDE_UFLAGS_H
#define INCLUDE_UFLAGS_H

/* -------------------- userec_t.uflag (unsigned int) */

#define UF_FAV_NOHILIGHT    0x00000001	// false if hilight favorite
#define UF_FAV_ADDNEW	    0x00000002	// true to add new board into one's fav
// #define UF_PAGER	    0x00000004	// deprecated by cuser.pager: true if pager was OFF last session
// #define UF_CLOAK	    0x00000008	// deprecated by cuser.invisible: true if cloak was ON last session
#define UF_FRIEND	    0x00000010	// true if show friends only
#define UF_BRDSORT	    0x00000020	// true if the boards sorted alphabetical
#define UF_ADBANNER	    0x00000040	// (was: MOVIE_FLAG, true if show advertisement banner
#define UF_ADBANNER_USONG   0x00000080	// true if show user songs in banner
// #define UF_MIND	    0x00000100	// deprecated: true if mind search mode open <-Heat
#define UF_DBCS_AWARE	    0x00000200	// true if DBCS-aware enabled.
#define UF_DBCS_NOINTRESC   0x00000400	// no Escapes interupting DBCS characters
#define UF_DBCS_DROP_REPEAT 0x00000800	// detect and drop repeated input from evil clients
// #define UF_DBCS_???	    0x00000800	// reserved
#define UF_NO_MODMARK	    0x00001000	// true if modified files are NOT marked
#define UF_COLORED_MODMARK  0x00002000	// true if mod-mark is coloured.
// #define UF_MODMARK_???   0x00004000	// reserved
// #define UF_MODMARK_???   0x00008000	// reserved
#define UF_DEFBACKUP	    0x00010000	// true if user defaults to backup
// #define UF_???	    0x00020000	// reserved
#define UF_REJ_OUTTAMAIL    0x00040000  // true if don't accept outside mails
// #define UF_???	    0x00080000	// reserved
#define UF_FOREIGN	    0x00100000  // true if a foreign
#define UF_LIVERIGHT	    0x00200000  // true if get "liveright" already
// #define UF_COUNTRY_???   0x00400000	// reserved
// #define UF_COUNTRY_???   0x00800000	// reserved
// #define UF_???	    0x01000000	// reserved
// #define UF_???	    0x02000000	// reserved
// #define UF_???	    0x04000000	// reserved
// #define UF_???	    0x08000000	// reserved
// #define UF_???	    0x10000000	// reserved
// #define UF_???	    0x20000000	// reserved
// #define UF_???	    0x40000000	// reserved
// #define UF_???	    0x80000000	// reserved

// Helper Macros
#define HasUserFlag(x)	 (cuser.uflag & (x))
#define TOBACKUP(x)	 ((cuser.uflag & UF_DEFBACKUP) ? \
	(x != 'n') : \
	(x == 'y') )
#define REJECT_OUTTAMAIL(x) (x.uflag & UF_REJ_OUTTAMAIL)
#define ISDBCSAWARE()	(cuser.uflag & UF_DBCS_AWARE)

/* -------------------- userec_t.uflag2 (unsigned int) */

#define OUF2_FAVNOHILIGHT    0x00000010  /* false if hilight favorite */
#define OUF2_FAVNEW_FLAG     0x00000020  /* true if add new board into one's fav */
#define OUF2_FOREIGN         0x00000100  /* true if a foreign */
#define OUF2_LIVERIGHT       0x00000200  /* true if get "liveright" already */
#define OUF2_REJ_OUTTAMAIL   0x00000400  /* true if don't accept outside mails */

#endif //  INCLUDE_UFLAGS_H
