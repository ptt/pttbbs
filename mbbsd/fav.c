/* $Id$ */
#include "bbs.h"

/**
 * Structure
 * =========
 * fav 檔的前兩個 byte 是版號，接下來才是真正的 data。
 *
 * fav 的主要架構如下：
 * 
 *   fav_t - 用來裝各種 entry(fav_type_t) 的 directory
 *     進入我的最愛時，看到的東西就是根據 fav_t 生出來的。
 *     裡面紀錄者，這一個 level 中有多少個看板、目錄、分隔線。(favh)
 *     是一個 array (with pre-allocated buffer)
 * 
 *   fav_type_t - fav entry 的 base class
 *     存取時透過 type 變數來得知正確的型態。
 * 
 *   fav_board_t / fav_line_t / fav_folder_t - derived class
 *     詳細情形請參考 fav.h 中的定義。
 *     以 cast_(board|line|folder)_t 來將一個 fav_type_t 作動態轉型。
 * 
 * Policy
 * ======
 * 為了避免過度的資料搬移，當將一個 item 從我的最愛中移除時，只將他的
 * FAVH_FAV flag 移除。而沒有這個 flag 的 item 也不被視為我的最愛。
 * 
 * 我的最愛中，沒設 FAVH_FAV 的資料，將在某些時候，如寫入檔案時，呼叫
 * rebuild_fav 清除乾淨。
 * 
 * Others
 * ======
 * 站長搬移看板所用的 t ，因為不能只存在 nbrd 裡面，又不然再弄出額外的空間，
 * 所以當站長不在我的最愛按了 t ，會把這個記錄暫存在 fav 中
 * (FAVH_ADM_TAG == 1, FAVH_FAV == 0)。
 */


/* the total number of items, every level. */
static int 	fav_number;

/* definition of fav stack, the top one is in use now. */
static int	fav_stack_num = 0;
static fav_t   *fav_stack[FAV_MAXDEPTH] = {0};

static char     dirty = 0;

/* fav_tmp is for recordinge while copying, moving, etc. */
static fav_t   *fav_tmp;
//static int	fav_tmp_snum; /* the sequence number in favh in fav_t */

#if 1 // DEPRECATED
static void fav4_read_favrec(FILE *frp, fav_t *fp);
#endif

/**
 * cast_(board|line|folder) 一族用於將 base class 作轉型
 * (不檢查實際 data type)
 */
inline static fav_board_t *cast_board(fav_type_t *p){
    return (fav_board_t *)p->fp;
}

inline static fav_line_t *cast_line(fav_type_t *p){
    return (fav_line_t *)p->fp;
}

inline static fav_folder_t *cast_folder(fav_type_t *p){
    return (fav_folder_t *)p->fp;
}

/**
 * 傳回指定的 fp(dir) 中的 fp->DataTail, 第一個沒用過的位置的 index
 */
inline static int get_data_tail(fav_t *fp){
    return fp->DataTail;
}

/**
 * 傳回指定 dir 中所用的 entry 的總數 (只算真的在裡面，而不算已被移除的)
 */
inline int get_data_number(fav_t *fp){
    return fp->nBoards + fp->nLines + fp->nFolders;
}

/**
 * 傳回目前所在的 dir pointer
 */
inline fav_t *get_current_fav(void){
    if (fav_stack_num == 0)
	return NULL;
    return fav_stack[fav_stack_num - 1];
}

/**
 * 將 ft(entry) cast 成一個 dir
 */
inline fav_t *get_fav_folder(fav_type_t *ft){
    return cast_folder(ft)->this_folder;
}

inline int get_item_type(fav_type_t *ft){
    return ft->type;
}

/**
 * 將一個指定的 dir pointer 存下來，之後可用 fav_get_tmp_fav 來存用
 */
inline static void fav_set_tmp_folder(fav_t *fp){
    fav_tmp = fp;
}

inline static fav_t *fav_get_tmp_fav(void){
    return fav_tmp;
}

/**
 * 將 fp(dir) 記的數量中，扣除一單位 ft(entry)
 */
static void fav_decrease(fav_t *fp, fav_type_t *ft)
{
    dirty = 1;

    switch (get_item_type(ft)){
	case FAVT_BOARD:
	    fp->nBoards--;
	    break;
	case FAVT_LINE:
	    fp->nLines--;
	    break;
	case FAVT_FOLDER:
	    fp->nFolders--;
	    break;
    }
    fav_number--;
}

/**
 * 將 fp(dir) 記的數量中，增加一單位 ft(entry)
 */
static void fav_increase(fav_t *fp, fav_type_t *ft)
{
    dirty = 1;

    switch (get_item_type(ft)){
	case FAVT_BOARD:
	    fp->nBoards++;
	    break;
	case FAVT_LINE:
	    fp->nLines++;
	    cast_line(ft)->lid = ++fp->lineID;
	    break;
	case FAVT_FOLDER:
	    fp->nFolders++;
	    cast_folder(ft)->fid = ++fp->folderID;
	    break;
    }
    fav_number++;
    fp->DataTail++;
}

inline static int get_folder_num(fav_t *fp) {
    return fp->nFolders;
}

/**
 * get_(folder|line)_id 傳回 fp 中一個新的 folder/line id
 */
inline static int get_folder_id(fav_t *fp) {
    return fp->folderID;
}

inline static int get_line_id(fav_t *fp) {
    return fp->lineID;
}

inline static int get_line_num(fav_t *fp) {
    return fp->nLines;
}

/**
 * 設定某個 flag。
 * @bit: 目前所有 flags 有: FAVH_FAV, FAVH_TAG, FAVH_UNREAD, FAVH_ADM_TAG
 * @param bool: FALSE: unset, TRUE: set, EXCH: opposite
 */
void set_attr(fav_type_t *ft, int bit, char bool){
    if (ft == NULL)
	return;
    if (bool == EXCH)
	ft->attr ^= bit;
    else if (bool == TRUE)
	ft->attr |= bit;
    else
	ft->attr &= ~bit;
}

inline int is_set_attr(fav_type_t *ft, char bit){
    return ft->attr & bit;
}

char *get_item_title(fav_type_t *ft)
{
    switch (get_item_type(ft)){
	case FAVT_BOARD:
	    assert(0<=cast_board(ft)->bid-1 && cast_board(ft)->bid-1<MAX_BOARD);
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
	    assert(0<=cast_board(ft)->bid-1 && cast_board(ft)->bid-1<MAX_BOARD);
	    return bcache[cast_board(ft)->bid - 1].title;
	case FAVT_FOLDER:
	    return "目錄";
	case FAVT_LINE:
	    return "----";
    }
    return NULL;
}


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
    void *p;
    assert(size>0);
    p = (void *)malloc(size);
    assert(p);
    memset(p, 0, size);
    return p;
}

/**
 * allocate header(fav_type_t, base class) 跟 fav_*_t (derived class)
 * @return 新 allocate 的空間
 */
static fav_type_t *fav_item_allocate(int type)
{
    int size;
    fav_type_t *ft;

    size = get_type_size(type);
    if (!size)
	return NULL;

    ft = (fav_type_t *)fav_malloc(sizeof(fav_type_t));
    ft->fp = fav_malloc(size);
    ft->type = type;
    return ft;
}

/**
 * 只複製 fav_type_t
 */
inline static void
fav_item_copy(fav_type_t *target, const fav_type_t *source){
    target->type = source->type;
    target->attr = source->attr;
    target->fp = source->fp;
}

inline fav_t *get_fav_root(void){
    return fav_stack[0];
}

/**
 * 是否為有效的 entry
 */
inline int valid_item(fav_type_t *ft){
    return ft->attr & FAVH_FAV;
}

/**
 * 清除 fp(dir) 中無效的 entry/dir。「無效」指的是沒有 FAVH_FAV flag，所以
 * 不包含不存在的看板。
 */
static void rebuild_fav(fav_t *fp)
{
    int i, j, nData;
    fav_type_t *ft;

    fav_number -= get_data_number(fp);
    fp->lineID = fp->folderID = 0;
    fp->nLines = fp->nFolders = fp->nBoards = 0;
    nData = fp->DataTail;
    fp->DataTail = 0;

    for (i = 0, j = 0; i < nData; i++){
	if (!valid_item(&fp->favh[i]))
	    continue;

	ft = &fp->favh[i];
	switch (get_item_type(ft)){
	    case FAVT_BOARD:
	    case FAVT_LINE:
		break;
	    case FAVT_FOLDER:
		rebuild_fav(get_fav_folder(&fp->favh[i]));
		break;
	    default:
		continue;
	}
	fav_increase(fp, &fp->favh[i]);
	if (i != j)
	    fav_item_copy(&fp->favh[j], &fp->favh[i]);
	j++;
    }
    fp->DataTail = get_data_number(fp);
}

inline void fav_cleanup(void)
{
    rebuild_fav(get_fav_root());
}

/* sort the fav */
static int favcmp_by_name(const void *a, const void *b)
{
    return strcasecmp(get_item_title((fav_type_t *)a), get_item_title((fav_type_t *)b));
}

void fav_sort_by_name(void)
{
    fav_t *fp = get_current_fav();
    
    if (fp == NULL)
	return;

    dirty = 1;
    rebuild_fav(fp);
    qsort(fp->favh, get_data_number(fp), sizeof(fav_type_t), favcmp_by_name);
}

static int favcmp_by_class(const void *a, const void *b)
{
    fav_type_t *f1, *f2;
    int cmp;

    f1 = (fav_type_t *)a;
    f2 = (fav_type_t *)b;
    if (get_item_type(f1) == FAVT_FOLDER)
	return -1;
    if (get_item_type(f2) == FAVT_FOLDER)
	return 1;
    if (get_item_type(f1) == FAVT_LINE)
	return 1;
    if (get_item_type(f2) == FAVT_LINE)
	return -1;

    cmp = strncasecmp(get_item_class(f1), get_item_class(f2), 4);
    if (cmp)
	return cmp;
    return strcasecmp(get_item_title(f1), get_item_title(f2));
}

void fav_sort_by_class(void)
{
    fav_t *fp = get_current_fav();
    
    if (fp == NULL)
	return;

    dirty = 1;
    rebuild_fav(fp);
    qsort(fp->favh, get_data_number(fp), sizeof(fav_type_t), favcmp_by_class);
}

/**
 * The following is the movement operations in the user interface.
 */

/**
 * 目錄層數是否達到最大值 FAV_MAXDEPTH
 */
inline int fav_stack_full(void){
    return fav_stack_num >= FAV_MAXDEPTH;
}

inline static int fav_stack_push_fav(fav_t *fp){
    if (fav_stack_full())
	return -1;
    fav_stack[fav_stack_num++] = fp;
    return 0;
}

inline static int fav_stack_push(fav_type_t *ft){
//    if (ft->type != FAVT_FOLDER)
//	return -1;
    return fav_stack_push_fav(get_fav_folder(ft));
}

inline static void fav_stack_pop(void){
    fav_stack[--fav_stack_num] = NULL;
}

void fav_folder_in(int fid)
{
    fav_type_t *tmp = getfolder(fid);
    if (get_item_type(tmp) == FAVT_FOLDER){
	fav_stack_push(tmp);
    }
}

void fav_folder_out(void)
{
    fav_stack_pop();
}

static void read_favrec(FILE *frp, fav_t *fp)
{
    int i;
    fav_type_t *ft;

    fread(&fp->nBoards, sizeof(fp->nBoards), 1, frp);
    fread(&fp->nLines, sizeof(fp->nLines), 1, frp);
    fread(&fp->nFolders, sizeof(fp->nFolders), 1, frp);
    fp->DataTail = get_data_number(fp);
    fp->nAllocs = fp->DataTail + FAV_PRE_ALLOC;
    fp->lineID = fp->folderID = 0;
    fp->favh = (fav_type_t *)fav_malloc(sizeof(fav_type_t) * fp->nAllocs);
    fav_number += get_data_number(fp);

    for(i = 0; i < fp->DataTail; i++){
	ft = &fp->favh[i];
	fread(&ft->type, sizeof(ft->type), 1, frp);
	fread(&ft->attr, sizeof(ft->attr), 1, frp);
	ft->fp = (void *)fav_malloc(get_type_size(ft->type));

	switch (ft->type) {
	    case FAVT_FOLDER:
		fread(&cast_folder(ft)->fid, sizeof(char), 1, frp);
		fread(&cast_folder(ft)->title, BTLEN + 1, 1, frp);
		break;
	    case FAVT_BOARD:
	    case FAVT_LINE:
		fread(ft->fp, get_type_size(ft->type), 1, frp);
		break;
	}
    }

    for(i = 0; i < fp->DataTail; i++){
	ft = &fp->favh[i];
	switch (ft->type) {
	    case FAVT_FOLDER: {
		fav_t *p = (fav_t *)fav_malloc(sizeof(fav_t));
		read_favrec(frp, p);
		cast_folder(ft)->this_folder = p;
		cast_folder(ft)->fid = ++(fp->folderID);
	    	break;
	    }
	    case FAVT_LINE:
		cast_line(ft)->lid = ++(fp->lineID);
		break;
	}
    }
}

/**
 * 從記錄檔中 load 出我的最愛。
 */
int fav_load(void)
{
    FILE *frp;
    char buf[PATHLEN];
    unsigned short version;
    fav_t *fp;
    if (fav_stack_num > 0)
	return -1;
    setuserfile(buf, FAV);

    if (!dashf(buf)) {
#if 1 // DEPRECATED
	char old[PATHLEN];
	setuserfile(old, FAV4);
	if (dashf(old)) {
	    if ((frp = fopen(old, "r")) == NULL)
		return -1;
	    fp = (fav_t *)fav_malloc(sizeof(fav_t));
	    fav_number = 0;
	    fav4_read_favrec(frp, fp);
	    fav_stack_push_fav(fp);
	    fclose(frp);

    	    fav_save();
	    setuserfile(old, FAV ".bak");
	    Copy(buf, old);
	}
	else
#endif
	{
	    fp = (fav_t *)fav_malloc(sizeof(fav_t));
	    fav_stack_push_fav(fp);
	}
	return 0;
    }

    if ((frp = fopen(buf, "r")) == NULL)
	return -1;
    fp = (fav_t *)fav_malloc(sizeof(fav_t));
    fav_number = 0;
    fread(&version, sizeof(version), 1, frp);
    // if (version != FAV_VERSION) { ... }
    read_favrec(frp, fp);
    fav_stack_push_fav(fp);
    fclose(frp);
    return 0;
}

/* write to the rec file */
static void write_favrec(FILE *fwp, fav_t *fp)
{
    int i;
    fav_type_t *ft;

    if (fp == NULL)
	return;

    fwrite(&fp->nBoards, sizeof(fp->nBoards), 1, fwp);
    fwrite(&fp->nLines, sizeof(fp->nLines), 1, fwp);
    fwrite(&fp->nFolders, sizeof(fp->nFolders), 1, fwp);
    fp->DataTail = get_data_number(fp);
    
    for(i = 0; i < fp->DataTail; i++){
	ft = &fp->favh[i];
	fwrite(&ft->type, sizeof(ft->type), 1, fwp);
	fwrite(&ft->attr, sizeof(ft->attr), 1, fwp);

	switch (ft->type) {
	    case FAVT_FOLDER:
		fwrite(&cast_folder(ft)->fid, sizeof(char), 1, fwp);
		fwrite(&cast_folder(ft)->title, BTLEN + 1, 1, fwp);
		break;
	    case FAVT_BOARD:
	    case FAVT_LINE:
		fwrite(ft->fp, get_type_size(ft->type), 1, fwp);
		break;
	}
    }
    
    for(i = 0; i < fp->DataTail; i++){
	if (fp->favh[i].type == FAVT_FOLDER)
	    write_favrec(fwp, get_fav_folder(&fp->favh[i]));
    }
}

/**
 * 把記錄檔 save 進我的最愛。
 * @note fav_cleanup() 會先被呼叫。
 */
int fav_save(void)
{
    FILE *fwp;
    char buf[PATHLEN], buf2[PATHLEN];
    unsigned short version = FAV_VERSION;
    fav_t *fp = get_fav_root();
    if (fp == NULL)
	return -1;

    fav_cleanup();
    if (!dirty)
	return 0;

    setuserfile(buf2, FAV);
    snprintf(buf, sizeof(buf), "%s.tmp.%x",buf2, getpid());
    fwp = fopen(buf, "w");
    if(fwp == NULL)
	return -1;
    fwrite(&version, sizeof(version), 1, fwp);
    write_favrec(fwp, fp);

    fflush(fwp);
    if (!ferror(fwp)) {
	fclose(fwp);
	Rename(buf, buf2);
    }
    else
	fclose(fwp);

    return 0;
}

/**
 * remove ft (設為 invalid，實際上會等到 save 時才清除)
 */
static inline void fav_free_item(fav_type_t *ft)
{
    set_attr(ft, 0xFFFF, FALSE);
}

/**
 * delete ft from fp
 */
static int fav_remove(fav_t *fp, fav_type_t *ft)
{
    if (fp == NULL || ft == NULL)
	return -1;
    fav_free_item(ft);
    fav_decrease(fp, ft);
    return 0;
}

/**
 * free the mem of fp recursively
 */
static void fav_free_branch(fav_t *fp)
{
    int i;
    fav_type_t *ft;
    if (fp == NULL)
	return;
    for(i = 0; i < fp->DataTail; i++){
	ft = &fp->favh[i];
	switch(get_item_type(ft)){
	    case FAVT_FOLDER:
		fav_free_branch(cast_folder(ft)->this_folder);
		break;
	    case FAVT_BOARD:
	    case FAVT_LINE:
		if (ft->fp)
		    free(ft->fp);
		break;
	}
    }
    free(fp->favh);
    free(fp);
    fp = NULL;
}

/**
 * free the mem of the whole fav tree recursively
 */
void fav_free(void)
{
    fav_free_branch(get_fav_root());

    /* reset the stack */
    fav_stack_num = 0;
}

/**
 * 從目前的 dir 中找出特定類別 (type)、id 為 id 的 entry。
 * 找不到傳回 NULL
 */
static fav_type_t *get_fav_item(int id, int type)
{
    int i;
    fav_type_t *ft;
    fav_t *fp = get_current_fav();

    if (fp == NULL)
	return NULL;

    for(i = 0; i < fp->DataTail; i++){
	ft = &fp->favh[i];
	if (!valid_item(ft) || get_item_type(ft) != type)
	    continue;
	if (fav_getid(ft) == id)
	    return ft;
    }
    return NULL;
}

/**
 * 從目前的 dir 中 remove 特定類別 (type)、id 為 id 的 entry。
 */
void fav_remove_item(int id, char type)
{
    fav_remove(get_current_fav(), get_fav_item(id, type));
}

/**
 * get*(bid) 傳回目前的 dir 中該類別 id == bid 的 entry。
 */
fav_type_t *getadmtag(int bid)
{
    int i;
    fav_t *fp = get_fav_root();
    fav_type_t *ft;
    assert(0<=bid-1 && bid-1<MAX_BOARD);
    for (i = 0; i < fp->DataTail; i++) {
	ft = &fp->favh[i];
	if (get_item_type(ft) == FAVT_BOARD && cast_board(ft)->bid == bid && is_set_attr(ft, FAVH_ADM_TAG))
	    return ft;
    }
    return NULL;
}

fav_type_t *getboard(int bid)
{
    assert(0<=bid-1 && bid-1<MAX_BOARD);
    return get_fav_item(bid, FAVT_BOARD);
}

fav_type_t *getfolder(int fid)
{
    return get_fav_item(fid, FAVT_FOLDER);
}

char *get_folder_title(int fid)
{
    return get_item_title(get_fav_item(fid, FAVT_FOLDER));
}


char getbrdattr(int bid)
{
    fav_type_t *fb = getboard(bid);
    if (!fb)
	return 0;
    return fb->attr;
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

inline static int is_maxsize(void){
    return fav_number >= MAX_FAV;
}

/**
 * 每次一個 dir 滿時，這個 function 會將 buffer 大小調大 FAV_PRE_ALLOC 個，
 * 直到上限為止。
 */
static int enlarge_if_full(fav_t *fp)
{
    fav_type_t * p;
    /* enlarge the volume if need. */
    if (is_maxsize())
	return -1;
    if (fp->DataTail < fp->nAllocs)
	return 1;

    /* realloc and clean the tail */
    p = (fav_type_t *)realloc(fp->favh, sizeof(fav_type_t) * (fp->nAllocs + FAV_PRE_ALLOC));
    if( p == NULL )
	return -1;

    fp->favh = p;
    memset(fp->favh + fp->nAllocs, 0, sizeof(fav_type_t) * FAV_PRE_ALLOC);
    fp->nAllocs += FAV_PRE_ALLOC;
    return 0;
}

/**
 * 將一個新的 entry item 加入 fp 中
 */
static int fav_add(fav_t *fp, fav_type_t *item)
{
    if (enlarge_if_full(fp) < 0)
	return -1;
    fav_item_copy(&fp->favh[fp->DataTail], item);
    fav_increase(fp, item);
    return 0;
}

static void move_in_folder(fav_t *fav, int src, int dst)
{
    int i, count;
    fav_type_t tmp;

    if (fav == NULL)
	return;
    count = get_data_number(fav);
    if (src >= fav->DataTail)
	src = count;
    if (dst >= fav->DataTail)
	dst = count;
    if (src == dst)
	return;

    dirty = 1;

    /* Find real locations of src and dst in fav->favh[] */
    for(count = i = 0; count <= src && i < fav->DataTail; i++)
	if (valid_item(&fav->favh[i]))
	    count++;
    if (count != src + 1) return;
    src = i - 1;
    for(count = i = 0; count <= dst && i < fav->DataTail; i++)
	if (valid_item(&fav->favh[i]))
	    count++;
    if (count != dst + 1) return;
    dst = i - 1;

    fav_item_copy(&tmp, &fav->favh[src]);

    if (src < dst) {
	for(i = src; i < dst; i++)
	    fav_item_copy(&fav->favh[i], &fav->favh[i + 1]);
    }
    else { // dst < src
	for(i = src; i > dst; i--)
	    fav_item_copy(&fav->favh[i], &fav->favh[i - 1]);
    }
    fav_item_copy(&fav->favh[dst], &tmp);
}

/**
 * 將目前目錄中第 src 個 entry 移到 dst。
 * @note src/dst 是 user 實際上看到的位置，也就是不包含 invalid entry。
 */
void move_in_current_folder(int from, int to)
{
    move_in_folder(get_current_fav(), from, to);
}

/**
 * the following defines the interface of add new fav_XXX
 */

/**
 * allocate 一個 folder entry
 */
inline static fav_t *alloc_folder_item(void){
    fav_t *fp = (fav_t *)fav_malloc(sizeof(fav_t));
    fp->nAllocs = FAV_PRE_ALLOC;
    fp->favh = (fav_type_t *)fav_malloc(sizeof(fav_type_t) * FAV_PRE_ALLOC);
    return fp;
}

static fav_type_t *init_add(fav_t *fp, int type)
{
    fav_type_t *ft;
    if (is_maxsize())
	return NULL;
    if ((ft = fav_item_allocate(type)) == NULL)
	return NULL;
    set_attr(ft, FAVH_FAV, TRUE);
    fav_add(fp, ft);
    return ft;
}

/**
 * 新增一分隔線
 * @return 加入的 entry 指標
 */
fav_type_t *fav_add_line(void)
{
    fav_t *fp = get_current_fav();
    if (fp == NULL || get_line_num(fp) >= MAX_LINE)
	return NULL;
    return init_add(fp, FAVT_LINE);
}

/**
 * 新增一目錄
 * @return 加入的 entry 指標
 */
fav_type_t *fav_add_folder(void)
{
    fav_t *fp = get_current_fav();
    fav_type_t *ft;
    if (fav_stack_full())
	return NULL;
    if (fp == NULL || get_folder_num(fp) >= MAX_FOLDER)
	return NULL;
    ft = init_add(fp, FAVT_FOLDER);
    if (ft == NULL)
	return NULL;
    cast_folder(ft)->this_folder = alloc_folder_item();
    return ft;
}

/**
 * 將指定看板加入目前的目錄。
 * @return 加入的 entry 指標
 * @note 不允許同一個板被加入兩次
 */
fav_type_t *fav_add_board(int bid)
{
    fav_t *fp = get_current_fav();
    fav_type_t *ft = getboard(bid); 

    if (fp == NULL)
	return NULL;
    if (ft != NULL)
	return ft;

    ft = init_add(fp, FAVT_BOARD);

    if (ft == NULL)
	return NULL;
    cast_board(ft)->bid = bid;
    return ft;
}

/**
 * for administrator to move/administrate board
 */
fav_type_t *fav_add_admtag(int bid)
{   
    fav_t *fp = get_fav_root();
    fav_type_t *ft;
    if (is_maxsize())
	return NULL;
    ft = fav_item_allocate(FAVT_BOARD);
    if (ft == NULL)
	return NULL; 
    // turn on  FAVH_ADM_TAG
    set_attr(ft, FAVH_ADM_TAG, TRUE);
    fav_add(fp, ft);
    cast_board(ft)->bid = bid;
    return ft; 
}   


/* everything about the tag in fav mode.
 * I think we don't have to implement the function 'cross-folder' tag.*/

/**
 * 將目前目錄下，由 type & id 指定的 entry 標上/取消 tag
 * @param bool 同 set_attr
 * @note 若同一個目錄不幸有同樣的東西，只有第一個會作用。
 */
void fav_tag(int id, char type, char bool) {
    fav_type_t *ft = get_fav_item(id, type);
    if (ft != NULL)
	set_attr(ft, FAVH_TAG, bool);
}

static void fav_dosomething_tagged_item(fav_t *fp, int (*act)(fav_t *, fav_type_t *))
{
    int i;
    for(i = 0; i < fp->DataTail; i++){
	if (valid_item(&fp->favh[i]) && is_set_attr(&fp->favh[i], FAVH_TAG))
	    if ((*act)(fp, &fp->favh[i]) < 0)
		break;
    }
}

static int remove_tagged_item(fav_t *fp, fav_type_t *ft)
{
    // do not remove the folder if it's on the stack
    if (get_item_type(ft) == FAVT_FOLDER) {
	int i;
	for(i = 0; i < FAV_MAXDEPTH && fav_stack[i] != NULL; i++) {
	    if (fav_stack[i] == get_fav_folder(ft)){
		set_attr(ft, FAVH_TAG, FALSE);
		return 0;
	    }
	}
    }
    return fav_remove(fp, ft);
}

inline static int fav_remove_tagged_item(fav_t *fp){
    fav_dosomething_tagged_item(fp, remove_tagged_item);
    return 0;
}

/* add an item into a fav_t.
 * here we must give the line and foler a new id to prevent an old one is exist.
 */
static int add_and_remove_tag(fav_t *fp, fav_type_t *ft)
{
    fav_type_t *tmp;
    // do not remove the folder if it's on the stack
    if (get_item_type(ft) == FAVT_FOLDER) {
	int i;
	for(i = 0; i < FAV_MAXDEPTH && fav_stack[i] != NULL; i++) {
	    if (fav_stack[i] == get_fav_folder(ft)){
		set_attr(ft, FAVH_TAG, FALSE);
		return 0;
	    }
	}
    }
    tmp = fav_malloc(sizeof(fav_type_t));
    fav_item_copy(tmp, ft);
    /* since fav_item_copy is symbolic link, we disable removed link to
     * prevent double-free when doing fav_clean(). */
    ft->fp = NULL;
    set_attr(tmp, FAVH_TAG, FALSE);

    if (fav_add(fav_get_tmp_fav(), tmp) < 0)
	return -1;
    fav_remove(fp, ft);
    return 0;
}

inline static int fav_add_tagged_item(fav_t *fp){
    if (fp == fav_get_tmp_fav())
	return -1;
    fav_dosomething_tagged_item(fp, add_and_remove_tag);
    return 0;
}

static void fav_do_recursively(fav_t *fp, int (*act)(fav_t *))
{
    int i;
    fav_type_t *ft;
    for(i = 0; i < fp->DataTail; i++){
	ft = &fp->favh[i];
	if (!valid_item(ft))
	    continue;
	if (get_item_type(ft) == FAVT_FOLDER && get_fav_folder(ft) != NULL){
	    fav_do_recursively(get_fav_folder(ft), act);
	}
    }
    (*act)(fp);
}

static void fav_dosomething_all_tagged_item(int (*act)(fav_t *))
{
    fav_do_recursively(get_fav_root(), act);
}

/**
 * fav_*_all_tagged_item 在整個我的最愛上對已標上 tag 的 entry 做某件事。
 */
void fav_remove_all_tagged_item(void)
{
    fav_dosomething_all_tagged_item(fav_remove_tagged_item);
}

void fav_add_all_tagged_item(void)
{
    fav_set_tmp_folder(get_current_fav());
    fav_dosomething_all_tagged_item(fav_add_tagged_item);
}

inline static int remove_tag(fav_t *fp, fav_type_t  *ft)
{
    set_attr(ft, FAVH_TAG, FALSE);
    return 0;
}

inline static int remove_tags(fav_t *fp)
{
    fav_dosomething_tagged_item(fp, remove_tag);
    return 0;
}

/**
 * 移除我的最愛所有的 tags
 */
void fav_remove_all_tag(void)
{
    fav_dosomething_all_tagged_item(remove_tags);
}

/**
 * 設定 folder 的中文名稱
 */
void fav_set_folder_title(fav_type_t *ft, char *title)
{
    if (get_item_type(ft) != FAVT_FOLDER)
	return;
    dirty = 1;
    strlcpy(cast_folder(ft)->title, title, sizeof(cast_folder(ft)->title));
}

#define BRD_OLD 0
#define BRD_NEW 1
#define BRD_END 2
/**
 * 如果 user 開啟 FAVNEW_FLAG 的功能:
 *  mode == 1: update 看板，並將新看板加入我的最愛。
 *  mode == 0: update 資訊但不加入。
 *
 *  @return 加入的看板數
 *  PS. count 就讓它數完，才不會在下一次 login 又從一半開始數。
 */
int updatenewfav(int mode)
{
    /* mode: 0: don't write to fav  1: write to fav */
    int i, fd, brdnum;
    int count = 0;
    char fname[80], *brd;

    if(!(cuser.uflag2 & FAVNEW_FLAG))
	return 0;

    setuserfile(fname, FAVNB);

    if( (fd = open(fname, O_RDWR | O_CREAT, 0600)) != -1 ){

	assert(numboards>=0);
	brdnum = numboards; /* avoid race */

	if ((brd = (char *)malloc((brdnum + 1) * sizeof(char))) == NULL)
	    return -1;
	memset(brd, 0, (brdnum + 1) * sizeof(char));

	i = read(fd, brd, brdnum * sizeof(char));
	if (i < 0) {
	    free(brd);
	    close(fd);
	    vmsg("favorite subscription error");
	    return -1;
	}

	brd[i] = BRD_END;
	
	for(i = 0; i < brdnum && brd[i] != BRD_END; i++){
	    if(brd[i] == BRD_NEW){
		/* check the permission if the board exsits */
		if(bcache[i].brdname[0] && HasBoardPerm(&bcache[i])){
		    if(mode && !(bcache[i].brdattr & BRD_SYMBOLIC)) {
			fav_add_board(i + 1);
			count++;
		    }
		    brd[i] = BRD_OLD;
		}
	    }
	    else{
		if(!bcache[i].brdname[0])
		    brd[i] = BRD_NEW;
	    }
	}

	if( i < brdnum) { // the board number may change
	    for(; i < brdnum; ++i){
		if(bcache[i].brdname[0] && HasBoardPerm(&bcache[i])){
		    if(mode && !(bcache[i].brdattr & BRD_SYMBOLIC)) {
			fav_add_board(i + 1);
			count++;
		    }
		    brd[i] = BRD_OLD;
		}
		else
		    brd[i] = BRD_NEW;
	    }
	}

	brd[i] = BRD_END;

	lseek(fd, 0, SEEK_SET);
	write(fd, brd, (brdnum + 1) * sizeof(char));

	free(brd);
	close(fd);
    }

    return count;
}

void subscribe_newfav(void)
{
    updatenewfav(0);
}

#if 1 // DEPRECATED
typedef struct {
    char            fid;
    char            title[BTLEN + 1];
    int             this_folder;
} fav_folder4_t;

typedef struct {
    int             bid;
    time4_t         lastvisit;		/* UNUSED */
    char	    attr;
} fav4_board_t;

static int fav4_get_type_size(int type)
{
    switch (type){
	case FAVT_BOARD:
	    return sizeof(fav4_board_t);
	case FAVT_FOLDER:
	    return sizeof(fav_folder_t);
	case FAVT_LINE:
	    return sizeof(fav_line_t);
    }
    return 0;
}

static void fav4_read_favrec(FILE *frp, fav_t *fp)
{
    int i;
    fav_type_t *ft;

    fread(&fp->nBoards, sizeof(fp->nBoards), 1, frp);
    fread(&fp->nLines, sizeof(fp->nLines), 1, frp);
    fread(&fp->nFolders, sizeof(fp->nFolders), 1, frp);
    fp->DataTail = get_data_number(fp);
    fp->nAllocs = fp->DataTail + FAV_PRE_ALLOC;
    fp->lineID = fp->folderID = 0;
    fp->favh = (fav_type_t *)fav_malloc(sizeof(fav_type_t) * fp->nAllocs);
    fav_number += get_data_number(fp);

    for(i = 0; i < fp->DataTail; i++){
	ft = &fp->favh[i];
	fread(&ft->type, sizeof(ft->type), 1, frp);
	fread(&ft->attr, sizeof(ft->attr), 1, frp);
	ft->fp = (void *)fav_malloc(fav4_get_type_size(ft->type));

	/* TODO A pointer has different size between 32 and 64-bit arch.
	 * But the pointer in fav_folder_t is irrelevant here. 
	 * In order not to touch the current .fav4, fav_folder4_t is used
	 * here.  It should be FIXED in the next version. */
	switch (ft->type) {
	    case FAVT_FOLDER:
		fread(ft->fp, sizeof(fav_folder4_t), 1, frp);
		break;
	    case FAVT_BOARD:
	    case FAVT_LINE:
		fread(ft->fp, fav4_get_type_size(ft->type), 1, frp);
		break;
	}
    }

    for(i = 0; i < fp->DataTail; i++){
	ft = &fp->favh[i];
	switch (ft->type) {
	    case FAVT_FOLDER: {
		fav_t *p = (fav_t *)fav_malloc(sizeof(fav_t));
		fav4_read_favrec(frp, p);
		cast_folder(ft)->this_folder = p;
		cast_folder(ft)->fid = ++(fp->folderID);
	    	break;
	    }
	    case FAVT_LINE:
		cast_line(ft)->lid = ++(fp->lineID);
		break;
	}
    }
}
#endif
