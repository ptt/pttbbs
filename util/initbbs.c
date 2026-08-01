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
    STRLCPY(buf, "home/?");
    for(i = 0; i < 26; i++) {
	buf[5] = 'A' + i;
	Mkdir(buf);
	buf[5] = 'a' + i;
	Mkdir(buf);
    }
}

static void initBoardsDIR() {
    int i;
    char buf[256];
    
    Mkdir("boards");
    STRLCPY(buf, "boards/?");
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
    STRLCPY(buf, "man/boards/?");
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
    setbpath(buf, b->brdname);
    Mkdir(buf);
    setapath(buf, b->brdname);
    Mkdir(buf);
}

static void initBoards() {
    FILE *fp = fopen(".BRD", "w");
    boardheader_t b;
    
    if(fp) {
	memset(&b, 0, sizeof(b));
	
	STRLCPY(b.brdname, "SYSOP");
	STRLCPY(b.title, "嘰哩 ◎站長好!");
	b.brdattr = BRD_POSTMASK;
	b.level = 0;
	b.gid = 2;
	newboard(fp, &b);

	STRLCPY(b.brdname, "1...........");
	strcpy(b.title, ".... Σ中央政府  《高壓危險,非人可敵》");
	b.brdattr = BRD_GROUPBOARD;
	b.level = PERM_SYSOP;
	b.gid = 1;
	newboard(fp, &b);
	
	STRLCPY(b.brdname, "junk");
	STRLCPY(b.title, "發電 ◎雜七雜八的垃圾");
	b.brdattr = 0;
	b.level = PERM_SYSOP;
	b.gid = 2;
	newboard(fp, &b);
	
	STRLCPY(b.brdname, "Security");
	STRLCPY(b.title, "發電 ◎站內系統安全");
	b.brdattr = 0;
	b.level = PERM_SYSOP;
	b.gid = 2;
	newboard(fp, &b);
	
	STRLCPY(b.brdname, "2...........");
	STRLCPY(b.title, ".... Σ市民廣場     報告  站長  ㄜ！");
	b.brdattr = BRD_GROUPBOARD;
	b.level = 0;
	b.gid = 1;
	newboard(fp, &b);
	
	STRLCPY(b.brdname, BN_ALLPOST);
	STRLCPY(b.title, "嘰哩 ◎跨板式LOCAL新文章");
	b.brdattr = BRD_POSTMASK;
	b.level = PERM_SYSOP;
	b.gid = 5;
	newboard(fp, &b);
	
	STRLCPY(b.brdname, "deleted");
	STRLCPY(b.title, "嘰哩 ◎資源回收筒");
	b.brdattr = 0;
	b.level = PERM_BM;
	b.gid = 5;
	newboard(fp, &b);
	
	STRLCPY(b.brdname, "Note");
	STRLCPY(b.title, "嘰哩 ◎動態看板及歌曲投稿");
	b.brdattr = 0;
	b.level = 0;
	b.gid = 5;
	newboard(fp, &b);
	
	STRLCPY(b.brdname, "Record");
	STRLCPY(b.title, "嘰哩 ◎我們的成果");
	b.brdattr = 0 | BRD_POSTMASK;
	b.level = 0;
	b.gid = 5;
	newboard(fp, &b);
	
	
	STRLCPY(b.brdname, "WhoAmI");
	STRLCPY(b.title, "嘰哩 ◎呵呵，猜猜我是誰！");
	b.brdattr = 0;
	b.level = 0;
	b.gid = 5;
	newboard(fp, &b);
	
	STRLCPY(b.brdname, "EditExp");
	STRLCPY(b.title, "嘰哩 ◎範本精靈投稿區");
	b.brdattr = 0;
	b.level = 0;
	b.gid = 5;
	newboard(fp, &b);

	STRLCPY(b.brdname, "ALLHIDPOST");
	STRLCPY(b.title, "嘰哩 ◎跨板式LOCAL新文章(隱板)");
	b.brdattr = BRD_POSTMASK | BRD_HIDE;
	b.level = PERM_SYSOP;
	b.gid = 5;
	newboard(fp, &b);
	
#ifdef BN_FIVECHESS_LOG
	STRLCPY(b.brdname, BN_FIVECHESS_LOG);
	STRLCPY(b.title, "棋藝 ◎" BBSNAME "五子棋譜 站上對局全紀錄");
	b.brdattr = BRD_POSTMASK;
	b.level = PERM_SYSOP;
	b.gid = 5;
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
    STRLCPY(f.owner, "SYSOP");
    sprintf(f.date, "%2d/%02d", tm->tm_mon + 1, tm->tm_mday);
    f.multi.money = 0;
    f.filemode = 0;
    
    if((fp = fopen("man/boards/N/Note/.DIR", "w"))) {
	STRLCPY(f.filename, "SONGBOOK");
	STRLCPY(f.title, "◆ 【點 歌 歌 本】");
	fwrite(&f, sizeof(f), 1, fp);
	Mkdir("man/boards/N/Note/SONGBOOK");
	
	STRLCPY(f.filename, "SYS");
	STRLCPY(f.title, "◆ <系統> 動態看板");
	fwrite(&f, sizeof(f), 1, fp);
	Mkdir("man/boards/N/Note/SYS");
		
	STRLCPY(f.filename, "SONGO");
	STRLCPY(f.title, "◆ <點歌> 動態看板");
	fwrite(&f, sizeof(f), 1, fp);
	Mkdir("man/boards/N/Note/SONGO");

	STRLCPY(f.filename, "AD");
	STRLCPY(f.title, "◆ <廣告> 動態看板");
	fwrite(&f, sizeof(f), 1, fp);
	Mkdir("man/boards/N/Note/AD");
	
	STRLCPY(f.filename, "NEWS");
	STRLCPY(f.title, "◆ <新聞> 動態看板");
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
		"警告!  initbbs只用在「第一次安裝」的時候.\n"
		"若您的站台已經上線,  initbbs將會破壞掉原有資料!\n\n"
		"將把 BBS 安裝在 " BBSHOME "\n\n"
		"確定要執行, 請使用 initbbs -DoIt\n");
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
