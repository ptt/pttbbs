#include "bbs.h"

static void initDir() {
    Mkdir("adm");
    Mkdir("boards");
    Mkdir("etc");
    Mkdir("log");
    Mkdir("man");
    Mkdir("man/boards");
    Mkdir("out");
    Mkdir("tmp");
    Mkdir("run");
    Mkdir("jobspool");
}

static void initHome() {
    int i;
    char buf[256];
    
    Mkdir("home");
    strcpy(buf, "home/?");
    for(i = 0; i < 26; i++) {
	buf[5] = 'A' + i;
	Mkdir(buf);
	buf[5] = 'a' + i;
	Mkdir(buf);
#if 0
	/* in current implementation we don't allow 
	 * id as digits so we don't create now. */
	if(i >= 10)
	    continue;
	/* 0~9 */
	buf[5] = '0' + i;
	Mkdir(buf);
#endif
    }
}

static void initBoardsDIR() {
    int i;
    char buf[256];
    
    Mkdir("boards");
    strcpy(buf, "boards/?");
    for(i = 0; i < 26; i++) {
	buf[7] = 'A' + i;
	Mkdir(buf);
	buf[7] = 'a' + i;
	Mkdir(buf);
	if(i >= 10)
	    continue;
	/* 0~9 */
	buf[7] = '0' + i;
	Mkdir(buf);
    }
}

static void initManDIR() {
    int i;
    char buf[256];
    
    Mkdir("man");
    Mkdir("man/boards");
    strcpy(buf, "man/boards/?");
    for(i = 0; i < 26; i++) {
	buf[11] = 'A' + i;
	Mkdir(buf);
	buf[11] = 'a' + i;
	Mkdir(buf);
	if(i >= 10)
	    continue;
	/* 0~9 */
	buf[11] = '0' + i;
	Mkdir(buf);
    }
}

static void initPasswds() {
    int i;
    userec_t u;
    FILE *fp = fopen(".PASSWDS", "w");
    
    memset(&u, 0, sizeof(u));
    if(fp) {
	for(i = 0; i < MAX_USERS; i++)
	    fwrite(&u, sizeof(u), 1, fp);
	fclose(fp);
    }
}

static void newboard(FILE *fp, boardheader_t *b) {
    char buf[256];
    
    fwrite(b, sizeof(boardheader_t), 1, fp);
    sprintf(buf, "boards/%c/%s", b->brdname[0], b->brdname);
    Mkdir(buf);
    sprintf(buf, "man/boards/%c/%s", b->brdname[0], b->brdname);
    Mkdir(buf);
}

static void initBoards() {
    FILE *fp = fopen(".BRD", "w");
    boardheader_t b;
    
    if(fp) {
	memset(&b, 0, sizeof(b));

	strcpy(b.brdname, "0ClassRoot");
	strcpy(b.title, ".... �U���հQ�װ�");
	b.brdattr = BRD_GROUPBOARD;
	b.level = PERM_SYSOP;
	b.gid = 2;
	newboard(fp, &b);

	strcpy(b.brdname, "0_Group");
	strcpy(b.title, ".... �U�����F��  �m�����M�I,�D�H�i�ġn");
	b.brdattr = BRD_GROUPBOARD;
	b.level = PERM_SYSOP;
	b.gid = 1;
	newboard(fp, &b);

	strcpy(b.brdname, "A_Group");
	strcpy(b.title, ".... �U�����s��     ���i  ����  ���I");
	b.brdattr = BRD_GROUPBOARD;
	b.level = 0;
	b.gid = 1;
	newboard(fp, &b);

	strcpy(b.brdname, "SYSOP");
	strcpy(b.title, "�T�� �������n!");
	b.brdattr = BRD_POSTMASK;
	b.level = 0;
	b.gid = 3;
	newboard(fp, &b);

	strcpy(b.brdname, "junk");
	strcpy(b.title, "�o�q �����C���K���U��");
	b.brdattr = 0;
	b.level = PERM_SYSOP;
	b.gid = 2;
	newboard(fp, &b);
	
	strcpy(b.brdname, "Security");
	strcpy(b.title, "�o�q �������t�Φw��");
	b.brdattr = 0;
	b.level = PERM_SYSOP;
	b.gid = 2;
	newboard(fp, &b);

	strcpy(b.brdname, "ALLHIDPOST");
	strcpy(b.title, "�T�� ����O��LOCAL�s�峹(���O)");
	b.brdattr = BRD_POSTMASK | BRD_HIDE;
	b.level = PERM_SYSOP;
	b.gid = 2;
	newboard(fp, &b);

	strcpy(b.brdname, BN_ALLPOST);
	strcpy(b.title, "�T�� ����O��LOCAL�s�峹");
	b.brdattr = BRD_POSTMASK;
	b.level = PERM_SYSOP;
	b.gid = 3;
	newboard(fp, &b);
	
	strcpy(b.brdname, "deleted");
	strcpy(b.title, "�T�� ���귽�^����");
	b.brdattr = 0;
	b.level = PERM_BM;
	b.gid = 3;
	newboard(fp, &b);
	
	strcpy(b.brdname, "Note");
	strcpy(b.title, "�T�� ���ʺA�ݪO�κq����Z");
	b.brdattr = 0;
	b.level = 0;
	b.gid = 3;
	newboard(fp, &b);
	
	strcpy(b.brdname, "Record");
	strcpy(b.title, "�T�� ���ڭ̪����G");
	b.brdattr = 0 | BRD_POSTMASK;
	b.level = 0;
	b.gid = 3;
	newboard(fp, &b);
	
	
	strcpy(b.brdname, "WhoAmI");
	strcpy(b.title, "�T�� �������A�q�q�ڬO�֡I");
	b.brdattr = 0;
	b.level = 0;
	b.gid = 3;
	newboard(fp, &b);
	
	strcpy(b.brdname, "EditExp");
	strcpy(b.title, "�T�� ���d�����F��Z��");
	b.brdattr = 0;
	b.level = 0;
	b.gid = 3;
	newboard(fp, &b);

#ifdef BN_FIVECHESS_LOG
	strcpy(b.brdname, BN_FIVECHESS_LOG);
	strcpy(b.title, "���� ��" BBSNAME "���l���� ���W�什������");
	b.brdattr = BRD_POSTMASK;
	b.level = PERM_SYSOP;
	b.gid = 3;
	newboard(fp, &b);
#endif

	fclose(fp);
    }
}

static void initMan() {
    FILE *fp;
    fileheader_t f;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    
    memset(&f, 0, sizeof(f));
    strcpy(f.owner, "SYSOP");
    sprintf(f.date, "%2d/%02d", tm->tm_mon + 1, tm->tm_mday);
    f.multi.money = 0;
    f.filemode = 0;
    
    if((fp = fopen("man/boards/N/Note/.DIR", "w"))) {
	strcpy(f.filename, "SONGBOOK");
	strcpy(f.title, "�� �i�I �q �q ���j");
	fwrite(&f, sizeof(f), 1, fp);
	Mkdir("man/boards/N/Note/SONGBOOK");
	
	strcpy(f.filename, "SYS");
	strcpy(f.title, "�� <�t��> �ʺA�ݪO");
	fwrite(&f, sizeof(f), 1, fp);
	Mkdir("man/boards/N/Note/SYS");
		
	strcpy(f.filename, "SONGO");
	strcpy(f.title, "�� <�I�q> �ʺA�ݪO");
	fwrite(&f, sizeof(f), 1, fp);
	Mkdir("man/boards/N/Note/SONGO");

	strcpy(f.filename, "AD");
	strcpy(f.title, "�� <�s�i> �ʺA�ݪO");
	fwrite(&f, sizeof(f), 1, fp);
	Mkdir("man/boards/N/Note/AD");
	
	strcpy(f.filename, "NEWS");
	strcpy(f.title, "�� <�s�D> �ʺA�ݪO");
	fwrite(&f, sizeof(f), 1, fp);
	Mkdir("man/boards/N/Note/NEWS");
	
	fclose(fp);
    }
    
}

static void initSymLink() {
    symlink(BBSHOME "/man/boards/N/Note/SONGBOOK", BBSHOME "/etc/SONGBOOK");
    symlink(BBSHOME "/man/boards/N/Note/SONGO", BBSHOME "/etc/SONGO");
    symlink(BBSHOME "/man/boards/E/EditExp", BBSHOME "/etc/editexp");
}

static void initHistory() {
    FILE *fp = fopen("etc/history.data", "w");
    
    if(fp) {
	fprintf(fp, "0 0 0 0");
	fclose(fp);
    }
}

int main(int argc, char **argv)
{
    if( argc != 2 || strcmp(argv[1], "-DoIt") != 0 ){
	fprintf(stderr,
		"ĵ�i!  initbbs�u�Φb�u�Ĥ@���w�ˡv���ɭ�.\n"
		"�Y�z�����x�w�g�W�u,  initbbs�N�|�}�a���즳���!\n\n"
		"�N�� BBS �w�˦b " BBSHOME "\n\n"
		"�T�w�n����, �Шϥ� initbbs -DoIt\n");
	return 1;
    }

    if(chdir(BBSHOME)) {
	perror(BBSHOME);
	exit(1);
    }
    
    initDir();
    initHome();
    initBoardsDIR();
    initManDIR();
    initPasswds();
    initBoards();
    initMan();
    initSymLink();
    initHistory();
    
    return 0;
}
