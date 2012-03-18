#!/usr/bin/env python
# -*- coding: Big5 -*-

import collections
import struct

IDLEN = 12
IPV4LEN = 15
PASSLEN = 14
REGLEN = 38

PASSWD_VERSION = 4194

#define PASSWD_VERSION	4194

"""
typedef struct userec_t {
    uint32_t	version;	/* version number of this sturcture, we
    				 * use revision number of project to denote.*/

    char	userid[IDLEN+1];/* 使用者ID */
    char	realname[20];	/* 真實姓名 */
    char	nickname[24];	/* 暱稱 */
    char	passwd[PASSLEN];/* 密碼 */

    char	pad_1;

    uint32_t    uflag;		/* 習慣1 , see uflags.h */
    uint32_t    deprecated_uflag2;		/* deprecated: 習慣2 , see uflags.h */
    uint32_t    userlevel;	/* 權限 */
    uint32_t    numlogindays;	/* 上線資歷 (每日最多+1的登入次數) */
    uint32_t    numposts;	/* 文章篇數 */
    time4_t	firstlogin;	/* 註冊時間 */
    time4_t	lastlogin;	/* 最近上站時間(包含隱身) */
    char	lasthost[IPV4LEN+1];/* 上次上站來源 */
    int32_t     money;		/* Ptt幣 */

    char	unused_1[4];

    char	email[50];	/* Email */
    char	address[50];	/* 住址 */
    char	justify[REGLEN + 1];/* 審核資料 */
    uint8_t	month;		/* 生日 月 */
    uint8_t	day;		/* 生日 日 */
    uint8_t	year;		/* 生日 年 */
    uint8_t     nonuse_sex;     /* 性別 (已停用), 未清空過 */

    uint8_t	pager_ui_type;	/* 呼叫器界面類別 (was: WATER_*) */
    uint8_t	pager;		/* 呼叫器狀態 */
    uint8_t	invisible;	/* 隱形狀態 */

    char	unused_3[2];

    uint32_t    exmailbox;	/* 購買信箱數 TODO short 就夠了 */

    // r3968 移出 sizeof(chicken_t)=128 bytes
    char	chkpad0[4];

    char	career[40];	/* 學歷職業 */
    char	phone[20];	/* 電話 */

    uint32_t	old_numlogins;	/* 轉換前的 numlogins, 備份檢視用 */
    char	chkpad1[48];
    time4_t	lastseen;	/* 最近上站時間(隱身不計) */
    time4_t	chkpad2[2];	/* in case 有人忘了把 time4_t 調好... */
    // 以上應為 sizeof(chicken_t) 同等大小
    
    time4_t	lastsong;	/* 上次點歌時間 */
    uint32_t	loginview;	/* 進站畫面 */

    uint8_t	unused_4;	// was: channel
    uint8_t	pad_2;

    uint16_t	vl_count;	/* 違法記錄 ViolateLaw counter */
    uint16_t	five_win;	/* 五子棋戰績 勝 */
    uint16_t	five_lose;	/* 五子棋戰績 敗 */
    uint16_t	five_tie;	/* 五子棋戰績 和 */
    uint16_t	chc_win;	/* 象棋戰績 勝 */
    uint16_t	chc_lose;	/* 象棋戰績 敗 */
    uint16_t	chc_tie;	/* 象棋戰績 和 */
    int32_t     mobile;		/* 手機號碼 */
    char	mind[4];	/* 心情 XXX not a null-terminate string */
    uint16_t	go_win;		/* 圍棋戰績 勝 */
    uint16_t	go_lose;	/* 圍棋戰績 敗 */
    uint16_t	go_tie;		/* 圍棋戰績 和 */

    char	unused_5[5];	/* 從前放 ident 身份證字號，使用前請先清0 */

    uint8_t	signature;	/* 慣用簽名檔 */
    uint8_t	goodpost;	/* 評價為好文章數 */
    uint8_t	badpost;	/* 評價為壞文章數 */
    uint8_t	unused_6;	/* 從前放競標好評(goodsale), 使用前請先清0 */
    uint8_t	unused_7;	/* 從前放競標壞評(badsale),  使用前請先清0 */
    char	myangel[IDLEN+1];/* 我的小天使 */

    char	pad_3;

    uint16_t	chess_elo_rating;/* 象棋等級分 */
    uint32_t    withme;		/* 我想找人下棋，聊天.... */
    time4_t	timeremovebadpost;/* 上次刪除劣文時間 */
    time4_t	timeviolatelaw; /* 被開罰單時間 */

    char	pad_tail[28];
} PACKSTRUCT userec_t;
"""

BTLEN = 48

BOARDHEADER_SIZE = 256
BOARDHEADER_FMT = (
    ("brdname": "%ds" % (IDLEN + 1)),
    ("title": "%ds" % (BTLEN + 1)),
    ("BM": "%ds" % (IDLEN * 3 + 3)),
    ("pad1": "3s"),
    ("brdattr": "I"),  # uint32_t
    ("chesscountry": "B"),
    ("vote_limit_posts": "B"),
    ("vote_limit_logins": "B"),
    ("pad2_1": "1B"),
    ("bupdate": "I"),  # time4_t
    ("post_limit_posts": "B"),
    ("post_limit_logins": "B"),
    ("pad2_2": "1B"),
    ("bvote": "1B"),
    ("vtime": "I"),  # time4_t
    ("level": "I"),  # uint32_t
    ("perm_reload": "I"),  # time4_t
    ("gid": "I"),  # uint32_t
    ("next": "2I"),  # uint32_t
    ("firstchild": "2I"),  # uint32_t
    ("parent": "I"),  # uint32_t
    ("childcount": "I"),  # uint32_t
    ("nuser": "I"),  # uint32_t
    ("postexpire": "I"),  # uint32_t
    ("endgamble": "I"),  # time4_t
    ("posttype", "33s"),
    ("posttype_f", "B"),
    ("fastrecommend_pause", "B"),
    ("vote_limit_badpost": "B"),
    ("post_limit_badpost": "B"),
    ("pad3", "3s"),
    ("SRexpire", "I"),  # time4_t
    ("pad4", "40s"),
    )

BRD_NOCOUNT            = 0x00000002      # 不列入統計 
BRD_NOTRAN             = 0x00000004      # 不轉信 
BRD_GROUPBOARD         = 0x00000008      # 群組板 
BRD_HIDE               = 0x00000010      # 隱藏板 (看板好友才可看) 
BRD_POSTMASK           = 0x00000020      # 限制發表或閱讀 
BRD_ANONYMOUS          = 0x00000040      # 匿名板 
BRD_DEFAULTANONYMOUS   = 0x00000080      # 預設匿名板 
BRD_NOCREDIT           = 0x00000100      # 發文無獎勵看板 
BRD_VOTEBOARD          = 0x00000200      # 連署機看板 
BRD_WARNEL             = 0x00000400      # 連署機看板 
BRD_TOP                = 0x00000800      # 熱門看板群組 
BRD_NORECOMMEND        = 0x00001000      # 不可推薦 
BRD_BLOG               = 0x00002000      # (已停用) 部落格 
BRD_BMCOUNT            = 0x00004000      # 板主設定列入記錄 
BRD_SYMBOLIC           = 0x00008000      # symbolic link to board 
BRD_NOBOO              = 0x00010000      # 不可噓 
BRD_LOCALSAVE          = 0x00020000      # 預設 Local Save 
BRD_RESTRICTEDPOST     = 0x00040000      # 板友才能發文 
BRD_GUESTPOST          = 0x00080000      # guest能 post 
BRD_COOLDOWN           = 0x00100000      # 冷靜 
BRD_CPLOG              = 0x00200000      # 自動留轉錄記錄 
BRD_NOFASTRECMD        = 0x00400000      # 禁止快速推文 
BRD_IPLOGRECMD         = 0x00800000      # 推文記錄 IP 
BRD_OVER18             = 0x01000000      # 十八禁 
BRD_NOREPLY            = 0x02000000      # 不可回文 
BRD_ALIGNEDCMT         = 0x04000000      # 對齊式的推文 
BRD_NOSELFDELPOST      = 0x08000000      # 不可自刪 


FNLEN = 28  # Length of filename
TTLEN = 64  # Length of title
DISP_TTLEN = 46

FILEHEADER_FMT = (
        ("filename", "%ds" % FNLEN),
        ("modified", "I"),
        ("pad", "B"),
        ("recommend", "b"),
        ("owner", "%ds" % (IDLEN + 2)),
        ("date", "6s"),
        ("title", "%ds" % (TTLEN + 1)),
        ("pad2", "B"),
        ("multi", "i"),
        ("filemode", "B"),
        ("pad3", "3s"))

FILEHEADER_SIZE = 128

def get_fileheader_format():
    return '<' + ''.join(value for _, value in FILEHEADER_FMT)

def unpack_fileheader(blob):
    fmt = get_fileheader_format()
    assert struct.calcsize(fmt) == FILEHEADER_SIZE
    return dict(zip((name for name, _ in FILEHEADER_FMT),
        struct.unpack_from(fmt, blob)))

FILE_LOCAL     = 0x01    # local saved,  non-mail
FILE_READ      = 0x01    # already read, mail only
FILE_MARKED    = 0x02    # non-mail + mail
FILE_DIGEST    = 0x04    # digest,       non-mail
FILE_REPLIED   = 0x04    # replied,      mail only
FILE_BOTTOM    = 0x08    # push_bottom,  non-mail
FILE_MULTI     = 0x08    # multi send,   mail only
FILE_SOLVED    = 0x10    # problem solved, sysop/BM non-mail only
FILE_HIDE      = 0x20    # hide,        in announce
FILE_BID       = 0x20    # bid,         in non-announce
FILE_BM        = 0x40    # BM only,     in announce
FILE_VOTE      = 0x40    # for vote,    in non-announce
FILE_ANONYMOUS = 0x80    # anonymous file

STRLEN = 80 # Length of most string data


if __name__ == '__main__':
    with open('/home/bbs/boards/A/ALLPOST/.DIR', 'rb') as f:
	entry = f.read(FILEHEADER_SIZE)
    header = unpack_fileheader(entry)
    print header
    print header['title'].decode('big5')
