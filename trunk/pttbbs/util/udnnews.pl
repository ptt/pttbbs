#!/usr/bin/perl
#
# 請註意！
# 本程式版權屬於 PttBBS ,
# 但只表示您可以公開免費取得本程式,
# 並不表示您可以使用本程式.
#
# 本程式將直接連至 udnnews網站取得當前新聞,
# 而新聞內容的版權是屬於 聯合新聞網 所有.
# 亦即, 您若沒有聯合新聞網之書面授權,
# 您並「不能」使用本程式下載該網新聞.
#

use lib '/home/bbs/bin/';
use LocalVars;
use strict;
use vars qw/@titles/;

chdir '/home/bbs';
getudnnewstitle(\@titles);
foreach( @titles ){
    postout({brdname => 'udnnews',
             title   => strreplace(FormatChinese($_->[1])),
             owner   => 'udnnews.',
             content => getudnnewscontent("http://www.udn.com/NEWS/FOCUSNEWS/$_->[0]")});
}

sub strreplace
{
    my($str) = @_;
    $str =~ s/十/十/g;
    $str =~ s/卅/卅/g;
    $str =~ s/游錫\&\#22531\;/游揆/g;
    return $str;
}

sub getudnnewscontent($)
{
    my($url) = @_;
    my($buf, $content, $ret);
    $buf = `$LYNX -source '$url'`;
    ($content) = $buf =~ m|<!-- start of content -->(.*?)<!-- end of content -->|s;
#    ($content) = $buf =~ m|<p><font color="#CC0033" class="text12">(.*?)<tr valign="top">|s if( !$content );
    $content =~ s/<br>/\n/g;
    $content =~ s/<p>/\n/gi;
    $content =~ s/<.*?>//g;
    $content =~ s/\r//g;
    $content =~ s/\n\n\n/\n\n/g;
    $content =~ s/\n\n\n//g;
    $content = strreplace($content);
    undef $ret;
    foreach( split(/\n/, $content) ){
        s/ //g;
        $ret .= FormatChinese($_, 60). "\n" if( $_ );
    }
    return
           "作者: udnnews.(聯合新聞) 看板: udnnews".
           "標題: ".
           "時間: 即時".
           "※ [轉錄自 $url ]\n\n$ret\n\n".
           "--\n\n 聯合新聞網 http://www.udn.com/ 獨家授權批踢踢實業坊 ".
           "\n 未經允許請勿擅自使用 ";
}

sub getudnnewstitle($)
{
    my($ra_titles) = @_;
    my($url, $title);
    open FH, "$LYNX -source http://www.udn.com/NEWS/FOCUSNEWS/ | $GREP '<font color=\"#FF9933\">' |";
    while( <FH> ){
        ($url, $title) = $_ =~ m|<font color="#FF9933">．</font><a href="(.*?)"><font color="#003333">(.*?)</font></a><font color="#003333">|;
        $title =~ s/<.*?>//g;
        push @{$ra_titles}, [$url, $title];
    }
    close FH;
    return @{$ra_titles};
}

sub FormatChinese
{
    my($str, $length) = @_;
    my($i, $ret, $count, $s);
    return if( !$str );
    for( $i = 0 ; $i < length($str) ; ++$i ){
        if( ord(substr($str, $i, 1)) > 127 ){
            $ret .= substr($str, $i, 2);
            ++$i;
        }
        else{
            for( $count = 0, $s = $i ; $i < length($str) ; ++$i, ++$count ){
                last if( ord(substr($str, $i, 1)) > 127 );
            }
            --$i;
            $ret .= ' ' if( $count % 2 == 1);
            $ret .= substr($str, $s, $count);
        }
    }
    if( $length ){
        $str = $ret;
        undef $ret;
        while( $str ){
            $ret .= substr($str, 0, $length)."\n";
            $str = substr($str, $length);
        }
    }
    return $ret;
}

sub postout
{
    my($param) = @_;
    return if( !$param->{title} );
    open FH, ">/tmp/postout.$$";
    print FH $param->{content};
#print "$param->{content}";
    close FH;

    system("bin/post '$param->{brdname}' '$param->{title}' '$param->{owner}' /tmp/postout.$$");
    unlink "/tmp/postout.$$";
}
