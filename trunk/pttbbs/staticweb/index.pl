#!/usr/bin/perl
# $Id: index.pl,v 1.4 2003/07/11 10:11:24 in2 Exp $
use lib qw/./;
use LocalVars;
use CGI qw/:standard/;
use strict;
use Template;
use boardlist;
use b2g;

sub main
{
    my($tmpl, %rh, $bid);

    if( param('gb') ){
	$rh{gb} = 1;
	$rh{encoding} = 'gb2312';
	$rh{lang} = 'zh_CN';
	$rh{charset} = 'gb2312';    }
    else{
	print redirect("/man.pl/$1/")
	    if( $ENV{REDIRECT_REQUEST_URI} =~ m|/\?(.*)| );
	$rh{encoding} = 'Big5';
	$rh{lang} = 'zh_TW';
	$rh{charset} = 'big5';
    }

    charset('');
    print header();

    ($bid) = $ENV{PATH_INFO} =~ m|^/(\d+)/$|;
    $bid ||= 0;

    if( !$brd{$bid} ){
	print "sorry, this bid $bid not found :(";
	return ;
    }

    foreach( @{$brd{$bid}} ){
	next if( $_->[0] == -1 && ! -e "$MANDATA/$_->[1].db" );
	$_->[2] =~ s/([\xA1-\xF9].)/$b2g{$1}/eg if( $rh{gb} );
	push @{$rh{dat}}, $_;
    }

    $tmpl = Template->new({INCLUDE_PATH => '.',
			   ABSOLUTE => 0,
			   RELATIVE => 0,
			   RECURSION => 0,
			   EVAL_PERL => 0,
			   COMPILE_EXT => '.tmpl',
			   COMPILE_DIR => $MANCACHE});
    $tmpl->process('index.html', \%rh);
}

main();
1;
