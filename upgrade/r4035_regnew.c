/* $Id$ */
#include "bbs.h"
#include "cmbbs.h"

// New style (Regform2) file names:
#define FN_REGFORM      "regform"       // registration form in user home
#define FN_REGFORM_LOG  "regform.log"   // regform history in user home
#define FN_REQLIST      "reg.wait"      // request list file, in global directory (replacing fn_register)

#define FN_OLDREG       "register.new"

////////////////////////////////////////////////////////////////////////////
// Regform Utilities
////////////////////////////////////////////////////////////////////////////

// TODO define and use structure instead, even in reg request file.
typedef struct {
    // current format:
    // (optional) num: unum, date
    // [0] uid: xxxxx   (IDLEN=12)
    // [1] name: RRRRRR (20)
    // [2] career: YYYYYYYYYYYYYYYYYYYYYYYYYY (40)
    // [3] addr: TTTTTTTTT (50)
    // [4] phone: 02DDDDDDDD (20)
    // [5] email: x (50) (deprecated)
    // [6] mobile: (deprecated)
    // [7] ----
    //     lasthost: 16
    char userid[IDLEN+1];

    char exist;
    char online;
    char pad   [ 5];     // IDLEN(12)+1+1+1+5=20

    char name  [20];
    char career[40];
    char addr  [50];
    char phone [20];
} RegformEntry;

// regform format utilities
int
load_regform_entry(RegformEntry *pre, FILE *fp)
{
    char buf[STRLEN];
    char *v;

    memset(pre, 0, sizeof(RegformEntry));
    while (fgets(buf, sizeof(buf), fp))
    {
        if (buf[0] == '-')
            break;
        buf[sizeof(buf)-1] = 0;
        v = strchr(buf, ':');
        if (v == NULL)
            continue;
        *v++ = 0;
        if (*v == ' ') v++;
        chomp(v);

        if (strcmp(buf, "uid") == 0)
            strlcpy(pre->userid, v, sizeof(pre->userid));
        else if (strcmp(buf, "name") == 0)
            strlcpy(pre->name, v, sizeof(pre->name));
        else if (strcmp(buf, "career") == 0)
            strlcpy(pre->career, v, sizeof(pre->career));
        else if (strcmp(buf, "addr") == 0)
            strlcpy(pre->addr, v, sizeof(pre->addr));
        else if (strcmp(buf, "phone") == 0)
            strlcpy(pre->phone, v, sizeof(pre->phone));
    }
    return pre->userid[0] ? 1 : 0;
}

int
print_regform_entry(const RegformEntry *pre, FILE *fp, int close)
{
    fprintf(fp, "uid: %s\n",    pre->userid);
    fprintf(fp, "name: %s\n",   pre->name);
    fprintf(fp, "career: %s\n", pre->career);
    fprintf(fp, "addr: %s\n",   pre->addr);
    fprintf(fp, "phone: %s\n",  pre->phone);
    if (close)
        fprintf(fp, "----\n");
    return 1;
}

////////////////////////////////////////////////////////////////////////////
// Regform2 API
////////////////////////////////////////////////////////////////////////////

// registration queue
int
regq_append(const char *userid)
{
    if (file_append_record(FN_REQLIST, userid) < 0)
        return 0;
    return 1;
}

int 
regq_find(const char *userid)
{
    return file_find_record(FN_REQLIST, userid);
}

// user home regform operation
int 
regfrm_exist(const char *userid)
{
    char fn[PATHLEN];
    sethomefile(fn, userid, FN_REGFORM);
    return  dashf(fn) ? 1 : 0;
}

int
regfrm_load(const char *userid, RegformEntry *pre)
{
    FILE *fp = NULL;
    char fn[PATHLEN];
    int ret = 0;
    sethomefile(fn, userid, FN_REGFORM);
    if (!dashf(fn))
        return 0;

    fp = fopen(fn, "rt");
    if (!fp)
        return 0;
    ret = load_regform_entry(pre, fp);
    fclose(fp);
    return ret;
}

int 
regfrm_save(const char *userid, const RegformEntry *pre)
{
    FILE *fp = NULL;
    char fn[PATHLEN];
    int ret = 0;
    sethomefile(fn, userid, FN_REGFORM);

    fp = fopen(fn, "wt");
    if (!fp)
        return 0;
    ret = print_regform_entry(pre, fp, 1);
    fclose(fp);
    return ret;
}

int main()
{
    FILE *fp = NULL;
    RegformEntry re;
    chdir(BBSHOME);
    fp = fopen(FN_OLDREG, "rt");
    if(!fp) { 
        printf("no register.new file. abort.\n");
        return 0;
    }
    while (load_regform_entry(&re, fp))
    {
        printf("converting: %s\n", re.userid);
        regfrm_save(re.userid, &re);
        if (!regq_find(re.userid))
            regq_append(re.userid);
    }
    fclose(fp);
    return 0;
}
