/* $Id$ */
#include "bbs.h"

#define NUM_KINDS   15		/* 有多少種動物 */

static const char * const cage[17] = {
    "誕生", "週歲", "幼年", "少年", "青春", "青年",
    "青年", "活力", "壯年", "壯年", "壯年", "中年",
    "中年", "老年", "老年", "老摳摳", "古希"};
static const char * const chicken_type[NUM_KINDS] = {
    "小雞", "美少女", "勇士", "蜘蛛",
    "恐龍", "老鷹", "貓", "蠟筆小新",
    "狗狗", "惡魔", "忍者", "ㄚ扁",
    "馬英九", "就可人", "羅莉"};
static const char * const chicken_food[NUM_KINDS] = {
    "雞飼料", "營養厚片", "雞排便當", "死蝴蝶",
    "屍體", "小雞", "貓餅乾", "小熊餅乾",
    "寶錄", "靈氣", "飯團", "便當",
    "雞腿", "笑話文章", "水果沙拉"};
static          const int egg_price[NUM_KINDS] = {
    5, 25, 30, 40,
    80, 50, 15, 35,
    17, 100, 85, 200,
    200, 100, 77};
static          const int food_price[NUM_KINDS] = {
    4, 6, 8, 10,
    12, 12, 5, 6,
    5, 20, 15, 23,
    23, 10, 19};
static const char * const attack_type[NUM_KINDS] = {
    "啄", "鞭打", "槌", "咬",
    "撞擊", "啄", "抓", "踢",
    "咬", "燃燒", "暗擊", "棍打",
    "劍擊", "冷凍光線", "香吻一枚"};

static const char * const damage_degree[] = {
    "蚊子似的", "騷癢似的", "小力的", "輕微的",
    "有點疼的", "使力的", "傷人的", "重重的",
    "使全力的", "惡狠狠的", "危險的", "瘋狂的",
    "猛烈的", "狂風暴雨似的", "驚天動地的",
    "致命的", NULL};

enum {
    OO, FOOD, WEIGHT, CLEAN, RUN, ATTACK, BOOK, HAPPY, SATIS,
    TEMPERAMENT, TIREDSTRONG, SICK, HP_MAX, MM_MAX
};

static          const short time_change[NUM_KINDS][14] =
/* 補品 食物 體重 乾淨 敏捷 攻擊力 知識 快樂 滿意 氣質 疲勞 病氣 滿血 滿法 */
{
    /* 雞 */
    {1, 1, 30, 3, 8, 3, 3, 40, 9, 1, 7, 3, 30, 1},
    /* 美少女 */
    {1, 1, 110, 1, 4, 7, 41, 20, 9, 25, 25, 7, 110, 15},
    /* 勇士 */
    {1, 1, 200, 5, 4, 10, 33, 20, 15, 10, 27, 1, 200, 9},
    /* 蜘蛛 */
    {1, 1, 10, 5, 8, 1, 1, 5, 3, 1, 4, 1, 10, 30},
    /* 恐龍 */
    {1, 1, 1000, 9, 1, 13, 4, 12, 3, 1, 200, 1, 1000, 3},
    /* 老鷹 */
    {1, 1, 90, 7, 10, 7, 4, 12, 3, 30, 20, 5, 90, 20},
    /* 貓 */
    {1, 1, 30, 5, 5, 6, 4, 8, 3, 15, 7, 4, 30, 21},
    /* 蠟筆小新 */
    {1, 1, 100, 9, 7, 7, 20, 50, 10, 8, 24, 4, 100, 9},
    /* 狗 */
    {1, 1, 45, 8, 7, 9, 3, 40, 20, 3, 9, 5, 45, 1},
    /* 惡魔 */
    {1, 1, 45, 10, 11, 11, 5, 21, 11, 1, 9, 5, 45, 25},
    /* 忍者 */
    {1, 1, 45, 2, 12, 10, 25, 1, 1, 10, 9, 5, 45, 26},
    /* 阿扁 */
    {1, 1, 150, 4, 8, 13, 95, 25, 7, 10, 25, 5, 175, 85},
    /* 馬英九 */
    {1, 1, 147, 2, 10, 10, 85, 20, 4, 25, 25, 5, 145, 95},
    /* 就可人 */
    {1, 1, 200, 3, 15, 15, 50, 50, 10, 5, 10, 2, 300, 0},
    /* 羅利 */
    {1, 1, 80, 2, 9, 10, 2, 5, 7, 8, 12, 1, 135, 5},
};

int
reload_chicken(void)
{
    userec_t xuser;
    chicken_t *mychicken = &cuser.mychicken;

    passwd_query(usernum, &xuser);
    memcpy(mychicken, &xuser.mychicken, sizeof(chicken_t));
    if (!mychicken->name[0])
	return 0;
    else
	return 1;
}

#define CHICKENLOG  "etc/chicken"

static int
new_chicken(void)
{
    chicken_t *mychicken = &cuser.mychicken;
    int             price, i;

    clear();
    move(2, 0);
    outs("歡迎光臨 " ANSI_COLOR(33) "◎" ANSI_COLOR(37;44) " Ptt寵物市場 " ANSI_COLOR(33;40) "◎" ANSI_RESET ".. "
	 "目前蛋價：\n"
	 "(a)小雞 $5   (b)美少女 $25  (c)勇士    $30  (d)蜘蛛  $40  "
	 "(e)恐龍 $80\n"
	 "(f)老鷹 $50  (g)貓     $15  (h)蠟筆小新$35  (i)狗狗  $17  "
	 "(j)惡魔 $100\n"
	 "(k)忍者 $85  (l)阿扁   $200 (m)馬英九  $200 (n)就可人$100 "
	 "[o]羅莉 $77\n"
	 "[0]自己 $0\n");
    i = getans("請選擇你要養的動物：");

    i -= 'a';
    if (i < 0 || i > NUM_KINDS - 1)
	return 0;

    mychicken->type = i;

    reload_money();
    price = egg_price[(int)mychicken->type];
    if (cuser.money < price) {
	vmsg("錢不夠買蛋蛋,蛋蛋要 %d 元", price);
	return 0;
    }
    vice(price, "寵物蛋");
    while (strlen(mychicken->name) < 3)
	getdata(8, 0, "幫牠取個好名字：", mychicken->name,
		sizeof(mychicken->name), DOECHO);

    log_file(CHICKENLOG, LOG_CREAT | LOG_VF,
              ANSI_COLOR(31) "%s " ANSI_RESET "養了一隻叫" ANSI_COLOR(33) " %s " ANSI_RESET "的 "
              ANSI_COLOR(32) "%s" ANSI_RESET "  於 %s\n", cuser.userid,
              mychicken->name, chicken_type[(int)mychicken->type], ctime4(&now));
    mychicken->lastvisit = mychicken->birthday = mychicken->cbirth = now;
    mychicken->food = 0;
    mychicken->weight = time_change[(int)mychicken->type][WEIGHT] / 3;
    mychicken->clean = 0;
    mychicken->run = time_change[(int)mychicken->type][RUN];
    mychicken->attack = time_change[(int)mychicken->type][ATTACK];
    mychicken->book = time_change[(int)mychicken->type][BOOK];
    mychicken->happy = time_change[(int)mychicken->type][HAPPY];
    mychicken->satis = time_change[(int)mychicken->type][SATIS];
    mychicken->temperament = time_change[(int)mychicken->type][TEMPERAMENT];
    mychicken->tiredstrong = 0;
    mychicken->sick = 0;
    mychicken->hp = time_change[(int)mychicken->type][WEIGHT];
    mychicken->hp_max = time_change[(int)mychicken->type][WEIGHT];
    mychicken->mm = 0;
    mychicken->mm_max = 0;
    return 1;
}

static void
show_chicken_stat(const chicken_t * thechicken, int age)
{
    struct tm      *ptime;

    ptime = localtime4(&thechicken->birthday);
    prints(" Name :" ANSI_COLOR(33) "%s" ANSI_RESET " (" ANSI_COLOR(32) "%s" ANSI_RESET ")%*s生日  "
	   ":" ANSI_COLOR(31) "%02d" ANSI_RESET "年" ANSI_COLOR(31) "%2d" ANSI_RESET "月" ANSI_COLOR(31) "%2d" ANSI_RESET "日 "
	   "(" ANSI_COLOR(32) "%s %d歲" ANSI_RESET ")\n"
	   " 體:" ANSI_COLOR(33) "%5d/%-5d" ANSI_RESET " 法:" ANSI_COLOR(33) "%5d/%-5d" ANSI_RESET " 攻擊力:"
	   ANSI_COLOR(33) "%-7d" ANSI_RESET " 敏捷  :" ANSI_COLOR(33) "%-7d" ANSI_RESET " 知識 :" ANSI_COLOR(33) "%-7d"
	   ANSI_RESET " \n"
	   " 快樂 :" ANSI_COLOR(33) "%-7d" ANSI_RESET " 滿意 :" ANSI_COLOR(33) "%-7d" ANSI_RESET " 疲勞  :"
	   ANSI_COLOR(33) "%-7d" ANSI_RESET " 氣質  :" ANSI_COLOR(33) "%-7d " ANSI_RESET "體重 :"
	   ANSI_COLOR(33) "%-5.2f" ANSI_RESET " \n"
	   " 病氣 :" ANSI_COLOR(33) "%-7d" ANSI_RESET " 乾淨 :" ANSI_COLOR(33) "%-7d" ANSI_RESET " 食物  :"
	   ANSI_COLOR(33) "%-7d" ANSI_RESET " 大補丸:" ANSI_COLOR(33) "%-7d" ANSI_RESET " 藥品 :" ANSI_COLOR(33) "%-7d"
	   ANSI_RESET " \n",
	   thechicken->name, chicken_type[(int)thechicken->type],
	   (int)(15 - strlen(thechicken->name)), "",
	   ptime->tm_year % 100, ptime->tm_mon + 1, ptime->tm_mday,
	 cage[age > 16 ? 16 : age], age, thechicken->hp, thechicken->hp_max,
	   thechicken->mm, thechicken->mm_max,
	   thechicken->attack, thechicken->run, thechicken->book,
	   thechicken->happy, thechicken->satis, thechicken->tiredstrong,
	   thechicken->temperament,
	   ((float)(thechicken->hp_max + (thechicken->weight / 50))) / 100,
	   thechicken->sick, thechicken->clean, thechicken->food,
	   thechicken->oo, thechicken->medicine);
}

#define CHICKEN_PIC "etc/chickens"

void
show_chicken_data(chicken_t * thechicken, chicken_t * pkchicken)
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
    showtitle(pkchicken ? "Ｐtt鬥雞場" : "Ｐtt養雞場", BBSName);
    move(1, 0);

    show_chicken_stat(thechicken, age);

    snprintf(buf, sizeof(buf), CHICKEN_PIC "/%c%d", thechicken->type + 'a',
	     age > 16 ? 16 : age);
    show_file(buf, 5, 14, NO_RELOAD);

    move(18, 0);

    if (thechicken->sick)
	outs("生病了...");
    if (thechicken->sick > thechicken->hp / 5)
	outs(ANSI_COLOR(5;31) "擔心...病重!!" ANSI_RESET);

    if (thechicken->clean > 150)
	outs(ANSI_COLOR(31) "又臭又髒的.." ANSI_RESET);
    else if (thechicken->clean > 80)
	outs("有點髒..");
    else if (thechicken->clean < 20)
	outs(ANSI_COLOR(32) "很乾淨.." ANSI_RESET);

    if (thechicken->weight > thechicken->hp_max * 4)
	outs(ANSI_COLOR(31) "快飽死了!." ANSI_RESET);
    else if (thechicken->weight > thechicken->hp_max * 3)
	outs(ANSI_COLOR(32) "飽嘟嘟.." ANSI_RESET);
    else if (thechicken->weight < (thechicken->hp_max / 4))
	outs(ANSI_COLOR(31) "快餓死了!.." ANSI_RESET);
    else if (thechicken->weight < (thechicken->hp_max / 2))
	outs("餓了..");

    if (thechicken->tiredstrong > thechicken->hp * 1.7)
	outs(ANSI_COLOR(31) "累得昏迷了..." ANSI_RESET);
    else if (thechicken->tiredstrong > thechicken->hp)
	outs("累了..");
    else if (thechicken->tiredstrong < thechicken->hp / 4)
	outs(ANSI_COLOR(32) "精力旺盛..." ANSI_RESET);

    if (thechicken->hp < thechicken->hp_max / 4)
	outs(ANSI_COLOR(31) "體力用盡..奄奄一息.." ANSI_RESET);
    if (thechicken->happy > 500)
	outs(ANSI_COLOR(32) "很快樂.." ANSI_RESET);
    else if (thechicken->happy < 100)
	outs("不快樂..");
    if (thechicken->satis > 500)
	outs(ANSI_COLOR(32) "很滿足.." ANSI_RESET);
    else if (thechicken->satis < 50)
	outs("不滿足..");

    if (pkchicken) {
	outc('\n');
	show_chicken_stat(pkchicken, age);
	outs("[任意鍵] 攻擊對方 [q] 落跑 [o] 吃大補丸");
    }
}

static void
ch_eat(void)
{
    chicken_t *mychicken = &cuser.mychicken;
    if (mychicken->food) {
	mychicken->weight += time_change[(int)mychicken->type][WEIGHT] +
	mychicken->hp_max / 5;
	mychicken->tiredstrong +=
	    time_change[(int)mychicken->type][TIREDSTRONG] / 2;
	mychicken->hp_max++;
	mychicken->happy += 5;
	mychicken->satis += 7;
	mychicken->food--;
	move(10, 10);

	show_file(CHICKEN_PIC "/eat", 5, 14, NO_RELOAD);
	pressanykey();
    }
}

static void
ch_clean(void)
{
    chicken_t *mychicken = &cuser.mychicken;
    mychicken->clean = 0;
    mychicken->tiredstrong +=
	time_change[(int)mychicken->type][TIREDSTRONG] / 3;
    show_file(CHICKEN_PIC "/clean", 5, 14, NO_RELOAD);
    pressanykey();
}

static void
ch_guess(void)
{
    char           *guess[3] = {"剪刀", "石頭", "布"}, me, ch, win;

    chicken_t *mychicken = &cuser.mychicken;
    mychicken->happy += time_change[(int)mychicken->type][HAPPY] * 1.5;
    mychicken->satis += time_change[(int)mychicken->type][SATIS];
    mychicken->tiredstrong += time_change[(int)mychicken->type][TIREDSTRONG];
    mychicken->attack += time_change[(int)mychicken->type][ATTACK] / 4;
    move(20, 0);
    clrtobot();
    outs("你要出[" ANSI_COLOR(32) "1" ANSI_RESET "]" ANSI_COLOR(33) "剪刀" ANSI_RESET "(" ANSI_COLOR(32) "2" ANSI_RESET ")"
	 ANSI_COLOR(33) "石頭" ANSI_RESET "(" ANSI_COLOR(32) "3" ANSI_RESET ")" ANSI_COLOR(33) "布" ANSI_RESET ":\n");
    me = igetch();
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
ch_book(void)
{
    chicken_t *mychicken = &cuser.mychicken;
    mychicken->book += time_change[(int)mychicken->type][BOOK];
    mychicken->tiredstrong += time_change[(int)mychicken->type][TIREDSTRONG];
    show_file(CHICKEN_PIC "/read", 5, 14, NO_RELOAD);
    pressanykey();
}

static void
ch_kiss(void)
{
    chicken_t *mychicken = &cuser.mychicken;
    mychicken->happy += time_change[(int)mychicken->type][HAPPY];
    mychicken->satis += time_change[(int)mychicken->type][SATIS];
    mychicken->tiredstrong +=
	time_change[(int)mychicken->type][TIREDSTRONG] / 2;
    show_file(CHICKEN_PIC "/kiss", 5, 14, NO_RELOAD);
    pressanykey();
}

static void
ch_hit(void)
{
    chicken_t *mychicken = &cuser.mychicken;
    mychicken->attack += time_change[(int)mychicken->type][ATTACK];
    mychicken->run += time_change[(int)mychicken->type][RUN];
    mychicken->mm_max += time_change[(int)mychicken->type][MM_MAX] / 15;
    mychicken->weight -= mychicken->hp_max / 15;
    mychicken->hp -= (int)((float)time_change[(int)mychicken->type][HP_MAX] *
			   random() / (RAND_MAX + 1.0)) / 2 + 1;

    if (mychicken->book > 2)
	mychicken->book -= 2;
    if (mychicken->happy > 2)
	mychicken->happy -= 2;
    if (mychicken->satis > 2)
	mychicken->satis -= 2;
    mychicken->tiredstrong += time_change[(int)mychicken->type][TIREDSTRONG];
    show_file(CHICKEN_PIC "/hit", 5, 14, NO_RELOAD);
    pressanykey();
}

void
ch_buyitem(int money, const char *picture, int *item, int haveticket)
{
    int             num = 0;
    char            buf[5];

    getdata_str(b_lines - 1, 0, "要買多少份呢:",
		buf, sizeof(buf), DOECHO, "1");
    num = atoi(buf);
    if (num < 1)
	return;
    reload_money();
    if (cuser.money/money >= num) {
	*item += num;
	if( haveticket )
	    vice(money * num, "購買寵物,賭盤項目");
	else
	    demoney(-money * num);
	show_file(picture, 5, 14, NO_RELOAD);
        pressanykey();
    } else {
	vmsg("現金不夠 !!!");
    }
    usleep(100000); // sleep 0.1s
}

static void
ch_eatoo(void)
{
    chicken_t *mychicken = &cuser.mychicken;
    if (mychicken->oo > 0) {
	mychicken->oo--;
	mychicken->tiredstrong = 0;
	if (mychicken->happy > 5)
	    mychicken->happy -= 5;
	show_file(CHICKEN_PIC "/oo", 5, 14, NO_RELOAD);
	pressanykey();
    }
}

static void
ch_eatmedicine(void)
{
    chicken_t *mychicken = &cuser.mychicken;
    if (mychicken->medicine > 0) {
	mychicken->medicine--;
	mychicken->sick = 0;
	if (mychicken->hp_max > 10)
	    mychicken->hp_max -= 3;
	mychicken->hp = mychicken->hp_max;
	if (mychicken->happy > 10)
	    mychicken->happy -= 10;
	show_file(CHICKEN_PIC "/medicine", 5, 14, NO_RELOAD);
	pressanykey();
    }
}

static void
ch_kill(void)
{
    chicken_t *mychicken = &cuser.mychicken;
    int        ans;

    ans = getans("棄養要被罰 100 元, 是否要棄養?(y/N)");
    if (ans == 'y') {

	vice(100, "棄養寵物費");
	more(CHICKEN_PIC "/deadth", YEA);
	log_file(CHICKENLOG, LOG_CREAT | LOG_VF,
		 ANSI_COLOR(31) "%s " ANSI_RESET "把 " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(32) " %s "
		 ANSI_RESET "宰了 於 %s\n", cuser.userid, mychicken->name,
		 chicken_type[(int)mychicken->type], ctime4(&now));
	mychicken->name[0] = 0;
    }
}

static int
ch_sell(int age)
{
    chicken_t *mychicken = &cuser.mychicken;
    /*
     * int money = (mychicken->weight -
     * time_change[(int)mychicken->type][WEIGHT])
     * (food_price[(int)mychicken->type])/4 + ( + ((mychicken->clean /
     * time_change[(int)mychicken->type][CLEAN]) + (mychicken->run /
     * time_change[(int)mychicken->type][RUN]) + (mychicken->attack /
     * time_change[(int)mychicken->type][ATTACK]) + (mychicken->book /
     * time_change[(int)mychicken->type][BOOK]) + (mychicken->happy /
     * time_change[(int)mychicken->type][HAPPY]) + (mychicken->satis /
     * time_change[(int)mychicken->type][SATIS]) + (mychicken->temperament /
     * time_change[(int)mychicken->type][TEMPERAMENT]) -
     * (mychicken->tiredstrong /
     * time_change[(int)mychicken->type][TIREDSTRONG]) - (mychicken->sick /
     * time_change[(int)mychicken->type][SICK]) + (mychicken->hp /
     * time_change[(int)mychicken->type][HP_MAX]) + (mychicken->mm /
     * time_change[(int)mychicken->type][MM_MAX]) + 7 - abs(age - 7)) * 3 ;
     */
    int             money = (age * food_price[(int)mychicken->type] * 3
			     + (mychicken->hp_max * 10 + mychicken->weight) /
			time_change[(int)mychicken->type][HP_MAX]) * 3 / 2 -
    mychicken->sick, ans;

    if (money < 0)
	money = 0;
    else if (money > MAX_CHICKEN_MONEY)
	money = MAX_CHICKEN_MONEY;
    //防止怪雞
    if (mychicken->type == 1 || mychicken->type == 7) {
	outs("\n" ANSI_COLOR(31) " ㄜ..親愛的..販賣人口是會犯法的唷.." ANSI_RESET);
	pressanykey();
	return 0;
    }
    if (age < 5) {
	outs("\n 還未成年不能賣");
	pressanykey();
	return 0;
    }
    if (age > 30) {
	outs("\n" ANSI_COLOR(31) " 這..太老沒人要了" ANSI_RESET);
	pressanykey();
	return 0;
    }
    ans = getans("這隻%d歲%s可以賣 %d 元, 是否要賣?(y/N)", age, 
                 chicken_type[(int)mychicken->type], money);
    if (ans == 'y') {
	log_file(CHICKENLOG, LOG_CREAT | LOG_VF,
		 ANSI_COLOR(31) "%s" ANSI_RESET " 把 " ANSI_COLOR(33) "%s" ANSI_RESET " "
                 ANSI_COLOR(32) "%s" ANSI_RESET " 用 " ANSI_COLOR(36) "%d" ANSI_RESET " 賣了 於 %s\n",
                 cuser.userid, mychicken->name, 
                 chicken_type[(int)mychicken->type], money, ctime4(&now));
	mychicken->lastvisit = mychicken->name[0] = 0;
	passwd_update(usernum, &cuser);
	more(CHICKEN_PIC "/sell", YEA);
	demoney(money);
	return 1;
    }
    return 0;
}

static void
geting_old(int *hp, int *weight, int diff, int age)
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

/* 依時間變動的資料 */
void
time_diff(chicken_t * thechicken)
{
    int             diff;
    int             theage = ((now - thechicken->cbirth) / (60 * 60 * 24));

    thechicken->type %= NUM_KINDS;
    diff = (now - thechicken->lastvisit) / 60;

    if ((diff) < 1)
	return;

    if (theage > 13)		/* 老死 */
	geting_old(&thechicken->hp_max, &thechicken->weight, diff, theage);

    thechicken->lastvisit = now;
    thechicken->weight -= thechicken->hp_max * diff / 540;	/* 體重 */
    if (thechicken->weight < 1) {
	thechicken->sick -= thechicken->weight / 10;	/* 餓得病氣上升 */
	thechicken->weight = 1;
    }
    /* 清潔度 */
    thechicken->clean += diff * time_change[(int)thechicken->type][CLEAN] / 30;

    /* 快樂度 */
    thechicken->happy -= diff / 60;
    if (thechicken->happy < 0)
	thechicken->happy = 0;
    thechicken->attack -=
	time_change[(int)thechicken->type][ATTACK] * diff / (60 * 32);
    if (thechicken->attack < 0)
	thechicken->attack = 0;
    /* 攻擊力 */
    thechicken->run -= time_change[(int)thechicken->type][RUN] * diff / (60 * 32);
    /* 敏捷 */
    if (thechicken->run < 0)
	thechicken->run = 0;
    thechicken->book -= time_change[(int)thechicken->type][BOOK] * diff / (60 * 32);
    /* 知識 */
    if (thechicken->book < 0)
	thechicken->book = 0;
    /* 氣質 */
    thechicken->temperament++;

    thechicken->satis -= diff / 60 / 3 * time_change[(int)thechicken->type][SATIS];
    /* 滿意度 */
    if (thechicken->satis < 0)
	thechicken->satis = 0;

    /* 髒病的 */
    if (thechicken->clean > 1000)
	thechicken->sick += (thechicken->clean - 400) / 10;

    if (thechicken->weight > 1)
	thechicken->sick -= diff / 60;
    /* 病氣恢護 */
    if (thechicken->sick < 0)
	thechicken->sick = 0;
    thechicken->tiredstrong -= diff *
	time_change[(int)thechicken->type][TIREDSTRONG] / 4;
    /* 疲勞 */
    if (thechicken->tiredstrong < 0)
	thechicken->tiredstrong = 0;
    /* hp_max */
    if (thechicken->hp >= thechicken->hp_max / 2)
	thechicken->hp_max +=
	    time_change[(int)thechicken->type][HP_MAX] * diff / (60 * 12);
    /* hp恢護 */
    if (!thechicken->sick)
	thechicken->hp +=
	    time_change[(int)thechicken->type][HP_MAX] * diff / (60 * 6);
    if (thechicken->hp > thechicken->hp_max)
	thechicken->hp = thechicken->hp_max;
    /* mm_max */
    if (thechicken->mm >= thechicken->mm_max / 2)
	thechicken->mm_max +=
	    time_change[(int)thechicken->type][MM_MAX] * diff / (60 * 8);
    /* mm恢護 */
    if (!thechicken->sick)
	thechicken->mm += diff;
    if (thechicken->mm > thechicken->mm_max)
	thechicken->mm = thechicken->mm_max;
}

static void
check_sick(void)
{
    chicken_t *mychicken = &cuser.mychicken;
    /* 髒病的 */
    if (mychicken->tiredstrong > mychicken->hp * 0.3 && mychicken->clean > 150)
	mychicken->sick += (mychicken->clean - 150) / 10;
    /* 累病的 */
    if (mychicken->tiredstrong > mychicken->hp * 1.3)
	mychicken->sick += time_change[(int)mychicken->type][SICK];
    /* 病氣太重還做事減hp */
    if (mychicken->sick > mychicken->hp / 5) {
	mychicken->hp -= (mychicken->sick - mychicken->hp / 5) / 4;
	if (mychicken->hp < 0)
	    mychicken->hp = 0;
    }
}

static int
deadtype(const chicken_t * thechicken)
{
    chicken_t *mychicken = &cuser.mychicken;
    int             i;

    if (thechicken->hp <= 0)	/* hp用盡 */
	i = 1;
    else if (thechicken->tiredstrong > thechicken->hp * 3)	/* 操勞過度 */
	i = 2;
    else if (thechicken->weight > thechicken->hp_max * 5)	/* 肥胖過度 */
	i = 3;
    else if (thechicken->weight == 1 &&
	     thechicken->sick > thechicken->hp_max / 4)
	i = 4;			/* 餓死了 */
    else if (thechicken->satis <= 0)	/* 很不滿意 */
	i = 5;
    else
	return 0;

    if (thechicken == mychicken) {
	log_file(CHICKENLOG, LOG_CREAT | LOG_VF,
                 ANSI_COLOR(31) "%s" ANSI_RESET " 所疼愛的" ANSI_COLOR(33) " %s" ANSI_COLOR(32) " %s "
                 ANSI_RESET "掛了 於 %s\n", cuser.userid, thechicken->name,
                 chicken_type[(int)thechicken->type], ctime4(&now));
	mychicken->name[0] = 0;
	passwd_update(usernum, &cuser);
    }
    return i;
}

int
showdeadth(int type)
{
    switch (type) {
	case 1:
	more(CHICKEN_PIC "/nohp", YEA);
	break;
    case 2:
	more(CHICKEN_PIC "/tootired", YEA);
	break;
    case 3:
	more(CHICKEN_PIC "/toofat", YEA);
	break;
    case 4:
	more(CHICKEN_PIC "/nofood", YEA);
	break;
    case 5:
	more(CHICKEN_PIC "/nosatis", YEA);
	break;
    default:
	return 0;
    }
    more(CHICKEN_PIC "/deadth", YEA);
    return type;
}

int
isdeadth(const chicken_t * thechicken)
{
    int             i;

    if (!(i = deadtype(thechicken)))
	return 0;
    return showdeadth(i);
}

static void
ch_changename(void)
{
    chicken_t *mychicken = &cuser.mychicken;
    char      newname[20] = "";

    getdata_str(b_lines - 1, 0, "嗯..改個好名字吧:", newname, 18, DOECHO,
		mychicken->name);

    if (strlen(newname) >= 3 && strcmp(newname, mychicken->name)) {
	strlcpy(mychicken->name, newname, sizeof(mychicken->name));
	log_file(CHICKENLOG, LOG_CREAT | LOG_VF, 
                ANSI_COLOR(31) "%s" ANSI_RESET " 把疼愛的" ANSI_COLOR(33) " %s" ANSI_COLOR(32) " %s "
                ANSI_RESET "改名為" ANSI_COLOR(33) " %s" ANSI_RESET " 於 %s\n",
                 cuser.userid, mychicken->name,
                 chicken_type[(int)mychicken->type], newname, ctime4(&now));
    }
}

static int
select_menu(int age)
{
    chicken_t *mychicken = &cuser.mychicken;
    char            ch;

    reload_money();
    move(19, 0);
    prints(ANSI_COLOR(44;37) " 錢 :" ANSI_COLOR(33) " %-10d                                  "
	   "                       " ANSI_RESET "\n"
	   ANSI_COLOR(33) "(" ANSI_COLOR(37) "1" ANSI_COLOR(33) ")清理 (" ANSI_COLOR(37) "2" ANSI_COLOR(33) ")吃飯 "
	   "(" ANSI_COLOR(37) "3" ANSI_COLOR(33) ")猜拳 (" ANSI_COLOR(37) "4" ANSI_COLOR(33) ")唸書 "
	   "(" ANSI_COLOR(37) "5" ANSI_COLOR(33) ")親他 (" ANSI_COLOR(37) "6" ANSI_COLOR(33) ")打他 "
	   "(" ANSI_COLOR(37) "7" ANSI_COLOR(33) ")買%s$%d (" ANSI_COLOR(37) "8" ANSI_COLOR(33) ")吃補丸\n"
	   "(" ANSI_COLOR(37) "9" ANSI_COLOR(33) ")吃病藥 (" ANSI_COLOR(37) "o" ANSI_COLOR(33) ")買大補丸$100 "
	   "(" ANSI_COLOR(37) "m" ANSI_COLOR(33) ")買藥$10 (" ANSI_COLOR(37) "k" ANSI_COLOR(33) ")棄養 "
	   "(" ANSI_COLOR(37) "s" ANSI_COLOR(33) ")賣掉 (" ANSI_COLOR(37) "n" ANSI_COLOR(33) ")改名 "
	   "(" ANSI_COLOR(37) "q" ANSI_COLOR(33) ")離開:" ANSI_RESET,
	   cuser.money,
    /*
     * chicken_food[(int)mychicken->type],
     * chicken_type[(int)mychicken->type],
     * chicken_type[(int)mychicken->type],
     */
	   chicken_food[(int)mychicken->type],
	   food_price[(int)mychicken->type]);
    do {
	switch (ch = igetch()) {
	case '1':
	    ch_clean();
	    check_sick();
	    break;
	case '2':
	    ch_eat();
	    check_sick();
	    break;
	case '3':
	    ch_guess();
	    check_sick();
	    break;
	case '4':
	    ch_book();
	    check_sick();
	    break;
	case '5':
	    ch_kiss();
	    break;
	case '6':
	    ch_hit();
	    check_sick();
	    break;
	case '7':
	    ch_buyitem(food_price[(int)mychicken->type], CHICKEN_PIC "/food",
		       &mychicken->food, 1);
	    break;
	case '8':
	    ch_eatoo();
	    break;
	case '9':
	    ch_eatmedicine();
	    break;
	case 'O':
	case 'o':
	    ch_buyitem(100, CHICKEN_PIC "/buyoo", &mychicken->oo, 1);
	    break;
	case 'M':
	case 'm':
	    ch_buyitem(10, CHICKEN_PIC "/buymedicine", &mychicken->medicine, 1);
	    break;
	case 'N':
	case 'n':
	    ch_changename();
	    break;
	case 'K':
	case 'k':
	    ch_kill();
	    return 0;
	case 'S':
	case 's':
	    if (!ch_sell(age))
		break;
	case 'Q':
	case 'q':
	    return 0;
	}
    } while (ch < ' ' || ch > 'z');
    return 1;
}

static int
recover_chicken(chicken_t * thechicken)
{
    char            buf[200];
    int             price = egg_price[(int)thechicken->type], money = price + (random() % price);

    if (now - thechicken->lastvisit > (60 * 60 * 24 * 7))
	return 0;
    outmsg(ANSI_COLOR(33;44) "★靈界守衛" ANSI_COLOR(37;45) " 別害怕 我是來幫你的 " ANSI_RESET);
    bell();
    igetch();
    outmsg(ANSI_COLOR(33;44) "★靈界守衛" ANSI_COLOR(37;45) " 你無法丟到我水球 因為我是聖靈, "
	   "最近缺錢想賺外快 " ANSI_RESET);
    bell();
    igetch();
    snprintf(buf, sizeof(buf), ANSI_COLOR(33;44) "★靈界守衛" ANSI_COLOR(37;45) " "
	     "你有一個剛走不久的%s要招換回來嗎? 只要%d元唷 " ANSI_RESET,
	     chicken_type[(int)thechicken->type], price * 2);
    outmsg(buf);
    bell();
    getdata_str(21, 0, "    選擇：(N:坑人嘛/y:請幫幫我)", buf, 3, LCECHO, "N");
    if (buf[0] == 'y' || buf[0] == 'Y') {
	reload_money();
	if (cuser.money < price * 2) {
	    outmsg(ANSI_COLOR(33;44) "★靈界守衛" ANSI_COLOR(37;45) " 什麼 錢沒帶夠 "
		   "沒錢的小鬼 快去籌錢吧 " ANSI_RESET);
	    bell();
	    igetch();
	    return 0;
	}
	strlcpy(thechicken->name, "[撿回來的]", sizeof(thechicken->name));
	thechicken->hp = thechicken->hp_max;
	thechicken->sick = 0;
	thechicken->satis = 2;
	vice(money, "靈界守衛");
	snprintf(buf, sizeof(buf),
		 ANSI_COLOR(33;44) "★靈界守衛" ANSI_COLOR(37;45) " OK了 記得餵他點東西 "
		 "不然可能失效 念在我也有玩Ptt 拿你%d就好 " ANSI_RESET, money);
	outmsg(buf);
	bell();
	igetch();
	return 1;
    }
    outmsg(ANSI_COLOR(33;44) "★靈界守衛" ANSI_COLOR(37;45) " 竟然說我坑人! 這年頭命真不值錢 "
	   "除非我再來找你 你再也沒機會了 " ANSI_RESET);
    bell();
    igetch();
    thechicken->lastvisit = 0;
    passwd_update(usernum, &cuser);
    return 0;
}

#define lockreturn0(unmode, state) if(lockutmpmode(unmode, state)) return 0

int
chicken_main(void)
{
    chicken_t *mychicken = &cuser.mychicken;
    int age;
    lockreturn0(CHICKEN, LOCK_MULTI);
    reload_chicken();
    if (!mychicken->name[0] && !recover_chicken(mychicken) && !new_chicken()) {
	unlockutmpmode();
	return 0;
    }
    age = ((now - mychicken->cbirth) / (60 * 60 * 24));
    do {
	time_diff(mychicken);
	if (isdeadth(mychicken))
	    break;
	show_chicken_data(mychicken, NULL);
    } while (select_menu(age));
    reload_money();
    passwd_update(usernum, &cuser);
    unlockutmpmode();
    return 0;
}

int
chickenpk(int fd)
{
    chicken_t *mychicken = &cuser.mychicken;
    char            mateid[IDLEN + 1], data[200], buf[200];
    int             ch = 0;

    userinfo_t     *uin = &SHM->uinfo[currutmp->destuip];
    userec_t        ouser;
    chicken_t      *ochicken = &ouser.mychicken;
    int             r, attmax, i, datac, duid = currutmp->destuid, catched = 0,
                    count = 0;

    lockreturn0(CHICKEN, LOCK_MULTI);

    strlcpy(mateid, currutmp->mateid, sizeof(mateid));
    /* 把對手的id用local buffer記住 */

    getuser(mateid, &ouser);
    reload_chicken();
    if (!ochicken->name[0] || !mychicken->name[0]) {
	bell();
	vmsg("有一方沒有寵物");	/* Ptt:妨止page時把寵物賣掉 */
	add_io(0, 0);
	close(fd);
	unlockutmpmode();
	return 0;
    }
    show_chicken_data(ochicken, mychicken);
    add_io(fd, 3);		/* 把fd加到igetch監視 */
    while (1) {
	r = random();
	ch = igetch();
	getuser(mateid, &ouser);
	reload_chicken();
	show_chicken_data(ochicken, mychicken);
	time_diff(mychicken);

	i = mychicken->attack * mychicken->hp / mychicken->hp_max;
	for (attmax = 2; (i = i * 9 / 10); attmax++);

	if (ch == I_OTHERDATA) {
	    count = 0;
	    datac = recv(fd, data, sizeof(data), 0);
	    if (datac <= 1)
		break;
	    move(17, 0);
	    outs(data + 1);
	    switch (data[0]) {
	    case 'c':
		catched = 1;
		move(16, 0);
		outs("要放他走嗎?(y/N)");
		break;
	    case 'd':
		move(16, 0);
		outs("阿~倒下了!!");
		break;
	    }
	    if (data[0] == 'd' || data[0] == 'q' || data[0] == 'l')
		break;
	    continue;
	} else if (currutmp->turn) {
	    count = 0;
	    currutmp->turn = 0;
	    uin->turn = 1;
	    mychicken->tiredstrong++;
	    switch (ch) {
	    case 'y':
		if (catched == 1) {
		    snprintf(data, sizeof(data),
			     "l讓 %s 落跑了\n", ochicken->name);
		}
		break;
	    case 'n':
		catched = 0;
	    default:
	    case 'k':
		r = r % (attmax + 2);
		if (r) {
		    snprintf(data, sizeof(data),
			     "M%s %s%s %s 傷了 %d 點\n", mychicken->name,
			     damage_degree[r / 3 > 15 ? 15 : r / 3],
			     attack_type[(int)mychicken->type],
			     ochicken->name, r);
		    ochicken->hp -= r;
		} else
		    snprintf(data, sizeof(data),
			     "M%s 覺得手軟出擊無效\n", mychicken->name);
		break;
	    case 'o':
		if (mychicken->oo > 0) {
		    mychicken->oo--;
		    mychicken->hp += 300;
		    if (mychicken->hp > mychicken->hp_max)
			mychicken->hp = mychicken->hp_max;
		    mychicken->tiredstrong = 0;
		    snprintf(data, sizeof(data), "M%s 吃了顆大補丸補充體力\n",
			     mychicken->name);
		} else
		    snprintf(data, sizeof(data),
			    "M%s 想吃大補丸, 可是沒有大補丸可吃\n",
			    mychicken->name);
		break;
	    case 'q':
		if (r % (mychicken->run + 1) > r % (ochicken->run + 1))
		    snprintf(data, sizeof(data), "q%s 落跑了\n",
			     mychicken->name);
		else
		    snprintf(data, sizeof(data),
			     "c%s 想落跑, 但被 %s 抓到了\n",
			     mychicken->name, ochicken->name);
		break;
	    }
	    if (deadtype(ochicken)) {
		strtok(data, "\n");
		strlcpy(buf, data, sizeof(buf));
		snprintf(data, sizeof(data), "d%s , %s 被 %s 打死了\n",
			 buf + 1, ochicken->name, mychicken->name);
	    }
	    move(17, 0);
	    outs(data + 1);
	    i = strlen(data) + 1;
	    passwd_update(duid, &ouser);
	    passwd_update(usernum, &cuser);
	    send(fd, data, i, 0);
	    if (data[0] == 'q' || data[0] == 'd')
		break;
	} else {
	    move(17, 0);
	    if (count++ > 30)
		break;
	}
    }
    add_io(0, 0);		/* 把igetch恢復回 */
    pressanykey();
    close(fd);
    showdeadth(deadtype(mychicken));
    unlockutmpmode();
    return 0;
}
