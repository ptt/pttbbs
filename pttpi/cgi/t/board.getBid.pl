#!/usr/bin/perl
# $Id: board.getBid.pl,v 1.1 2003/05/19 01:57:22 in2 Exp $
use Frontier::Client;
use Data::Dumper;
do 'host.pl';

$brdname = $ARGV[0] || 'SYSOP';
$server = Frontier::Client->new(url => $server_url);
$result = $server->call('board.getBid', $brdname);

print "board.getBid($brdname) from $server_url:\n";
print Dumper($result);
