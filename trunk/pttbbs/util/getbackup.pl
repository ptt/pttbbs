#!/usr/bin/perl
if( !@ARGV ){
    print "usage:\tgetbackup.pl\tbrd BRDNAME\n";
    print "\t\t\tman BRDNAME\n";
    print "\t\t\tusr USERID [all|fav|frlist]\n";
}

chdir "/home/bbs/backup/";

$prefix = substr($ARGV[1], 0, 1);
if( $ARGV[0] eq 'usr' ){
    `rm -Rf home`;
    `tar zxvf home.$prefix.tgz home/$prefix/$ARGV[1]`;
    if( $ARGV[2] ne 'all' && $ARGV[2] ne 'fav' && $ARGV[2] ne 'frlist' ){
	print "usr command '$ARGV[2]' unknown\n";
	exit;
    }
    if( $ARGV[2] eq 'all' ){
	`rm -Rf /home/bbs/home/$prefix/$ARGV[1]`;
	`mv home/$prefix/$ARGV[1] /home/bbs/home/$prefix/$ARGV[1]`;
    }
    elsif( $ARGV[2] eq 'fav' ){
	`mv home/$prefix/$ARGV[1]/.fav /home/bbs/home/$prefix/$ARGV[1]/.fav`;
    }
    elsif( $ARGV[2] eq 'frlist' ){
	`mv home/$prefix/$ARGV[1]/overrides /home/bbs/home/$prefix/$ARGV[1]/overrides`;
    }
}
elsif( $ARGV[0] eq 'man' ){
}

