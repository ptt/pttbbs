#!/usr/bin/perl
# $Id$
use CGI qw/:standard/;
use lib qw/./;

sub main
{
    print redirect("/blog.pl/$1/")
	if( $ENV{REDIRECT_REQUEST_URI} =~ m|/\?(.*)| );

    return redirect("/blog.pl/$BLOGdefault/");
}

main();
1;

