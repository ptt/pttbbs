#!/usr/bin/perl
# $Id: index.pl,v 1.2 2003/07/04 05:59:05 in2 Exp $
use lib qw/./;
use LocalVars;
use CGI qw/:standard/;
use strict;
use Template;

sub main
{
    my($tmpl, %rh);

    print redirect("/man.pl/$1/")
	if( $ENV{REDIRECT_REQUEST_URI} =~ m|/\?(.*)| );

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
