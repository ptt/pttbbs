#!/usr/bin/perl
use lib '/home/bbs/bin/';
use LocalVars;
use strict;
use vars qw/$BACKHOME $MANROOT $HOMEROOT $BOARDROOT/;

$BACKHOME = "$BBSHOME/backup";
$MANROOT  = "man/boards";
$HOMEROOT = "home";
$BOARDROOT= "boards";

chdir $BBSHOME;
my @baktable = (['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'],
		['I', 'J', 'K', 'L', 'M', 'N', 'O', 'P'],
		['Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X'],
		['Y', 'Z', 'a', 'b', 'c', 'd', 'e'],
		['f', 'g', 'h', 'i', 'j', 'k', 'l'],
		['m', 'n', 'o', 'p', 'q', 'r', 's'],
		['t', 'u', 'v', 'w', 'x', 'y', 'z']);
my (undef,undef,undef,undef,undef,undef,$wday) = localtime(time); 
my $week = defined($ARGV[0]) ? $ARGV[0] : $wday;

no strict 'subs';
setpriority(PRIO_PROCESS, $$, 20);
use strict subs;

my($orig, $to);
foreach $orig ( <$BACKHOME/*new*> ){
	$to = $orig;
	$to =~ s/\.new//g;
	docmd("mv $orig $to");
}

foreach( @{$baktable[$week]} ){
    docmd("$TAR zcf  $BACKHOME/man.$_.new.tgz   $MANROOT/$_/*");
    docmd("$TAR zcfh $BACKHOME/home.$_.new.tgz  $HOMEROOT/$_");
    docmd("$TAR zcf  $BACKHOME/board.$_.new.tgz $BOARDROOT/$_/*");
}

if( $week == 0 ){
    docmd("$TAR zcf $BACKHOME/general.new.tgz .BRD .PASSWDS .act .note .polling .post .post.old adm bin cron etc innd log note.ans note.dat pttbbs register.log ussong");
}

sub docmd
{
    print "@_\n";
    `@_`;
}
