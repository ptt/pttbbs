#!/usr/bin/perl
# $Id: article.readfn.pl,v 1.1 2003/05/19 02:06:34 in2 Exp $
use Frontier::Client;
use Frontier::RPC2;
use MIME::Base64;
use Data::Dumper;
do 'host.pl';

$bid = $ARGV[0] || 0;
$fn = $ARGV[1] || 'M.1047292518.A.48E';

$server = Frontier::Client->new(url => $server_url);
$result = $server->call('article.readfn', $bid, $fn);

print decode_base64($result->{content}->value());
