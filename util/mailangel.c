/* $Id$ */
#include "bbs.h"

#ifndef PLAY_ANGEL
int main(){ return 0; }
#else

int total[MAX_USERS + 1];
int *list;
int count;
char *mailto = NULL;
char *mailfile = NULL;

void readData();
void sendResult();
void mailUser(char *userid);

int main(int argc, char* argv[]){
    if (argc < 2) {
	fprintf(stderr, "Usage: %s file [userid]\n", argv[0]);
	exit(1);
    }
    mailfile = argv[1];

    if (argc > 2)
	mailto = argv[2];

    readData();
    if (mailto)
	mailUser(mailto);
    else
	sendResult();

    return 0;
}

int mailalertuser(char* userid)
{
    userinfo_t *uentp=NULL;
    if (userid[0] && (uentp = search_ulist_userid(userid)))
	uentp->alerts|=ALERT_NEW_MAIL;
    return 0;
}      

void readData(){
    int i, j;
    userec_t user;
    FILE* fp;

    attach_SHM();

    fp = fopen(BBSHOME "/.PASSWDS", "rb");
    j = count = 0;
    while (fread(&user, sizeof(userec_t), 1, fp) == 1) {
	++j; /* j == uid */
	if (user.userlevel & PERM_ANGEL) {
	    ++count;
	    ++total[j]; /* make all angel have total > 0 */
	} else { /* don't have PERM_ANGEL */
	    total[j] = INT_MIN;
	}
    }
    fclose(fp);

    list = (int *) malloc(count * sizeof(int));
    j = 0;
    for (i = 1; i <= MAX_USERS; ++i)
	if (total[i] > 0) {
	    list[j] = i;
	    ++j;
	}
}

void sendResult(){
    int i;
    for (i = 0; i < count; ++i) {
	mailUser(SHM->userid[list[i] - 1]);
//	printf("%s\n", SHM->userid[list[i] - 1]);
    }
}

void mailUser(char *userid)
{
    int count;
    FILE *fp, *fp2;
    time4_t t;
    fileheader_t header;
    struct stat st;
    char filename[512];

    fp2 = fopen(mailfile, "r");
    if (fp2 == NULL) {
	fprintf(stderr, "Cannot open file %s\n", mailfile);
	return;
    }

    sprintf(filename, BBSHOME "/home/%c/%s", userid[0], userid);
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
    fprintf(fp, "作者: 小天使系統\n"
	    "標題: 給小天使的一封信\n"
	    "時間: %s\n",
	    ctime4(&t));

    while ((count = fread(filename, 1, sizeof(filename), fp2))) {
	fwrite(filename, 1, count, fp);
    }
    fclose(fp);
    fclose(fp2);

    strcpy(header.title, "給小天使的一封信");
    strcpy(header.owner, "小天使系統");
    sprintf(filename, BBSHOME "/home/%c/%s/.DIR", userid[0], userid);
    append_record(filename, &header, sizeof(header));
    mailalertuser(userid);
    printf("%s\n", userid);
}
#endif /* defined PLAY_ANGEL */
