
#define FAVT_BOARD  1
#define FAVT_FOLDER 2
#define FAVT_LINE   3

#define FAVH_FAV	1
#define FAVH_TAG	2
#define FAVH_UNREAD	4

#define TRUE  1
#define FALSE 0

#define FAV_PRE_ALLOC	8
#define FAV_MAXDEPTH	5
#define MAX_FAV		1024
#define MAX_LINE	32
#define MAX_FOLDER	32

#define FAV3		".fav3"
#define FAV4		".fav4"

typedef struct {
    char	    type;
    char	    attr;
    /* *fp could be *fav_board_t or *fav_folder_t. */
    void	   *fp;
} fav_type_t;

typedef struct {
    short           nAllocs;
    short           nDatas;
    short	    nBoards;
    char            nLines;
    char            nFolders;

    /* record of boards/folders */
    fav_type_t	   *favh;
} fav_t;

typedef struct {
    short           bid;
    time_t          lastvisit;
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
