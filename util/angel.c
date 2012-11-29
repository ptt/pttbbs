/* $Id$ */
#include "bbs.h"
#include "daemons.h"

#define QUOTE(x) #x
#define EXPAND_AND_QUOTE(x) QUOTE(x)
#define STR_ANGELBEATS_PERF_MIN_PERIOD \
        EXPAND_AND_QUOTE(ANGELBEATS_PERF_MIN_PERIOD)

#ifndef PLAY_ANGEL
int main(){ return 0; }
#else

int total[MAX_USERS + 1];
int (*list)[2];
int count;
char* mailto = "SYSOP";


void readData();
void sendResult();
void slurp(FILE* to, FILE* from);

int main(int argc, char* argv[]){
    if (argc > 1)
	mailto = argv[1];

    readData();
    sendResult();
    return 0;
}

void appendLogFile(FILE *output,
                   const char *filename,
                   const char *prefix) {
    FILE *fp = fopen(filename, "r");
    if (!fp)
        return;
    remove(filename);

    fputs(prefix, output);
    slurp(output, fp);
    fclose(fp);
}

void readData(){
    int i, j;
    int k;
    userec_t user;
    FILE* fp;

    attach_SHM();

    fp = fopen(BBSHOME "/.PASSWDS", "rb");
    j = count = 0;
    while (fread(&user, sizeof(userec_t), 1, fp) == 1) {
	++j; /* j == uid */
	if (user.myangel[0]) {
	    i = searchuser(user.myangel, NULL);
	    if (i)
		++total[i];
	}
	if (user.userlevel & PERM_ANGEL) {
	    ++count;
	    ++total[j]; /* make all angel have total > 0 */
	} else { /* don't have PERM_ANGEL */
	    total[j] = INT_MIN;
	}
    }
    fclose(fp);

    list = (int(*)[2]) malloc(count * sizeof(int[2]));
    k = j = 0;
    for (i = 1; i <= MAX_USERS; ++i)
	if (total[i] > 0) {
	    list[j][0] = total[i] - 1;
	    list[j][1] = i;
	    ++j;
	}

    qsort(list, count, sizeof(int[2]), cmp_int_desc);
}

int mailalertuser(char* userid)
{
    userinfo_t *uentp=NULL;
    if (userid[0] && (uentp = search_ulist_userid(userid)))
         uentp->alerts|=ALERT_NEW_MAIL;
    return 0;
}

void sendResult(){
    int i;
    FILE* fp;
    time4_t t;
    fileheader_t header;
    struct stat st;
    char filename[512];

    sprintf(filename, BBSHOME "/home/%c/%s", mailto[0], mailto);
    if (stat(filename, &st) == -1) {
	if (Mkdir(filename) == -1) {
	    fprintf(stderr, "mail box create error %s \n", filename);
	    return;
	}
    }
    else if (!(st.st_mode & S_IFDIR)) {
	fprintf(stderr, "mail box error\n");
	return;
    }

    stampfile(filename, &header);
    fp = fopen(filename, "w");
    if (fp == NULL) {
	fprintf(stderr, "Cannot open file %s\n", filename);
	return;
    }

    t = time(NULL);
    fprintf(fp, "作者: " BBSMNAME " 站方統計\n"
	    "標題: 小天使統計資料\n"
	    "時間: %s\n"
	    "\n現在全站小天使有 %d 位:\n",
	    ctime4(&t), count);
    for (i = 0; i < count; ++i)
	fprintf(fp, "%15s %5d 人\n", SHM->userid[list[i][1] - 1], list[i][0]);
    if (i % 4 != 0)
        fputc('\n', fp);

    appendLogFile(fp, BBSHOME "/log/angel_perf.txt",
                  "\n== 本周小天使活動資料記錄 ==\n"
                  " (說明: Samples 指的是新小主人找天使時有在線上的次數\n"
                  "        Pause1  指的是 Samples 中有幾次神諭呼叫器設停收\n"
                  "        Pause2  指的是 Samples 中有幾次神諭呼叫器設關閉\n"
                  "  因此，Samples 與其它人差太多代表不常上線\n"
                  "        Pause2  接近 Samples 代表此天使都在打混\n"
                  "  另外, Samples 每"  STR_ANGELBEATS_PERF_MIN_PERIOD
                  "秒最多更新一次)\n"
                  );
    appendLogFile(fp, BBSHOME "/log/changeangel.log",
                  "\n== 本周更換小天使記錄 ==\n");

    fputs("\n--\n\n  本資料由 angel 程式產生\n\n", fp);
    fclose(fp);

    strcpy(header.title, "小天使統計資料");
    strcpy(header.owner, "站方統計");
    sethomedir(filename, mailto);
    append_record(filename, &header, sizeof(header));
    mailalertuser(mailto);
}

void slurp(FILE* to, FILE* from)
{
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

#endif /* defined PLAY_ANGEL */
