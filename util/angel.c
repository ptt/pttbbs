/* $Id$ */
#include "bbs.h"

#ifndef PLAY_ANGEL
int main(){ return 0; }
#else

int total[MAX_USERS];
int reject[4];
int rej_question;

int ListCmp(const void * a, const void * b){
    return *(int*)b - *(int*)a;
}

int main(int argc, char* argv[]){
    char* mailto = "SYSOP";
    int (*list)[2];
    int i, j;
    time_t t;
    int count;
    int nReport = 100;
    userec_t user;
    FILE* fp = fopen(BBSHOME "/.PASSWDS", "rb");
    int pipefd[2];

    if(argc > 1)
	mailto = argv[1];
    if(argc > 2)
	nReport = atoi(argv[2]);
    attach_SHM();

    j = count = 0;
    while(fread(&user, sizeof(userec_t), 1, fp) == 1){
	++j; /* j == uid */
	if(user.myangel[0]){
	    i = searchuser(user.myangel);
	    if(i)
		++total[i];
	}
	if(user.userlevel & PERM_ANGEL){
	    ++count;
	    ++reject[((user.uflag2 & ANGEL_MASK) >> 12)];
	    ++total[j]; /* make all angel have total > 0 */
	    if(user.uflag2 & REJ_QUESTION)
		++rej_question;
	}
    }
    fclose(fp);

    if(nReport > count)
	nReport = count;

    list = (int(*)[2]) malloc(count * sizeof(int[2]));
    j = 0;
    for(i = 0; i < MAX_USERS; ++i)
	if(total[i]){
	    list[j][0] = total[i] - 1;
	    list[j][1] = i;
	    ++j;
	}

    qsort(list, count, sizeof(int[2]), ListCmp);

    /* Ok, let's send the result. */
    pipe(pipefd);
    if((i = fork()) == -1){
	fprintf(stderr, "fork error: %s\nDidn't send the result.\n",
		strerror(errno));
	return 0;
    }else if(i == 0){
	/* child */
	dup2(pipefd[0], 0);
	close(pipefd[0]); /* use 0 instead of pipefd[0], don't need any more */
	close(pipefd[1]); /* close write site */
	execlp("bbsmail", "bbsmail", mailto, NULL);
	fprintf(stderr, "execlp error: %s\nDidn't send the result.\n",
		strerror(errno));
	return 0;
    }
    /* parent, don't need read site */
    close(pipefd[0]);
    fp = fdopen(pipefd[1], "w");

    fprintf(fp, "Subject: 小天使統計資料\n"
	    "From: 站方統計\n"
	    "\n現在全站小天使有 %d 位\n"
	    "\n小主人數最多的 %d 位小天使:\n", count, nReport);
    for(i = 0; i < nReport; ++i)
	fprintf(fp, "%15s%3d 人\n", SHM->userid[list[i][1] - 1], list[i][0]);
    fprintf(fp, "\n現在男女皆收的小天使有 %d 位\n"
	    "只收女生的有 %d 位  (共 %d 位收女生)\n"
	    "只收男生的有 %d 位  (共 %d 位收男生)\n"
	    "不接受新小天使的有 %d 位\n",
	    reject[0], reject[1], reject[1] + reject[0],
	    reject[2], reject[2] + reject[0], reject[3]);
    fprintf(fp, "\n現在開放小主人問問題的小天使有 %d 位\n"
	    "不開放的有 %d 位\n",
	    count - rej_question, rej_question);

    time(&t);
    fprintf(fp, "\n--\n\n"
	    "  本資料由 angel 程式產生於 %s\n",
	    ctime(&t));
    fclose(fp);
    return 0;
}

#endif /* defined PLAY_ANGEL */
