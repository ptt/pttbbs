#!/usr/bin/perl
use IO::Socket::INET;
$BBSHOME = '/home/bbs';

open NODES, "<$BBSHOME/innd/nodelist.bbs";
while( <NODES> ){
    next if( /^\#/ );

    ($nodename, $host) = $_ =~ /^(\S+)\s+(\S+)/;
    next if( !$nodename );

    $sock = IO::Socket::INET->new(PeerAddr => $host,
				  PeerPort => 119,
				  Proto => 'tcp');
    next if( !$sock );
    $sock->write("list\r\nquit\r\n");
    $sock->read($data, 104857600);

    foreach( split("\n", $data) ){
	$group{$nodename}{$1} = 1
	    if( /^([A-Za-z0-9\.]+) \d+ \d+ y/ );
    }
}

open FEEDS, "<$BBSHOME/innd/newsfeeds.bbs";
while( <FEEDS> ){
    ++$line;
    next if( /^\#/ );

    next if( !(($gname, $board, $nodename) =
	       $_ =~ /^([\w\.]+)\s+(\w+)\s+(\w+)/) );

    if( !-d ("$BBSHOME/boards/". substr($board, 0, 1). "/$board") ){
	print "$line: board not found ($board)\n";
	next;
    }

    if( !$group{$nodename} ){
	print "$line: node not found ($nodename)\n";
	next;
    }

    if( !$group{$nodename}{$gname} ){
	print "$line: group not found ($gname)\n";
    }
}

