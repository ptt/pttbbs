#!/usr/bin/perl
use lib '/home/bbs/bin/';
use LocalVars;
# 每農曆初一, 十五就會跑一次 $ARGV[0]
# 資料來源 http://tw.weathers.yahoo.com/
open FH, "$LYNX -source http://tw.weathers.yahoo.com/ | grep '民國'|";
$din = <FH>;
close FH;

($month, $day) = $din =~ /農曆 (.*?)月 (.*?)日/;
system("@ARGV") if( $day eq '一' || $day eq '一十五' );
