/* $Id: page.c,v 1.10 2003/01/19 16:06:06 kcwu Exp $ */
#include "bbs.h"

#define hpressanykey(a) {move(22, 0); prints(a); pressanykey();}
static void
filt_railway(char *fpath)
{
    char            buf[256], tmppath[32];
    FILE           *fp = fopen(fpath, "w"), *tp;

    snprintf(tmppath, sizeof(tmppath), "%s.railway", fpath);
    if (!fp || !(tp = fopen(tmppath, "r"))) // XXX fclose(fp) if tp fail
	return;

    while (fgets(buf, 255, tp)) {
	if (strstr(buf, "INLINE"))
	    continue;
	if (strstr(buf, "LINK"))
	    break;
	fprintf(fp, "%s", buf);
    }
    fclose(fp);
    fclose(tp);
    unlink(tmppath);
}

int
main_railway()
{
    fileheader_t    mhdr;
    char            genbuf[200];
    int             from, to, time_go, time_reach;
    char            tt[2], type[2];
    char            command[256], buf[8];
    char           *addr[] = {
	"基隆", "八堵", "七堵", "五堵", "汐止", "南港", "松山", "台北", "萬華",
	"板橋", "樹林", "山佳", "鶯歌", "桃園", "內壢", "中壢", "埔心", "楊梅",
	"湖口", "新豐", "竹北", "新竹", "香山", "崎頂", "竹南", "造橋", "豐富",
	"談文", "大山", "後龍", "龍港", "白沙屯", "新埔", "通霄", "苑裡",
	"日南", "大甲", "臺中港", "清水", "沙鹿", "龍井", "大肚", "追分",
	"苗栗", "南勢", "銅鑼", "三義", "勝興", "泰安", "后里", "豐原", "潭子",
	"台中", "烏日", "成功\", "彰化", "花壇", "員林", "永靖", "社頭",
	"田中", "二水", "林內", "石榴", "斗六", "斗南", "石龜", "大林",
	"民雄", "嘉義", "水上", "南靖", "後壁", "新營", "柳營", "林鳳營",
	"隆田", "拔林", "善化", "新市", "永康", "台南", "保安", "中洲",
	"大湖", "路竹", "岡山", "橋頭", "楠梓", "左營", "高雄", "鳳山",
	"九曲堂", "屏東", NULL, NULL
    };

    setutmpmode(RAIL_WAY);
    clear();
    move(0, 25);
    prints("\033[1;37;45m 火車查詢系統 \033[1;44;33m作者:Heat\033[m");
    move(1, 0);
    outs("\033[1;33m\n"
	 " 1.基隆    16.中壢     31.龍港     46.銅鑼     61.田中    76.林鳳營   91.高雄\n"
	 " 2.八堵    17.埔心     32.白沙屯   47.三義     62.二水    77.隆田     92.鳳山\n"
	 " 3.七堵    18.楊梅     33.新埔     48.勝興     63.林內    78.拔林     93.九曲堂\n"
	 " 4.五堵    19.湖口     34.通霄     49.泰安     64.石榴    79.善化     94.屏東\n"
       " 5.汐止    20.新豐     35.苑裡     50.后里     65.斗六    80.新市\n"
       " 6.南港    21.竹北     36.日南     51.豐原     66.斗南    81.永康\n"
       " 7.松山    22.新竹     37.大甲     52.潭子     67.石龜    82.台南\n"
       " 8.台北    23.香山     38.臺中港   53.台中     68.大林    83.保安\n"
       " 9.萬華    24.崎頂     39.清水     54.烏日     69.民雄    84.中洲\n"
      "10.板橋    25.竹南     40.沙鹿     55.成功\     70.嘉義    85.大湖\n"
       "11.樹林    26.造橋     41.龍井     56.彰化     71.水上    86.路竹\n"
       "12.山佳    27.豐富     42.大肚     57.花壇     72.南靖    87.岡山\n"
       "13.鶯歌    28.談文     43.追分     58.員林     73.後壁    88.橋頭\n"
       "14.桃園    29.大山     44.苗栗     59.永靖     74.新營    89.楠梓\n"
	 "15.內壢    30.後龍     45.南勢     60.社頭     75.柳營    90.左營\033[m");

    getdata(17, 0, "\033[1;35m你確定要搜尋嗎?[y/n]:\033[m", buf, 2, LCECHO);
    if (buf[0] != 'y' && buf[0] != 'Y')
	return 0;
    while (1)
	if (getdata(18, 0, "\033[1;35m請輸入起站(1-94):\033[m", buf, 3, LCECHO) &&
	    (from = atoi(buf)) >= 1 && from <= 94)
	    break;
    while (1)
	if (getdata(18, 40, "\033[1;35m請輸入目的地(1-94):\033[m",
		    buf, 3, LCECHO) &&
	    (to = atoi(buf)) >= 1 && to <= 94)
	    break;
    while (1)
	if (getdata(19, 0, "\033[1;35m請輸入時間區段(0-23) 由:\033[m",
		    buf, 3, LCECHO) &&
	    (time_go = atoi(buf)) >= 0 && time_go <= 23)
	    break;
    while (1)
	if (getdata(19, 40, "\033[1;35m到:\033[m", buf, 3, LCECHO) &&
	    (time_reach = atoi(buf)) >= 0 && time_reach <= 23)
	    break;
    while (1)
	if (getdata(20, 0, "\033[1;35m想查詢 1:對號快車  2:普通平快\033[m",
		    type, 2, LCECHO) && (type[0] == '1' || type[0] == '2'))
	    break;
    while (1)
	if (getdata(21, 0, "\033[1;35m欲查詢 1:出發時間  2:到達時間\033[m",
		    tt, sizeof(tt), LCECHO) &&
	    (tt[0] == '1' || tt[0] == '2'))
	    break;
    sethomepath(genbuf, cuser.userid);
    stampfile(genbuf, &mhdr);
    strlcpy(mhdr.owner, "Ptt搜尋器", sizeof(mhdr.owner));
    strncpy(mhdr.title, "火車時刻搜尋結果", TTLEN);

    snprintf(command, sizeof(command), "echo \"from-station=%s&to-station=%s"
	    "&from-time=%02d00&to-time=%02d00&tt=%s&type=%s\" | "
	    "lynx -dump -post_data "
	    "\"http://www.railway.gov.tw/cgi-bin/timetk.cgi\" > %s.railway",
	    addr[from - 1], addr[to - 1], time_go, time_reach,
	    (tt[0] == '1') ? "start" : "arriv",
	    (type[0] == '1') ? "fast" : "slow", genbuf);

    system(command);
    filt_railway(genbuf);
    sethomedir(genbuf, cuser.userid);
    if (append_record(genbuf, &mhdr, sizeof(mhdr)) == -1)
	return -1;
    hpressanykey("\033[1;31m我們會把搜尋結果很快就寄給你唷  ^_^\033[m");
    return 0;
}
