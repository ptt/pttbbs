#!/usr/bin/perl
# $Id: board.getNarticle.pl,v 1.1 2003/05/19 02:01:15 in2 Exp $
use Frontier::Client;
use Frontier::RPC2;
use MIME::Base64;
use Data::Dumper;
do 'host.pl';

$bid = $ARGV[0] || 0;

$server = Frontier::Client->new(url => $server_url);
$result = $server->call('board.getNarticle', $bid);

print Dumper($result);
