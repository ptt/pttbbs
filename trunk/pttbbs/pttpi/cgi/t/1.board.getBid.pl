#!/usr/bin/perl
use Frontier::Client;
use Data::Dumper;
do 'host.pl';

$brdname = $ARGV[0] || 'SYSOP';
$server = Frontier::Client->new(url => $server_url);
$result = $server->call('board.getBid', $brdname);

print "board.getBid($brdname) from $server_url:\n";
print Dumper($result);
