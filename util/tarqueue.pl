#!/usr/bin/perl
# $Id$
use lib '/home/bbs/bin/';
use LocalVars;
use strict;
use Mail::Sender;
use POSIX;

no strict 'subs';
setpriority(PRIO_PROCESS, $$, 20);
use strict subs;
chdir $BBSHOME;
open LOG, ">> log/tarqueue.log";

foreach my $board ( <$JOBSPOOL/tarqueue.*> ){
    $board =~ s/.*tarqueue\.//;
    ProcessBoard($board);
    unlink "$JOBSPOOL/tarqueue.$board";
}
close DIR;
close LOG;

sub ProcessBoard
{
    my($board)= @_;
    my($cmd, $owner, $email, $bakboard, $bakman, $now);

    $now = substr(POSIX::ctime(time()), 0, -1);
    open FH, "< $JOBSPOOL/tarqueue.$board";
    chomp($owner = <FH>);
    chomp($email = <FH>);
    chomp(($bakboard, $bakman) = split(/,/, <FH>));
    close FH;

    print LOG sprintf("%-28s %-12s %-12s %d %d %s\n",
		      $now, $owner, $board, $bakboard, $bakman, $email);

    MakeMail({tartarget  => "$TMP/$board.tgz",
	      tarsource  => "boards/". substr($board, 0, 1). "/$board",
	      mailto     => "$board的板主$owner <$email>",
	      subject    => "$board的看板備份",
	      from       => "$board的板主$owner <$owner.bbs\@$MYHOSTNAME>",
	      body       =>
	    "\n\n\t $owner 您好，收到這封信，表示您已經收到看板備份。\n\n".
	    "\t謝謝您的耐心等待，以及使用 $hostname的看板備份系統，\n\n".
	    "\t如有任何疑問，歡迎寄信給站長，我們會很樂於給予協助。\n\n\n".
	    "\t最後，祝 $owner 平安快樂！ ^_^\n\n\n".
	    "\t $hostname站長群. \n\t$now"
	    }) if( $bakboard );

    MakeMail({tartarget  => "$TMP/man.$board.tgz",
	      tarsource  => "man/boards/". substr($board, 0, 1). "/$board",
	      mailto     => "$board的板主$owner <$email>",
	      subject    => "$board的精華區備份",
	      from       => "$board的板主$owner <$owner.bbs\@$MYHOSTNAME>",
	      body       =>
	    "\n\n\t $owner 您好，收到這封信，表示您已經收到精華區備份。\n\n".
	    "\t謝謝您的耐心等待，以及使用 $hostname的看板備份系統，\n\n".
	    "\t如有任何疑問，歡迎寄信給站長，我們會很樂於給予協助。\n\n\n".
	    "\t最後，祝 $owner 平安快樂！ ^_^\n\n\n".
	    "\t $hostname站長群. \n\t$now"
	    }) if( $bakman );

}

sub MakeMail
{
    my($arg) = @_;
    my $sender;
    `$TAR zcf $arg->{tartarget} $arg->{tarsource}`;
    $sender = new Mail::Sender{smtp => $SMTPSERVER,
			       from => $arg->{from}};
    $sender->MailFile({to         => $arg->{mailto},       
		       subject    => $arg->{subject},
		       msg        => $arg->{body},
		       file       => $arg->{tartarget},
		       b_charset  => 'big5',
		       b_encoding => '8bit',
		       ctype      => 'application/x-tar-gz'});
    unlink $arg->{tartarget};
}
