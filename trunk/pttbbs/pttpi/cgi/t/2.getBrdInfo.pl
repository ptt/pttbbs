#!/usr/bin/perl
# $Id: 2.getBrdInfo.pl,v 1.1 2003/05/19 01:52:05 in2 Exp $
use Frontier::Client;
use Frontier::RPC2;
use MIME::Base64;
use Data::Dumper;
do 'host.pl';

$bid = $ARGV[0] || 0;

$server = Frontier::Client->new(url => $server_url);
$result = $server->call('board.getBrdInfo', $bid);

print Dumper($result);
print decode_base64($result->{title}->value());
