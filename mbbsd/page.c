/* $Id$ */
#include "bbs.h"

#define hpressanykey(a) {move(22, 0); outs(a); pressanykey();}
#define TITLE "\033[1;37;45m 火車查詢系統 \033[1;44;33m原作者:Heat\033[m"

static void
print_station(const char * const addr[6][100], int path, int *line, int *num)
{
	int i;

	*num = 0;
	move(*line,0);
	do{
		for(i=0; i<7 && addr[path - 1][*num]!=NULL; i++){
			prints(" %2d.%-6s", (*num)+1, addr[path - 1][*num]);
			(*num)++;
		}
		outc('\n');
		(*line)++;
	}while(i==7);
}

int
main_railway()
{
    fileheader_t    mhdr;
    char            genbuf[200];
    int             from, to, time_go, time_reach, date, path;
    int             line, station_num;
    char            tt[2], type[2];
    char            command[256], buf[8];
	static const char * const addr[6][100] = {
		{
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
			"九曲堂", "屏東", NULL
		},
		{
			"樹林", "板橋", "萬華", "台北", "松山", "南港", "汐止", "基隆", "八堵",
			"暖暖", "四腳亭", "瑞芳", "侯硐", "三貂嶺", "牡丹", "雙溪", "貢寮",
			"福隆", "石城", "大里", "大溪", "龜山", "外澳", "頭城", "頂埔", "礁溪",
			"四城", "宜蘭", "二結", "中里", "羅東", "冬山", "新馬", "蘇澳新站",
			"蘇澳", "永樂", "東澳", "南澳", "武塔", "漢本", "和平", "和仁", "崇德",
			"新城", "景美", "北埔", "花蓮", "吉安", "志學", "平和", "壽豐", "豐田",
			"溪口", "南平", "鳳林", "萬榮", "光復", "大富", "富源", "瑞北", "瑞穗",
			"三民", "玉里", "安通", "東里", "東竹", "富里", "池上", "海瑞", "關山",
			"月美", "瑞和", "瑞源", "鹿野", "山里", "台東", NULL
		},
		{
			"高雄", "鳳山", "後庄", "九曲堂", "六塊厝", "屏東", "歸來", "麟洛",
			"西勢", "竹田", "潮州", "崁頂", "南州", "鎮安", "林邊", "佳冬", "東海",
			"枋寮", "加祿", "內獅", "枋山", "古莊", "大武", "瀧溪", "多良", "金崙",
			"太麻里", "知本", "康樂", "台東", NULL
		},
		{
			"八堵", "暖暖", "四腳亭", "瑞芳", "侯硐", "三貂嶺", "大華", "十分",
			"望古", "嶺腳", "平溪", "菁桐", NULL
		},
		{
			"新竹", "竹中", "上員", "榮華", "竹東", "橫山", "九讚頭", "合興", "南河",
			"內灣", NULL
		},
		{
			"台中", "烏日", "成功\", "彰化", "花壇", "員林", "永靖", "社頭", "田中",
			"二水", "源泉", "濁水", "龍泉", "集集", "水里", "車埕", NULL
		}
	};

    setutmpmode(RAIL_WAY);
    clear();
    move(0, 25);
    outs(TITLE);
    move(1, 0);

    getdata(3, 0, "\033[1;35m你確定要搜尋嗎?[y/n]:\033[m", buf, 2, LCECHO);
    if (buf[0] != 'y' && buf[0] != 'Y')
	return 0;
    outs("\033[1;33m1.西部幹線(含台中線)  2.東部幹線(含北迴線)\n");
    outs("\033[1;33m3.南迴線  4.平溪線  5.內灣線  6.集集線\n");
    while (1)
    if (getdata(7, 0, "\033[1;35m請選擇路線(1-6):\033[m", buf, 2, LCECHO) &&
   	 (path = atoi(buf)) >= 1 && path <= 6)
	    break;

    clear();
    move(0, 25);
    outs(TITLE);
	line = 3;
	print_station(addr, path, &line, &station_num);
    sprintf(genbuf, "\033[1;35m請輸入起站(1-%d):\033[m", station_num);
    while (1)
	if (getdata(line, 0, genbuf, buf, 3, LCECHO) && (from = atoi(buf)) >= 1 && from <= station_num)
	   	break;
    sprintf(genbuf, "\033[1;35m請輸入終站(1-%d):\033[m", station_num);
    while (1)
	if (getdata(line, 40, genbuf, buf, 3, LCECHO) && (to = atoi(buf)) >= 1 && to <= station_num)
	   	break;
	line++;
	
    while (1)
	if (getdata(line, 0, "\033[1;35m請輸入時間區段(0-23) 由:\033[m",
		    buf, 3, LCECHO) &&
	    (time_go = atoi(buf)) >= 0 && time_go <= 23)
	    break;
    while (1)
	if (getdata(line, 40, "\033[1;35m到:\033[m", buf, 3, LCECHO) &&
	    (time_reach = atoi(buf)) >= 0 && time_reach <= 23)
	    break;
	line++;
	if (path<=3){
    while (1)
		if (getdata(line, 0, "\033[1;35m想查詢 1:對號快車  2:普通平快\033[m",
		    	type, 2, LCECHO) && (type[0] == '1' || type[0] == '2'))
	    	break;
		line++;
	}
    while (1)
	if (getdata(line, 0, "\033[1;35m欲查詢 1:出發時間  2:到達時間\033[m",
		    tt, sizeof(tt), LCECHO) &&
	    (tt[0] == '1' || tt[0] == '2'))
	    break;
	line++;
    while (1)
	if (getdata(line, 0, "\033[1;35m請輸入欲查詢日期(0-29)天後\033[m",
		    buf, 3, LCECHO) && (date = atoi(buf))>=0 && date<=29)
	    break;
	line++;

    sethomepath(genbuf, cuser.userid);
    stampfile(genbuf, &mhdr);
    strlcpy(mhdr.owner, "Ptt搜尋器", sizeof(mhdr.owner));
    strncpy(mhdr.title, "火車時刻搜尋結果", TTLEN);

    snprintf(command, sizeof(command), "echo \"path=%d from-station=%s to-station=%s"
	    " from-time=%02d to-time=%02d tt=%s type=%s date=%d\" | "BBSHOME"/bin/railway_wrapper.pl > %s",
	    path, addr[path - 1][from - 1], addr[path - 1][to - 1], time_go, time_reach,
	    (tt[0] == '1') ? "start" : "arriv",
	    (type[0] == '1') ? "fast" : "slow", date, genbuf);

    system(command);
    sethomedir(genbuf, cuser.userid);
    if (append_record(genbuf, &mhdr, sizeof(mhdr)) == -1)
	return -1;
    hpressanykey("\033[1;31m我們會把搜尋結果很快地寄給你唷  ^_^\033[m");
    return 0;
}
