#!/usr/bin/perl
# $Id: article.list.pl,v 1.1 2003/05/19 02:04:14 in2 Exp $
use Frontier::Client;
use Frontier::RPC2;
use MIME::Base64;
use Data::Dumper;
do 'host.pl';

$bid = $ARGV[0] || 0;

$server = Frontier::Client->new(url => $server_url);
$result = $server->call('article.list',
			$bid, # bid
			0);   # from # article

print Dumper($result);
