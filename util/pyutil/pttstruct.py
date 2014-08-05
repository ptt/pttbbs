#!/usr/bin/env python
# -*- coding: Big5 -*-

import collections
import struct
import big5

IDLEN = 12
IPV4LEN = 15
PASSLEN = 14
REGLEN = 38

PASSWD_VERSION = 4194

USEREC_SIZE = 512
USEREC_FMT = (
	("version", "I"),
	("userid", "%ds" % (IDLEN+1)),
	("realname", "20s"),
	("nickname", "24s"),
	("passwd", "%ds" % PASSLEN),
	("pad_1", "B"),
	("uflag", "I"),
	("deprecated_uflag2", "I"),
	("userlevel", "I"),
	("numlogindays", "I"),
	("numposts", "I"),
	("firstlogin", "I"),
	("lastlogin", "I"),
	("lasthost", "%ds" % (IPV4LEN+1)),
	("money", "I"),
	("_unused", "4s"),
	("email", "50s"),
	("address", "50s"),
	("justify", "%ds" % (REGLEN + 1)),
	("month", "B"),
	("day", "B"),
	("year", "B"),
	("_unused3", "B"),
	("pager_ui_type", "B"),
	("pager", "B"),
	("invisible", "B"),
	("_unused4", "2s"),
	("exmailbox", "I"),
	("_unused5", "4s"),
	("career", "40s"),
	("phone", "20s"),
	("_unused6", "I"),
	("chkpad1", "44s"),
	("role", "I"),
	("lastseen", "I"),
	("timesetangel", "I"),
	("timeplayangel", "I"),
	("lastsong", "I"),
	("loginview", "I"),
	("_unused8", "B"),
	("pad_2", "B"),
	("vl_count", "H"),
	("five_win", "H"),
	("five_lose", "H"),
	("five_tie", "H"),
	("chc_win", "H"),
	("chc_lose", "H"),
	("chc_tie", "H"),
	("mobile", "I"),
	("mind", "4s"),
	("go_win", "H"),
	("go_lose", "H"),
	("go_tie", "H"),
	("dark_win", "H"),
	("dark_lose", "H"),
	("_unused9", "B"),
	("signature", "B"),
	("_unused19", "B"),
	("badpost", "B"),
	("dark_tie", "H"),
	("myangel", "%ds" % (IDLEN + 1)),
	("pad_3", "B"),
	("chess_elo_rating", "H"),
	("withme", "I"),
	("timeremovebadpost", "I"),
	("timeviolatelaw", "I"),
	("pad_trail", "28s"),
	)

BTLEN = 48

BOARDHEADER_SIZE = 256
BOARDHEADER_FMT = (
    ("brdname", "%ds" % (IDLEN + 1)),
    ("title", "%ds" % (BTLEN + 1)),
    ("BM", "%ds" % (IDLEN * 3 + 3)),
    ("pad1", "3s"),
    ("brdattr", "I"),  # uint32_t
    ("chesscountry", "B"),
    ("vote_limit_posts", "B"),
    ("vote_limit_logins", "B"),
    ("pad2_1", "B"),
    ("bupdate", "I"),  # time4_t
    ("_post_limit_posts", "B"),
    ("post_limit_logins", "B"),
    ("pad2_2", "B"),
    ("bvote", "B"),
    ("vtime", "I"),  # time4_t
    ("level", "I"),  # uint32_t
    ("perm_reload", "I"),  # time4_t
    ("gid", "I"),  # uint32_t
    ("next0", "I"),  # uint32_t
    ("next1", "I"),  # uint32_t
    ("firstchild0", "I"),  # uint32_t
    ("firstchild1", "I"),  # uint32_t
    ("parent", "I"),  # uint32_t
    ("childcount", "I"),  # uint32_t
    ("nuser", "I"),  # uint32_t
    ("postexpire", "I"),  # uint32_t
    ("endgamble", "I"),  # time4_t
    ("posttype", "33s"),
    ("posttype_f", "B"),
    ("fastrecommend_pause", "B"),
    ("vote_limit_badpost", "B"),
    ("post_limit_badpost", "B"),
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

def get_format(format_pattern):
    return '<' + ''.join(value for _, value in format_pattern)

def unpack_data(blob, format_pattern):
    fmt = get_format(format_pattern)
    data = dict(zip((name for name, _ in format_pattern),
		    struct.unpack_from(fmt, blob)))
    # Convert C-style strings.
    for name, pat in format_pattern:
	if 's' in pat:
	    data[name] = big5.decode(data[name].partition(chr(0))[0])
    return data

def pack_data(data, format_pattern):
    fmt = get_format(format_pattern)
    values = (big5.encode(data[name]) if 's' in f else data[name]
	      for name, f in format_pattern)
    return struct.pack(fmt, *values)

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
    assert struct.calcsize(get_format(BOARDHEADER_FMT)) == BOARDHEADER_SIZE
    assert struct.calcsize(get_format(FILEHEADER_FMT)) == FILEHEADER_SIZE
    assert struct.calcsize(get_format(USEREC_FMT)) == USEREC_SIZE

    with open('/home/bbs/boards/A/ALLPOST/.DIR', 'rb') as f:
	entry = f.read(FILEHEADER_SIZE)
    header = unpack_data(entry, FILEHEADER_FMT)
    print header
    xblob = pack_data(header, FILEHEADER_FMT)
    assert xblob == entry

    with open('/home/bbs/.PASSWDS', 'rb') as f:
	entry = f.read(USEREC_SIZE)
    user = unpack_data(entry, USEREC_FMT)
    print user

    with open('/home/bbs/.BRD', 'rb') as f:
	entry = f.read(BOARDHEADER_SIZE)
    board = unpack_data(entry, BOARDHEADER_FMT)
    print board

    print 'user name: ', big5.encode(user['realname'])
    print 'file title: ', big5.encode(header['title'])
    print 'board title: ', big5.encode(board['title'])
