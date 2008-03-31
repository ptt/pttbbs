/* $Id$ */
#include "bbs.h"

#ifndef PLAY_ANGEL
int main(){ return 0; }
#else

int total[MAX_USERS + 1];
int (*list)[2];
int nReport = 50;
int count;
char* mailto = "SYSOP";


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

    if(nReport > count)
	nReport = count;

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
    fprintf(fp, ": " BBSMNAME " よ参璸\n"
	    "夹肈: ぱㄏ参璸戈\n"
	    "丁: %s\n"
	    "\n瞷ぱㄏΤ %d \n"
	    "\n计程 %d ぱㄏ:\n",
	    ctime4(&t), count, nReport);
    for (i = 0; i < nReport; ++i)
	fprintf(fp, "%15s %5d \n", SHM->userid[list[i][1] - 1], list[i][0]);
    if (i % 4 != 0) fputc('\n', fp);

    {
	FILE* changefp = fopen(BBSHOME "/log/changeangel.log", "r");
	if (changefp) {
	    remove(BBSHOME "/log/changeangel.log");

	    fputs("\n== セ㏄传ぱㄏ魁 ==\n", fp);
	    slurp(fp, changefp);
	    fclose(changefp);
	}
    }

    fputs("\n--\n\n  セ戈パ angel 祘Α玻ネ\n\n", fp);
    fclose(fp);

    strcpy(header.title, "ぱㄏ参璸戈");
    strcpy(header.owner, "よ参璸");
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
