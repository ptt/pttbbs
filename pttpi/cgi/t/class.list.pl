#!/usr/bin/perl
# $Id: class.list.pl,v 1.1 2003/05/19 02:02:52 in2 Exp $
use Frontier::Client;
use Frontier::RPC2;
use MIME::Base64;
use Data::Dumper;
do 'host.pl';

$bid = $ARGV[0] || 0;

$server = Frontier::Client->new(url => $server_url);
$result = $server->call('class.list', $bid);

print Dumper($result);
