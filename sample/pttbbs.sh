#!/bin/sh
#
# 請注意！這個檔案將以 root 的權限執行！
# 預設使用 bbs這個帳號，安裝目錄為 /home/bbs。

case "$1" in
start)
	# 初始化 shared-memory, 載入 uhash, utmpsortd, timed(if necessary)
	# 如果使用 USE_HUGETLB, 請先保留 huge page 並設定好權限
	# (hugetlbfs 掛載點需可被 bbs 寫入), 詳見 pttbbs.conf 的 USE_HUGETLB 說明
	/usr/bin/su -fm bbs -c '/home/bbs/bin/shmctl init'

	# 寄信至站外
	/usr/bin/su -fm bbs -c /home/bbs/bin/outmail &

	# 啟動 port 23 (port 23須使用 root 才能進行 bind ) 以其他
	/home/bbs/bin/bbsctl start

	# 提示
	echo -n ' mbbsd'
	;;
stop)
	/usr/bin/killall outmail
	/usr/bin/killall mbbsd
	/usr/bin/killall shmctl
	/bin/sleep 2; /usr/bin/killall shmctl
	;;
*)
	echo "Usage: `basename $0` {start|stop}" >&2
	;;
esac

exit 0
