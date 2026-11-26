/*
 * gem.c - the gem areas (精華區) kept by gem.svc (CONFIG_GEM).
 *
 * The same menus, keys and prompts as announce.c, which keeps them in the
 * files of man/ when CONFIG_GEM is off. Here nothing of a gem area is a file:
 * directories are listed by gem.svc and articles read from it (through a file
 * in tmp/ for the readers and editors that take one). Directories are still
 * named by the paths of man/ (man/boards/<c>/<board>/D1/..., man/...,
 * home/<c>/<id>/man/...), which gem_path_parse() maps to a gem area of
 * gem.svc and a path in it.
 *
 * Pasting a copied directory shares it in gem.svc (copy on write).
 */
#include "bbs.h"
#include "psb.h"
#include "gem.h"

#if IS_ENABLED(CONFIG_GEM)

// for max file size limitation here, see edit.c
#define MAX_FILE_SIZE (32768*1024)

// used to force a page refresh.
#define A_INVALID_PAGE (9999)

#define SAFE_ZINDEX_CODELEN (STRLEN-15)

/* copy temp queue operation -------------------------------------- */

typedef struct {
    char     copyfile[PATHLEN];     // a file (post, mail) or a path of man/
    char     copytitle[TTLEN + 1];
    char     copyowner[IDLEN + 2];
} CopyQueue ;

#define COPYQUEUE_COMMON_SIZE (10)

static CopyQueue *copyqueue;
static int allocated_copyqueue = 0, used_copyqueue = 0, head_copyqueue = 0;

static int copyqueue_testin(CopyQueue *pcq)
{
    int i = 0;
    for (i = head_copyqueue; i < used_copyqueue; i++)
	if (strcmp(pcq->copyfile, copyqueue[i].copyfile) == 0)
	    return 1;
    return 0;
}

static int copyqueue_locate(CopyQueue *pcq)
{
    int i = 0;
    for (i = head_copyqueue; i < used_copyqueue; i++)
	if (strcmp(pcq->copyfile, copyqueue[i].copyfile) == 0)
	    return i;
    return -1;
}

static int copyqueue_fileinqueue(const char *fn)
{
    int i = 0;
    for (i = head_copyqueue; i < used_copyqueue; i++)
	if (strcmp(fn, copyqueue[i].copyfile) == 0)
	    return 1;
    return 0;
}

static void copyqueue_reset()
{
    allocated_copyqueue = 0;
    used_copyqueue = 0;
    head_copyqueue = 0;
}

static int copyqueue_append(CopyQueue *pcq)
{
    if(copyqueue_testin(pcq))
	return 0;
    if(head_copyqueue == used_copyqueue)
    {
	// empty queue, happy happy reset
	if(allocated_copyqueue > COPYQUEUE_COMMON_SIZE)
	{
	    // let's reduce it
	    allocated_copyqueue = COPYQUEUE_COMMON_SIZE;
	    copyqueue = (CopyQueue*)realloc( copyqueue,
		      allocated_copyqueue * sizeof(CopyQueue));
	}
	head_copyqueue = used_copyqueue = 0;
    }
    used_copyqueue ++;

    if(used_copyqueue > allocated_copyqueue)
    {
	allocated_copyqueue =
	    used_copyqueue + COPYQUEUE_COMMON_SIZE; // half page
	copyqueue = (CopyQueue*) realloc (copyqueue,
		sizeof(CopyQueue) * allocated_copyqueue);
	if(!copyqueue)
	{
	    vmsg("記憶體不足，拷貝失敗");
	    // try to reset
	    copyqueue_reset();
	    free(copyqueue);
	    copyqueue = NULL;
	    return 0;
	}
    }
    memcpy(&(copyqueue[used_copyqueue-1]), pcq, sizeof(CopyQueue));
    return 1;
}

static int copyqueue_toggle(CopyQueue *pcq)
{
    int i = copyqueue_locate(pcq);
    if(i >= 0)
    {
	/* remove it */
	used_copyqueue --;
	if(head_copyqueue > used_copyqueue)
	    head_copyqueue =used_copyqueue;
	if (i < used_copyqueue)
	{
	    memcpy(copyqueue + i, copyqueue+i+1,
		    sizeof(CopyQueue) * (used_copyqueue - i));
	}
	return 0;
    } else {
	copyqueue_append(pcq);
    }
    return 1;
}

// Returns the head of the queue without removing it.
static CopyQueue *copyqueue_peekhead()
{
    if(	used_copyqueue <= 0 ||
	head_copyqueue >= used_copyqueue)
	return NULL;
    return &(copyqueue[head_copyqueue]);
}

static CopyQueue *copyqueue_gethead()
{
    CopyQueue *cq = copyqueue_peekhead();
    if (cq)
	head_copyqueue++;
    return cq;
}

static int copyqueue_querysize()
{
    if(	used_copyqueue <= 0 ||
	head_copyqueue >= used_copyqueue)
	return 0;
    return (used_copyqueue - head_copyqueue);
}

/* end copy temp queue operation ----------------------------------- */

void
a_copyitem(const char *fpath, const char *title, const char *owner, int mode)
{
    CopyQueue cq;
    static int flFirstAlert = 1;

    memset(&cq, 0, sizeof(CopyQueue));
    STRLCPY(cq.copyfile, fpath);
    STRLCPY(cq.copytitle, title);
    if (owner)
	STRLCPY(cq.copyowner, owner);

    copyqueue_toggle(&cq);
    if (mode && flFirstAlert) {
	vmsg("[注意] 提醒您複製/標記後要貼上(p)或附加(a)後才能刪除原文!");
	flFirstAlert = 0;
    }
}

/* gem.svc ---------------------------------------------------------- */

// A menu of announce.c with the gem area it is in, and the entries of the
// page loaded (gem.svc's records, whose headers are also in m.header).
typedef struct {
    menu_t m;
    char root[GEM_ROOT_SIZE];
    char sub[PATHLEN];
    gem_entry_t *ents;
} gmenu_t;

#define FHSZ            sizeof(fileheader_t)

// The entry at the cursor (loaded), or NULL.
static gem_entry_t *
cur_entry(const gmenu_t *pm)
{
    int i = pm->m.now - pm->m.page;
    if (i < 0 || i >= pm->m.header_size || pm->m.now >= pm->m.num)
	return NULL;
    return &pm->ents[i];
}

static int
cur_kind(const gmenu_t *pm)
{
    gem_entry_t *e = cur_entry(pm);
    return e ? e->kind : 0;
}

// Tells of a failed call of gem.svc.
static void
gem_fail(const char *what)
{
    if (gem_last_error_is(GEM_ERR_QUOTA))
	vmsgf("%s失敗：超過精華區的限制 (%s)", what,
	      gem_last_error() + strlen(GEM_ERR_QUOTA));
    else if (gem_last_error_is(GEM_ERR_NOT_FOUND))
	vmsgf("%s失敗：項目可能已被它人更動，請退出本層目錄後再重試。", what);
    else
	vmsgf("%s失敗：%s", what, gem_last_error());
}

// The directory of this process in tmp/ for the files of gem.svc that the
// readers, editors and forwarders take.
static const char *
gem_tmpdir(void)
{
    static char dir[PATHLEN];
    if (!dir[0]) {
	snprintf(dir, sizeof(dir), "tmp/gem.%d", (int)getpid());
	Mkdir("tmp");
	Mkdir(dir);
    }
    return dir;
}

// Writes the file at the cursor to tmp/gem.<pid>/<its name> (into fpath).
static int
gem_fetch(const gmenu_t *pm, char *fpath, size_t sz)
{
    gem_entry_t *e = cur_entry(pm);
    if (!e || e->kind != GEM_KIND_FILE)
	return -1;
    snprintf(fpath, sz, "%s/%s", gem_tmpdir(), e->header.filename);
    return gem_get_to_file(pm->root, pm->sub, pm->m.now, e->header.filename,
			   fpath);
}

static int
a_loadname(gmenu_t * pm)
{
    // Must match the rows PSB shows in a_menu_rec(): t_lines minus the
    // 3 header lines and 1 footer line.
    int rows = t_lines - 4;
    if (rows <= 0)
	rows = 1;
    if (rows != pm->m.header_size) {
	pm->m.header_size = rows;
	pm->m.header = (fileheader_t *) realloc(pm->m.header, pm->m.header_size*FHSZ);
	pm->ents = (gem_entry_t *) realloc(pm->ents, pm->m.header_size*sizeof(gem_entry_t));
	assert(pm->m.header && pm->ents);
    }

    int len = gem_list(pm->root, pm->sub, pm->m.page, pm->m.header_size,
		       pm->ents, NULL, NULL);
    // if len < 0, the directory is not valid anymore.
    if (len < 0)
	return 0;
    for (int i = 0; i < len; i++)
	memcpy(&pm->m.header[i], &pm->ents[i].header, FHSZ);
    if (len < pm->m.header_size) {
	bzero(&pm->m.header[len], FHSZ * (pm->m.header_size - len));
	bzero(&pm->ents[len], sizeof(gem_entry_t) * (pm->m.header_size - len));
    }
    return 1;
}

static void
a_timestamp(char *buf, const time4_t *time)
{
    struct tm pt;

    localtime4_r(time, &pt);
    sprintf(buf, "%02d/%02d/%02d", pt.tm_mon + 1, pt.tm_mday, (pt.tm_year + 1900) % 100);
}

static int
a_searchtitle(gmenu_t * pm, int rev)
{
    static char     search_str[40] = "";
    int             pos;

    getdata(b_lines - 1, 1, "[搜尋]關鍵字:", search_str, sizeof(search_str), DOECHO);

    if (!*search_str)
	return pm->m.now;

    rev = rev ? -1 : 1;
    pos = pm->m.now;
    do {
	pos += rev;
	if (pos == pm->m.num)
	    pos = 0;
	else if (pos < 0)
	    pos = pm->m.num - 1;
	if (pos < pm->m.page || pos >= pm->m.page + pm->m.header_size) {
	    pm->m.page = pos - pos % pm->m.header_size;

	    if (!a_loadname(pm))
		return pm->m.now;
	}
	if (mbs_strcasestr(pm->m.header[pos - pm->m.page].title, search_str))
	    return pos;
    } while (pos != pm->m.now);
    return pm->m.now;
}

enum {
    NOBODY, MANAGER, SYSOP
};

static void
a_forward(const gmenu_t *pm, int mode)
{
    fileheader_t    fhdr;
    char            fpath[PATHLEN];
    const fileheader_t *pitem = &pm->m.header[pm->m.now - pm->m.page];

    if (gem_fetch(pm, fpath, sizeof(fpath)) < 0) {
	gem_fail("轉寄");
	return;
    }
    memset(&fhdr, 0, sizeof(fhdr));
    STRLCPY(fhdr.filename, pitem->filename);
    STRLCPY(fhdr.title, pitem->title);
    const char *msg = forward_result_msg(doforward(gem_tmpdir(), &fhdr, mode));
    unlink(fpath);
    if (msg)
	outmsg(msg);
}

// Puts the entry just added (at index at) under the cursor.
static void
a_additem(gmenu_t * pm, int at)
{
    pm->m.num++;
    pm->m.now = at;
    if (pm->m.now < pm->m.page || pm->m.now >= pm->m.page + pm->m.header_size) {
	pm->m.page = pm->m.now - (pm->m.now % pm->m.header_size);
    }
}

#define ADDITEM         0
#define ADDGROUP        1

static void
a_newitem(gmenu_t * pm, int mode)
{
    const char *mesg[2] = {
	"[新增文章] 請輸入標題：",	/* ADDITEM */
	"[新增目錄] 請輸入標題：",	/* ADDGROUP */
    };

    char            fpath[PATHLEN];
    fileheader_t    item;
    gem_entry_t     out;
    int             at;

    memset(&item, 0, sizeof(item));
    switch (mode) {
    case ADDITEM:
	// Edited in tmp/, stamped there (the name is kept unless taken).
	STRLCPY(fpath, gem_tmpdir());
	if (stampfile(fpath, &item) < 0)
	    return;
	STRLCPY(item.title, "◇ ");	/* A1BA */
	break;

    case ADDGROUP:
	if (pm->m.depth >= MAX_MAN_DEPTH)
	{
	    vmsgf("抱歉，精華區目錄最多 %d 層，無法再建立子目錄！", MAX_MAN_DEPTH);
	    return;
	}
	// gem.svc names it.
	fhdr_set_date(&item, now);
	STRLCPY(item.title, "◆ ");	/* A1BB */
	break;
    }

    if (!getdata(b_lines - 1, 1, mesg[mode], item.title + MACROSTRLEN("◇ "), 55, DOECHO)) {
	if (mode == ADDITEM)
	    unlink(fpath);
	return;
    }
    STRLCPY(item.owner, cuser.userid);
    switch (mode) {
    case ADDITEM:
	{
	    int edflags = 0;
# ifdef BN_BBSMOVIE
	    if (pm && pm->m.bid &&
		strcmp(getbcache(pm->m.bid)->brdname,
			BN_BBSMOVIE) == 0)
	    {
		edflags |= EDITFLAG_UPLOAD;
		edflags |= EDITFLAG_ALLOWLARGE;
	    }
# endif // BN_BBSMOVIE
	    if (vedit2(fpath, 0, NULL, edflags) == -1) {
		unlink(fpath);
		pressanykey();
		return;
	    }
	}
	at = gem_add_file(pm->root, pm->sub, -1, &item, fpath, &out);
	if (at < 0 && gem_last_error_is(GEM_ERR_EXISTS)) {
	    item.filename[0] = '\0';
	    at = gem_add_file(pm->root, pm->sub, -1, &item, fpath, &out);
	}
	unlink(fpath);
	break;

    default:
	at = gem_add(GEM_KIND_DIR, pm->root, pm->sub, -1, &item, NULL, 0, &out);
	break;
    }
    if (at < 0) {
	gem_fail(mode == ADDITEM ? "新增文章" : "新增目錄");
	return;
    }
    a_additem(pm, at);
}

// Splits a path of man/ into its gem area, directory and name. Returns 1
// if it is the path of an entry of a gem area.
static int
gem_split_entry(const char *path, char *root, size_t rootsz,
		char *dir, size_t dirsz, char *name, size_t namesz)
{
    char sub[PATHLEN];
    if (!gem_path_parse(path, root, rootsz, sub, sizeof(sub)) || !*sub)
	return 0;
    char *slash = strrchr(sub, '/');
    if (slash) {
	*slash = '\0';
	strlcpy(dir, sub, dirsz);
	strlcpy(name, slash + 1, namesz);
    } else {
	strlcpy(dir, "", dirsz);
	strlcpy(name, sub, namesz);
    }
    return 1;
}

static void
a_pasteitem(gmenu_t * pm, int mode)
{
    char            ans[2], skipAll = 0, multiple = 0;
    int             copied = 0;
    fileheader_t    item;

    CopyQueue *cq;

    move(b_lines - 1, 0);
    if(copyqueue_querysize() <= 0)
    {
	vmsg("請先執行複製(copy)命令後再貼上(paste)");
	return;
    }
    if(mode && copyqueue_querysize() > 1)
    {
	multiple = 1;
	move(b_lines-2, 0); clrtobot();
	outs("c: 對各項目個別確認是否要貼上, z: 全部不貼，同時重設並取消全部標記\n");
	getdata(b_lines - 1, 0, TEMPFORMAT(STRLEN, "確定要貼上全部共 %d 個項目嗎 (c/z/y/N)？ ",
		copyqueue_querysize()), ans, sizeof(ans), LCECHO);
	if(ans[0] == 'y')
	    skipAll = 1;
	else if(ans[0] == 'z')
	{
	    copyqueue_reset();
	    vmsg("已重設複製記錄。");
	    return;
	}
	else if (ans[0] != 'c')
	    return;
	clear();
    }
    while (copyqueue_querysize() > 0)
    {
	char sroot[GEM_ROOT_SIZE], sdir[PATHLEN], sname[FNSIZE];
	gem_entry_t src, out;
	int is_gem, at;

	cq = copyqueue_gethead();
	if(!cq->copyfile[0])
	    continue;
	if(mode && multiple)
	{
	    scroll();
	    move(b_lines-2, 0); clrtobot();
	    prints("%d. %s\n", ++copied,cq->copytitle);

	}

	// An entry of a gem area is copied in gem.svc (a directory is shared
	// until either side changes, even into itself); a post or a mail is
	// a file, added with its content.
	is_gem = gem_split_entry(cq->copyfile, sroot, sizeof(sroot),
				 sdir, sizeof(sdir), sname, sizeof(sname));
	if (is_gem && gem_entry(sroot, sdir, -1, sname, &src, NULL, NULL, 0) < 0) {
	    gem_fail("拷貝");
	    continue;
	}
	if (!is_gem && !dashf(cq->copyfile)) {
	    copyqueue_reset();
	    vmsg("無法拷貝！");
	    return;
	}
	if (is_gem && src.kind == GEM_KIND_DIR && pm->m.depth + 1 >= MAX_MAN_PASTE_DEPTH) {
	    vmsgf("貼上後目錄層數將超過上限 (%d層)，無法貼上！", MAX_MAN_PASTE_DEPTH);
	    continue;
	}
	if (mode && !skipAll) {
	    getdata(b_lines - 1, 0, TEMPFORMAT(STRLEN, "確定要拷貝[%s]嗎(Y/N)？[N] ", cq->copytitle),
		    ans, sizeof(ans), LCECHO);
	} else
	    ans[0] = 'y';
	if (ans[0] != 'y')
	    continue;

	const char *prefix, *title;
	memset(&item, 0, sizeof(item));
	fhdr_set_date(&item, now);
	if (*cq->copyowner) {
	    // Saved from a mail (mail_save): keeps its name.
	    const char *fname = strrchr(cq->copyfile, '/');
	    STRLCPY(item.filename, fname ? fname + 1 : cq->copyfile);
	    prefix = "◎";
	} else if (is_gem && src.kind == GEM_KIND_DIR) {
	    prefix = "◆";
	} else {
	    prefix = "◇";
	}
	STRLCPY(item.owner, *cq->copyowner ? cq->copyowner : cuser.userid);
	title = mbs_nth(cq->copytitle, 1);
	snprintf(item.title, sizeof(item.title), "%s%s", prefix, title ? title : "");

	if (is_gem)
	    at = gem_copy(0, sroot, sdir, -1, sname, pm->root, pm->sub, -1,
			  &item, &out);
	else
	    at = gem_add_file(pm->root, pm->sub, -1, &item, cq->copyfile, &out);
	if (at < 0 && gem_last_error_is(GEM_ERR_EXISTS) && *cq->copyowner) {
	    item.filename[0] = '\0';
	    at = gem_add_file(pm->root, pm->sub, -1, &item, cq->copyfile, &out);
	}
	if (at < 0) {
	    gem_fail("貼上");
	    continue;
	}
	a_additem(pm, at);
	cq->copyfile[0] = '\0';
    }
}

// The content of a copied item: of an entry of a gem area or of a file, to
// free().
static char *
copied_content(const CopyQueue *cq, size_t *len)
{
    char root[GEM_ROOT_SIZE], dir[PATHLEN], name[FNSIZE];
    if (gem_split_entry(cq->copyfile, root, sizeof(root), dir, sizeof(dir),
			name, sizeof(name)))
	return gem_get(root, dir, -1, name, len);

    FILE *fp = fopen(cq->copyfile, "r");
    char *data = NULL;
    if (!fp)
	return NULL;
    struct stat st;
    if (fstat(fileno(fp), &st) == 0 && S_ISREG(st.st_mode) &&
	(data = malloc(st.st_size + 1))) {
	*len = fread(data, 1, st.st_size, fp);
	data[*len] = '\0';
    }
    fclose(fp);
    return data;
}

// Appends the copied item cq to the current item. Returns 1 if appended.
static int
a_appendcopied(const gmenu_t * pm, CopyQueue *cq, int isask)
{
    char            ans[2] = "y", sig[2] = "y";
    char            root[GEM_ROOT_SIZE], dir[PATHLEN], name[FNSIZE];
    gem_entry_t     src, *dst = cur_entry(pm);
    int             is_gem;

    is_gem = gem_split_entry(cq->copyfile, root, sizeof(root), dir,
			     sizeof(dir), name, sizeof(name));
    if (is_gem ? (gem_entry(root, dir, -1, name, &src, NULL, NULL, 0) < 0 ||
		  src.kind != GEM_KIND_FILE) :
		 !dashf(cq->copyfile)) {
	vmsg("目錄不得附加於檔案後！");
	return 0;
    }

    AUTO_STR(fname, "%s/%s", pm->m.path, pm->m.header[pm->m.now - pm->m.page].filename);

    // if same file, abort.
    if (!dst || dst->kind != GEM_KIND_FILE || strcmp(fname, cq->copyfile) == 0) {
	vmsg("檔案不得附加於此！");
	return 0;
    }

    if (dst->bytes >= MAX_FILE_SIZE)
    {
	vmsg("檔案已超過最大限制，無法再附加");
	return 0;
    }

    // Ask everything before changing the file.
    if (isask) {
	move(b_lines - 2, 0); clrtobot();
	getdata(b_lines - 2, 1, TEMPFORMAT(STRLEN, "確定要將[%s]附加於此嗎(Y/N)？[N] ", cq->copytitle),
		ans, sizeof(ans), LCECHO);
	if (ans[0] != 'y')
	    return 0;
	getdata(b_lines - 1, 1,
		"是否收錄簽名檔部份(Y/N)？[Y] ",
		sig, sizeof(sig), LCECHO);
    }

    size_t dlen = 0, slen = 0;
    char *body = gem_get(pm->root, pm->sub, pm->m.now, dst->header.filename, &dlen);
    char *in = body ? copied_content(cq, &slen) : NULL;
    if (!body || !in) {
	free(body);
	free(in);
	gem_fail("附加");
	return 0;
    }

    // The separator and the lines of the copied item (up to its signature
    // unless asked to keep it), after the current content.
    char line[80];
    memset(line, '-', 74);
    line[74] = '\0';
    size_t cap = dlen + slen + sizeof(line) + 16;
    char *out = malloc(cap);
    if (!out) {
	free(body);
	free(in);
	return 0;
    }
    size_t n = dlen;
    memcpy(out, body, dlen);
    n += snprintf(out + n, cap - n, "\n> %s <\n\n", line);
    for (char *p = in; p < in + slen; ) {
	char *nl = memchr(p, '\n', in + slen - p);
	size_t k = nl ? (size_t)(nl - p + 1) : (size_t)(in + slen - p);
	if (sig[0] == 'n' && k == 3 && strncmp(p, "--\n", 3) == 0)
	    break;
	memcpy(out + n, p, k);
	n += k;
	p += k;
    }
    int rv = gem_put(pm->root, pm->sub, pm->m.now, dst->header.filename, out, n);
    free(out);
    free(body);
    free(in);
    if (rv < 0) {
	gem_fail("附加");
	return 0;
    }
    cq->copyfile[0] = '\0';
    return 1;
}

static void
a_appenditem(const gmenu_t * pm, int isask)
{
    move(b_lines - 1, 0);
    if(copyqueue_querysize() <= 0)
    {
	vmsg("請先執行 copy 命令後再 append");
	copyqueue_reset();
	return;
    }

    // Keep the copied item if not appended when asked, e.g. answering no
    // to append it somewhere else.
    if (a_appendcopied(pm, copyqueue_peekhead(), isask) || !isask)
	copyqueue_gethead();
}

typedef struct _iter_paste_tag_param {
    gmenu_t *pm;
    boardheader_t *bh;
    int mode;
    int copied;
    int item;
} _iter_paste_tag_param;

static int
_iter_paste_tag(void *item, void *optarg) {
    char buf[PATHLEN];
    char title[TTLEN + 1] = "◇  ";
    fileheader_t *fhdr = (fileheader_t*) item;
    _iter_paste_tag_param *param = (_iter_paste_tag_param*) optarg;

    param->item ++;
    // XXX many process crashed here as fhdr.filename[0] == 0
    // let's workaround it? or trace?
    if (!fhdr->filename[0])
    {
        grayout(0, b_lines-2, GRAYOUT_DARK);
        move(b_lines-1, 0); clrtobot();
        prints("處理 #%d 項發生錯誤。 請把你剛剛進行的完整步驟貼到 "
                BN_BUGREPORT " 板。\n", param->item);
        vmsg("忽略錯誤並繼續進行。");
        return 0;
    }

    if (!FindTaggedItem(fhdr))
        return 0;

    if (TagBoard == 0)
        sethomefile(buf, cuser.userid, fhdr->filename);
    else {
        post_file_of(param->bh->brdname, fhdr, buf, sizeof(buf));
    }

    if (!dashf(buf))
        return 0;

    snprintf(title, sizeof(title), "◇ %s", fhdr->title);
    a_copyitem(buf, title, 0, 0);
    if (param->mode) {
        param->mode--;
        a_pasteitem(param->pm, 0);
    } else
        a_appenditem(param->pm, 0);

    param->copied++;
    RemoveTagItem(fhdr);
    return IsEmptyTagList();
}

static int
a_pastetagpost(gmenu_t * pm, int mode)
{
    boardheader_t  *bh = NULL;
    char            dirname[PATHLEN];
    _iter_paste_tag_param param = {0};
    param.pm = pm;
    param.mode = mode;

    if (TagBoard == 0){
	sethomedir(dirname, cuser.userid);
    }
    else{
	bh = getbcache(TagBoard);
	setbdir(dirname, bh->brdname);
        param.bh = bh;
    }

    if (IsEmptyTagList()) {
	vmsg("請先在看板或信箱用 t 標記文章");
	return 0;
    }

    // mode > 0 merges the tagged posts into one new item.
    char ans[3];
    getdata(b_lines - 1, 0, mode > 0 ?
	    "確定要將標記的文章合併成一篇貼入此目錄嗎(y/N)？[N] " :
	    "確定要將標記的文章貼入此目錄嗎(y/N)？[N] ",
	    ans, sizeof(ans), LCECHO);
    if (ans[0] != 'y')
	return 0;

    /* since we use different tag features,
     * copyqueue is not required/used. */
    copyqueue_reset();
    apply_fileheader(dirname, _iter_paste_tag, &param);
    ClearTagList();

    vmsgf("已處理 %d 篇標記文章", param.copied);
    return param.copied;
}

static void
a_moveitem(gmenu_t * pm)
{
    char            newnum[5];
    int             num;
    char            buf[PATHLEN];
    gem_entry_t    *e = cur_entry(pm);

    if (!e)
	return;
    SNPRINTF(buf, "請輸入第 %d 選項的新次序：", pm->m.now + 1);
    if (!getdata(b_lines - 1, 1, buf, newnum, sizeof(newnum), DOECHO))
	return;
    num = (newnum[0] == '$') ? A_INVALID_PAGE : atoi(newnum) - 1;
    if (num >= pm->m.num)
	num = pm->m.num - 1;
    else if (num < 0)
	num = 0;
    if (gem_reorder(pm->root, pm->sub, pm->m.now, e->header.filename, num) < 0) {
	gem_fail("移動");
	return;
    }
    pm->m.now = num;
}

// Deletes a range of entries (to the trash of the area); directories are
// skipped.
static void
a_delrange(gmenu_t * pm)
{
    char numstr[8];
    int num1, num2, num, cdeleted = 0, failed = 0;

    clear();
    vs_hdr("刪除範圍");
    getdata(2, 0, "起點: ", numstr, 7, DOECHO);
    num1 = atoi(numstr);
    if (num1 <= 0) {
	vmsg("起點有誤");
	return;
    }
    getdata(3, 0, "終點: ", numstr, 7, DOECHO);
    num2 = atoi(numstr);
    if (num2 < num1) {
	vmsg("終點有誤");
	return;
    }
    num = num2 - num1 + 1;
    if (num > 1000) {
	vmsg("請勿一次刪除超過 1000 篇。");
	return;
    }
    gem_entry_t *recs = calloc(num, sizeof(gem_entry_t));
    if (!recs || gem_list(pm->root, pm->sub, num1 - 1, num, recs, NULL, NULL) != num) {
	free(recs);
	vmsg("無法取得指定範圍的資訊，請退出後稍候再試");
	return;
    }
    mvprints(5, 0, "#%06d %-*s %s\n"
		   " . . .\n"
		   "#%06d %-*s %s\n",
		   num1, IDLEN, recs[0].header.owner, recs[0].header.title,
		   num2, IDLEN, recs[num-1].header.owner, recs[num-1].header.title);
    outs("(範圍內的子目錄會被自動跳過，請另行用小 d 刪除)\n");
    getdata(10, 0, MSG_SURE_NY, numstr, 3, LCECHO);
    if (*numstr != 'y') {
	free(recs);
	return;
    }
    outmsg("處理中,請稍後...");
    refresh();
    for (int i = 0; i < num; i++) {
	gem_entry_t *e = &recs[i];
	if (e->kind == GEM_KIND_DIR) {
	    mvprints(b_lines-1, 0, "跳過子目錄: %s\n", e->header.title);
	    doupdate();
	    continue;
	}
	if (gem_delete(pm->root, pm->sub, -1, e->header.filename,
		       cuser.userid) < 0) {
	    failed = 1;
	    break;
	}
	cdeleted++;
    }
    free(recs);
    if (failed) {
	clear();
	vs_hdr("部份刪除失敗");
	prints("\n\n已刪除了 %d 個檔案，但無法刪除其它檔案。\n", cdeleted);
	prints("%s\n", gem_last_error());
	vmsg("無法刪除。可能有其它人正在同時刪除。");
    }
    gem_list(pm->root, pm->sub, 0, 0, NULL, &pm->m.num, NULL);
}

// Deletes the entry at the cursor: to the trash of the area (gem.ctl purge
// removes it for good after some days), where it can be restored.
static void
a_delete(gmenu_t * pm)
{
    char            ans[4];
    gem_entry_t    *e = cur_entry(pm);
    fileheader_t   *fhdr = &(pm->m.header[pm->m.now - pm->m.page]);
    const char *msg_errsync = "刪除檔案失敗，請退回上層目錄後再重試一次";

    if (!e)
	return;

    if (fhdr->filename[0] == 'H' && fhdr->filename[1] == '.') {
	getdata(b_lines - 1, 1, "您確定要刪除此精華區連線嗎(Y/N)？[N] ",
		ans, sizeof(ans), LCECHO);
	if (ans[0] != 'y')
	    return;
    } else if (e->kind == GEM_KIND_INDEX) {
	getdata(b_lines - 1, 1, "您確定要刪除精華區目錄索引嗎(Y/N)？[N] ",
		ans, sizeof(ans), LCECHO);
	if (ans[0] != 'y')
	    return;
    } else if (e->kind == GEM_KIND_FILE) {
	getdata(b_lines - 1, 1, "您確定要刪除此檔案嗎(Y/N)？[N] ", ans,
		sizeof(ans), LCECHO);
	if (ans[0] != 'y')
	    return;
    } else if (e->kind == GEM_KIND_DIR) {
	getdata(b_lines - 1, 1, "您確定要刪除整個目錄嗎(Y/N)？[N] ", ans,
		sizeof(ans), LCECHO);
	if (ans[0] != 'y')
	    return;
    } else {			/* Ptt 損毀的項目 */
	getdata(b_lines - 1, 1, "您確定要刪除此損毀的項目嗎(Y/N)？[N] ",
		ans, sizeof(ans), LCECHO);
	if (ans[0] != 'y')
	    return;
    }
    if (gem_delete(pm->root, pm->sub, pm->m.now, fhdr->filename,
		   cuser.userid) < 0) {
	vmsg(msg_errsync);
	return;
    }
    pm->m.num--;
}

// Writes back the header of the entry at the cursor.
static void
a_setheader(const gmenu_t *pm, const fileheader_t *item)
{
    const fileheader_t *fhdr = &pm->m.header[pm->m.now - pm->m.page];
    if (gem_set_header(pm->root, pm->sub, pm->m.now, fhdr->filename, item,
		       NULL) < 0)
        vmsg("無法變更，可能目錄有其它板主正在修改。請退出本層目錄後再重試。");
}

static void
a_newtitle(const gmenu_t * pm)
{
    char            buf[PATHLEN];
    fileheader_t    item, *fhdr;

    fhdr = &pm->m.header[pm->m.now - pm->m.page];
    memcpy(&item, fhdr, FHSZ);
    char *title = (char *)mbs_nth(item.title, 2);
    if (!title)
	title = item.title + strlen(item.title);
    STRLCPY(buf, title);
    if (getdata_buf(b_lines - 1, 0, "   新標題: ", buf, 60, DOECHO)) {
	strlcpy(title, buf, sizeof(item.title) - (title - item.title));
	a_setheader(pm, &item);
    }
}

static void
a_hideitem(const gmenu_t * pm)
{
    fileheader_t   *item = &pm->m.header[pm->m.now - pm->m.page];
    if (item->filemode & FILE_BM) {
	item->filemode &= ~FILE_BM;
	item->filemode &= ~FILE_HIDE;
    } else if (item->filemode & FILE_HIDE)
	item->filemode |= FILE_BM;
    else
	item->filemode |= FILE_HIDE;
    a_setheader(pm, item);
}

static void
a_editsign(const gmenu_t * pm)
{
    char            buf[PATHLEN];
    fileheader_t    item;

    memcpy(&item, &pm->m.header[pm->m.now - pm->m.page], FHSZ);
    SNPRINTF(buf, "%c%c", item.title[0], item.title[1]);
    if (getdata_buf(b_lines - 1, 1, "符號", buf, 3, DOECHO)) {
	item.title[0] = buf[0] ? buf[0] : ' ';
	item.title[1] = buf[1] ? buf[1] : ' ';
	item.title[2] = ' ';
	a_setheader(pm, &item);
    }
}

static void
a_showname(const gmenu_t * pm)
{
    const fileheader_t *fhdr = &pm->m.header[pm->m.now - pm->m.page];

    move(b_lines - 1, 0);
    switch (cur_kind(pm)) {
    case GEM_KIND_FILE:
	prints("此文章名稱為 %s", fhdr->filename);
	break;
    case GEM_KIND_DIR:
	prints("此目錄名稱為 %s", fhdr->filename);
	break;
    case GEM_KIND_INDEX:
	prints("此精華區目錄索引名稱為 %s", fhdr->filename);
	break;
    default:
	outs("此項目已損毀, 建議將其刪除！");
    }
    pressanykey();
}

static int
isvisible_man(const gmenu_t * me)
{
    fileheader_t   *fhdr = &me->m.header[me->m.now - me->m.page];
    /* board friend only effact when in board reading mode */
    if (me->m.level >= MANAGER)
	return 1;
    if (fhdr->filemode & FILE_BM)
	return 0;
    if (fhdr->filemode & FILE_HIDE)
    {
	if (currstat == ANNOUNCE ||
	    !is_hidden_board_friend(me->m.bid, currutmp->uid))
	    return 0;
    }
    return 1;
}

typedef struct {
    char bReturnToRoot;		// 用來跳出 recursion
    int  z_indexes [STRLEN/2];	// each index code takes minimal 2 characters
    const char *backup_dir;    // 砍文章時要存到哪
}   a_menu_session_t;

// look up current location
#define A_WHEREAMI_PREFIX_STR	"我在哪？ "
static int
a_where_am_i(const menu_t *root, int current_idx, const char *current_title)
{
    const menu_t *p;
    int lvl, num;
    const int max_lvl = t_lines -3; // 3: vs_hdr() + pause() + where_am_i
    char zidx_buf[STRLEN-sizeof(A_WHEREAMI_PREFIX_STR)-1] = "z";
    int  zidx_len;
    char abuf[12] = "";  // INT_MAX + sign + NUL
    int  last_idx_len = 0;
    char *zidx = zidx_buf + strlen(zidx_buf);
    char bskipping;

    move(1, 0); clrtobot(); outs(A_WHEREAMI_PREFIX_STR ANSI_COLOR(1));

    // decide length of last index (printed 1-based, as below)
    SNPRINTF(abuf, "-%d", current_idx + 1);
    last_idx_len = strlen(abuf)+1;
    // calculate remaining length
    zidx_len = sizeof(zidx_buf) - strlen(zidx) - last_idx_len;

    bskipping = 0;
    // first round, quick render zidx
    for (p = root; p != NULL; p = p->next)
    {
	SNPRINTF(abuf, "-%d", p->now+1);
	if (p->next == NULL)
	{
	    // tail. always print it.
	    strlcpy(zidx, bskipping ? abuf+1 : abuf,
		    zidx_buf + sizeof(zidx_buf) - zidx);
	} else if (bskipping) {
	    // skip iteration
	    continue;
	} else {
	    // determine if there's still space to print it
	    if ((int)strlen(abuf) > zidx_len)
	    {
		// re-print from previous entry
		char *s = strrchr(zidx_buf, '-');
		assert(s);
		zidx_len += strlen(s);
		strlcpy(s, "...", zidx_len+1);  // '-%d-' enough to hold '...'
		zidx_len -= strlen(s);
		bskipping = 1;
	    } else {
		// simply print it
		strlcpy(zidx, abuf, zidx_len);
		zidx_len -= strlen(zidx);
		zidx     += strlen(zidx);
	    }
	}
    }

    outs(zidx_buf); outs(ANSI_RESET);
    move(2, 0);

    bskipping = 0;
    // second round, render text output
    for (p = root, lvl = 0, num = 0; lvl < max_lvl; p = p->next)
    {
	int onum = num;
	if (p)
	    num = p->now +1;

	if (bskipping && p)
	    continue;

	move(lvl+2, 0);
	prints("%*s", lvl, "");

	//  decide if we need to skip.
	if (++lvl == max_lvl-1 && p && p->next)
	{
	    prints("... (中間已省略) ...");
	    bskipping = 1;
	    continue;
	}

	// in the first round, print topic and no number...
	if (onum)
	    prints("%2d. ", onum);

	// for last round, p == NULL -> use current title
	prints("%s\n", p ? p->mtitle : current_title);

	// break at last round (p == NULL)
	if (!p)
	    break;
    }
    return 0;
}

static int
a_parse_zindexes(const char *s, a_menu_session_t *sess)
{
    int i = 0;

    memset(sess->z_indexes, 0, sizeof(sess->z_indexes));

    // skip leading whitespaces
    while (*s && isascii(*s) && isspace(*s)) s++;

    // determine if leading character was 'z', which means 'reset root'
    if (*s == 'z' || *s == 'Z')
	sess->z_indexes[i++] = -1;

    if (strpbrk(s, "0123456789") == NULL)
	return (sess->z_indexes[i] == 0);

    while (NULL != (s = strpbrk(s, "0123456789")) &&
	    i+1 < (int)ARRAY_SIZE(sess->z_indexes) )
    {
	sess->z_indexes[i] = atoi(s);
	// for overflow, ignore all remaining.
	if (sess->z_indexes[i] < 0)
	{
	    sess->z_indexes[i] = 0;
	    break;
	}
	// only increase index
	if (sess->z_indexes[i])
	    i++;
	while(isascii(*s) && isdigit(*s)) s++;
    }

    return 1;
}

#define MULTI_SEARCH_PROMPT "新位置 (可輸入多層數字): "
static int
a_multi_search_num(char init, a_menu_session_t *sess)
{
    char buf[STRLEN - sizeof(MULTI_SEARCH_PROMPT) -1] = "";

    buf[0] = init;
    move(b_lines, 0);
    clrtoeol();
    outs(MULTI_SEARCH_PROMPT);
    sess->z_indexes[0] = sess->z_indexes[1] = 0;
    if (vgetstr(buf, sizeof(buf), VGET_DEFAULT, buf) < 1)
	return 0;

    a_parse_zindexes(buf, sess);
    if (!sess->z_indexes[1])
	return sess->z_indexes[0];
    return 0;
}

static int
a_menu_rec(const char *maintitle, const char *path,
	int lastlevel, int lastbid,
	char *trans_buffer,
	a_menu_session_t *sess,
	const int *preselect,
	const menu_t *root, menu_t* const parent);

typedef struct {
    gmenu_t *me;
    const char *path;
    char *trans_buffer;
    a_menu_session_t *sess;
    const int **preselect;
    const menu_t *root;
    menu_t *parent;
    int *returnvalue;
    cmd_layer_t *layers;
} announce_ctx_t;

static void
announce_sync_pos(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    cx->me->m.now = ctx->curr;
    cx->me->m.page = ctx->base;
}

static int
announce_cmd_search(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    announce_sync_pos(ctx);
    cx->me->m.now = a_searchtitle(cx->me, ctx->key == '?');
    ctx->curr = cx->me->m.now;
    ctx->reload = true;
    return 0;
}

static int
announce_cmd_whereami(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    menu_t *m = &cx->me->m;
    announce_sync_pos(ctx);
    const char *title = (m->num > 0 && m->now >= m->page &&
                         m->now < m->page + m->header_size)
                        ? m->header[m->now - m->page].title : "";
    a_where_am_i(cx->root, m->now, title);
    vmsg(NULL);
    ctx->redraw = true;
    return 0;
}

static int announce_cmd_select(cmd_ctx_t *ctx);

static int
announce_cmd_num_search(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    menu_t *m = &cx->me->m;
    announce_sync_pos(ctx);
    int n = a_multi_search_num(ctx->key, cx->sess);
    ctx->reload = true;
    if (n > 0) {
        m->now = n - 1;
        ctx->curr = m->now;
    } else if (n == 0 && cx->sess->z_indexes[0] == 0) {
        // empty/invalid input
    } else {
        *cx->preselect = cx->sess->z_indexes;
        if (**cx->preselect < 0) {
            if (cx->parent) {
                cx->sess->bReturnToRoot = 1;
                *cx->returnvalue = DONOTHING;
                ctx->quit = true;
                return 0;
            }
            (*cx->preselect)++;
        }
        if (**cx->preselect > 0) {
            m->now = **cx->preselect - 1;
            if (m->now >= m->num)
                m->now = m->num > 0 ? m->num - 1 : 0;
            ctx->curr = m->now;
            if ((*cx->preselect)[1]) {
                if (m->num <= 0) {
                    *cx->preselect = NULL;
                    return 0;
                }
                if (m->now < m->page ||
                    m->now >= m->page + m->header_size) {
                    m->page = (m->now / m->header_size) * m->header_size;
                    if (!a_loadname(cx->me)) {
                        *cx->preselect = NULL;
                        return 0;
                    }
                    ctx->base = m->page;
                }
                return announce_cmd_select(ctx);
            }
        } else {
            *cx->preselect = NULL;
        }
    }
    return 0;
}

static int
announce_cmd_copy(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    menu_t *m = &cx->me->m;
    announce_sync_pos(ctx);
    if (!isvisible_man(cx->me))
        return 0;
    AUTO_STR(fname, "%s/%s", cx->path, m->header[m->now - m->page].filename);
    if (!m->level && !HasUserPerm(PERM_SYSOP) &&
        (m->bid == 0 || !is_board_moderator(m->bid)) &&
        cur_kind(cx->me) == GEM_KIND_DIR)
        vmsg("只有板主才可以拷貝目錄唷!");
    else
        a_copyitem(fname, m->header[m->now - m->page].title, 0, 1);
    ctx->reload = true;
    if (ctx->curr + 1 < ctx->total)
        ctx->curr++;
    m->now = ctx->curr;
    return 0;
}

// Shows the table of contents of the area (an entry of GEM_KIND_INDEX).
static void
a_show_index(const gmenu_t *pm)
{
    size_t len;
    char *text = gem_index(pm->root, &len);
    if (!text) {
	gem_fail("讀取精華區目錄");
	return;
    }
    more_inmemory(text, (int)len, YEA);
    free(text);
}

// Shows the table of contents of the area from anywhere in it, whether or
// not it has an entry of it (new areas have none).
static int
announce_cmd_index(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    a_show_index(cx->me);
    ctx->reload = true;
    return 0;
}

static int
announce_cmd_select(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    gmenu_t *me = cx->me;
    menu_t *m = &me->m;
    m->now = ctx->curr;
    m->page = ctx->base;
    char fname[PATHLEN];
    if (m->now < 0 || m->now < m->page ||
        m->now >= m->page + m->header_size) {
        *cx->preselect = NULL;
        return 0;
    }
    fileheader_t *fhdr = &m->header[m->now - m->page];
    const int *newselect = *cx->preselect ? (*cx->preselect + 1) : NULL;
    *cx->preselect = NULL;

    if (!isvisible_man(me))
        return 0;
    if (IS_ENABLED(CONFIG_DEBUG))
        vmsgf("%s %s/%s", me->root, me->sub, fhdr->filename);

    int kind = cur_kind(me);
    SNPRINTF(fname, "%s/%s", cx->path, fhdr->filename);
    // The table of contents (what util/mandex wrote to .index): made by
    // gem.svc.
    if (kind == GEM_KIND_INDEX) {
        a_show_index(me);
        ctx->reload = true;
        return 0;
    }
    if (kind == GEM_KIND_LINK) {
        // A symbolic link of man/ that gem.ctl resolve-links did not make
        // into what it points to: gem.svc does not follow links.
        vmsg("此項目已損毀, 建議將其刪除！");
        return 0;
    }
    if (kind == GEM_KIND_FILE) {
        // Reading through the list, and the readers of the files kept here
        // (chess, BBSLua); a_menu goes on with the list after leaving.
        static const cmd_layer_t layers[] = {
            { more_list_nav_cmds, NULL },
            { more_reader_cmds, NULL },
            { NULL, NULL }
        };
        static const more_hooks_t hooks = { .layers = layers,
                                            .title = "精華區閱讀" };
        int more_result;
        if (gem_fetch(me, fname, sizeof(fname)) < 0) {
            gem_fail("讀取");
            return 0;
        }
        while ((more_result = more_ex(fname, YEA, &hooks))) {
            if (cx->trans_buffer &&
                currstat == OSONG) {
                char ans[4];
                move(22, 0);
                clrtoeol();
                getdata(22, 1,
                        "確定要選這篇嗎?[y/N]",
                        ans, sizeof(ans), LCECHO);
                if (ans[0] == 'y') {
                    // The caller reads the file (kept in tmp/).
                    strlcpy(cx->trans_buffer, fname, PATHLEN);
                    cx->sess->bReturnToRoot = 1;
                    if (currstat == OSONG) {
                        file_appendf(FN_USSONG, "%s\n", fhdr->title);
                    }
                    *cx->returnvalue = FULLUPDATE;
                    ctx->quit = true;
                    return 0;
                }
            }
            unlink(fname);
            if (more_result == READ_PREV) {
                if (--m->now < 0) {
                    m->now = 0;
                    break;
                }
            } else if (more_result == READ_NEXT) {
                if (++m->now >= m->num) {
                    m->now = m->num - 1;
                    break;
                }
            } else
                break;
            if (m->now < m->page ||
                m->now >= m->page + m->header_size) {
                m->page = (m->now / m->header_size) * m->header_size;
                if (!a_loadname(me))
                    break;
            }
            if (!isvisible_man(me))
                break;
            fhdr = &m->header[m->now - m->page];
            if (cur_kind(me) != GEM_KIND_FILE ||
                gem_fetch(me, fname, sizeof(fname)) < 0)
                break;
        }
        unlink(fname);
    } else if (kind == GEM_KIND_DIR) {
        int rv = a_menu_rec(m->header[m->now - m->page].title, fname,
                            m->level, m->bid, cx->trans_buffer,
                            cx->sess, newselect, cx->root, m);
        if (rv == DONOTHING) {
            assert(cx->sess->bReturnToRoot);
            if (!cx->parent) {
                assert(cx->sess->z_indexes[0] == -1);
                cx->sess->bReturnToRoot = 0;
                *cx->returnvalue = FULLUPDATE;
                *cx->preselect = cx->sess->z_indexes + 1;
                if (**cx->preselect > 0)
                    m->now = **cx->preselect - 1;
            } else {
                *cx->returnvalue = DONOTHING;
            }
        } else {
            *cx->returnvalue = FULLUPDATE;
        }
        m->next = NULL;
        if (cx->sess->bReturnToRoot) {
            ctx->quit = true;
            return 0;
        }
    }
    ctx->curr = m->now;
    ctx->reload = true;
    return 0;
}

static int
announce_cmd_forward(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    announce_sync_pos(ctx);
    if (!isvisible_man(cx->me))
        return 0;
    if (HasBasicUserPerm(PERM_LOGINOK) && cur_kind(cx->me) == GEM_KIND_FILE) {
        a_forward(cx->me, ctx->key);
    } else {
        vmsg("無法轉寄此項目");
    }
    ctx->reload = true;
    return 0;
}

static int
announce_cmd_edit(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    gmenu_t *me = cx->me;
    menu_t *m = &me->m;
    char fname[PATHLEN];
    announce_sync_pos(ctx);
    if (cur_kind(me) == GEM_KIND_FILE && m->level >= MANAGER) {
        int edflags = 0;
        *quote_file = 0;
#ifdef BN_BBSMOVIE
        if (m->bid && strcmp(getbcache(m->bid)->brdname, BN_BBSMOVIE) == 0) {
            edflags |= EDITFLAG_UPLOAD;
            edflags |= EDITFLAG_ALLOWLARGE;
        }
#endif
        if (gem_fetch(me, fname, sizeof(fname)) < 0) {
            gem_fail("編輯");
            return 0;
        }
        if (vedit2(fname, NA, NULL, edflags) != -1) {
            fileheader_t *fhdr = &m->header[m->now - m->page];
            if (gem_put_file(me->root, me->sub, m->now, fhdr->filename,
                             fname) < 0) {
                gem_fail("存檔");
            } else {
                fileheader_t item;
                memcpy(&item, fhdr, FHSZ);
                STRLCPY(item.owner, cuser.userid);
                gem_set_header(me->root, me->sub, m->now, fhdr->filename,
                               &item, NULL);
            }
        }
        unlink(fname);
        ctx->reload = true;
    }
    return 0;
}

static int
announce_cmd_newitem(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    announce_sync_pos(ctx);
    a_newitem(cx->me, ADDITEM);
    ctx->reload = true;
    return 0;
}

static int
announce_cmd_newdir(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    announce_sync_pos(ctx);
    a_newitem(cx->me, ADDGROUP);
    ctx->reload = true;
    return 0;
}

static int
announce_cmd_paste(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    announce_sync_pos(ctx);
    a_pasteitem(cx->me, 1);
    ctx->reload = true;
    return 0;
}

static int
announce_cmd_append(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    announce_sync_pos(ctx);
    if (cx->me->m.num <= 0)
        return 0;
    a_appenditem(cx->me, 1);
    ctx->reload = true;
    return 0;
}

static int
announce_cmd_editsign(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    announce_sync_pos(ctx);
    a_editsign(cx->me);
    ctx->reload = true;
    return 0;
}

static int
announce_cmd_paste_tag(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    announce_sync_pos(ctx);
    a_pastetagpost(cx->me, -1);
    *cx->returnvalue = DIRCHANGED;
    ctx->reload = true;
    return 0;
}

static int
announce_cmd_append_tag(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    announce_sync_pos(ctx);
    a_pastetagpost(cx->me, 1);
    *cx->returnvalue = DIRCHANGED;
    ctx->reload = true;
    return 0;
}

static int
announce_cmd_move(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    announce_sync_pos(ctx);
    a_moveitem(cx->me);
    ctx->reload = true;
    return 0;
}

static int
announce_cmd_del(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    announce_sync_pos(ctx);
    a_delete(cx->me);
    ctx->reload = true;
    return 0;
}

static int
announce_cmd_delrange(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    announce_sync_pos(ctx);
    a_delrange(cx->me);
    ctx->reload = true;
    return 0;
}

// Opens the trash of the area: what was deleted from it, to read, copy out
// or restore (announce_trash_cmds).
static int
announce_cmd_trash(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    gmenu_t *me = cx->me;
    char path[PATHLEN];

    if (gem_is_trash(me->root))
	return 0;
    snprintf(path, sizeof(path), "trash/%s", me->root);
    a_menu("精華區回收區", path, me->m.level, me->m.bid, NULL, NULL);
    ctx->reload = true;
    return 0;
}

// Restores the entry at the cursor of the top of a trash where it was.
static int
announce_cmd_restore(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    gmenu_t *me = cx->me;
    gem_entry_t *e;
    char area[GEM_ROOT_SIZE], from[PATHLEN], name[FNSIZE], prompt[STRLEN],
	 ans[3];
    int idx;

    announce_sync_pos(ctx);
    if (!(e = cur_entry(me)) || !gem_is_trash(me->root) || me->sub[0])
	return 0;
    STRLCPY(area, me->root + strlen(GEM_TRASH_PREFIX));
    STRLCPY(name, e->header.filename);
    if ((idx = gem_trash_from(area, name, from, sizeof(from))) < 0) {
	gem_fail("讀取");
	return 0;
    }
    snprintf(prompt, sizeof(prompt),
	     "還原到 %s 第 %d 項嗎(Y/N)？[N] ",
	     *from ? from : "最上層", idx + 1);
    getdata(b_lines - 1, 1, prompt, ans, sizeof(ans), LCECHO);
    if (ans[0] != 'y')
	return 0;
    if ((idx = gem_restore(area, name, from, sizeof(from))) < 0) {
	gem_fail("還原");
	return 0;
    }
    vmsgf("已還原到 %s 第 %d 項", *from ? from : "最上層", idx + 1);
    ctx->reload = true;
    return 0;
}

static int
announce_cmd_hide(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    announce_sync_pos(ctx);
    a_hideitem(cx->me);
    ctx->reload = true;
    return 0;
}

static int
announce_cmd_title(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    announce_sync_pos(ctx);
    a_newtitle(cx->me);
    ctx->reload = true;
    return 0;
}

static int
announce_cmd_showname(cmd_ctx_t *ctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)ctx->priv;
    announce_sync_pos(ctx);
    if (cx->me->m.num <= 0)
        return 0;
    a_showname(cx->me);
    ctx->reload = true;
    return 0;
}

static const cmd_t announce_cq_bm_cmds[] = {
    { 'c', "標記/複製", NULL, announce_cmd_copy, 0, CMD_PRIO_MAX, true },
    { 'p', "貼上/取消/重設", NULL, announce_cmd_paste, 0, CMD_PRIO_MAX },
    { 'a', "附加至文章後", NULL, announce_cmd_append, 0, CMD_PRIO_MAX, true },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static const cmd_t announce_cq_user_cmds[] = {
    { 'c', "標記/複製(無貼上權限)", NULL, announce_cmd_copy, 0, CMD_PRIO_MAX, true },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static const cmd_t announce_sysop_cmds[] = {
    { 'N', "查詢檔名", "查看項目對應的實體檔案名稱與路徑", announce_cmd_showname, 0, CMD_PRIO_LOW, true },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static const cmd_t announce_bm_cmds[] = {
    { 'n', "新增文章", "在目前目錄建立新文章", announce_cmd_newitem, 0, CMD_PRIO_HIGH },
    { 'g', "新增目錄", "在目前目錄建立新子目錄", announce_cmd_newdir, 0, CMD_PRIO_HIGH },
    { 'e', "編輯檔案", "修改選取文章的內容", announce_cmd_edit, 0, CMD_PRIO_HIGH, true },
    { 'E', NULL, NULL, announce_cmd_edit, 0, CMD_PRIO_NONE, true },
    { 'T', "修改標題", "修改選取項目的標題文字", announce_cmd_title, 0, CMD_PRIO_HIGH, true },
    { 'm', "移動文章", "調整精華區項目的順序位置", announce_cmd_move, 0, CMD_PRIO_HIGH, true },
    { 'd', "刪除文章", "刪除選取的精華區項目", announce_cmd_del, 0, CMD_PRIO_HIGH, true },
    { 'D', "範圍刪除", "刪除指定編號範圍內的多個項目", announce_cmd_delrange, 0, CMD_PRIO_LOW, true },
    { 'H', "閱讀權限", "切換項目的隱藏/板主專用/公開閱讀權限", announce_cmd_hide, 0, CMD_PRIO_HIGH, true },
    { 'f', "修改符號", "修改目錄的看板/進板畫面符號", announce_cmd_editsign, 0, CMD_PRIO_LOW, true },
    { 'p', "貼上項目", "貼上已複製的項目或重設標記", announce_cmd_paste, 0, CMD_PRIO_HIGH },
    { 'a', "附加項目", "將已複製的項目附加至選取文章之後", announce_cmd_append, 0, CMD_PRIO_HIGH, true },
    { Ctrl('P'), "貼上標記", "將看板或信箱內以 t 標記的文章貼入精華區", announce_cmd_paste_tag, 0, CMD_PRIO_LOW },
    { Ctrl('A'), "附加標記", "將看板或信箱內以 t 標記的文章附加至選取後", announce_cmd_append_tag, 0, CMD_PRIO_LOW, true },
    { 'u', "回收區", "查看、還原已刪除的項目", announce_cmd_trash, 0, CMD_PRIO_LOW },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

// The top of a trash, for those who may change the area (announce_cmd_trash).
static const cmd_t announce_trash_cmds[] = {
    { 'u', "還原項目", "將選取的項目還原到原來的位置", announce_cmd_restore, 0, CMD_PRIO_HIGH, true },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static const cmd_t announce_base_cmds[] = {
    { KEY_RIGHT, "讀取資料", "進入選取的目錄或閱讀文章", announce_cmd_select, 0, CMD_PRIO_NORM, true },
    { 'r', NULL, NULL, announce_cmd_select, 0, CMD_PRIO_NONE, true },
    { KEY_ENTER, NULL, NULL, announce_cmd_select, 0, CMD_PRIO_NONE, true },
    { '/', "搜尋", "搜尋目前目錄下的標題關鍵字", announce_cmd_search, 0, CMD_PRIO_NORM, true },
    { '?', NULL, "反向搜尋目前目錄下的標題關鍵字", announce_cmd_search, 0, CMD_PRIO_NONE, true },
    { 'i', "目錄索引", "顯示整個精華區的目錄索引", announce_cmd_index, 0, CMD_PRIO_LOW },
    { 'F', "寄回信箱", "將選取的文章轉寄回電子信箱", announce_cmd_forward, PERM_LOGINOK, CMD_PRIO_HIGH, true },
    { 'c', "複製項目", "標記或複製目前選取的項目", announce_cmd_copy, 0, CMD_PRIO_HIGH, true },
    { 't', NULL, NULL, announce_cmd_copy, 0, CMD_PRIO_NONE, true },
    { Ctrl('Y'), NULL, "查詢目前所在目錄位置與路徑(我在哪裡)", announce_cmd_whereami, 0, CMD_PRIO_NONE },
    { Ctrl('W'), NULL, NULL, announce_cmd_whereami, 0, CMD_PRIO_NONE },
    { 'z', NULL, "輸入路徑(z)快速跳轉", announce_cmd_num_search, 0, CMD_PRIO_NONE },
    { 'Z', NULL, NULL, announce_cmd_num_search, 0, CMD_PRIO_NONE },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static const cmd_t announce_empty_cmds[] = {
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static const char *
announce_caption(const gmenu_t *pm)
{
    if (copyqueue_querysize() > 0)
        return "標記項目";
    if (pm->m.level)
        return "精華管理";
    return "精華列表";
}

// Kept const to stay in .rodata / .data.rel.ro and avoid dirtying .data RSS.
static const VCOL announce_coldefs[] = {
    {"", 1, 1, 100},
    {"  編號   ", 9, 9, 20},
    {"標  題", 20, 46, 100},
    {"編  選       ", 13, 13, 50},
    {"日  期    ", 10, 10, 30},
    {0},
};
#define ANNOUNCE_COLS (ARRAY_SIZE(announce_coldefs) - 1)

static int
announce_header(PSB_CTX *psbctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)psbctx->cmd.priv;
    redraw_title("精華文章", cx->me->m.mtitle);
    cmd_set_has_item(psbctx->cmd.total > 0);
    vs_cmd_bar(VS_SUB_HEADER, NULL, cx->layers);
    return 0;
}

static int
announce_empty_renderer(PSB_CTX *psbctx GCC_UNUSED)
{
    outs("  《精華區》尚在吸取天地間的日月精華中... :)");
    return 0;
}

static int
announce_renderer(int idx, PSB_CTX *psbctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)psbctx->cmd.priv;
    gmenu_t *pm = cx->me;
    int n = idx - psbctx->cmd.base;
    if (n < 0 || n >= pm->m.header_size)
        return 0;
    fileheader_t *item = &pm->m.header[n];
    char buf[PATHLEN];
    int flTagged = 0;

    SNPRINTF(buf, "%s/%s", pm->m.path, item->filename);
    if (copyqueue_querysize() > 0 && copyqueue_fileinqueue(buf))
        flTagged = 1;
    time4_t dtime = (time4_t)pm->ents[n].mtime;
    a_timestamp(buf, &dtime);

    char num[32];
    SNPRINTF(num, "%6d%c%c", idx + 1,
           (item->filemode & FILE_BM) ? 'X' :
           (item->filemode & FILE_HIDE) ? ')' : '.',
           flTagged ? 'c' : ' ');
    char date_buf[32];
    SNPRINTF(date_buf, "[%s]", buf);

    render_columns(psbctx, "", num, item->title, item->owner, date_buf);
    return 0;
}

static int
announce_loader(PSB_CTX *psbctx)
{
    announce_ctx_t *cx = (announce_ctx_t *)psbctx->cmd.priv;
    gmenu_t *me = cx->me;
    int cq = copyqueue_querysize();
    cx->layers[0].cmds = cq > 0 ? ((me->m.level && !gem_is_trash(me->root)) ?
                                   announce_cq_bm_cmds : announce_cq_user_cmds) :
                         announce_empty_cmds;
    psbctx->cmd.caption = announce_caption(me);

    if (psbctx->cmd.reload) {
        int num;
        if (gem_list(me->root, me->sub, 0, 0, NULL, &num, NULL) < 0) {
            cx->sess->bReturnToRoot = 1;
            psbctx->cmd.quit = true;
            return -1;
        }
        me->m.num = num;
        psbctx->cmd.total = me->m.num;
        psbctx->cmd.curr = me->m.now;
        return 0;
    }

    me->m.now = psbctx->cmd.curr;
    me->m.page = psbctx->cmd.base;
    if (me->m.num > 0 && !a_loadname(me)) {
        cx->sess->bReturnToRoot = 1;
        psbctx->cmd.quit = true;
        return -1;
    }
    return 0;
}

static int
a_menu_rec(const char *maintitle, const char *path,
	int lastlevel, int lastbid,
	char *trans_buffer,
	a_menu_session_t *sess,
	const int *preselect,
	// we don't change root's value (but may change root pointer)
	// we may   change parent's value (but never change parent pointer)
	const menu_t *root, menu_t* const parent)
{
    gmenu_t         me = {{0}};
    char            fname[PATHLEN];
    int             returnvalue = FULLUPDATE;

    assert(sess);

    // prevent deep resursive directories
    if (strlen(path) + FNSIZE >= PATHLEN)
    {
	// it is not save to enter such directory.
	return returnvalue;
    }
    if (!gem_path_parse(path, me.root, sizeof(me.root), me.sub, sizeof(me.sub))) {
	vmsgf("不是精華區: %s", path);
	return returnvalue;
    }

    if (trans_buffer)
	trans_buffer[0] = '\0';

    if (parent)
    {
	parent->next = &me.m;
	me.m.depth = parent->depth + 1;
    } else {
	assert(root == NULL);
	root = &me.m;
	me.m.depth = 0;
    }

    me.m.header_size = (t_lines - 4 > 0) ? t_lines - 4 : 1;
    me.m.header = (fileheader_t *) calloc(me.m.header_size, FHSZ);
    me.ents = (gem_entry_t *) calloc(me.m.header_size, sizeof(gem_entry_t));
    me.m.path = path;
    STRLCPY(me.m.mtitle, maintitle);
    if (gem_list(me.root, me.sub, 0, 0, NULL, &me.m.num, NULL) < 0) {
	vmsgf("無法讀取精華區: %s", gem_last_error());
	me.m.num = 0;
    }
    me.m.bid = lastbid;

    /* 精華區-tree 中部份結構屬於 cuser ==> BM */

    if (!(me.m.level = lastlevel)) {
	char           *ptr;

	// warning: this is only valid for me.level.
	// is_uBM should not do anything except returning test result:
	// for ex, setting user BM permission automatically.
	// such extra behavior will result in any sub-op to have PERM_BM
	// ability, which leads to entering BM board without authority.
	// Thanks to mtdas@ptt for reporting this exploit.
	if (HasBasicUserPerm(PERM_LOGINOK) &&
            !HasUserPerm(PERM_NOCITIZEN) &&
            (ptr = strrchr(me.m.mtitle, '[')))
	    me.m.level = is_uBM(ptr + 1, cuser.userid);
    }

    if (preselect && !*preselect)
	preselect = NULL;

    me.m.now = preselect ? (*preselect - 1) : 0;
    if (me.m.num <= 0) {
        me.m.num = 0;
        me.m.now = 0;
    } else {
        if (me.m.now >= me.m.num)
            me.m.now = me.m.num - 1;
        if (me.m.now < 0)
            me.m.now = 0;
    }

    // A trash is read only, but for restoring from its top.
    int is_trash = gem_is_trash(me.root);
    cmd_layer_t layers[] = {
        { announce_empty_cmds, NULL },
        { (me.m.level >= SYSOP && !is_trash) ? announce_sysop_cmds : announce_empty_cmds, NULL },
        { me.m.level < MANAGER ? announce_empty_cmds :
          !is_trash ? announce_bm_cmds :
          !me.sub[0] ? announce_trash_cmds : announce_empty_cmds, NULL },
        { announce_base_cmds, NULL },
        { bbs_global_cmds, NULL },
        { NULL, NULL }
    };
    announce_ctx_t cx = {
        .me = &me,
        .path = path,
        .trans_buffer = trans_buffer,
        .sess = sess,
        .preselect = &preselect,
        .root = root,
        .parent = parent,
        .returnvalue = &returnvalue,
        .layers = layers,
    };
    for (int i = 0; layers[i].cmds; i++) {
        if (layers[i].cmds != bbs_global_cmds)
            layers[i].priv = &cx;
    }

    PSB_CTX psbctx = {
        .cmd = {
            .curr = me.m.now,
            .total = me.m.num,
            .priv = &cx,
            .caption = announce_caption(&me),
        },
        .wrap = true,
        .header_lines = 3,
        .footer_lines = 1,
        .cols = ANNOUNCE_COLS,
        .vcols = announce_coldefs,
        .loader = announce_loader,
        .header = announce_header,
        .renderer = announce_renderer,
        .empty_renderer = announce_empty_renderer,
        .layers = layers,
    };

    while (1) {
        if (preselect && *preselect && preselect[1]) {
            const int *next_sel = preselect + 1;
            preselect = NULL;
            if (me.m.now >= 0 && me.m.now < me.m.num) {
                me.m.page = (me.m.now / me.m.header_size) * me.m.header_size;
                if (a_loadname(&me) && isvisible_man(&me) &&
                    cur_kind(&me) == GEM_KIND_DIR) {
                    SNPRINTF(fname, "%s/%s", path, me.m.header[me.m.now - me.m.page].filename);
                    int rv = a_menu_rec(me.m.header[me.m.now - me.m.page].title, fname,
                                        me.m.level, me.m.bid, trans_buffer,
                                        sess, next_sel, root, &me.m);
                    me.m.next = NULL;
                    if (rv == DONOTHING) {
                        assert(sess->bReturnToRoot);
                        if (!parent) {
                            assert(sess->z_indexes[0] == -1);
                            sess->bReturnToRoot = 0;
                            returnvalue = FULLUPDATE;
                            preselect = sess->z_indexes + 1;
                            if (*preselect > 0)
                                me.m.now = *preselect - 1;
                            continue;
                        }
                        returnvalue = DONOTHING;
                        break;
                    }
                    returnvalue = FULLUPDATE;
                    if (sess->bReturnToRoot)
                        break;
                }
            }
        }
        preselect = NULL;
        psbctx.cmd.curr = me.m.now;
        psbctx.cmd.total = me.m.num;
        psbctx.cmd.quit = false;
        psb_main(&psbctx);
        if (returnvalue == DONOTHING && !parent && sess->bReturnToRoot && sess->z_indexes[0] == -1) {
            sess->bReturnToRoot = 0;
            returnvalue = FULLUPDATE;
            preselect = sess->z_indexes + 1;
            if (*preselect > 0)
                me.m.now = *preselect - 1;
            continue;
        }
        break;
    }

    if (ZA_Waiting())
        sess->bReturnToRoot = 1;

    free(me.m.header);
    free(me.ents);
    return returnvalue;
}

int
a_menu(const char *maintitle, const char *path,
	int lastlevel, int lastbid,
	char *trans_buffer, const char *backup_dir)
{
    a_menu_session_t sess = {0};
    sess.backup_dir = backup_dir;
    return a_menu_rec(maintitle, path,
	    lastlevel, lastbid, trans_buffer,
	    &sess, NULL, NULL, NULL);
}

int
Announce(void)
{
    setutmpmode(ANNOUNCE);
    a_menu(BBSNAME "佈告欄", "man",
	   ((HasUserPerm(PERM_SYSOP) ) ? SYSOP : NOBODY),
	   0,
	   NULL, NULL);
    return 0;
}

#endif // CONFIG_GEM
