#!/usr/bin/perl
# $Id$
$SaveNoRelay = 0;

$bbsuid = $ARGV[0];
undef @ARGV;

$msg = '';
$norelay = 0;

while( <> ){
    $msg .= $_;

    $norelay = 1
	if( (/^Content-Type:/i && 
	     ((/multipart/i && !/report/i) || /html/)) ||
	    /Content-Type: audio\/x-wav; name=\".*.exe\"/ );
}

if( $norelay ){
    if( $SaveNoRelay ){
	$fn = `/usr/bin/mktemp -q /tmp/norelay.XXXXXXXX`;
	open FH, ">$fn";
	print FH $msg;
	close FH;
    }
}
else{
    open OUT, "|/home/bbs/bin/realbbsmail $bbsuid";
    print OUT $msg;
    close OUT;
}
