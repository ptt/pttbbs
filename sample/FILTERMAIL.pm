#!/usr/bin/perl
# $Id$
# 本範例僅供參考, 配合 util/filtermail.pl 使用.
# 請依自行需要改寫後放置於 /home/bbs/bin 下.
# checkheader() 或 checkbody()  傳回為假時表示直接丟掉該封信.
package FILTERMAIL;

sub checkheader
{
    return 0
	if( $_[0] =~ /^Subject: .*行銷光碟/im ||
	    $_[0] =~ /^From: .*SpamCompany\.com/im );
    1;
}

sub checkbody
{
    return 0
	if( $_[0] =~ /<script language=\"JavaScript\"/im );
    1;
}

1;
