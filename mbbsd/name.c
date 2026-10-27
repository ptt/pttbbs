#include "bbs.h"

#define COMPLETE_MORE_MSG   "按空白鍵可列出更多項目"
#define COMPLETE_LIST_TITLE "相關資訊一覽表"

void
ToggleVector(struct Vector *list, int *recipient, const char *listfile, const char *msg)
{
    FILE           *fp;
    char            genbuf[STRLEN];

    if ((fp = fopen(listfile, "r"))) {
	while (fgets(genbuf, sizeof(genbuf), fp)) {
	    char *space = strpbrk(genbuf, str_space);
	    if (space) *space = '\0';
	    if (!genbuf[0])
		continue;
	    if (Vector_search(list, genbuf) < 0) {
		if (Vector_length(list) < MAX_MULTILIST) {
		    Vector_add(list, genbuf);
		    (*recipient)++;
		}
	    } else {
		Vector_remove(list, genbuf);
		(*recipient)--;
	    }
	}
	fclose(fp);
	ShowVector(list, 3, 0, msg, 0);
    }
}

int
ShowVector(struct Vector *list, int row, int col, const char * msg, int idx)
{
    int i, len;

    move(row, col);
    clrtobot();

    if (msg) {
	outs(msg);
	row++;
    }
    col = 0;

    int max_rows = b_lines - 1 - row;
    if (max_rows <= 0) max_rows = 1;

    len = Vector_MaxLen(list, idx, max_rows);
    while (len + col < t_columns) {
	for (i = row; idx < Vector_length(list) && (i < row + max_rows); i++) {
	    move(i, col);
	    outs(Vector_get(list, idx));
	    idx++;
	}
	col += len + 2;
	if (idx == Vector_length(list)) {
	    idx = 0;
	    break;
	}
	len = Vector_MaxLen(list, idx, max_rows);
    }

    return idx;
}

#define MAX_COMPLETE_CAP    128

static int
get_complete_capacity(int start_y, int item_len)
{
    int list_top = start_y + 1;
    int max_rows = b_lines - 1 - list_top;
    if (max_rows <= 0)
	max_rows = 1;
    int col_width = item_len + 2;
    if (col_width <= 0)
	col_width = 1;
    int max_cols = (t_columns - 1) / col_width;
    if (max_cols <= 0)
	max_cols = 1;
    int cap = max_rows * max_cols;
    if (cap > MAX_COMPLETE_CAP)
	cap = MAX_COMPLETE_CAP;
    if (cap <= 0)
	cap = 1;
    return cap;
}

static int
nc_cmp_id(const void *a, const void *b)
{
    return strcasecmp((const char *)a, (const char *)b);
}

static int
compute_lcp(const char *s1, const char *s2)
{
    int i = 0;
    while (s1[i] && s2[i] &&
	   chartoupper((unsigned char)s1[i]) == chartoupper((unsigned char)s2[i]))
	i++;
    return i;
}

/* Completion Provider Interface */

typedef struct {
    bool (*has_match)(const char *prefix, void *ctx);
    int (*get_candidates)(const char *prefix, void *ctx, int max_items,
			  char *dst_buf, bool *out_truncated,
			  int *out_selected_idx, char *out_selected_name,
			  char *first_match, char *last_match);
} complete_ops_t;

/* Vector Provider */

typedef struct {
    const struct Vector *base;
    bool is_usercomplete;
} vec_ctx_t;

static bool
vec_has_match(const char *prefix, void *ctx)
{
    vec_ctx_t *v = (vec_ctx_t *)ctx;
    int len = strlen(prefix);
    if (len == 0)
	return true;
    int first_uc = chartoupper((unsigned char)prefix[0]);
    for (int i = 0; i < v->base->length; ++i) {
	const char *item = v->base->base + v->base->size * i;
	if (!item[0])
	    continue;
	if (chartoupper((unsigned char)item[0]) != first_uc)
	    continue;
	if (len > 1 && strncasecmp(item + 1, prefix + 1, len - 1) != 0)
	    continue;
	return true;
    }
    return false;
}

static int
vec_get_candidates(const char *prefix, void *ctx, int max_items,
		   char *dst_buf, bool *out_truncated,
		   int *out_selected_idx, char *out_selected_name,
		   char *first_match, char *last_match)
{
    vec_ctx_t *v = (vec_ctx_t *)ctx;
    int len = strlen(prefix);
    int count = 0;
    int limit = max_items > 0 ? max_items : MAX_COMPLETE_CAP;
    char exact_id[IDLEN + 1] = "";
    int exact_idx = -1;
    int match_count = 0;
    int single_match_idx = -1;
    int first_uc = len > 0 ? chartoupper((unsigned char)prefix[0]) : 0;

    *out_truncated = false;
    *out_selected_idx = -1;
    out_selected_name[0] = '\0';
    first_match[0] = '\0';
    last_match[0] = '\0';

    if (len == 0 && v->is_usercomplete) {
	*out_truncated = true;
	return 0;
    }

    if (len > 0) {
	if (v->is_usercomplete) {
	    if (searchuser(prefix, exact_id) > 0) {
		exact_idx = 0;
		strlcpy(dst_buf, exact_id, IDLEN + 1);
		count = 1;
		match_count = 1;
	    }
	} else {
	    exact_idx = Vector_search(v->base, prefix);
	    if (exact_idx >= 0) {
		strlcpy(exact_id, Vector_get(v->base, exact_idx), IDLEN + 1);
		strlcpy(dst_buf, exact_id, IDLEN + 1);
		count = 1;
		match_count = 1;
	    }
	}
    }

    for (int i = 0; i < v->base->length; i++) {
	const char *item = v->base->base + v->base->size * i;
	if (!item[0])
	    continue;
	if (len > 0) {
	    if (chartoupper((unsigned char)item[0]) != first_uc)
		continue;
	    if (len > 1 && strncasecmp(item + 1, prefix + 1, len - 1) != 0)
		continue;
	}
	if (exact_id[0] && strcasecmp(item, exact_id) == 0)
	    continue;

	match_count++;
	single_match_idx = i;

	if (count < limit) {
	    strlcpy(dst_buf + count * (IDLEN + 1), item, IDLEN + 1);
	    count++;
	} else {
	    strlcpy(dst_buf + limit * (IDLEN + 1), "...", IDLEN + 1);
	    count = limit + 1;
	    *out_truncated = true;
	    break;
	}
    }

    if (count > 1) {
	int sort_count = *out_truncated ? (count - 1) : count;
	if (sort_count > 1)
	    qsort(dst_buf, sort_count, IDLEN + 1, nc_cmp_id);
    }

    if (count > 0) {
	int last_idx = *out_truncated ? (count - 2) : (count - 1);
	if (last_idx >= 0) {
	    strlcpy(first_match, dst_buf, IDLEN + 1);
	    strlcpy(last_match, dst_buf + last_idx * (IDLEN + 1), IDLEN + 1);
	}
    }

    if (!*out_truncated && match_count == 1 && count == 1) {
	*out_selected_idx = (single_match_idx >= 0 ? single_match_idx : 0);
	strlcpy(out_selected_name, dst_buf, IDLEN + 1);
    } else if (exact_id[0]) {
	*out_selected_idx = (exact_idx >= 0 ? exact_idx : 0);
	strlcpy(out_selected_name, exact_id, IDLEN + 1);
    }

    return count;
}

static const complete_ops_t vec_ops = {
    .has_match = vec_has_match,
    .get_candidates = vec_get_candidates,
};

/* GNC (Sorted Array with Callbacks) Provider */

static int
gnc_findbound(const char *str, int *START, int *END,
	      size_t nmemb, gnc_comp_func compar)
{
    int start, end, mid, cmp, strl;
    strl = strlen(str);

    start = -1, end = (int)nmemb - 1;
    if (end < 0) {
	*START = *END = -1;
	return -1;
    }
    while (end > start + 1) {
	mid = (start + end) / 2;
	cmp = (*compar)(mid, str, strl);
	if (cmp >= 0)
	    end = mid;
	else
	    start = mid;
    }
    if ((*compar)(end, str, strl) != 0) {
	*START = *END = -1;
	return -1;
    }
    *START = end;

    start = end;
    end = (int)nmemb;
    while (end > start + 1) {
	mid = (start + end) / 2;
	cmp = (*compar)(mid, str, strl);
	if (cmp <= 0)
	    start = mid;
	else
	    end = mid;
    }
    *END = start;
    return 0;
}

typedef struct {
    size_t nmemb;
    gnc_comp_func compar;
    gnc_perm_func permission;
    gnc_getname_func getname;
    bool is_online_user;
} gnc_ctx_t;

static bool
gnc_has_match(const char *prefix, void *ctx)
{
    gnc_ctx_t *g = (gnc_ctx_t *)ctx;
    int start = 0, end = 0;
    if (gnc_findbound(prefix, &start, &end, g->nmemb, g->compar) < 0)
	return false;
    for (int i = start; i <= end; ++i) {
	if ((*g->permission)(i))
	    return true;
    }
    return false;
}

static int
gnc_get_candidates(const char *prefix, void *ctx, int max_items,
		   char *dst_buf, bool *out_truncated,
		   int *out_selected_idx, char *out_selected_name,
		   char *first_match, char *last_match)
{
    gnc_ctx_t *g = (gnc_ctx_t *)ctx;
    int len = strlen(prefix);
    int limit = max_items > 0 ? max_items : MAX_COMPLETE_CAP;
    int start = 0, end = 0;

    *out_truncated = false;
    *out_selected_idx = -1;
    out_selected_name[0] = '\0';
    first_match[0] = '\0';
    last_match[0] = '\0';

    if (len == 0 && g->is_online_user) {
	*out_truncated = true;
	return 0;
    }

    if (gnc_findbound(prefix, &start, &end, g->nmemb, g->compar) < 0)
	return 0;

    int first = -1, last = -1;
    for (int i = start; i <= end; ++i) {
	if ((*g->permission)(i)) {
	    first = i;
	    break;
	}
    }
    if (first < 0)
	return 0;

    for (int i = end; i >= start; --i) {
	if ((*g->permission)(i)) {
	    last = i;
	    break;
	}
    }

    strlcpy(first_match, (*g->getname)(first), IDLEN + 1);
    strlcpy(last_match, (*g->getname)(last), IDLEN + 1);

    int count = 0;
    int permitted_count = 0;
    int exact_idx = -1;
    char exact_name[IDLEN + 1] = "";

    for (int i = first; i <= last; ++i) {
	if ((*g->permission)(i)) {
	    permitted_count++;
	    const char *name = (*g->getname)(i);
	    if (len > 0 && strcasecmp(name, prefix) == 0) {
		exact_idx = i;
		strlcpy(exact_name, name, IDLEN + 1);
	    }
	    if (count < limit) {
		strlcpy(dst_buf + count * (IDLEN + 1), name, IDLEN + 1);
		count++;
	    } else if (!*out_truncated) {
		strlcpy(dst_buf + limit * (IDLEN + 1), "...", IDLEN + 1);
		count = limit + 1;
		*out_truncated = true;
	    }
	}
    }

    if (permitted_count == 1) {
	*out_selected_idx = first;
	strlcpy(out_selected_name, (*g->getname)(first), IDLEN + 1);
    } else if (exact_idx >= 0) {
	*out_selected_idx = exact_idx;
	strlcpy(out_selected_name, exact_name, IDLEN + 1);
    }

    return count;
}

static const complete_ops_t gnc_ops = {
    .has_match = gnc_has_match,
    .get_candidates = gnc_get_candidates,
};

/* Unified Completion Engine */

#define COMPL_ALLOW_NONEXISTENT_PREFIX  (1 << 0)
#define COMPL_NO_EMPTY_LIST             (1 << 1)
#define COMPL_ASCII_ONLY                (1 << 2)
#define COMPL_NEWLINE_ON_EXIT           (1 << 3)

struct complete_engine_ctx {
    const complete_ops_t *ops;
    void *provider_ctx;
    int flags;
    const complete_hooks_t *hooks;
    int *exit_key_ptr;

    int start_y;
    int max_items;
    int page_dirty;

    char sublist_buf[(MAX_COMPLETE_CAP + 1) * (IDLEN + 1)];
    struct Vector sublist;
    int sublist_count;
    bool is_truncated;
    int selected_idx;
    char selected_name[IDLEN + 1];
    char first_match[IDLEN + 1];
    char last_match[IDLEN + 1];

    int dirty;
    int page_idx;
};

static void
complete_refresh_candidates(struct complete_engine_ctx *ctx, const char *prefix)
{
    ctx->sublist_count = ctx->ops->get_candidates(
	prefix, ctx->provider_ctx, ctx->max_items, ctx->sublist_buf,
	&ctx->is_truncated, &ctx->selected_idx, ctx->selected_name,
	ctx->first_match, ctx->last_match);
    Vector_init_const(&ctx->sublist, ctx->sublist_buf, ctx->sublist_count, IDLEN + 1);
    ctx->dirty = 0;
    ctx->page_idx = 0;
}

static int
complete_cb_peek(int key, VGET_RUNTIME *prt, void *instance)
{
    struct complete_engine_ctx *ctx = (struct complete_engine_ctx *)instance;
    prt->buf[prt->iend] = '\0';

    if (ctx->hooks && ctx->hooks->peek) {
	int r = ctx->hooks->peek(key, prt, ctx->hooks->ctx);
	if (r != VGETCB_NONE) {
	    if (ctx->exit_key_ptr)
		*ctx->exit_key_ptr = key;
	    if (r == VGETCB_END && prt->iend > 0) {
		if (ctx->dirty < 0)
		    complete_refresh_candidates(ctx, prt->buf);
		if (ctx->selected_idx >= 0) {
		    strlcpy(prt->buf, ctx->selected_name, prt->len);
		} else {
		    if (!(ctx->flags & COMPL_ALLOW_NONEXISTENT_PREFIX))
			prt->buf[0] = '\0';
		    ctx->selected_idx = -1;
		}
		prt->icurr = prt->iend = strlen(prt->buf);
	    } else if (r == VGETCB_ABORT) {
		prt->buf[0] = '\0';
		if (prt->len >= 2)
		    prt->buf[1] = (char)key;
		prt->icurr = prt->iend = 0;
		ctx->selected_idx = -1;
		return VGETCB_ABORT;
	    }
	    return r;
	}
    }

    switch (key) {
    case KEY_ENTER:
	if (ctx->exit_key_ptr)
	    *ctx->exit_key_ptr = KEY_ENTER;
	if (prt->iend == 0) {
	    prt->buf[0] = '\0';
	    ctx->selected_idx = -1;
	    break;
	}
	if (ctx->dirty < 0)
	    complete_refresh_candidates(ctx, prt->buf);

	if (ctx->selected_idx >= 0) {
	    strlcpy(prt->buf, ctx->selected_name, prt->len);
	} else {
	    if (!(ctx->flags & COMPL_ALLOW_NONEXISTENT_PREFIX))
		prt->buf[0] = '\0';
	    ctx->selected_idx = -1;
	}
	prt->icurr = prt->iend = strlen(prt->buf);
	break;

    case ' ':
    case KEY_TAB:
	if ((ctx->flags & COMPL_NO_EMPTY_LIST) && prt->iend == 0)
	    return VGETCB_NEXT;

	if (ctx->dirty < 0)
	    complete_refresh_candidates(ctx, prt->buf);

	if (ctx->sublist_count == 0) {
	    if (ctx->page_dirty) {
		move(ctx->start_y, 0);
		clrtobot();
		printdash(COMPLETE_LIST_TITLE, 0);
	    }
	    return VGETCB_NEXT;
	}

	if (!ctx->is_truncated && ctx->sublist_count == 1) {
	    const char *target = Vector_get(&ctx->sublist, 0);
	    if (strcmp(prt->buf, target) != 0) {
		strlcpy(prt->buf, target, prt->len);
		prt->icurr = prt->iend = strlen(prt->buf);
	    }
	} else if (ctx->sublist_count > 1) {
	    int lcp_len = compute_lcp(ctx->first_match, ctx->last_match);
	    if (lcp_len > (int)prt->iend ||
		(lcp_len == (int)prt->iend &&
		 strncmp(prt->buf, ctx->first_match, lcp_len) != 0)) {
		if (lcp_len >= (int)prt->len)
		    lcp_len = prt->len - 1;
		memcpy(prt->buf, ctx->first_match, lcp_len);
		prt->buf[lcp_len] = '\0';
		prt->icurr = prt->iend = lcp_len;
	    }
	}

	ctx->page_dirty = 1;
	move(ctx->start_y, 0);
	clrtobot();
	printdash(COMPLETE_LIST_TITLE, 0);
	ctx->page_idx = ShowVector(&ctx->sublist, ctx->start_y + 1, 0, NULL, ctx->page_idx);
	if (ctx->page_idx > 0)
	    vshowmsg(COMPLETE_MORE_MSG);
	return VGETCB_NEXT;

    case KEY_BS:
    case KEY_UP:
    case KEY_DOWN:
    case Ctrl('P'):
    case Ctrl('N'):
    case KEY_LEFT:
    case KEY_RIGHT:
    case KEY_HOME:
    case KEY_END:
	ctx->dirty = -1;
	break;

    default:
	if (isascii(key) && isprint(key)) {
	    if (!(ctx->flags & COMPL_ALLOW_NONEXISTENT_PREFIX)) {
		char temp_buf[STRLEN];
		if (prt->iend + 1 < (int)prt->len && prt->iend + 1 < (int)sizeof(temp_buf)) {
		    memcpy(temp_buf, prt->buf, prt->iend);
		    temp_buf[prt->iend] = (char)key;
		    temp_buf[prt->iend + 1] = '\0';
		    if (!ctx->ops->has_match(temp_buf, ctx->provider_ctx)) {
			bell();
			return VGETCB_NEXT;
		    }
		} else {
		    bell();
		    return VGETCB_NEXT;
		}
	    }
	    ctx->dirty = -1;
	} else if (ctx->hooks && ctx->hooks->fallback) {
	    int r = ctx->hooks->fallback(key, prt, ctx->hooks->ctx);
	    if (r != VGETCB_NONE) {
		if (ctx->exit_key_ptr)
		    *ctx->exit_key_ptr = key;
		return r;
	    }
	}
	break;
    }

    return VGETCB_NONE;
}

static int
complete_engine(const char *prompt, char *data, int len, const char *defval,
		int flags, const complete_ops_t *ops, void *provider_ctx,
		const complete_hooks_t *hooks, int *out_exit_key)
{
    if (out_exit_key)
	*out_exit_key = KEY_ENTER;

    if (prompt && *prompt) {
	outs(prompt);
	clrtoeol();
    }

    int start_y = vgety() + 1;
    if (flags & COMPL_NEWLINE_ON_EXIT) {
	if (start_y >= b_lines - 3)
	    start_y = b_lines - 3;
    } else {
	if (start_y >= b_lines - 2)
	    start_y = b_lines - 2;
    }

    struct complete_engine_ctx ctx = {
	.ops = ops,
	.provider_ctx = provider_ctx,
	.flags = flags,
	.hooks = hooks,
	.exit_key_ptr = out_exit_key,
	.start_y = start_y,
	.max_items = get_complete_capacity(start_y, len > IDLEN ? IDLEN : len),
	.page_dirty = 0,
	.sublist_count = 0,
	.is_truncated = false,
	.selected_idx = -1,
	.dirty = -1,
	.page_idx = 0,
    };

    complete_refresh_candidates(&ctx, defval ? defval : "");

    const VGET_CALLBACKS vcb = {
	.peek = complete_cb_peek,
    };

    int vflags = VGET_NO_NAV_EDIT;
    if (flags & COMPL_ASCII_ONLY)
	vflags |= VGET_ASCII_ONLY;

    int ret = vgetstring(data, len, vflags, defval, &vcb, &ctx);

    if (flags & COMPL_NEWLINE_ON_EXIT)
	outc('\n');

    if (ctx.page_dirty) {
	move(ctx.start_y, 0);
	clrtobot();
    }
    Vector_delete(&ctx.sublist);

    if (ret < 1) {
	if (data[1] == Ctrl('C') || data[1] == KEY_ESC) {
	    if (out_exit_key)
		*out_exit_key = (unsigned char)data[1];
	}
	data[0] = '\0';
	return -1;
    }

    if (ctx.selected_idx >= 0) {
	strlcpy(data, ctx.selected_name, len);
	return ctx.selected_idx;
    }

    if (!(flags & COMPL_ALLOW_NONEXISTENT_PREFIX))
	data[0] = '\0';
    return -1;
}

void
namecomplete3(const struct Vector *namelist, const char *prompt, char *data, const char *defval)
{
    vec_ctx_t vec_ctx = {
	.base = namelist,
	.is_usercomplete = false,
    };
    complete_engine(prompt, data, IDLEN + 1, defval, COMPL_ASCII_ONLY, &vec_ops, &vec_ctx, NULL, NULL);
}

void
namecomplete2(const struct Vector *namelist, const char *prompt, char *data)
{
    namecomplete3(namelist, prompt, data, NULL);
}

static int
vform_complete_peek(int key, VGET_RUNTIME *prt GCC_UNUSED, void *instance GCC_UNUSED)
{
    switch (key) {
    case KEY_UP:
    case KEY_DOWN:
    case KEY_STAB:
    case Ctrl('X'):
    case Ctrl('S'):
    case KEY_F10:
	return VGETCB_END;
    case KEY_ESC:
    case Ctrl('C'):
	return VGETCB_ABORT;
    default:
	return VGETCB_NONE;
    }
}

static int
compat_check_exit_key_peek(int key, VGET_RUNTIME *prt GCC_UNUSED, void *instance)
{
    int (*check_exit_key)(int key) = (int (*)(int))instance;
    int exit_type = check_exit_key(key);
    if (exit_type > 0)
	return VGETCB_END;
    if (exit_type < 0)
	return VGETCB_ABORT;
    return VGETCB_NONE;
}

int
usercomplete_hooks(const char *prompt, char *data, const char *defval,
		   const complete_hooks_t *hooks, int *out_exit_key)
{
    struct Vector namelist;
    Vector_init_const(&namelist, SHM->userid[0], MAX_USERS, IDLEN + 1);
    vec_ctx_t vec_ctx = {
	.base = &namelist,
	.is_usercomplete = true,
    };
    int flags = COMPL_ASCII_ONLY | COMPL_ALLOW_NONEXISTENT_PREFIX | COMPL_NO_EMPTY_LIST;
    int exit_key = KEY_ENTER;
    complete_engine(prompt, data, IDLEN + 1, defval, flags, &vec_ops, &vec_ctx,
		    hooks, &exit_key);
    if (out_exit_key)
	*out_exit_key = exit_key;
    return exit_key;
}

int
usercomplete_ex(const char *prompt, char *data, const char *defval,
		int (*check_exit_key)(int key))
{
    if (check_exit_key) {
	complete_hooks_t hooks = {
	    .peek = compat_check_exit_key_peek,
	    .fallback = NULL,
	    .ctx = (void *)check_exit_key,
	};
	return usercomplete_hooks(prompt, data, defval, &hooks, NULL);
    }
    return usercomplete_hooks(prompt, data, defval, NULL, NULL);
}

int
usercomplete_vform(const char *prompt, char *data, const char *defval)
{
    complete_hooks_t hooks = {
	.peek = vform_complete_peek,
	.fallback = NULL,
	.ctx = NULL,
    };
    return usercomplete_hooks(prompt, data, defval, &hooks, NULL);
}

void
usercomplete2(const char *prompt, char *data, const char *defval)
{
    usercomplete_hooks(prompt, data, defval, NULL, NULL);
}

void
usercomplete(const char *prompt, char *data)
{
    usercomplete2(prompt, data, NULL);
}

int
generalnamecomplete(const char *prompt, char *data, int len, size_t nmemb,
		    gnc_comp_func compar, gnc_perm_func permission,
		    gnc_getname_func getname)
{
    gnc_ctx_t gnc_ctx = {
	.nmemb = nmemb,
	.compar = compar,
	.permission = permission,
	.getname = getname,
	.is_online_user = (getname == &completeutmp_getname),
    };
    int flags = COMPL_ASCII_ONLY | COMPL_NEWLINE_ON_EXIT;
    if (gnc_ctx.is_online_user)
	flags |= COMPL_NO_EMPTY_LIST;
    return complete_engine(prompt, data, len, NULL, flags, &gnc_ops, &gnc_ctx, NULL, NULL);
}

/* general complete functions (brdshm) */
int
completeboard_compar(int where, const char *str, int len)
{
    boardheader_t *bh = &bcache[SHM->bsorted[0][where]];
    return strncasecmp(bh->brdname, str, len);
}

int
completeboard_permission(int where)
{
    boardheader_t *bptr = &bcache[SHM->bsorted[0][where]];
    return (!(bptr->brdattr & BRD_SYMBOLIC) &&
	    (CBRD_IS_GROUP_OP() || HasBoardPerm(bptr)) &&
	    !(bptr->brdattr & BRD_GROUPBOARD));
}

int
complete_board_and_group_permission(int where)
{
    boardheader_t *bptr = &bcache[SHM->bsorted[0][where]];
    return (!(bptr->brdattr & BRD_SYMBOLIC) &&
	    (CBRD_IS_GROUP_OP() || HasBoardPerm(bptr)));

}

char           *
completeboard_getname(int where)
{
    return bcache[SHM->bsorted[0][where]].brdname;
}

/* general complete functions (utmpshm) */
typedef struct {
    int *slots;
    int count;
} gnc_utmp_ctx_t;

static gnc_utmp_ctx_t gnc_utmp = { NULL, 0 };

int
completeutmp_compar(int where, const char *str, int len)
{
    if (!gnc_utmp.slots || where < 0 || where >= gnc_utmp.count)
        return 1;
    userinfo_t *u = &SHM->uinfo[gnc_utmp.slots[where]];
    return strncasecmp(u->userid, str, len);
}

int
completeutmp_permission(int where)
{
    if (!gnc_utmp.slots || where < 0 || where >= gnc_utmp.count)
        return 0;
    userinfo_t *u = &SHM->uinfo[gnc_utmp.slots[where]];
    return (unlikely(HasUserPerm(PERM_SYSOP)) ||
	    unlikely(HasUserPerm(PERM_SEECLOAK)) ||
	    isvisible(currutmp, u));
}

char           *
completeutmp_getname(int where)
{
    if (!gnc_utmp.slots || where < 0 || where >= gnc_utmp.count)
        return "";
    return SHM->uinfo[gnc_utmp.slots[where]].userid;
}

int
complete_online_user(const char *prompt, char *data, int len)
{
    gnc_utmp.slots = get_utmp_snapshot(&gnc_utmp.count);
    if (gnc_utmp.slots) {
        sort_utmp_snapshot(gnc_utmp.slots, gnc_utmp.count, UTMP_SORT_USERID);
    }
    int idx = generalnamecomplete(prompt, data, len, gnc_utmp.count,
                                  &completeutmp_compar, &completeutmp_permission,
                                  &completeutmp_getname);
    int res = (gnc_utmp.slots && idx >= 0 && idx < gnc_utmp.count)
              ? gnc_utmp.slots[idx] : -1;
    if (gnc_utmp.slots) {
        free_utmp_snapshot(gnc_utmp.slots);
        gnc_utmp.slots = NULL;
        gnc_utmp.count = 0;
    }
    return res;
}

static void
format_multilist_title(char *buf, size_t sz, const char *prefix, int count)
{
    size_t len = prefix ? strlen(prefix) : 0;
    while (len > 0 && (prefix[len - 1] == '\n' || prefix[len - 1] == '\r'))
        len--;
    if (len > 0)
        snprintf(buf, sz, "%.*s (共 %d 名)", (int)len, prefix, count);
    else
        snprintf(buf, sz, "名單： (共 %d 名)", count);
}

void
multi_user_list(struct Vector *namelist, int *recipient,
                const char *title, const char *msg_prefix, int flags)
{
    char uid[IDLEN + 1];
    char genbuf[PATHLEN];
    char listfile[] = "list.0";
    char msg_title[80];
    int i, cRemoved;

    while (1) {
        vs_hdr(title);
        format_multilist_title(msg_title, sizeof(msg_title), msg_prefix, Vector_length(namelist));
        ShowVector(namelist, 3, 0, msg_title, 0);
        move(1, 0);
        outs("(I)引入好友 (O)引入上線通知 (0-9)特別名單 (E)檔案編輯/貼上名單");
        getdata(2, 0,
                "(A)增加     (D)刪除         (M)確認名單   (Q)取消 ？[M]",
                genbuf, 4, LCECHO);
        switch (genbuf[0]) {
        case 'a':
            while (1) {
                if (Vector_length(namelist) >= MAX_MULTILIST) {
                    vmsgf("名單人數已達上限 (%d 人)！", MAX_MULTILIST);
                    break;
                }
                move(1, 0);
                usercomplete("請輸入要增加的代號(只按 ENTER 結束新增): ", uid);
                if (uid[0] == '\0')
                    break;
                move(2, 0);
                clrtoeol();
                if (!searchuser(uid, uid))
                    outs(ERR_UID);
                else if ((flags & MULTILIST_EXCLUDE_SELF) && strcasecmp(uid, cuser.userid) == 0)
                    outs("不能加入自己喔！");
                else if (strcasecmp(uid, STR_GUEST) == 0)
                    outs("不能加入 guest！");
                else if ((flags & MULTILIST_CHECK_REJECT) && is_rejected(uid))
                    outs("對方拒收信件！");
                else if (Vector_search(namelist, uid) < 0) {
                    Vector_add(namelist, uid);
                    (*recipient)++;
                }
                format_multilist_title(msg_title, sizeof(msg_title), msg_prefix, Vector_length(namelist));
                ShowVector(namelist, 3, 0, msg_title, 0);
            }
            break;
        case 'd':
            while (*recipient) {
                move(1, 0);
                namecomplete2(namelist, "請輸入要刪除的代號(只按 ENTER 結束刪除): ", uid);
                if (uid[0] == '\0')
                    break;
                if (Vector_remove(namelist, uid))
                    (*recipient)--;
                format_multilist_title(msg_title, sizeof(msg_title), msg_prefix, Vector_length(namelist));
                ShowVector(namelist, 3, 0, msg_title, 0);
            }
            break;
        case 'e':
            {
                char tmpfile[PATHLEN];
                FILE *fp;
                setuserfile(tmpfile, "multi_list.tmp");
                if ((fp = fopen(tmpfile, "w"))) {
                    fprintf(fp, "# 請在下方輸入或貼上 ID 名單（單次上限 %d 人，# 開頭為註解自動忽略）\n"
                                "# 支援格式：\n"
                                "#   1. 一行一個 ID，或以空白、逗號分隔多個 ID\n"
                                "#   2. 貼上文章推文（如：推 userid: 內容），系統會擷取 ID 並去除重複 ID\n",
                            MAX_MULTILIST);
                    for (i = 0; i < Vector_length(namelist); i++)
                        fprintf(fp, "%s\n", Vector_get(namelist, i));
                    fclose(fp);
                }
                if (veditfile(tmpfile) != -1 && (fp = fopen(tmpfile, "r"))) {
                    char line[ANSILINELEN];
                    Vector_delete(namelist);
                    Vector_init(namelist, IDLEN + 1);
                    *recipient = 0;
                    while (fgets(line, sizeof(line), fp)) {
                        char *src = line, *dst = line;
                        while (*src) {
                            if (*src == ESC_CHR && src[1] == '[') {
                                src += 2;
                                while (*src && !isalpha((unsigned char)*src))
                                    src++;
                                if (*src)
                                    src++;
                            } else {
                                *dst++ = *src++;
                            }
                        }
                        *dst = '\0';

                        src = line;
                        while (*src && isspace((unsigned char)*src))
                            src++;
                        if (!*src || *src == '#')
                            continue;

                        char *colon = strchr(src, ':');
                        if (colon)
                            *colon = '\0';

                        char *saveptr = NULL;
                        for (char *tok = strtok_r(src, " \t\r\n,;.", &saveptr);
                             tok && Vector_length(namelist) < MAX_MULTILIST;
                             tok = strtok_r(NULL, " \t\r\n,;.", &saveptr)) {
                            if (!isalpha((unsigned char)tok[0]))
                                continue;
                            if (searchuser(tok, uid) &&
                                strcasecmp(uid, STR_GUEST) != 0 &&
                                !((flags & MULTILIST_EXCLUDE_SELF) && strcasecmp(uid, cuser.userid) == 0) &&
                                !((flags & MULTILIST_CHECK_REJECT) && is_rejected(uid)) &&
                                Vector_search(namelist, uid) < 0) {
                                Vector_add(namelist, uid);
                                (*recipient)++;
                            }
                        }
                    }
                    fclose(fp);
                }
                unlink(tmpfile);
            }
            break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            listfile[5] = genbuf[0];
            genbuf[0] = '1';
        case 'i':
            setuserfile(genbuf, genbuf[0] == '1' ? listfile : FN_OVERRIDES);
            ToggleVector(namelist, recipient, genbuf, msg_title);
            break;
        case 'o':
            setuserfile(genbuf, FN_ALOHAED);
            ToggleVector(namelist, recipient, genbuf, msg_title);
            break;
        case 'q':
            *recipient = 0;
            return;
        default:
            vs_hdr(title);
            outs("\n正在檢查名單... \n");
            doupdate();
            cRemoved = 0;
            for (i = 0; i < Vector_length(namelist); i++) {
                const char *p = Vector_get(namelist, i);
                if (searchuser(p, uid) &&
                    strcasecmp(STR_GUEST, uid) != 0 &&
                    !((flags & MULTILIST_EXCLUDE_SELF) && strcasecmp(cuser.userid, uid) == 0) &&
                    !((flags & MULTILIST_CHECK_REJECT) && is_rejected(uid)))
                    continue;
                if (!cRemoved)
                    outs("下列 ID 無效或無法加入，已自名單移除；"
                         "請重新確認名單。\n\n");
                cRemoved++;
                prints("%-.*s ", IDLEN, p);
                Vector_remove(namelist, p);
                i--;
                if ((cRemoved + 1) * (IDLEN + 1) >= t_columns)
                    outs("\n");
            }
            *recipient = Vector_length(namelist);
            if (cRemoved) {
                pressanykey();
                continue;
            }
            return;
        }
    }
}
