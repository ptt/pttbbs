#!/usr/local/bin/pperl
# $Id$
use lib qw(/home/bbs/bin/);
use FILTERMAIL;
$bbsuid = $ARGV[0];

undef @ARGV;
undef $header;
undef $body;

while( <> ){
    $header .= $_;
    last if( $_ =~ /^\n/ );
}
while( <> ){
    $body .= $_;
}

if( FILTERMAIL::checkheader($header) && FILTERMAIL::checkbody($body) ){
    open FH, "|/home/bbs/bin/realbbsmail $bbsuid";
    print FH $header;
    print FH $body;
    close FH;
}
=xxx
else {
    $fn = `/usr/bin/mktemp -q /tmp/norelay.XXXXXXXX`;
    open FH, ">$fn";
    print FH $msg;
    close FH;
}
=cut
