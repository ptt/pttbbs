#!/usr/bin/perl
if( -e '/usr/bin/mktemp' ){
    $fn = `/usr/bin/mktemp -q /tmp/filtermail.XXXXXXXX`;
}
else{
    $fn = `/bin/mktemp -q /tmp/filtermail.XXXXXXXX`;
}
chomp $fn;

$bbsuid = $ARGV[0];
undef @ARGV;

open FH, ">$fn";
$norelay = 0;
while( <> ){
    print FH $_;
#    if( ($bbsuid) = $_ =~ /for \<(\w+)\.bbs\@ptt/ ){
#    }
    if( /^Content-Type:/i ){
	$norelay = 1 if( (/multipart/i && !/report/i) || /html/ );
	last;
    }
}
while( <> ){
    print FH $_;
}
close FH;

if( !$norelay ){
    open FH, "<$fn";
    open OUT, "|/home/bbs/bin/realbbsmail $bbsuid";
    while( <FH> ){
	print OUT $_;
    }
    close FH;
    close OUT;
    unlink $fn;
}
else{
    $to = substr($fn, rindex($fn, '/') + 1);
    #`/bin/mv $fn /tmp/norelay/$to`;
    unlink $fn;
}

