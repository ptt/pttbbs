#!/usr/bin/perl
# $Id$
use lib '/home/bbs/bin/';
use Time::Local;
use LocalVars;
use Mail::Sender;
use IO::All;

sub main
{
    foreach $fndes ( <$JOBSPOOL/water.des.*> ){
	($userid, $mailto, $outmode, $fnsrc) = parsedes($fndes);
	next if( !userid || $mailto !~ /\@/ || !-e $fnsrc );

	print "($userid, $mailto, $outmode, $fnsrc)\n";
	undef %water;
	process($fnsrc, "$TMP/water/", $outmode, $userid);
	output($mailto eq '.' ? "$userid.bbs\@$MYHOSTNAME" : $mailto,
	       $mailto eq '.' || $mailto =~ /\.bbs/);
	unlink($fndes, $fnsrc);
    }
}

sub parsedes($)
{
    my $t < io($_[0]);
    my $fnsrc = $_[0];
    $fnsrc =~ s/\.des\./\.src\./;
    return (split("\n", $t), $fnsrc);
}

sub process($$$$)
{
    my($fn, $outdir, $outmode, $me) = @_;
    open DIN, "<$fn";
    while( <DIN> ){
	next if( !(($cmode, $who, $time, $say, $orig) = parse($_)) || !who );

	if( $outmode ){
	    $water{$who} .= $orig;
	} else {
	    next if( $say =~ /<<(¤W|¤U)¯¸³qª¾>> -- §Ú(¨«|¨Ó)Åo¡I/ );
	    if( $time - $LAST{$who} > 1800 ){
		$water{$who} .= (scalar localtime($LAST{$who}))."\n\n"
		    if( $LAST{$who} );
		$water{$who} .= scalar localtime($time) . "\n";
	    }
	    
	    $len = max(length($who), length($me)) + 1;
	    $water{$who} .= sprintf("%-${len}s %s\n",
				    ($cmode ? $who : $me).':' ,
				    $say);
	    $LAST{$who} = $time;
	}
    }
    if( $outmode == 0 ){
	$water{$_} .= scalar localtime($LAST{$_})
	    foreach( keys %LAST );
    }
}

sub parse($)
{
    my($str) = @_;
    my($cmode, $who, $year, $month, $day, $hour, $min, $sec, $say);
    $cmode = ($str =~ /^To/) ? 0 : 1;
    ($who, $say, $month, $day, $year, $hour, $min, $sec) =
	$cmode ?
	$str =~ m|¡¹(\w+?)\[37;45m\s*(.*).*?\[(\w+)/(\w+)/(\w+) (\w+):(\w+):(\w+)\]| :
	$str =~ m|^To (\w+):\s*(.*)\[(\d+)/(\d+)/(\d+) (\d+):(\d+):(\d+)\]|;
    return ( !(1 <= $month && $month <= 12 &&
	       1 <= $day   && $day   <= 31 &&
	       0 <= $hour  && $hour  <= 23 &&
	       0 <= $min   && $min   <= 59 &&
	       0 <= $sec   && $sec   <= 59 &&
	       1970 <= $year && $year <= 2038) ?
	     () :
	     ($cmode, $who,
	      timelocal($sec, $min, $hour, $day, $month - 1, $year),
	      $say, $_[0]) );
}

sub output
{
    my($tomail, $bbsmail) = @_;
    my $ms = new Mail::Sender{smtp    => $SMTPSERVER,
			      from    => "$userid.bbs\@$MYHOSTNAME",
			      charset => 'big5'};

    foreach( keys %water ){
	$ms->MailMsg({to      => $tomail,
		      subject => "©M $_ ªº¤ô²y°O¿ý",
		      msg     => $water{$_}});
    }
}

sub max
{
    return $_[0] > $_[1] ? $_[0] : $_[1];
}

main();
1;
