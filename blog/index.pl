#!/usr/bin/perl
# $Id: index.pl,v 1.2 2003/06/20 04:35:51 in2 Exp $
use CGI qw/:standard/;
use lib qw/./;
use LocalVars;

sub main
{
    print redirect("/blog.pl/$1/")
	if( $ENV{REDIRECT_REQUEST_URI} =~ m|/\?(.*)| );

    return redirect("/blog.pl/$BLOGdefault/");
}

main();
1;

