#!/usr/bin/perl
# $Id: stock.perl,v 1.1 2002/03/07 15:13:46 in2 Exp $
#
# 不能跑的話，看看 bbspost 的路徑是否正確。
# 如果發出的 post 沒有氣象報告而是說 URL 找不到，則確定一下能不能看到
# 中央氣象局的 WWW 及 URL 是否正確。
# 理論上適用所有 Eagle BBS 系列。
#                                       -- Beagle Apr 13 1997
open(BBSPOST, "| bin/webgrep >etc/stock.tmp");
# 日期
open(DATE, "date +'%a %b %d %T %Y' |");
$date = <DATE>;
chop $date;
close DATE;

# Header
# 內容
#open(WEATHER, "/usr/local/bin/lynx -dump http://www.dashin.com.tw/bulletin_board/today_stock_price.htm |"); while (<WEATHER>) {
open(WEATHER, "/usr/bin/lynx -dump http://quotecenter.jpc.com.tw/today_stock_price.htm |"); while(<WEATHER>) {
  print BBSPOST if ($_ ne "\n");
}
close WEATHER;

# 簽名檔
print BBSPOST "\n--\n";
print BBSPOST "我是beagle所有可愛的小餅乾...跨海為Ptt服務\n";
print BBSPOST "--\n";
print BBSPOST "☆ [Origin: ◎果醬小站◎] [From: [藍莓鬆餅屋]       ] ";

close BBSPOST;

