#!/usr/bin/perl
if( !@ARGV ){
    print "usage:\tgetbackup.pl\tbrd BRDNAME\n";
    print "\t\t\tman BRDNAME\n";
    print "\t\t\tusr USERID [all|fav|frlist|bbsrc]\n";
}

if( !-e '/home/bbs/backup/home.a.tgz' ){
    print "there's no /home/bbs/backup/home.a.tgz?\n";
    print "or no mount on /home/bbs/backup?\n";
    exit;
}

chdir "/home/bbs/backup/";

$prefix = substr($ARGV[1], 0, 1);
if( $ARGV[0] eq 'usr' ){
    $userid = $ARGV[1];
    `rm -Rf home`;
    `tar zxvf home.$prefix.tgz home/$prefix/$userid`;
    shift @ARGV; shift @ARGV;
    foreach( @ARGV ){
	if( $_ ne 'all' && $_ ne 'fav' && $_ ne 'frlist' &&
	    $_ ne 'bbsrc' && $_ ){
	    print "usr command '$_' unknown\n";
	    exit;
	}
	if( $_ eq 'all' ){
	    `rm -Rf /home/bbs/home/$prefix/$userid`;
	    `mv home/$prefix/$userid /home/bbs/home/$prefix/$userid`;
	}
	elsif( $_ eq 'fav' ){
	    `mv home/$prefix/$userid/.fav /home/bbs/home/$prefix/$userid/.fav`;
	}
	elsif( $_ eq 'frlist' ){
	    `mv home/$prefix/$userid/overrides /home/bbs/home/$prefix/$userid/overrides`;
	}
	elsif( $_ eq 'bbsrc' ){
	    `mv home/$prefix/$userid/.bbsrc /home/bbs/home/$prefix/$userid/.bbsrc`;
	}
    }
}
elsif( $ARGV[0] eq 'brd' ){
    chdir '/home/bbs';
    `mv boards/$prefix/$ARGV[1] boards.error/$ARGV[1]`;
    `tar zxvf backup/board.$prefix.tgz boards/$prefix/$ARGV[1]`;
}
elsif( $ARGV[0] eq 'man' ){
    chdir '/home/bbs';
    `mv man/boards/$prefix/$ARGV[1] boards.error/man.$ARGV[1]`;
    `tar zxvf backup/man.$prefix.tgz man/boards/$prefix/$ARGV[1]`;
}
