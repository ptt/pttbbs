/* $Id$ */
#ifndef PTT_FAV_H
#define PTT_FAV_H


#define FAV_VERSION 3363

#define FAVT_BOARD  1
#define FAVT_FOLDER 2
#define FAVT_LINE   3

#define FAVH_FAV	1
#define FAVH_TAG	2
#define FAVH_UNREAD	4
#define FAVH_ADM_TAG	8
/* ������ t �Ӻ޲z (eg.�h��) �ݪO�� �ª��@�k�O��o�� tag �_�Ӫ��ݪO
 * �O���b fav �̭��C���F���A�h���L�O����A�o���~��u�ΡC*/

#define FALSE	0
#define TRUE	1
#define EXCH	2

#define FAV_PRE_ALLOC	8
#define FAV_MAXDEPTH	5
#define MAX_FAV		1024
#define MAX_LINE	64
#define MAX_FOLDER	64
#define NEW_FAV_THRESHOLD	12 /* half page */

#define FAV		".fav"
#define FAV4		".fav4"
#define FAVNB      	".favnb"

typedef struct {
    char	    type;
    char	    attr;
    /* *fp could be *fav_board_t or *fav_folder_t. */
    void	   *fp;
} fav_type_t;

typedef struct {
    short           nAllocs;
    short           DataTail;		/* the tail of item list that user
					   have ever used */
    short	    nBoards;		/* number of the boards */
    char            nLines;		/* number of the lines */
    char            nFolders;		/* number of the folders */
    char	    lineID;		/* current max line id */
    char	    folderID;		/* current max folder id */

    fav_type_t	   *favh;		/* record of boards/folders */
} fav_t;

typedef struct {
    int             bid;
    time4_t         lastvisit;		/* UNUSED */
    char	    attr;
} fav_board_t;

typedef struct {
    char	    fid;
    char	    title[BTLEN + 1];
    fav_t	   *this_folder;
} fav_folder_t;

typedef struct {
    char	    lid;
} fav_line_t;


#endif /* PTT_FAV_H */