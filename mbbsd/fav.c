#include "bbs.h"

#ifdef MEM_CHECK
static int	memcheck;
#endif

/* the total number of items */
static int 	fav_number;

/* definition of fav stack, the top one is in use now. */
static int	fav_stack_num = 0;
static fav_t   *fav_stack[FAV_MAXDEPTH] = {0};

/* current fav_type_t placement */
static int	fav_place;

/* fav_old is for recordinge while copying, moving, etc. */
static fav_t   *fav_old;
static int	fav_old_snum; /* the sequence number in favh in fav_t */


/* "current" means what at the position of the cursor */
inline fav_t *get_current_fav(void){
    if (fav_stack_num == 0)
	return NULL;
    return fav_stack[fav_stack_num];
}

inline static int get_item_type(fav_type_t *ft){
    return ft->type;
}

inline fav_type_t *get_current_entry(void){
    return &fav_stack[fav_stack_num]->favh[fav_place];
}

inline void fav_set_old_folder(fav_t *fp){
    fav_old = fp;
}

/* for casting */
inline static fav_board_t *cast_board(fav_type_t *p){
    return (fav_board_t *)p->fp;
}

inline static fav_line_t *cast_line(fav_type_t *p){
    return (fav_line_t *)p->fp;
}

inline static fav_folder_t *cast_folder(fav_type_t *p){
    return (fav_folder_t *)p->fp;
}
/* --- */

inline static void line_decrease(fav_t *fp) {
//    fp->nLines--;
    fav_number--;
}

inline static void board_decrease(fav_t *fp) {
    fp->nBoards--;
    fav_number--;
}

inline static void folder_decrease(fav_t *fp) {
//    fp->nFolders--;
    fav_number--;
}
/* --- */

inline static void line_increase(fav_t *fp) {
    fp->nLines++;
    fav_number++;
}

inline static void board_increase(fav_t *fp) {
    fp->nBoards++;
    fav_number++;
}

inline static void folder_increase(fav_t *fp) {
    fp->nFolders++;
    fav_number++;
}
/* --- */

inline static int get_folder_num(fav_t *fp) {
    return fp->nFolders;
}

inline static int get_line_num(fav_t *fp) {
    return fp->nLines;
}

/* bool:
 *   0: unset 1: set 2: opposite */
inline void set_attr(fav_type_t *ft, int bit, int bool){
    if (bool == 2)
	bool = !(ft->attr & bit);
    if (bool)
	ft->attr |= bit;
    else
	ft->attr &= ~bit;
}

inline int is_set_attr(fav_type_t *ft, int bit){
    return ft->attr & bit;
}
/* --- */

static char *get_item_title(fav_type_t *ft)
{
    switch (get_item_type(ft)){
	case FAVT_BOARD:
	    return bcache[cast_board(ft)->bid - 1].brdname;
	case FAVT_FOLDER:
	    return cast_folder(ft)->title;
	case FAVT_LINE:
	    return "----";
    }
    return NULL;
}

static char *get_item_class(fav_type_t *ft)
{
    switch (get_item_type(ft)){
	case FAVT_BOARD:
	    return bcache[cast_board(ft)->bid - 1].title;
	case FAVT_FOLDER:
	    return cast_folder(ft)->title;
	case FAVT_LINE:
	    return "----";
    }
    return NULL;
}

#ifdef MEM_CHECK
inline void fav_set_memcheck(int n) {
    memcheck = n;
}

inline int fav_memcheck(void) {
    return memcheck;
}
#endif
/* ---*/

static int get_type_size(int type)
{
    switch (type){
	case FAVT_BOARD:
	    return sizeof(fav_board_t);
	case FAVT_FOLDER:
	    return sizeof(fav_folder_t);
	case FAVT_LINE:
	    return sizeof(fav_line_t);
    }
    return 0;
}

inline static void* fav_malloc(int size){
    void *p = (void *)malloc(size);
    memset(p, 0, size);
    return p;
}

/* allocate the header(fav_type_t) and item it self. */
static fav_type_t *fav_allocate(int type)
{
    int size = 0;
    fav_type_t *ft = (fav_type_t *)fav_malloc(sizeof(fav_type_t));

    size = get_type_size(type);
    if (size) {
	ft->fp = fav_malloc(size);
	ft->type = type;
    }
    return ft;
}

inline static fav_t *get_fav_root(void){
    return fav_stack[0];
}

/* return: the exact number after cleaning
 * reset the line number, board number, folder number, and total number (number)
 */
static void clean_fav(fav_t *fp)
{
    int i, bid;
    fp->nLines = fp->nBoards = fp->nFolders = 0;
    for (i = 0; i < fp->nAllocs; i++){
	if (!is_set_attr(&fp->favh[i], FAVH_FAV))
	    continue;
	switch (get_item_type(&fp->favh[i])){
	    case FAVT_BOARD:
		bid = cast_board(&fp->favh[i])->bid;
		if (!validboard(bid - 1))
		    continue;
		board_increase(fp);
		break;
	    case FAVT_LINE:
		line_increase(fp);
		break;
	    case FAVT_FOLDER:
		clean_fav(cast_folder(fp->favh[i].fp)->this_folder);
		folder_increase(fp);
		break;
	    default:
		continue;
	}
    }
}

static void cleanup(void)
{
    fav_number = 0;
    clean_fav(get_fav_root());
}

/* sort the fav */
static int favcmp_by_name(const void *a, const void *b)
{
    return strcasecmp(get_item_title((fav_type_t *)a), get_item_title((fav_type_t *)b));
}

void fav_sort_by_name(void)
{
    clean_fav(get_current_fav());
    qsort(get_current_fav(), get_current_fav()->nDatas, sizeof(fav_type_t), favcmp_by_name);
}

static int favcmp_by_class(const void *a, const void *b)
{
    fav_type_t *f1, *f2;
    int cmp;

    f1 = (fav_type_t *)a;
    f2 = (fav_type_t *)b;
    if (f1->type == FAVT_FOLDER || f2->type == FAVT_FOLDER)
	return -1;
    if (f1->type == FAVT_LINE || f2->type == FAVT_LINE)
	return 1;

    cmp = strncasecmp(get_item_class(f1), get_item_class(f2), 4);
    if (cmp)
	return cmp;
    return strcasecmp(get_item_title(f1), get_item_title(f1));
}

void fav_sort_by_class(void)
{
    clean_fav(get_current_fav());
    qsort(get_current_fav(), get_current_fav()->nDatas, sizeof(fav_type_t), favcmp_by_class);
}
/* --- */

inline static void
fav_item_copy_symbolic(fav_type_t *target, const fav_type_t *source){
    target->type = source->type;
    target->attr = source->attr;
    target->fp = source->fp;
}

inline static void
fav_item_copy(fav_type_t *target, const fav_type_t *source){
    target = fav_allocate(source->type);
    fav_item_copy_symbolic(target, source);
}

/* The following is the movement operations in the user interface. */
inline static void fav_stack_push_fav(fav_t *fp){
    fav_stack[fav_stack_num++] = fp;
    fav_place = 0;
}

inline static void fav_stack_push(fav_type_t *ft){
    if (ft->type != FAVT_FOLDER)
	return;
    fav_stack_push_fav(cast_folder(ft)->this_folder);
}

inline static void fav_stack_pop(void){
    fav_stack[--fav_stack_num] = NULL;
    fav_place = 0;
}

void fav_folder_in(void)
{
    fav_type_t *tmp = get_current_entry();
    if (tmp->type == FAVT_FOLDER){
	fav_stack_push(tmp);
    }
}

void fav_folder_out(void)
{
    fav_stack_pop();
}

void fav_cursor_up(void)
{
    fav_t *ft = get_current_fav();
    while(1){
	if (fav_place == 0)
	    fav_place = ft->nBoards - 1;
	if (ft->favh[fav_place].attr & FAVH_FAV)
	    break;
	fav_place--;
    }
}

void fav_cursor_down(void)
{
    fav_t *ft = get_current_fav();
    while(1){
	if (fav_place == ft->nBoards - 1)
	    fav_place = 0;
	if (ft->favh[fav_place].attr & FAVH_FAV)
	    break;
	fav_place++;
    }
}
/* --- */

/* load from the rec file */
static void read_favrec(int fd, fav_t *fp)
{
    int i, total;
    fp = (fav_t *)malloc(sizeof(fav_t));
    read(fd, &fp->nDatas, sizeof(fp->nDatas));
    read(fd, &fp->nBoards, sizeof(fp->nBoards));
    read(fd, &fp->nLines, sizeof(fp->nLines));
    read(fd, &fp->nFolders, sizeof(fp->nFolders));
    total = fp->nBoards + fp->nLines + fp->nFolders;
    fp->favh = (fav_type_t *)malloc(sizeof(fav_type_t) * total);

    for(i = 0; i < total; i++){
	read(fd, &fp->favh[i].type, sizeof(fp->favh[i].type));
	read(fd, &fp->favh[i].attr, sizeof(fp->favh[i].attr));
	fp->favh[i].fp = (void *)malloc(get_type_size(fp->favh[i].type));
	read(fd, fp->favh[i].fp, get_type_size(fp->favh[i].type));
    }

    for(i = 0; i < total; i++){
	if (fp->favh[i].type == FAVT_FOLDER)
	    read_favrec(fd, cast_folder(&fp->favh[i])->this_folder);
    }
}

int fav_load(void)
{
    int fd;
    char buf[128];
    fav_t *fp;
    if (fav_stack_num > 0)
	return -1;
    setuserfile(buf, FAV4);
    fd = open(buf, O_RDONLY);
    if (fd < 0){
	return -1;
    }
    fp = (fav_t *)fav_malloc(sizeof(fav_t));
    fav_stack_push_fav(fp);
    close(fd);
    fav_set_memcheck(MEM_CHECK);
    return 0;
}
/* --- */

/* write to the rec file */
static void write_favrec(int fd, fav_t *fp)
{
    int i, total;
    write(fd, &fp->nDatas, sizeof(fp->nDatas));
    write(fd, &fp->nBoards, sizeof(fp->nBoards));
    write(fd, &fp->nLines, sizeof(fp->nLines));
    write(fd, &fp->nFolders, sizeof(fp->nFolders));
    total = fp->nBoards + fp->nLines + fp->nFolders;
    
    for(i = 0; i < total; i++){
	write(fd, &fp->favh[i].type, sizeof(fp->favh[i].type));
	write(fd, &fp->favh[i].attr, sizeof(fp->favh[i].attr));
	write(fd, fp->favh[i].fp, get_type_size(fp->favh[i].type));
    }
    
    for(i = 0; i < total; i++){
	if (fp->favh[i].type == FAVT_FOLDER)
	    write_favrec(fd, cast_folder(&fp->favh[i])->this_folder);
    }
}

int fav_save(void)
{
    int fd;
    char buf[128], buf2[128];
#ifdef MEM_CHECK
    if (fav_memcheck() != MEM_CHECK)
	return -1;
#endif
    fav_t *fp = get_fav_root();
    if (fp == NULL)
	return -1;
    cleanup();
    setuserfile(buf, FAV4".tmp");
    setuserfile(buf2, FAV4);
    fd = open(buf, O_TRUNC | O_WRONLY);
    if (fd < 0)
	return -1;
    write_favrec(fd, fp);
    close(fd);
    Rename(buf, buf2);
    return 0;
}
/* --- */

/* It didn't need to remove it self, just remove all the attributes.
 * It'll be remove when it save to the record file. */
static void fav_free_item(fav_type_t *ft)
{
    free(ft->fp);
    set_attr(ft, 0xFFFF, FALSE);
    ft = NULL;
}

static int fav_remove(fav_t *fp, fav_type_t *ft)
{
    switch(get_item_type(ft)){
	case FAVT_BOARD:
	    board_decrease(fp);
	    break;
	case FAVT_FOLDER:
	    folder_decrease(fp);
	    break;
	case FAVT_LINE:
	    line_decrease(fp);
	    break;
    }
    fav_free_item(ft);
    return 0;
}

void fav_remove_current(void)
{
    fav_remove(get_current_fav(), get_current_entry());
}

void fav_remove_board_from_whole(int bid)
{
    int i;
    fav_t *fp = get_current_fav();
    fav_type_t *ft;
    for(i = 0; i < fp->nAllocs; i++){
	ft = &fp->favh[i];
	if (ft->attr & FAVT_BOARD && cast_board(ft)->bid == bid)
	    fav_remove(fp, ft);
	else if (ft->attr & FAVT_FOLDER){
	    fav_stack_push(ft);
	    fav_remove_board_from_whole(bid);
	    fav_stack_pop();
	}
    }
}

static fav_type_t *get_favrec(short bid, int type)
{
    int i;
    fav_type_t *ft;
    fav_t *fp = get_current_fav();

    for(i = 0; i < fp->nAllocs; i++){
	ft = &fp->favh[i];
	if (get_item_type(ft) != type)
	    continue;
	if (!is_set_attr(ft, FAVH_FAV))
	    continue;
	if (cast_board(ft)->bid == bid)
	    return ft;
    }
    return NULL;
}

static fav_type_t *getboard(short bid)
{
    return get_favrec(bid, FAVT_BOARD);
}


char getbrdattr(short bid)
{
    fav_type_t *fb = getboard(bid);
    if (!fb)
	return 0;
    return fb->attr;
}

time_t getbrdtime(short bid)
{
    fav_type_t *fb = getboard(bid);
    if (!fb)
	return 0;
    return cast_board(fb)->lastvisit;
}

void setbrdtime(short bid, time_t t)
{
    fav_type_t *fb = getboard(bid);
    if (fb)
	cast_board(fb)->lastvisit = t;
}

int fav_getid(fav_type_t *ft)
{
    switch(get_item_type(ft)){
	case FAVT_FOLDER:
	    return cast_folder(ft)->fid;
	case FAVT_LINE:
	    return cast_line(ft)->lid;
	case FAVT_BOARD:
	    return cast_board(ft)->bid;
    }
    return -1;
}

/* suppose we don't add too much fav_type_t at the same time. */
static int enlarge_if_full(fav_t *fp)
{
    /* enlarge the volume if need. */
    if (fp->nDatas >= MAX_FAV)
	return -1;
    if (fp->nDatas != fp->nAllocs)
	return 0;

    fp->nAllocs += ALLOC_NUM;
    fav_type_t *tmp = (fav_type_t *)realloc(fp, sizeof(fav_type_t) * ALLOC_NUM);
    memset(tmp, 0, ALLOC_NUM);
    return 1;
}

inline int is_maxsize(void){
    return fav_number == MAX_FAV;
}

int fav_add(fav_t *fp, fav_type_t *item)
{
    if (enlarge_if_full(fp) < 0)
	return -1;
    fav_item_copy(&fp->favh[fp->nBoards], item);
    fp->nDatas++;
    return 0;
}

static void fav_move_into_folder(fav_t *from, int where, fav_t *to)
{
    fav_type_t *tmp = &from->favh[where];
    if (from != to){
	if (enlarge_if_full(to) < 0)
	    return;
    }
    fav_add(to, tmp);
    fav_remove(from, tmp);
}

/* just move, in one folder */
static void move_in_folder(fav_t *fav, int from, int to)
{
    int i;
    fav_type_t tmp;
    fav_item_copy(&tmp, &fav->favh[to]);

    if (from < to) {
	for(i = from; i < to; i++)
	    fav_item_copy_symbolic(&fav->favh[i], &fav->favh[i + 1]);
    }
    else {
	for(i = to; i > from; i--)
	    fav_item_copy_symbolic(&fav->favh[i], &fav->favh[i - 1]);
    }
    fav_item_copy(&fav->favh[to], &tmp);
}

void move_in_current_folder(int from, int to)
{
    move_in_folder(get_current_fav(), from, to);
}

void fav_move(int from, int to)
{
    fav_type_t *ft = get_current_entry();
    if (ft->type == FAVT_FOLDER)
	fav_move_into_folder(fav_old, fav_old_snum, get_current_fav());
    move_in_folder(get_current_fav(), from, to);
}

/* set the current fav_t and fav_type to old one. */
void fav_set_current_old(void)
{
    fav_old = get_current_fav();
    fav_old_snum = fav_place;
}

/* the following defines the interface of add new fav_XXX */
static int init_add(fav_type_t *ft, fav_t *fp, int type, int place)
{
    fp = get_current_fav();
    if (is_maxsize())
	return -1;
    ft = fav_allocate(type);
    set_attr(ft, FAVH_FAV, TRUE);
    fav_add(fp, ft);
    if (place >= 0)
	move_in_folder(fp, fp->nDatas - 1, place);
    return 0;
}

/* if place < 0, just put the item to the tail */
int fav_add_line(int place)
{
    fav_t *fp = NULL;
    fav_type_t *ft = NULL;
    if (init_add(ft, fp, FAVT_LINE, place) < 0)
	return -1;
    cast_line(ft)->lid = get_line_num(fp);
    line_increase(fp);
    return 0;
}

int fav_add_folder(int place)
{
    fav_t *fp = NULL;
    fav_type_t *ft = NULL;
    if (init_add(ft, fp, FAVT_FOLDER, place) < 0)
	return -1;
    cast_folder(ft)->fid = get_folder_num(fp);
    folder_increase(fp);
    return 0;
}

int fav_add_board(int bid, int place)
{
    fav_t *fp = get_current_fav();
    fav_type_t *ft = NULL;
    if (init_add(ft, fp, FAVT_BOARD, place) < 0)
	return -1;
    cast_board(ft)->bid = bid;
    board_increase(fp);
    return 0;
}
/* --- */

/* everything about the tag in fav mode.
 * I think we don't have to implement the function 'cross-folder' tag.*/
void fav_tag_current(int bool)
{
    fav_type_t *ft = get_current_entry();
    set_attr(ft, FAVH_TAG, bool);
}

static void fav_dosomething_tagged_item(fav_t *fp, int (*act)(fav_t *, fav_type_t *))
{
    int i;
    for(i = 0; i < fp->nAllocs; i++){
	if (is_set_attr(&fp->favh[i], FAVH_TAG))
	    (*act)(fp, &fp->favh[i]);
    }
}

void fav_remove_tagged_item(fav_t *fp)
{
    fav_dosomething_tagged_item(fp, fav_remove);
}

void fav_add_tagged_item(fav_t *fp)
{
    fav_dosomething_tagged_item(fp, fav_remove);
}

static void fav_dosomething_all_tagged_item(void (*act)(fav_t *))
{
    int i;
    fav_type_t *ft;
    fav_t *fp =  get_current_fav();
    for(i = 0; i < fp->nAllocs; i++){
	ft = &fp->favh[i];
	if (ft->attr & FAVT_FOLDER){
	    fav_stack_push(ft);
	    fav_dosomething_all_tagged_item(act);
	    fav_stack_pop();
	}
    }
    (*act)(get_current_fav());
}

void fav_remove_all_tagged_item(void)
{
    fav_dosomething_all_tagged_item(fav_remove_tagged_item);
}

void fav_add_all_tagged_item(void)
{
    fav_dosomething_all_tagged_item(fav_add_tagged_item);
}

inline static int remove_tag(fav_t *fp, fav_type_t  *ft)
{
    set_attr(ft, FAVH_TAG, FALSE);
    return 0;
}

inline static void remove_tags(fav_t *fp)
{
    fav_dosomething_tagged_item(fp, remove_tag);
}

void fav_remove_all_tag(void)
{
    fav_dosomething_all_tagged_item(remove_tags);
}
/* --- */

void fav_set_folder_title(char *title)
{
    fav_type_t *ft = get_current_entry();
    if (ft->type != FAVT_FOLDER)
	return;
    strlcpy(cast_folder(ft)->title, title, sizeof(cast_folder(ft)->title));
}

/* free the mem of whole fav tree */
static void fav_free_branch(fav_t *fp)
{
    int i;
    fav_type_t *ft;
    for(i = 0; i < fp->nAllocs; i++){
	ft = &fp->favh[i];
	if (ft->attr & FAVT_FOLDER)
	    fav_free_branch(cast_folder(ft)->this_folder);
	fav_remove(fp, ft);
    }
    free(fp);
    fp = NULL;
}

void fav_free(void)
{
    fav_free_branch(get_fav_root());
}
/* --- */

/* old struct */
#define BRD_UNREAD      1
#define BRD_FAV         2
#define BRD_LINE        4
#define BRD_TAG         8 
#define BRD_GRP_HEADER 16

typedef struct {
  short           bid;
  char            attr;
  time_t          lastvisit;
} fav3_board_t;

typedef struct {
    short           nDatas;
    short           nAllocs;
    char            nLines;
    fav_board_t    *b;
} fav3_t;

int fav_v3_to_v4(void)
{
    int i, fd, fdw;
    char buf[128];

    short nDatas;
    char nLines;
    fav_t fav4;
    fav3_board_t *brd;
    
    setuserfile(buf, FAV4);
    fd = open(buf, O_RDONLY);
    if (fd >= 0){
	close(fd);
	return 0;
    }
    fdw = open(buf, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fdw < 0)
	return -1;
    setuserfile(buf, FAV3);
    fd = open(buf, O_RDONLY);
    if (fd < 0)
	return -1;

    read(fd, &nDatas, sizeof(nDatas));
    read(fd, &nLines, sizeof(nLines));

    fav4.nDatas = nDatas;
    fav4.nBoards = nDatas - nLines;
    fav4.nLines = nLines;
    fav4.nFolders = 0;
    fav4.favh = (fav_type_t *)malloc(sizeof(fav_type_t) * fav4.nDatas);
    memset(fav4.favh, 0, sizeof(fav_type_t) * fav4.nDatas);

    brd = (fav3_board_t *)malloc(sizeof(fav3_board_t) * nDatas);
    read(fd, brd, sizeof(fav3_board_t) * nDatas);

    for(i = 0; i < fav4.nDatas; i++){
	fav4.favh[i].type = brd[i].attr & BRD_LINE ? FAVT_LINE : FAVT_BOARD;

	if (brd[i].attr & BRD_UNREAD)
	    fav4.favh[i].attr |= FAVH_UNREAD;
	if (brd[i].attr & BRD_FAV)
	    fav4.favh[i].attr |= FAVH_FAV;
	if (brd[i].attr & BRD_TAG)
	    fav4.favh[i].attr |= FAVH_TAG;

	fav4.favh[i].fp = (void *)malloc(get_type_size(fav4.favh[i].type));
	if (brd[i].attr & BRD_LINE){
	    fav4.favh[i].type = FAVT_LINE;
	    cast_line(&fav4.favh[i])->lid = -brd[i].bid;
	}
	else{
	    fav4.favh[i].type = FAVT_BOARD;
	    cast_board(&fav4.favh[i])->bid = brd[i].bid;
	    cast_board(&fav4.favh[i])->lastvisit = brd[i].lastvisit;
	}
    }

    write_favrec(fd, &fav4);
    fav_free_branch(&fav4);
    free(brd);
    return 0;
}
