#!/usr/bin/perl
use lib '/home/bbs/bin/';
use LocalVars;
use Time::Local;
use POSIX;
use FileHandle;
use strict;
use Mail::Sender;

my($fndes, $fnsrc, $userid, $mailto, $outmode);
foreach $fndes ( <$JOBSPOOL/water.des.*> ){ #des: userid, mailto, outmode
    (open FH, "< $fndes") or next;
    chomp($userid = <FH>);
    chomp($mailto = <FH>);
    chomp($outmode= <FH>);
    close FH;
    next if( !$userid );
    print "$userid, $mailto, $outmode\n";
    `rm -Rf $TMP/water`;
    `mkdir $TMP/water`;

    $fnsrc = $fndes;
    $fnsrc =~ s/\.des\./\.src\./;
    eval{
	process($fnsrc, "$TMP/water/", $outmode, $userid);
    };
    if( $@ ){
	print "$@\n";
    }
    else{
	chdir "$TMP/water";
	if( $mailto eq '.' || $mailto =~ /\.bbs/ ){
	    $mailto = "$userid.bbs\@$hostname" if( $mailto eq '.' );
	    foreach my $fn ( <$TMP/water/*> ){
		my $who = substr($fn, rindex($fn, '/') + 1);
		my $content = '';
		open FH, "< $fn";while( <FH> ){chomp;$content .= "$_\n";}
		if( !MakeMail({mailto  => $mailto,
			       subject => "©M $who ªº¤ô²y°O¿ý",
			       body    => $content,
			   }) ){ print "fault\n"; }
	    }
	    unlink $fnsrc;
	    unlink $fndes;
	}
	else{
	    if( MakeMail({tartarget => "$TMP/$userid.waterball.tgz",
			  tarsource => "*",
			  mailto    => "$userid <$mailto>",
			  subject   => "¤ô²y¬ö¿ý",
			  body      => 
			      "\n $hostname ¯¸ªø¸s ". POSIX::ctime(time())}
			 ) ){
		unlink $fnsrc;
		unlink $fndes;
	    }
	}
    }
}

sub process
{
    my($fn, $outdir, $outmode, $me) = @_;
    my($cmode, $who, $time, $say, $orig, %FH, %LAST, $len);
    open DIN, "< $fn";
    while( <DIN> ){
	chomp;
	next if( !(($cmode, $who, $time, $say, $orig) = parse($_)) );
	next if( !$who );

	if( ! $FH{$who} ){
	    $FH{$who} = new FileHandle "> $outdir/$who";
	}
	if( $outmode == 0 ){
	    next if( $say =~ /<<¤U¯¸³qª¾>> -- §Ú¨«Åo¡I/ ||
		     $say =~ /<<¤W¯¸³qª¾>> -- §Ú¨Ó°Õ¡I/    );
	    if( $time - $LAST{$who} > 1800 ){
		if( $LAST{$who} != 0 ){
		    ($FH{$who})->print( POSIX::ctime($LAST{$who}) , "\n");
		}
		($FH{$who})->print( POSIX::ctime($time) );
		$LAST{$who} = $time;
	    }
	    $len = (length($who) > length($me) ? length($who) : length($me))+1;
	    ($FH{$who})->printf("%-${len}s %s\n", ($cmode?$who:$me).':', $say);
	}
	elsif( $outmode == 1 ){
	    ($FH{$who})->print("$orig\n");
	}
    }
    if( $outmode == 0 ){
	foreach( keys %FH ){
	    ($FH{$_})->print( POSIX::ctime($LAST{$_}) );
	}
    }
    foreach( keys %FH ){
	($FH{$_})->close();
    }
    close DIN;
}

sub parse
{
    my $dat = $_[0];
    my($cmode, $who, $year, $month, $day, $hour, $min, $sec, $say);
    if( $dat =~ /^To/ ){
	$cmode = 0;
	($who, $say, $month, $day, $year, $hour, $min, $sec) =
	    $dat =~ m|^To (\w+):\s*(.*)\[(\d+)/(\d+)/(\d+) (\d+):(\d+):(\d+)\]|;
    }
    else{
	$cmode = 1;
	($who, $say, $month, $day, $year, $hour, $min, $sec) =
	    $dat =~ m|¡¹(\w+?)\[37;45m\s*(.*)\[m \[0m\[(\w+)/(\w+)/(\w+) (\w+):(\w+):(\w+)\]|;

    }
#    $time = timelocal($sec,$min,$hours,$mday,$mon,$year);

    return undef if( $month == 0 );
    return ($cmode, $who, timelocal($sec, $min, $hour, $day, $month - 1, $year), $say, $_[0]);
}

sub MakeMail
{
    my($arg) = @_;
    my $sender;
    `$TAR zcf $arg->{tartarget} $arg->{tarsource}`
	if( $arg->{tarsource} );
    $sender = new Mail::Sender{smtp => $SMTPSERVER,
			       from => "$hostname¤ô²y¾ã²zµ{¦¡ <$userid.bbs\@$MYHOSTNAME>"};
    foreach( 0..3 ){
	if( (!$arg->{tartarget} &&
	     $sender->MailMsg({to      => $arg->{mailto},
			       subject => $arg->{subject},
			       msg     => $arg->{body}
			   }) ) ||
	    ($arg->{tartarget} && 
	     $sender->MailFile({to      => $arg->{mailto},
				subject => $arg->{subject},
				msg     => $arg->{body},
				file    => $arg->{tartarget}})) ){
		unlink $arg->{tartarget} if( $arg->{tartarget} );
		return 1;
	    }
    }
    $sender->MailMsg({to      => "$userid.bbs\@$MYHOSTNAME",
		      subject => "µLªk±H¥X¤ô²y¾ã²z",
		      msg     =>
			  "¿Ë·Rªº¨Ï¥ÎªÌ±z¦n\n\n".
			  "§Aªº¤ô²y¾ã²z°O¿ýµLªk±H¹F«ü©w¦ì¸m $mailto \n\n".
			  "$hostname¯¸ªø¸s ·q¤W ".POSIX::ctime(time())});
    unlink $arg->{tartarget} if( $arg->{tartarget} );
    return 1;
}
