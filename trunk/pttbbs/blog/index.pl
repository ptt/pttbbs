#!/usr/bin/perl
# $Id: index.pl,v 1.1 2003/06/02 15:23:32 in2 Exp $
use CGI qw/:standard/;

print redirect("http://blog.ptt2.cc/blog.pl/$1/")
    if( $ENV{REDIRECT_REQUEST_URI} =~ m|/\?(.*)| );

return redirect("http://blog.ptt2.cc/blog.pl/Blog/");
