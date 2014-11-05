#include "bbs.h"
#include "daemons.h"

#define QUOTE(x) #x
#define EXPAND_AND_QUOTE(x) QUOTE(x)
#define STR_ANGELBEATS_PERF_MIN_PERIOD \
        EXPAND_AND_QUOTE(ANGELBEATS_PERF_MIN_PERIOD)
#define die(format...) { fprintf(stderr, format); exit(1); }

#define REPORT_AUTHOR   "[天使公會]"
#define REPORT_SUBJECT  "小天使統計資料"

#ifndef PLAY_ANGEL
int main(){ return 0; }
#else

void slurp(FILE* to, FILE* from) {
    char buf[4096]; // 4K block
    int count;

    while ((count = fread(buf, 1, sizeof(buf), from)) > 0) {
	char * p = buf;
	while (count > 0) {
	    int i = fwrite(p, 1, count, to);

	    if (i <= 0) return;

	    p += i;
	    count -= i;
	}
    }
}

void appendLogFile(FILE *output,
                   const char *filename,
                   const char *prefix,
                   int delete_file) {
    FILE *fp = fopen(filename, "r");
    if (!fp)
        return;
    if (delete_file)
        remove(filename);

    fputs(prefix, output);
    slurp(output, fp);
    fclose(fp);
}

typedef struct {
    int uid;
    int is_angel;
    int masters;
    int masters_week;
    int masters_month;
    int masters_quarter;
    int masters_season;
    int masters_period;
} AngelRecord;

int buildMasterInfo(AngelRecord *rec, int num_recs) {
    userec_t user;
    int uid = 0, angel_uid;
    FILE *fp;
    int count = 0;
    time4_t now = time4(NULL);

    memset(rec, 0, sizeof(*rec) * num_recs);

    fp = fopen(FN_PASSWD, "rb");
    while (fread(&user, sizeof(user), 1, fp) == 1) {
        AngelRecord *r = rec + uid, *angel = NULL;
        uid++;
        r->uid = uid;
        assert(uid <= num_recs);
        if (!*user.userid)
            continue;
        if (user.role & ROLE_ANGEL_ACTIVITY)
            continue;
        if (user.userlevel & PERM_ANGEL) {
            r->is_angel = 1;
            count++;
        }
        if (!*user.myangel)
            continue;
        angel_uid = searchuser(user.myangel, NULL);
        if (!angel_uid)
            continue;
        angel = rec + (angel_uid - 1);
        angel->masters++;
        if (now - user.timeplayangel < DAY_SECONDS * 7)
            angel->masters_week++;
        if (now - user.timeplayangel < DAY_SECONDS * 30)
            angel->masters_month++;
        if (now - user.timeplayangel < DAY_SECONDS * 90)
            angel->masters_quarter++;
        if (now - user.timeplayangel < DAY_SECONDS * 120)
            angel->masters_season++;
        if (now - user.timeplayangel < DAY_SECONDS * 180)
            angel->masters_period++;
    }
    fclose(fp);
    return count;
}

int sortAngelRecord(const void *pb, const void *pa) {
    const AngelRecord *a = (const AngelRecord *)pa,
          *b = (const AngelRecord *)pb;
    assert(a->is_angel == b->is_angel);
    if (a->masters_month != b->masters_month)
        return a->masters_month - b->masters_month;
    return a->masters - b->masters;
}

int generateReport(FILE *fp, AngelRecord *rec, int num_recs, int delete_file) {
    time4_t t = time4(NULL);
    int i;

    fprintf(fp, "作者: %s\n標題: %s\n時間: %s\n",
            REPORT_AUTHOR, REPORT_SUBJECT, ctime4(&t));

    fprintf(fp, "現在全站小天使有 %d 位:\n", num_recs);
    fprintf(fp,
            " (後面數字為全部小主人數 |  7天內 | 30天內 | 90天內 |  120天 |  180天\n"
            "  的活躍小主人數(在該段時間內有傳送訊息給任一小天使的主人)\n"
	    "  但注意目前活躍小主人僅統計「主人有送訊息」，無法得知小天使\n"
	    "  是否掛站 - 所以請配合抽查結果評估。)\n");
    for (i = 0; i < num_recs; i++)
        fprintf(fp, "%15s | %6d | %6d | %6d | %6d | %6d | %6d\n",
                getuserid(rec[i].uid),
                rec[i].masters,
                rec[i].masters_week,
                rec[i].masters_month,
                rec[i].masters_quarter,
                rec[i].masters_season,
		rec[i].masters_period
		);
    fputs("\n", fp);

    appendLogFile(fp, "log/angel_perf.txt",
                  "\n== 本周小天使活動資料記錄 ==\n"
                  " (說明: Start 是開始統計的時間\n"
                  "        Duration 是幾秒統計一次\n"
                  "        Sample 指的是每次統計時天使在線上的次數\n"
                  "        Pause1 指的是 Sample 中有幾次神諭呼叫器設停收\n"
                  "        Pause2 指的是 Sample 中有幾次神諭呼叫器設關閉)\n",
                  delete_file);

    appendLogFile(fp, "log/change_angel_nick.log",
                  "\n== 本周小天使暱稱變更記錄 ==\n",
                  delete_file);

    appendLogFile(fp, "log/changeangel.log",
                  "\n== 本周更換小天使記錄 ==\n",
                  delete_file);

    fprintf(fp, "\n--\n  本資料由%s%s自動產生\n\n", BBSNAME, REPORT_AUTHOR);
    return 0;
}

void usage(const char *myname) {
    fprintf(stderr, "Usage: %s [-m user][-b board]\n", myname);
    exit(1);
}

int main(int argc, char *argv[]){
    AngelRecord *rec, *angels;
    int count, i, iangel;
    int uid, bid;
    const char *myname = argv[0];
    const char *target = NULL;
    int target_is_user = 0;
    char target_name[PATHLEN];
    char output_path[PATHLEN] = "", output_dir[PATHLEN] = "";
    struct fileheader_t fhdr;
    FILE *fp = stdout;

    chdir(BBSHOME);
    attach_SHM();

    while (argc > 2) {
        if (strcmp(argv[1], "-m") == 0) {
            target = argv[2];
            target_is_user = 1;
            argc -= 2, argv += 2;
            if ((uid = searchuser(target, target_name)) < 1)
                die("Invalid user: %s\n", target);
            target = target_name;
            sethomepath(output_path, target);
            sethomedir(output_dir, target);
        } else if (strcmp(argv[1], "-b") == 0) {
            boardheader_t *bp;
            target = argv[2];
            target_is_user = 0;
            argc -= 2, argv += 2;
            if ((bid = getbnum(target)) < 1)
                die("Invalid board: %s\n", target);
            bp = getbcache(bid);
            strlcpy(target_name, bp->brdname, sizeof(target_name));
            target = target_name;
            setbpath(output_path, target);
            setbfile(output_dir, target, ".DIR");
        } else {
            usage(myname);
        }
    }
    if (argc != 1)
        usage(myname);

    if (target) {
        stampfile(output_path, &fhdr);
        fp = fopen(output_path, "wt");
        if (!fp)
            die("Failed to create: %s\n", output_path);
    }

    rec = (AngelRecord *)malloc(sizeof(AngelRecord) * MAX_USERS);
    assert(rec);
    count = buildMasterInfo(rec, MAX_USERS);
    // TODO remove expired angels.

    angels = (AngelRecord *)malloc(sizeof(AngelRecord) * count);
    assert(angels);
    for (i = 0, iangel = 0; i < MAX_USERS; i++) {
        if (!rec[i].is_angel)
            continue;
        memcpy(angels + iangel, rec + i, sizeof(AngelRecord));
        iangel++;
    }
    qsort(angels, count, sizeof(AngelRecord), sortAngelRecord);
    generateReport(fp, angels, count, target ? 1 : 0);

    if (target) {
        fclose(fp);
        strlcpy(fhdr.title, REPORT_SUBJECT, sizeof(fhdr.title));
        strlcpy(fhdr.owner, REPORT_AUTHOR, sizeof(fhdr.owner));
        append_record(output_dir, &fhdr, sizeof(fhdr));

        if (target_is_user) {
            userinfo_t *uentp = search_ulistn(uid, 1);
            if (uentp)
                uentp->alerts |= ALERT_NEW_MAIL;
        } else {
            touchbtotal(bid);
        }
    }
    return 0;
}

#endif /* defined PLAY_ANGEL */
