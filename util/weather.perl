#!/usr/bin/perl
# $Id: weather.perl,v 1.8 2003/05/06 16:54:42 victor Exp $
#
# 不能跑的話，看看 bbspost 的路徑是否正確。
# 如果發出的 post 沒有氣象報告而是說 URL 找不到，則確定一下能不能看到
# 中央氣象局的 WWW 及 URL 是否正確。
# 理論上適用所有 Eagle BBS 系列。
#                                       -- Beagle Apr 13 1997

use lib '/home/bbs/bin';
use LocalVars;

weather_report('etc/weather.today', 'ftp://ftpsv.cwb.gov.tw/pub/forecast/W002.txt');
weather_report('etc/weather.tomorrow', 'ftp://ftpsv.cwb.gov.tw/pub/forecast/W003.txt');

sub weather_report
{
    my ($file, $link) = @_;
    open(BBSPOST, "| $file");

# Header
# 內容
#open(WEATHER, "$LYNX -assume_charset=big5 -assume_local_charset=big5 -dump http://www.cwb.gov.tw/V3.0/weather/text/W03.htm |");
    open(WEATHER, "$LYNX -assume_charset=big5 -assume_local_charset=big5 -dump -nolist $link|");

    while (<WEATHER>) {
	print BBSPOST if ($_ ne "\n");
    }
    close WEATHER;

# 簽名檔
    print BBSPOST "\n--\n";
    print BBSPOST "我是beagle所有可愛的小餅乾...跨海為Ptt服務\n";
    print BBSPOST "--\n";
    print BBSPOST "☆ [Origin: ◎果醬小站◎] [From: [藍莓鬆餅屋]       ] ";

    close BBSPOST;
}
