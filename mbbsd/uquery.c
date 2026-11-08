// Query user (login) records
#include "bbs.h"
#include "vform.h"
#include <arpa/inet.h>

#define UQUERY_SVC_TIMEOUT_MS   (3000)
#define UQUERY_MAX_TERMS        (16)  // ID/IPs per entry
#define UQUERY_PERMS            (PERM_SYSOP | PERM_ACCOUNTS | PERM_POLICE_MAN)

enum { UQ_TYPE_LOGIN = 0, UQ_TYPE_REG };
static const char * const uq_types[] = { "登入記錄", "註冊記錄" };

enum { UQ_SORT_TIME = 0, UQ_SORT_USERID, UQ_SORT_IP };
static const char * const uq_sorts[] = { "時間", "ID, 時間", "IP, 時間" };
static const char * const uq_sort_json[] = {
    "\"md\",\"t\"", "\"userid\",\"md\",\"t\"", "\"ip\",\"md\",\"t\"",
};

typedef struct {
    int  type;
    int  year;
    int  mds, mde;
    char userids[STRLEN];
    char ips[STRLEN];
    int  sort;
    bool related;
} uquery_form_t;

static int
uq_this_year(void)
{
    time_t t = time(NULL);
    struct tm tm;

    localtime_r(&t, &tm);
    return tm.tm_year + 1900;
}

static bool
uq_valid_md(int md)
{
    int m = md / 100, d = md % 100;
    return m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

static const char *
uq_md_validator(const vform_field_t *field GCC_UNUSED, const void *val_ptr,
                void *priv GCC_UNUSED)
{
    return uq_valid_md(*(const int *)val_ptr) ? NULL :
        "日期格式為 MMDD (例: 1108 為 11 月 8 日)";
}

// Calls fn for each term in s (separated by spaces or commas); stops and
// returns what fn returns if not NULL.
static const char *
uq_each_term(const char *s, const char *(*fn)(const char *term, void *ctx),
             void *ctx)
{
    char buf[STRLEN], *p, *save = NULL;

    strlcpy(buf, s, sizeof(buf));
    for (p = strtok_r(buf, " ,", &save); p; p = strtok_r(NULL, " ,", &save)) {
        const char *err = fn(p, ctx);
        if (err)
            return err;
    }
    return NULL;
}

static const char *
uq_check_userid(const char *term, void *ctx)
{
    int *n = (int *)ctx;
    const char *p;

    if (strlen(term) > IDLEN)
        return "ID 太長";
    for (p = term; *p; p++)
        if (!isalnum((unsigned char)*p) && !strchr("._-", *p))
            return "ID 只能有英文字母、數字";
    if (++*n > UQUERY_MAX_TERMS)
        return "一次最多查 16 個 ID";
    return NULL;
}

static const char *
uq_check_ip(const char *term, void *ctx)
{
    int *n = (int *)ctx;
    struct in_addr addr;

    if (inet_pton(AF_INET, term, &addr) != 1)
        return "IP 格式錯誤 (只支援 IPv4, 例: 1.2.3.4)";
    if (++*n > UQUERY_MAX_TERMS)
        return "一次最多查 16 個 IP";
    return NULL;
}

static const char *
uq_userids_validator(const vform_field_t *field GCC_UNUSED, const void *val_ptr,
                     void *priv GCC_UNUSED)
{
    int n = 0;
    return uq_each_term((const char *)val_ptr, uq_check_userid, &n);
}

static const char *
uq_ips_validator(const vform_field_t *field GCC_UNUSED, const void *val_ptr,
                 void *priv GCC_UNUSED)
{
    int n = 0;
    return uq_each_term((const char *)val_ptr, uq_check_ip, &n);
}

static bool
uq_is_login(const vform_field_t *field GCC_UNUSED, const void *data,
            void *priv GCC_UNUSED)
{
    return ((const uquery_form_t *)data)->type == UQ_TYPE_LOGIN;
}

static bool
uq_blank(const char *s)
{
    for (; *s; s++)
        if (*s != ' ' && *s != ',')
            return false;
    return true;
}

static const char *
uq_form_validator(const vform_def_t *def GCC_UNUSED, const void *data,
                  void *priv GCC_UNUSED)
{
    const uquery_form_t *f = (const uquery_form_t *)data;

    if (f->mds > f->mde)
        return "起始日期不可晚於結束日期";
    if (f->type == UQ_TYPE_LOGIN && uq_blank(f->userids) && uq_blank(f->ips))
        return "查詢登入記錄需要指定 ID 或 IP";
    return NULL;
}

static const vform_field_t uquery_fields[] = {
    VFORM_FIELD_SELECT("查詢類型", "登入記錄或註冊記錄",
                       uquery_form_t, type, uq_types, ARRAY_SIZE(uq_types)),
    VFORM_FIELD_INT("年份", "要查詢的年份", uquery_form_t, year, 2000, 9999, NULL),
    VFORM_FIELD_INT("起始日期", "MMDD (例: 0101 為 1 月 1 日)", uquery_form_t, mds,
                    101, 1231, uq_md_validator),
    VFORM_FIELD_INT("結束日期", "MMDD (例: 1231 為 12 月 31 日)", uquery_form_t, mde,
                    101, 1231, uq_md_validator),
    VFORM_FIELD_TEXT("使用者 ID", "可多個, 以空白或逗號分隔", NULL,
                     uquery_form_t, userids, VGET_ASCII_ONLY, uq_userids_validator),
    VFORM_FIELD_TEXT("IP", "可多個 IPv4, 以空白或逗號分隔", NULL,
                     uquery_form_t, ips, VGET_ASCII_ONLY, uq_ips_validator),
    VFORM_FIELD_SELECT("排序", "依序排列的欄位",
                       uquery_form_t, sort, uq_sorts, ARRAY_SIZE(uq_sorts)),
    {
        .label = "同 IP 記錄",
        .hint = "一併列出同 IP 前後 5 分鐘內的登入",
        .type = VFORM_BOOL,
        .offset = offsetof(uquery_form_t, related),
        .size = MEMBER_SIZE(uquery_form_t, related),
        .is_visible = uq_is_login,
    },
};

static const vform_def_t uquery_form_def = {
    .title = "查詢使用者登入/註冊記錄",
    .caption = "查詢記錄",
    .subtitle = "結果會以站內信寄給您, 資料量大時需要一些時間",
    .mode = VFORM_MODE_INPLACE,
    .fields = uquery_fields,
    .num_fields = ARRAY_SIZE(uquery_fields),
    .form_validator = uq_form_validator,
    .submit_prompt = "確定送出查詢嗎？[Y/n] ",
};

typedef struct {
    char *buf;
    size_t size;
    int n;
} uq_json_list_t;

// Appends a term (already validated: no characters to escape) to a JSON list.
static const char *
uq_append_term(const char *term, void *ctx)
{
    uq_json_list_t *l = (uq_json_list_t *)ctx;
    size_t len = strlen(l->buf);

    snprintf(l->buf + len, l->size - len, "%s\"%s\"", l->n++ ? "," : "", term);
    return NULL;
}

static void
uq_json_list(char *buf, size_t size, const char *terms)
{
    uq_json_list_t l = { buf, size, 0 };

    buf[0] = '\0';
    uq_each_term(terms, uq_append_term, &l);
}

// Gets the string value of key in a flat JSON object (no escapes needed).
static void
uq_json_get(const char *json, const char *key, char *out, size_t size)
{
    char pat[32];
    const char *p, *e;

    out[0] = '\0';
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    if (!(p = strstr(json, pat)))
        return;
    p += strlen(pat);
    if (!(e = strchr(p, '"')))
        return;
    snprintf(out, size, "%.*s", (int)(e - p), p);
}

// Sends the request; returns 0 if queued (with the id), or -1 (with why).
static int
uq_request(const char *req, char *id, size_t idsz, char *err, size_t errsz)
{
    char resp[1024];
    int fd, n, len = (int)strlen(req);

    id[0] = err[0] = '\0';
    if ((fd = svc_connect(GET_SVC_SOCKET(uquery))) < 0) {
        strlcpy(err, "查詢服務 (uquery) 沒有回應, 請通知站長", errsz);
        return -1;
    }
    if (svc_write(fd, req, len) != len) {
        close(fd);
        strlcpy(err, "無法送出查詢", errsz);
        return -1;
    }
    n = svc_read(fd, resp, sizeof(resp) - 1, UQUERY_SVC_TIMEOUT_MS);
    close(fd);
    if (n <= 0) {
        strlcpy(err, "查詢服務 (uquery) 沒有回應, 請通知站長", errsz);
        return -1;
    }
    resp[n] = '\0';
    if (strstr(resp, "\"status\":\"ok\"")) {
        uq_json_get(resp, "id", id, idsz);
        return 0;
    }
    uq_json_get(resp, "error", err, errsz);
    if (!err[0])
        strlcpy(err, "查詢失敗", errsz);
    return -1;
}

int
u_query_records(void)
{
    uquery_form_t form;
    char userids[STRLEN * 2], ips[STRLEN * 2], req[1024], id[64], err[256];

    if (!IS_ENABLED(CONFIG_UQUERY_SVC) || !HasUserPerm(UQUERY_PERMS))
        return 0;

    memset(&form, 0, sizeof(form));
    form.type = UQ_TYPE_LOGIN;
    form.year = uq_this_year();
    form.mds = 101;
    form.mde = 1231;
    form.sort = UQ_SORT_TIME;

    if (vform_run(&uquery_form_def, &form, NULL) <= 0)
        return 0;
    if (form.year > uq_this_year()) {
        vmsg("年份不可超過今年");
        return 0;
    }

    uq_json_list(userids, sizeof(userids), form.userids);
    uq_json_list(ips, sizeof(ips), form.ips);
    snprintf(req, sizeof(req),
             "{\"action\":\"query\",\"requester\":\"%s\",\"type\":\"%s\","
             "\"year\":%d,\"mds\":%d,\"mde\":%d,\"userids\":[%s],\"ips\":[%s],"
             "\"sort\":[%s],\"related\":\"%s\"}\n",
             cuser.userid, form.type == UQ_TYPE_REG ? "reg" : "login",
             form.year, form.mds, form.mde, userids, ips,
             uq_sort_json[form.sort],
             (form.type == UQ_TYPE_LOGIN && form.related) ? "sameip" : "");

    if (uq_request(req, id, sizeof(id), err, sizeof(err)) < 0) {
        vmsgf("查詢失敗: %s", err);
        return 0;
    }
    vmsgf("查詢已送出 (%s), 結果稍後會寄到您的信箱", id);
    return 0;
}
