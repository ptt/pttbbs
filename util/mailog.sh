#!/bin/sh
# $Id: mailog.sh,v 1.1 2002/03/07 15:13:46 in2 Exp $
#
# 整理出廣告信名單

bin/antispam 
bin/post Record 今日違法廣告信名單 [Ptt警察局] etc/spam
bin/post Security 站外來信紀錄mailog [系統安全局] etc/mailog
rm etc/mailog
