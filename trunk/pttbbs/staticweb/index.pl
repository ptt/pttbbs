#!/usr/bin/perl
# $Id: index.pl,v 1.3 2003/07/05 05:19:18 in2 Exp $
use lib qw/./;
use LocalVars;
use CGI qw/:standard/;
use strict;
use Template;

sub main
{
    my($tmpl, %rh);

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

    foreach( </home/web/man/data/*.db> ){
	s/.*\///;
	s/\.db//;
	push @{$rh{dat}}, {brdname => $_};
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
