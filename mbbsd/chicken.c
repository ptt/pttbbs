#include "bbs.h"

#define NUM_KINDS   15		/* 有多少種動物 */
#define CHICKENLOG  "etc/chicken"
#define CHICKEN_PIC "etc/chickens"

static const char * const cage[17] = {
    "誕生", "週歲", "幼年", "少年", "青春", "青年",
    "青年", "活力", "壯年", "壯年", "壯年", "中年",
    "中年", "老年", "老年", "老摳摳", "古希"};
static const char * const chicken_type[NUM_KINDS] = {
    "小雞", "美少女", "勇士", "蜘蛛",
    "恐龍", "老鷹", "貓", "蠟筆小新",
    "狗狗", "惡魔", "忍者", "ㄚ扁",
    "馬英九", "就可人", "蘿莉"};
static const char * const chicken_food[NUM_KINDS] = {
    "飼料", "營養厚片", "雞排便當", "昆蟲",
    "肉", "肉", "貓餅乾", "小熊餅乾",
    "狗食", "靈氣", "飯團", "便當",
    "雞腿", "笑話文章", "水果沙拉"};
static const int egg_price[NUM_KINDS] = {
    5, 25, 30, 40,
    80, 50, 15, 35,
    17, 100, 85, 200,
    200, 100, 77};
static const int food_price[NUM_KINDS] = {
    4, 6, 8, 10,
    12, 12, 5, 6,
    5, 20, 15, 23,
    23, 10, 19};

enum {
    OO, FOOD, WEIGHT, DIRTY, BOOK, HAPPY, SATIS, TIREDSTRONG, SICK, HP_MAX,
    MAX_CHANGE
};

static const short time_change[NUM_KINDS][MAX_CHANGE] =
     /* 補品 食物 體重 髒亂 學問 快樂 滿意 疲勞 病氣 滿血 */
{
    /* 雞   */ {1, 1,   30,  3,  3, 40,  9,   7, 3,   30},
    /* 少女 */ {1, 1,  110,  1, 41, 20,  9,  25, 7,  110},
    /* 勇士 */ {1, 1,  200,  5, 33, 20, 15,  27, 1,  200},
    /* 蜘蛛 */ {1, 1,   10,  5,  1,  5,  3,   4, 1,   10},
    /* 恐龍 */ {1, 1, 1000,  9,  4, 12,  3, 200, 1, 1000},
    /* 老鷹 */ {1, 1,   90,  7,  4, 12,  3,  20, 5,   90},
    /* 貓   */ {1, 1,   30,  5,  4,  8,  3,   7, 4,   30},
    /* 小新 */ {1, 1,  100,  9, 20, 50, 10,  24, 4,  100},
    /* 狗   */ {1, 1,   45,  8,  3, 40, 20,   9, 5,   45},
    /* 惡魔 */ {1, 1,   45, 10,  5, 21, 11,   9, 5,   45},
    /* 忍者 */ {1, 1,   45,  2, 25,  1,  1,   9, 5,   45},
    /* 阿扁 */ {1, 1,  150,  4, 95, 25,  7,  25, 5,  175},
    /* 騜   */ {1, 1,  147,  2, 85, 20,  4,  25, 5,  145},
    /* 就可 */ {1, 1,  200,  3, 50, 50, 10,  10, 2,  300},
    /* 蘿莉 */ {1, 1,   80,  2,  2,  5,  7,  12, 1,  135},
};

static void
show_chicken_picture(const char *fpath)
{
    // TODO change 5 to 4?
    show_file(fpath, 5, 14, SHOWFILE_ALLOW_ALL);
}

#ifdef HAVE_CHICKEN_CS
// External "common sense" module.
#include "chicken_cs.c"
#endif

static chicken_t*
load_live_chicken(const char *uid)
{
    char fn[PATHLEN];
    int fd = 0;
    chicken_t *p = NULL;

    if (!uid || !uid[0]) return NULL;
    sethomefile(fn, uid, FN_CHICKEN);
    if (!dashf(fn)) return NULL;
    fd = open(fn, O_RDWR);
    if (fd < 0) return NULL;

    // now fd is valid. open and mmap.
    p = mmap(NULL, sizeof(chicken_t), PROT_READ|PROT_WRITE, MAP_SHARED,
	    fd, 0);
    close(fd);
    return p;
}

static int
load_chicken(const char *uid, chicken_t *mychicken)
{
    char fn[PATHLEN];
    int fd = 0;

    memset(mychicken, 0, sizeof(chicken_t));
    if (!uid || !uid[0]) return 0;
    sethomefile(fn, uid, FN_CHICKEN);
    if (!dashf(fn)) return 0;
    fd = open(fn, O_RDONLY);
    if (fd < 0) return 0;
    if (read(fd, mychicken, sizeof(chicken_t)) > 0 && mychicken->name[0]) {
	close(fd);
	return 1;
    }
    close(fd);
    return 0;
}

static void
free_live_chicken(chicken_t *p)
{
    if (!p) return;
    munmap(p, sizeof(chicken_t));
}

static int
new_chicken(void)
{
    chicken_t mychicken;
    int  price, i;
    int fd;
    char fn[PATHLEN];
    const short *delta = NULL;

    memset(&mychicken, 0, sizeof(chicken_t));

    clear();
    move(2, 0);
    outs("歡迎光臨 " ANSI_COLOR(33) "◎" ANSI_COLOR(37;44) " "
	 BBSMNAME "寵物市場 " ANSI_COLOR(33;40) "◎" ANSI_RESET ".. "
	 "目前蛋價：\n"
	 "(a)小雞 $5   (b)美少女 $25  (c)勇士    $30  (d)蜘蛛  $40  "
	 "(e)恐龍 $80\n"
	 "(f)老鷹 $50  (g)貓     $15  (h)蠟筆小新$35  (i)狗狗  $17  "
	 "(j)惡魔 $100\n"
	 "(k)忍者 $85  (n)就可人$100  (m)蘿莉    $77\n"
	 "[0]不想買了 $0\n");
    i = vans("請選擇你要養的動物：");

    // since (o) is confusing to some people, we alias 'm' to 'o'.
    if (i == 'm') i = 'o';

    // (m, l) were political person.
    // do not make them in a BBS system...
    if (i == 'm' || i == 'l')
	return 0;

    i -= 'a';
    if (i < 0 || i > NUM_KINDS - 1)
	return 0;

    mychicken.type = i;

    price = egg_price[(int)mychicken.type];
    reload_money();
    if (cuser.money < price) {
	vmsgf(MONEYNAME "不夠買蛋(要 %d)", price);
	return 0;
    }

    while (strlen(mychicken.name) < 3)
    {
	getdata(8, 0, "幫牠取個好名字：", mychicken.name,
		sizeof(mychicken.name), DOECHO);
    }

    delta = time_change[(int)mychicken.type];
    mychicken.lastvisit = mychicken.birthday = mychicken.cbirth = now;
    mychicken.weight = delta[WEIGHT] / 3;
    mychicken.book = delta[BOOK];
    mychicken.happy = delta[HAPPY];
    mychicken.satis = delta[SATIS];
    mychicken.hp = delta[WEIGHT];
    mychicken.hp_max = delta[WEIGHT];

    reload_money();
    if (cuser.money < price)
    {
	vmsg("錢不夠了。");
	return 0;
    }
    pay(price, "寵物蛋");

    // flush it
    setuserfile(fn, FN_CHICKEN);
    fd = OpenCreate(fn, O_WRONLY);
    if (fd < 0)
    {
	vmsg("系統錯誤: 無法建立資料，請至 " BN_BUGREPORT " 報告。");
	return 0;
    }

    write(fd, &mychicken, sizeof(chicken_t));
    close(fd);

    // log data
    log_filef(CHICKENLOG, LOG_CREAT,
              ANSI_COLOR(31) "%s " ANSI_RESET
              "養了一隻叫" ANSI_COLOR(33) " %s " ANSI_RESET "的 "
              ANSI_COLOR(32) "%s" ANSI_RESET "  於 %s\n", cuser.userid,
              mychicken.name, chicken_type[(int)mychicken.type], Cdate(&now));
    return 1;
}

static void
show_chicken_stat(const chicken_t * thechicken, int age)
{
    struct tm ptime;
    char hp_buf[STRLEN];

    // line 1: status
    // line 2: buff
    // line 3: debuff

    localtime4_r(&thechicken->birthday, &ptime);
    prints("名字: " ANSI_COLOR(33) "%s" ANSI_RESET
           " (" ANSI_COLOR(32) "%s" ANSI_RESET ")%*s"
           "生日:" ANSI_COLOR(33) "%d" ANSI_RESET "年"
           ANSI_COLOR(33) "%d" ANSI_RESET "月"
           ANSI_COLOR(33) "%d" ANSI_RESET "日 "
	   "(" ANSI_COLOR(32) "%s %d歲" ANSI_RESET ")\n",
	   thechicken->name, chicken_type[(int)thechicken->type],
           (int)(30 - strlen(thechicken->name) -
            strlen(chicken_type[(int)thechicken->type])),
           "", ptime.tm_year + 1900, ptime.tm_mon + 1, ptime.tm_mday,
	   cage[age > 16 ? 16 : age], age);

    snprintf(hp_buf, sizeof(hp_buf), "%d / %d", thechicken->hp,
             thechicken->hp_max);

    prints( "體力:" ANSI_COLOR(33) " %-19s" ANSI_RESET
           " 體重:" ANSI_COLOR(33) "%-7.1f" ANSI_RESET
           " 食物:" ANSI_COLOR(36) "%-7d" ANSI_RESET
           " 藥品:" ANSI_COLOR(36) "%-7d" ANSI_RESET
           " 補品:" ANSI_COLOR(36) "%-7d" ANSI_RESET
           "\n",
           hp_buf,
	   ((float)(thechicken->hp_max + (thechicken->weight / 50))) / 100,
           thechicken->food,
           thechicken->medicine,
	   thechicken->oo);

    prints( "快樂:" ANSI_COLOR(32) "%-7d" ANSI_RESET
           " 滿意:" ANSI_COLOR(32) "%-7d" ANSI_RESET
           " 學問:" ANSI_COLOR(32) "%-7d" ANSI_RESET
           " 髒亂:" ANSI_COLOR(31) "%-7d" ANSI_RESET
           " 生病:" ANSI_COLOR(31) "%-7d" ANSI_RESET
           " 疲勞:" ANSI_COLOR(31) "%-7d" ANSI_RESET
           "\n",
	   thechicken->happy,
           thechicken->satis,
           thechicken->book,
           thechicken->dirty,
	   thechicken->sick,
           thechicken->tired);
}

static void
show_chicken_data(chicken_t * thechicken)
{
    char            buf[1024];
    int age = ((now - thechicken->cbirth) / (60 * 60 * 24));
    if (age < 0) {
	thechicken->birthday = thechicken->cbirth = now - 10 * (60 * 60 * 24);
	age = 10;
    }

    /* Ptt:debug */
    thechicken->type %= NUM_KINDS;
    clear();
    showtitle(BBSMNAME2 "養雞場", BBSName);
    move(1, 0);

    show_chicken_stat(thechicken, age);
    snprintf(buf, sizeof(buf), CHICKEN_PIC "/%c%d", thechicken->type + 'a',
	     age > 16 ? 16 : age);
    show_chicken_picture(buf);
    move(18, 0);

    // status line - try harder to not exceed column 80.

    if (thechicken->sick > thechicken->hp / 5)
	outs(ANSI_COLOR(5;31) "病重!" ANSI_RESET);
    else if (thechicken->sick)
	outs("生病了.");

    if (thechicken->dirty > 150)
	outs(ANSI_COLOR(31) "又臭又髒." ANSI_RESET);
    else if (thechicken->dirty > 80)
	outs("有點髒.");
    else if (thechicken->dirty < 20)
	outs(ANSI_COLOR(32) "很乾淨." ANSI_RESET);

    if (thechicken->weight > thechicken->hp_max * 4)
	outs(ANSI_COLOR(31) "快撐死了." ANSI_RESET);
    else if (thechicken->weight > thechicken->hp_max * 3)
	outs(ANSI_COLOR(32) "飽嘟嘟." ANSI_RESET);
    else if (thechicken->weight < (thechicken->hp_max / 4))
	outs(ANSI_COLOR(31) "快餓死了." ANSI_RESET);
    else if (thechicken->weight < (thechicken->hp_max / 2))
	outs("餓了.");

    if (thechicken->tired > thechicken->hp * 1.7)
	outs(ANSI_COLOR(31) "累到昏迷." ANSI_RESET);
    else if (thechicken->tired > thechicken->hp)
	outs("累了.");
    else if (thechicken->tired < thechicken->hp / 4)
	outs(ANSI_COLOR(32) "精力旺盛." ANSI_RESET);

    if (thechicken->hp < thechicken->hp_max / 4)
	outs(ANSI_COLOR(31) "體力用盡." ANSI_RESET);

    if (thechicken->happy > 500)
	outs(ANSI_COLOR(32) "很快樂." ANSI_RESET);
    else if (thechicken->happy < 100)
	outs("不快樂.");

    if (thechicken->satis > 500)
	outs(ANSI_COLOR(32) "很滿足." ANSI_RESET);
    else if (thechicken->satis < 50)
	outs("不滿足.");
}

// Actions

static void
ch_eat(chicken_t *mychicken)
{
    if (mychicken->food) {
	mychicken->weight += time_change[(int)mychicken->type][WEIGHT] +
	mychicken->hp_max / 5;
	mychicken->tired +=
	    time_change[(int)mychicken->type][TIREDSTRONG] / 2;
	mychicken->hp_max++;
	mychicken->happy += 5;
	mychicken->satis += 7;
	mychicken->food--;
	move(10, 10);

	show_chicken_picture(CHICKEN_PIC "/eat");
	pressanykey();
    }
}

static void
ch_clean(chicken_t *mychicken)
{
    mychicken->dirty = 0;
    mychicken->tired +=
	time_change[(int)mychicken->type][TIREDSTRONG] / 3;
    show_chicken_picture(CHICKEN_PIC "/clean");
    pressanykey();
}

static void
ch_guess(chicken_t *mychicken)
{
    char           *guess[3] = {"剪刀", "石頭", "布"}, me, ch, win;

    mychicken->happy += time_change[(int)mychicken->type][HAPPY] * 1.5;
    mychicken->satis += time_change[(int)mychicken->type][SATIS];
    mychicken->tired += time_change[(int)mychicken->type][TIREDSTRONG];
    move(20, 0);
    clrtobot();
    outs("你要出[" ANSI_COLOR(32) "1" ANSI_RESET "]" ANSI_COLOR(33) "剪刀"
         ANSI_RESET "(" ANSI_COLOR(32) "2" ANSI_RESET ")"
	 ANSI_COLOR(33) "石頭" ANSI_RESET "(" ANSI_COLOR(32) "3" ANSI_RESET ")"
         ANSI_COLOR(33) "布" ANSI_RESET ":\n");
    me = vkey();
    me -= '1';
    if (me > 2 || me < 0)
	me = 0;
    win = (int)(3.0 * random() / (RAND_MAX + 1.0)) - 1;
    ch = (me + win + 3) % 3;
    prints("%s:%s !      %s:%s !.....%s",
	   cuser.userid, guess[(int)me], mychicken->name, guess[(int)ch],
	   win == 0 ? "平手" : win < 0 ? "耶..贏了 :D!!" : "嗚..我輸了 :~");
    pressanykey();
}

static void
ch_book(chicken_t *mychicken)
{
    mychicken->book += time_change[(int)mychicken->type][BOOK];
    mychicken->tired += time_change[(int)mychicken->type][TIREDSTRONG];
    show_chicken_picture(CHICKEN_PIC "/read");
    pressanykey();
}

static void
ch_kiss(chicken_t *mychicken)
{
    mychicken->happy += time_change[(int)mychicken->type][HAPPY];
    mychicken->satis += time_change[(int)mychicken->type][SATIS];
    mychicken->tired +=
	time_change[(int)mychicken->type][TIREDSTRONG] / 2;
    show_chicken_picture(CHICKEN_PIC "/kiss");
    pressanykey();
}

static void
ch_hit(chicken_t *mychicken)
{
    mychicken->weight -= mychicken->hp_max / 15;
    mychicken->hp -= (int)((float)time_change[(int)mychicken->type][HP_MAX] *
			   random() / (RAND_MAX + 1.0)) / 2 + 1;

    if (mychicken->book > 2)
	mychicken->book -= 2;
    if (mychicken->happy > 2)
	mychicken->happy -= 2;
    if (mychicken->satis > 2)
	mychicken->satis -= 2;
    mychicken->tired += time_change[(int)mychicken->type][TIREDSTRONG];
    show_chicken_picture(CHICKEN_PIC "/hit");
    pressanykey();
}

static void
ch_buyitem(int money, const char *picture, int *item, chicken_t *mychicken GCC_UNUSED)
{
    int num = 0;
    char buf[5];
    char prompt[STRLEN];

#ifdef HAVE_CHICKEN_CS
    if (ch_buyitem_cs(money, item, mychicken))
        return;
#endif
    snprintf(prompt, sizeof(prompt),
             "單價 $%d " MONEYNAME "，要買多少份呢: ", money);

    getdata_str(b_lines - 1, 0, prompt, buf, sizeof(buf), NUMECHO, "1");
    num = atoi(buf);
    if (num < 1)
	return;
    reload_money();
    if (cuser.money/money >= num) {
	*item += num;
        pay(money * num, "寵物商店");
	show_chicken_picture(picture);
        pressanykey();
    } else {
	vmsg("現金不夠 !!!");
    }
    usleep(100000); // sleep 0.1s
}

static void
ch_eatoo(chicken_t *mychicken)
{
    if (mychicken->oo > 0) {
	mychicken->oo--;
	mychicken->tired = 0;
	if (mychicken->happy > 5)
	    mychicken->happy -= 5;
	show_chicken_picture(CHICKEN_PIC "/oo");
	pressanykey();
    }
}

static void
ch_eatmedicine(chicken_t *mychicken)
{
    if (mychicken->medicine > 0) {
	mychicken->medicine--;
	mychicken->sick = 0;
	if (mychicken->hp_max > 10)
	    mychicken->hp_max -= 3;
	mychicken->hp = mychicken->hp_max;
	if (mychicken->happy > 10)
	    mychicken->happy -= 10;
	show_chicken_picture(CHICKEN_PIC "/medicine");
	pressanykey();
    }
}

static void
ch_kill(chicken_t *mychicken)
{
    int        ans;

    ans = vans("棄養要被罰 100 元, 是否要棄養?(y/N)");
    if (ans == 'y') {

	pay(100, "棄養寵物費");
	more(CHICKEN_PIC "/deadth", YEA);
	log_filef(CHICKENLOG, LOG_CREAT,
		 ANSI_COLOR(31) "%s " ANSI_RESET "把 "
                 ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(32) " %s "
		 ANSI_RESET "宰了 於 %s\n", cuser.userid, mychicken->name,
		 chicken_type[(int)mychicken->type], Cdate(&now));
	mychicken->name[0] = 0;
    }
}

static void
ch_getting_old(int *hp, int *weight, int diff, int age)
{
    float           ex = 0.9;

    if (age > 70)
	ex = 0.1;
    else if (age > 30)
	ex = 0.5;
    else if (age > 20)
	ex = 0.7;

    diff /= 60 * 6;
    while (diff--) {
	*hp *= ex;
	*weight *= ex;
    }
}

static void debug_rpt(const char *msg GCC_UNUSED, chicken_t *c GCC_UNUSED) {
#ifdef DBG_CHICKEN
    if (!msg) {
        msg = "";
        clear();
    }
    if (c) {
        prints("%s hp:%d/%d weight:%d sick:%d dirty:%d\n",
               msg, c->hp, c->hp_max, c->weight, c->sick, c->dirty);
    } else {
        vmsg(msg);
    }
#endif
}

static void debug_timediff(chicken_t *c GCC_UNUSED) {
#ifdef DBG_CHICKEN
    char buf[7];
    vs_hdr2("寵物", "測試");
    getdata(2, 0, "要模擬幾小時未進入的狀態？", buf, sizeof(buf), NUMECHO);
    syncnow();
    if (*buf)
        c->lastvisit = now - atoi(buf) * 60 * 60;
#endif
}

/* 依時間變動的資料 */
static void
time_diff(chicken_t * thechicken)
{
    int             diff;
    int             theage = ((now - thechicken->cbirth) / DAY_SECONDS);
    const short *delta = time_change[(int)thechicken->type];

    thechicken->type %= NUM_KINDS;
    diff = (now - thechicken->lastvisit) / 60;

    if (diff < 1)
	return;

    debug_rpt(NULL, thechicken);

    if (theage > 13)		/* 老死 */
	ch_getting_old(&thechicken->hp_max, &thechicken->weight, diff, theage);
    debug_rpt("AFTER getting_old", thechicken);

    thechicken->lastvisit = now;
    // 注意: 從前有個腦殘把顯示的 weight 改成 (max_hp + weight / 50) / 100
    // 加上從前的老化公式導致一堆人的寵都是 weight=1 ，從此變出了所謂暗黑養法。
    // 一堆爛掉失衡的公式無從修起，就亂搞吧。
    thechicken->weight -= thechicken->hp_max * diff / 540;	/* 體重 */
    if (thechicken->weight < 1) {
	thechicken->sick -= thechicken->weight / 10;	/* 餓得病氣上升 */
	thechicken->weight = 1;
    }
    debug_rpt("AFTER weight-hp_max", thechicken);

    /* 快樂度 */
    thechicken->happy -= diff / 60;
    if (thechicken->happy < 0)
	thechicken->happy = 0;

    /* 學問 */
    thechicken->book -= delta[BOOK] * diff / (60 * 32);
    if (thechicken->book < 0)
	thechicken->book = 0;

    /* 滿意度 */
    thechicken->satis -= (diff / 180.0 * delta[SATIS]);
    if (thechicken->satis < 0)
	thechicken->satis = 0;

    /* 髒亂 */
    thechicken->dirty += diff * delta[DIRTY] / 30;
    debug_rpt("AFTER dirty+=diff*delta/30", thechicken);

    /* 生病(髒亂/體重) */
    if (thechicken->dirty > 1000)
	thechicken->sick += (thechicken->dirty - 400) / 10;
    if (thechicken->weight > 1)
	thechicken->sick -= diff / 60;
    debug_rpt("AFTER weight>1?sick-=diff/60", thechicken);

    /* 病氣恢護 */
    if (thechicken->sick < 0)
	thechicken->sick = 0;
    thechicken->tired -= (diff * delta[TIREDSTRONG] / 4);

    /* 疲勞 */
    if (thechicken->tired < 0)
	thechicken->tired = 0;

    /* hp_max */
    if (thechicken->hp >= thechicken->hp_max / 2)
	thechicken->hp_max += delta[HP_MAX] * diff / (60 * 12);
    /* hp恢護 */
    if (!thechicken->sick)
	thechicken->hp += delta[HP_MAX] * diff / (60 * 6);
    if (thechicken->hp > thechicken->hp_max)
	thechicken->hp = thechicken->hp_max;
    debug_rpt("AFTER hp-recovery", thechicken);
    debug_rpt("time_diff", NULL);
}

static void
check_sick(chicken_t *mychicken)
{
    /* 髒病的 */
    if (mychicken->tired > mychicken->hp * 0.3 && mychicken->dirty > 150)
	mychicken->sick += (mychicken->dirty - 150) / 10;
    /* 累病的 */
    if (mychicken->tired > mychicken->hp * 1.3)
	mychicken->sick += time_change[(int)mychicken->type][SICK];
    /* 病氣太重還做事減hp */
    if (mychicken->sick > mychicken->hp / 5) {
	mychicken->hp -= (mychicken->sick - mychicken->hp / 5) / 4;
	if (mychicken->hp < 0)
	    mychicken->hp = 0;
    }
}

static int
revive_chicken(chicken_t *thechicken, int admin)
{
    int c = 0;
    assert(thechicken);
    // check deadtype for what to do

    strlcpy(thechicken->name, "[撿回來的]", sizeof(thechicken->name));

    c = thechicken->hp_max / 5 +1;
    if (c < 2)	    c = 2;
    if (c > thechicken->hp_max) c = thechicken->hp_max;
    thechicken->hp = c;
    if (admin)
	thechicken->hp = thechicken->hp_max;

    thechicken->weight = thechicken->hp; // weight = 1 時 sick 有機再次死亡
    if (thechicken->weight < 2)	// 避免病死或餓死
	thechicken->weight = 2;

    thechicken->satis = 2; // 滿意降低
    thechicken->tired = thechicken->hp; // 若歸零則過太爽

    if (admin)
    {
	thechicken->sick = 0;
	thechicken->tired = 0;
    }

    thechicken->lastvisit = now; // really need so?
#ifdef HAVE_CHICKEN_CS
    revive_chicken_cs(thechicken);
#endif
    return 0;
}

enum DEADTYPE {
    DEADTYPE_NOTYET,
    DEADTYPE_NOHP,
    DEADTYPE_TOOTIRED,
    DEADTYPE_TOOFAT,
    DEADTYPE_NOFOOD,
    DEADTYPE_NOSATIS,
    DEADTYPE_UNKNOWN,
};

static int
deadtype(const chicken_t * thechicken, chicken_t *mychicken)
{
    int             i;

    if (thechicken->hp <= 0) // hp用盡
	i = DEADTYPE_NOHP;
    else if (thechicken->tired > thechicken->hp * 3) // 操勞過度
	i = DEADTYPE_TOOTIRED;
    else if (thechicken->weight > thechicken->hp_max * 5) // 肥胖過度
	i = DEADTYPE_TOOFAT;
    else if (thechicken->weight == 1 &&
	     thechicken->sick > thechicken->hp_max / 4) // 餓死了
	i = DEADTYPE_NOFOOD;
    else if (thechicken->satis <= 0) // 很不滿意
	i = DEADTYPE_NOSATIS;
    else
	return DEADTYPE_NOTYET;

    if (thechicken == mychicken) {
	log_filef(CHICKENLOG, LOG_CREAT,
                 ANSI_COLOR(31) "%s" ANSI_RESET " 所疼愛的"
                 ANSI_COLOR(33) " %s" ANSI_COLOR(32) " %s "
                 ANSI_RESET "掛了 於 %s\n", cuser.userid, thechicken->name,
                 chicken_type[(int)thechicken->type], Cdate(&now));
	mychicken->name[0] = 0;
    }
    return i;
}

static int
showdeath(int type)
{
    char fn[MAXPATHLEN];
    const char *death_file_names[] = {
        "nohp",
        "tootired",
        "toofat",
        "nofood",
        "nosatis",
    };
    if (type <= DEADTYPE_NOTYET || type >= DEADTYPE_UNKNOWN)
        return 0;

    snprintf(fn, sizeof(fn), CHICKEN_PIC "/%s", death_file_names[type - 1]);
    more(fn, YEA);
    // "deadth" here is a typo, but let's take it (otherwise we need to also
    // change resource file name).
    more(CHICKEN_PIC "/deadth", YEA);
    return type;
}

static int
isdeath(const chicken_t * thechicken, chicken_t *mychicken)
{
    int             i;

    if (!(i = deadtype(thechicken, mychicken)))
	return 0;
    return showdeath(i);
}

static void
ch_changename(chicken_t *mychicken)
{
    char      newname[20] = "";

    getdata_str(b_lines - 1, 0, "改個好名字吧: ", newname, sizeof(newname)-1,
                DOECHO, mychicken->name);

    if (strlen(newname) >= 3 && strcmp(newname, mychicken->name)) {
	strlcpy(mychicken->name, newname, sizeof(mychicken->name));
	log_filef(CHICKENLOG, LOG_CREAT,
                ANSI_COLOR(31) "%s" ANSI_RESET " 把疼愛的" ANSI_COLOR(33)
                " %s" ANSI_COLOR(32) " %s "
                ANSI_RESET "改名為" ANSI_COLOR(33) " %s" ANSI_RESET " 於 %s\n",
                 cuser.userid, mychicken->name,
                 chicken_type[(int)mychicken->type], newname, Cdate(&now));
    }
}

static int
select_menu(int age GCC_UNUSED, chicken_t *mychicken)
{
    char            ch;

    reload_money();
    move(19, 0);

    vbarf(ANSI_COLOR(44;37) " " MONEYNAME ":" ANSI_COLOR(33) " %-10d"
#ifdef HAVE_CHICKEN_CS
          ANSI_COLOR(37) "  常識點數 :" ANSI_COLOR(33) " %-10d"
#endif
          , cuser.money
#ifdef HAVE_CHICKEN_CS
          , mychicken->commonsense
#endif
          );

    prints("\n" ANSI_COLOR(33) "(" ANSI_COLOR(37) "1" ANSI_COLOR(33) ")清理 "
           "(" ANSI_COLOR(37) "2" ANSI_COLOR(33) ")吃飯 "
	   "(" ANSI_COLOR(37) "3" ANSI_COLOR(33) ")猜拳   "
           "(" ANSI_COLOR(37) "4" ANSI_COLOR(33) ")唸書 "
	   "(" ANSI_COLOR(37) "5" ANSI_COLOR(33) ")親他 "
           "(" ANSI_COLOR(37) "6" ANSI_COLOR(33) ")打他 "
	   "(" ANSI_COLOR(37) "7" ANSI_COLOR(33) ")買%s "
           "(" ANSI_COLOR(37) "8" ANSI_COLOR(33) ")吃補品\n"
	   "(" ANSI_COLOR(37) "9" ANSI_COLOR(33) ")吃藥 "
	   "(" ANSI_COLOR(37) "m" ANSI_COLOR(33) ")買藥 "
           "(" ANSI_COLOR(37) "o" ANSI_COLOR(33) ")買補品 "
           "(" ANSI_COLOR(37) "k" ANSI_COLOR(33) ")棄養 "
	   "(" ANSI_COLOR(37) "n" ANSI_COLOR(33) ")改名 "
#ifdef HAVE_CHICKEN_CS
	   "(" ANSI_COLOR(37) "s" ANSI_COLOR(33) ")常識問答 "
#endif
	   "(" ANSI_COLOR(37) "q" ANSI_COLOR(33) ")離開:" ANSI_RESET,
	   chicken_food[(int)mychicken->type]);
    do {
	switch (ch = vkey()) {
	case '1':
	    ch_clean(mychicken);
	    check_sick(mychicken);
	    break;
	case '2':
	    ch_eat(mychicken);
	    check_sick(mychicken);
	    break;
	case '3':
	    ch_guess(mychicken);
	    check_sick(mychicken);
	    break;
	case '4':
	    ch_book(mychicken);
	    check_sick(mychicken);
	    break;
	case '5':
	    ch_kiss(mychicken);
	    break;
	case '6':
	    ch_hit(mychicken);
	    check_sick(mychicken);
	    break;
	case '7':
	    ch_buyitem(food_price[(int)mychicken->type], CHICKEN_PIC "/food",
		       &mychicken->food,  mychicken);
	    break;
	case '8':
	    ch_eatoo(mychicken);
	    break;
	case '9':
	    ch_eatmedicine(mychicken);
	    break;
	case 'O':
	case 'o':
	    ch_buyitem(100, CHICKEN_PIC "/buyoo", &mychicken->oo, mychicken);
	    break;
	case 'M':
	case 'm':
            ch_buyitem(10, CHICKEN_PIC "/buymedicine", &mychicken->medicine,
                       mychicken);
	    break;
	case 'N':
	case 'n':
	    ch_changename(mychicken);
	    break;
	case 'K':
	case 'k':
	    ch_kill(mychicken);
	    return 0;
	case 'Q':
	case 'q':
	    return 0;
#ifdef HAVE_CHICKEN_CS
	case 'S':
	case 's':
	    ch_teach(mychicken);
	    break;
#endif
	}
    } while (ch < ' ' || ch > 'z');
    return 1;
}

static int
recover_chicken(chicken_t * thechicken)
{
    char            buf[200];
    int             price = egg_price[(int)thechicken->type];
    int		    money = price;

    // money is a little less than price.
#ifdef HAVE_CHICKEN_CS
    if (recover_chicken_cs(thechicken))
        return 1;
#endif

    money = price + (random() % price);
    price *= 2;

    if (now - thechicken->lastvisit > (60 * 60 * 24 * 7))
	return 0;

    vs_hdr2(" 養雞場 ", " 復活寵物");
    prints("\n你有一個剛死亡不久的 %s 要招喚回來嗎? 只要 %d 元唷...\n",
           chicken_type[(int)thechicken->type], price);
    do {
	getdata(10, 0, " 要花錢復活寵物嗎？ [y/n]: ",
		buf, 2, LCECHO);
    }
    while (buf[0] != 'y' && buf[0] != 'n');

    if (buf[0] == 'n') {
        thechicken->lastvisit = 0;
        return 0;
    }

    reload_money();
    if (cuser.money < price) {
        vmsg("錢不夠喔，請籌錢後再來");
        return 0;
    }

    pay(money, "靈界守衛");
    revive_chicken(thechicken, 0);
    move(12, 0);
    prints("寵物已復活。 請記得加以餵食避免再度死亡。\n"
           "感謝您對寵物的愛心，復活費打折，僅收 $%d。\n", money);
    pressanykey();
    return 1;
}

void
chicken_toggle_death(const char *uid)
{
    chicken_t *mychicken = load_live_chicken(uid);

    assert(uid);
    if (!mychicken)
    {
	vmsgf("%s 沒養寵物。", uid);
    }
    else if (mychicken->name[0])
    {
	mychicken->name[0] = 0;
	vmsgf("%s 的寵物被殺死了", uid);
    }
    else
    {
	revive_chicken(mychicken, 1);
	strlcpy(mychicken->name, "[死]", sizeof(mychicken->name));
	vmsgf("%s 的寵物復活了", uid);
    }
    free_live_chicken(mychicken);
}

void
chicken_query(const char *userid)
{
    chicken_t xchicken;

    if (!load_chicken(userid, &xchicken))
    {
	move(1, 0);
	clrtobot();
	prints("\n\n%s 並沒有養寵物..", userid);
    } else {
        debug_timediff(&xchicken);
	time_diff(&xchicken);
	if (!isdeath(&xchicken, NULL))
	{
	    show_chicken_data(&xchicken);
#ifdef HAVE_CHICKEN_CS
            prints("常識點數: %d", xchicken.commonsense);
#endif
	    prints("\n\n以上是 %s 的寵物資料..", userid);
	} else {
	    move(1, 0);
	    clrtobot();
	    prints("\n\n%s 的寵物死掉了...", userid);
	}
    }

    pressanykey();
}


#define lockreturn0(unmode, state) if(lockutmpmode(unmode, state)) return 0

int
chicken_main(void)
{
    int age;
    chicken_t *mychicken;
    lockreturn0(CHICKEN, LOCK_MULTI);
    mychicken = load_live_chicken(cuser.userid);
    debug_timediff(mychicken);
    if (mychicken && !mychicken->name[0])
    {
	// possible for recovery
	recover_chicken(mychicken);
    }
    if (!mychicken || !mychicken->name[0])
    {
	free_live_chicken(mychicken);
	mychicken = NULL;

	// create new?
	if (new_chicken())
	    mychicken = load_live_chicken(cuser.userid);

	// exit if still no valid data.
	if (!mychicken || !mychicken->name[0])
	{
	    unlockutmpmode();
	    free_live_chicken(mychicken);
	    return 0;
	}
    }
    assert(mychicken);
    age = ((now - mychicken->cbirth) / DAY_SECONDS);
    do {
	time_diff(mychicken);
	if (isdeath(mychicken, mychicken))
	    break;
	show_chicken_data(mychicken);
    } while (select_menu(age, mychicken));
    unlockutmpmode();
    free_live_chicken(mychicken);
    return 0;
}
