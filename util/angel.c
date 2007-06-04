/* $Id$ */
#include "bbs.h"

#ifndef PLAY_ANGEL
int main(){ return 0; }
#else

int total[MAX_USERS + 1];
unsigned char reject[MAX_USERS + 1];
int nReject[4];
int rej_question;
int double_rej;
int (*list)[2];
int *rej_list;
int nReport = 50;
int count;
char* mailto = "SYSOP";

int ListCmp(const void * a, const void * b){
    return *(int*)b - *(int*)a;
}

int RejCmp(const void * a, const void * b){
    return strcasecmp(SHM->userid[*(int*)a - 1], SHM->userid[*(int*)b - 1]);
}

void readData();
void sendResult();
void slurp(FILE* to, FILE* from);

int main(int argc, char* argv[]){
    if (argc > 1)
	mailto = argv[1];
    if (argc > 2)
	nReport = atoi(argv[2]);

    readData();
    sendResult();
    return 0;
}

void readData(){
    int i, j;
    int k;
    userec_t user;
    FILE* fp;

    attach_SHM();

    fp = fopen(BBSHOME "/.Angel", "rb");
    if (fp != 0) {
	fread(reject, 1, sizeof(reject), fp);
	fclose(fp);
    }

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
	    ++nReject[((user.uflag2 & ANGEL_MASK) >> 12)];
	    ++total[j]; /* make all angel have total > 0 */
	    if (user.uflag2 & REJ_QUESTION) {
		++rej_question;
		if (++reject[j] >= 2)
		    ++double_rej;
	    } else
		reject[j] = 0;
	} else { /* don't have PERM_ANGEL */
	    total[j] = INT_MIN;
	    reject[j] = 0;
	}
    }
    fclose(fp);

    fp = fopen(BBSHOME "/.Angel", "wb");
    if (fp != NULL) {
	fwrite(reject, sizeof(reject), 1, fp);
	fclose(fp);
    }

    if(nReport > count)
	nReport = count;

    list = (int(*)[2]) malloc(count * sizeof(int[2]));
    rej_list = (int*) malloc(double_rej * sizeof(int));
    k = j = 0;
    for (i = 1; i <= MAX_USERS; ++i)
	if (total[i] > 0) {
	    list[j][0] = total[i] - 1;
	    list[j][1] = i;
	    ++j;
	    if (reject[i] >= 2)
		rej_list[k++] = i;
	}

    qsort(list, count, sizeof(int[2]), ListCmp);
    qsort(rej_list, double_rej, sizeof(int), RejCmp);
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
	if (mkdir(filename, 0755) == -1) {
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
    fprintf(fp, "作者: Ptt 站方統計\n"
	    "標題: 小天使統計資料\n"
	    "時間: %s\n"
	    "\n現在全站小天使有 %d 位\n"
	    "\n小主人人數最多的 %d 位小天使:\n",
	    ctime4(&t), count, nReport);
    for (i = 0; i < nReport; ++i)
	fprintf(fp, "%15s %5d 人\n", SHM->userid[list[i][1] - 1], list[i][0]);
    fprintf(fp, "\n現在男女皆收的小天使有 %d 位\n"
	    "只收女生的有 %d 位  (共 %d 位收女生)\n"
	    "只收男生的有 %d 位  (共 %d 位收男生)\n"
	    "不接受新小天使的有 %d 位\n",
	    nReject[0], nReject[1], nReject[1] + nReject[0],
	    nReject[2], nReject[2] + nReject[0], nReject[3]);
    fprintf(fp, "\n現在開放小主人問問題的小天使有 %d 位\n"
	    "不開放的有 %d 位\n"
	    "連續兩次統計都不開放的有 %d 位:\n",
	    count - rej_question, rej_question, double_rej);
    for (i = 0; i < double_rej; ++i) {
	fprintf(fp, "%13s %3d 次", SHM->userid[rej_list[i] - 1],
		reject[rej_list[i]]);
	if (i % 4 == 3) fputc('\n', fp);
    }
    if (i % 4 != 0) fputc('\n', fp);

    {
	FILE* changefp = fopen(BBSHOME "/log/changeangel.log", "r");
	if (changefp) {
	    remove(BBSHOME "/log/changeangel.log");

	    fputs("\n== 本周更換小天使紀錄 ==\n", fp);
	    slurp(fp, changefp);
	    fclose(changefp);
	}
    }

    fputs("\n--\n\n  本資料由 angel 程式產生\n\n", fp);
    fclose(fp);

    strcpy(header.title, "小天使統計資料");
    strcpy(header.owner, "站方統計");
    sprintf(filename, BBSHOME "/home/%c/%s/.DIR", mailto[0], mailto);
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
