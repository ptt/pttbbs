/* $Id$ */
#include "bbs.h"
#define LOGPASS BBSHOME "/etc/winguess.log"

static int
check_data(const char *str)
{
    int             i, j;

    if (strlen(str) != 4)
	return -1;
    for (i = 0; i < 4; i++)
	if (str[i] < '0' || str[i] > '9')
	    return -1;
    for (i = 0; i < 4; i++)
	for (j = i + 1; j < 4; j++)
	    if (str[i] == str[j])
		return -1;
    return 1;
}

static char    *
get_data(char data[5], int count)
{
    while (1) {
	getdata(6, 0, "輸入四位數字(不重複): ", data, 5, LCECHO);
	if (check_data(data) == 1)
	    break;
    }
    return data;
}

static int
guess_play(const char *data, const char *answer, int count)
{
    int             A_num = 0, B_num = 0;
    int             i, j;

    for (i = 0; i < 4; i++) {
	if (data[i] == answer[i])
	    A_num++;
	for (j = 0; j < 4; j++)
	    if (i == j)
		continue;
	    else if (data[i] == answer[j]) {
		B_num++;
		break;
	    }
    }
    if (A_num == 4)
	return 1;
    move(count + 8, 55);
    prints("%s => " ANSI_COLOR(1;32) "%dA %dB" ANSI_RESET, data, A_num, B_num);
    return 0;
}

static int
result(int correct, int number)
{
    char            a = 0, b = 0, i, j;
    char            n1[5], n2[5];

    snprintf(n1, sizeof(n1), "%04d", correct);
    snprintf(n2, sizeof(n2), "%04d", number);
    for (i = 0; i < 4; i++)
	for (j = 0; j < 4; j++)
	    if (n1[(int)i] == n2[(int)j])
		b++;
    for (i = 0; i < 4; i++)
	if (n1[(int)i] == n2[(int)i]) {
	    b--;
	    a++;
	}
    return 10 * a + b;
}

static int
legal(int number)
{
    char            i, j;
    char            temp[5];

    snprintf(temp, sizeof(temp), "%04d", number);
    for (i = 0; i < 4; i++)
	for (j = i + 1; j < 4; j++)
	    if (temp[(int)i] == temp[(int)j])
		return 0;
    return 1;
}

static void
initcomputer(char flag[])
{
    int             i;

    for (i = 0; i < 10000; i++)
	if (legal(i))
	    flag[i] = 1;
	else
	    flag[i] = 0;
}

static int
computer(int correct, int total, char flag[], int n[])
{
    int             guess;
    static int      j;
    int             k, i;
    char            data[5];

    if (total == 1) {
	do {
	    guess = random() % 10000;
	} while (!legal(guess));
    } else
	guess = n[random() % j];
    k = result(correct, guess);
    if (k == 40) {
	move(total + 8, 25);
	snprintf(data, sizeof(data), "%04d", guess);
	prints("%s => 猜中了!!", data);
	return 1;
    } else {
	move(total + 8, 25);
	snprintf(data, sizeof(data), "%04d", guess);
	prints("%s => " ANSI_COLOR(1;32) "%dA %dB" ANSI_RESET, data, k / 10, k % 10);
    }
    j = 0;
    for (i = 0; i < 10000; i++)
	if (flag[i]) {
	    if (result(i, guess) != k)
		flag[i] = 0;
	    else
		n[j++] = i;
	}
    return 0;
}

static void
Diff_Random(char *answer)
{
    register int    i = 0, j, k;

    while (i < 4) {
	k = random() % 10 + '0';
	for (j = 0; j < i; j++)
	    if (k == answer[j])
		break;
	if (j == i) {
	    answer[j] = k;
	    i++;
	}
    }
    answer[4] = 0;
}

int
guess_main(void)
{
    char            data[5];
    char            computerwin = 0, youwin = 0;
    int             count = 0, c_count = 0;
    char            ifcomputer[2];
    char            answer[5];
    char            yournum[5];
    const int max_guess = 10;

    // these variables are not very huge, no need to use malloc
    // to prevent heap allocation.
    char	    flag[10000];
    int		    n[1500];

    setutmpmode(GUESSNUM);
    clear();
    showtitle("猜數字", BBSName);

    Diff_Random(answer);
    move(2, 0);
    clrtoeol();

    getdata(4, 0, "您要和電腦比賽嗎? <Y/n>[y]:",
		ifcomputer, sizeof(ifcomputer), LCECHO);
    *ifcomputer = (*ifcomputer == 'n') ? 0 : 1;

    if (ifcomputer[0]) {
	do {
	    getdata(5, 0, "請輸入您要讓電腦猜的數字: ",
		    yournum, sizeof(yournum), LCECHO);
	} while (!legal(atoi(yournum)));
	move(8, 25);
	outs("電腦猜");
	initcomputer(flag);
    }
    move(8, 55);
    outs("你猜");
    while (((!computerwin || !youwin) && count < max_guess && (ifcomputer[0])) 
	    || (!ifcomputer[0] && count < max_guess && !youwin)) {
	if (!computerwin && ifcomputer[0]) {
	    ++c_count;
	    if (computer(atoi(yournum), c_count, flag, n))
		computerwin = 1;
	}
	move(20, 55);
	prints("第 %d/%d 次機會 ", count + 1, max_guess);
	if (!youwin) {
	    ++count;
	    if (guess_play(get_data(data, count), answer, count))
		youwin = 1;
	}
    }
    move(17, 33);
    if (ifcomputer[0]) {
	if (count > c_count) {
	    outs("  你輸給電腦了");
	} else if (count < c_count) {
	    outs("真厲害, 讓你猜到囉");
	} else {
	    prints("真厲害, 和電腦打成平手了");
	}
	pressanykey();
	return 1;
    }
    if (youwin) {
	if (count < 5) {
	    outs("真厲害!");
	} else if (count > 5) {
	    outs("唉, 太多次才猜出來了");
	} else {
	    outs("五次猜出來, 還可以~");
	    move(18, 35);
	    clrtoeol();
	}
	pressanykey();
	return 1;
    }
    move(17, 32);
    prints("嘿嘿 標準答案是 %s ", answer);
    move(18, 32);
    outs("下次再來吧");
    pressanykey();
    return 1;
}
