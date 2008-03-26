#!/usr/bin/env perl
# $Id$
use CGI qw/:cgi :html2/;
use lib qw/./;
use LocalVars;
use DB_File;
use strict;
use Data::Dumper;
use Template;
use OurNet::FuzzyIndex;
use Data::Serializer;
use Time::HiRes qw/gettimeofday tv_interval/;
use b2g;
use POSIX;
use Compress::Zlib;

use vars qw/%db $brdname $fpath $isgb $tmpl/;

sub main
{
    my($rh, $key) = ();

    if( !(($brdname, $fpath) = $ENV{PATH_INFO} =~ m|^/([\w\-]+?)(/.*)|) ||
	(!exists $db{$brdname} &&
	 !tie %{$db{$brdname}}, 'DB_File',
	 "$MANDATA/$brdname.db", O_RDONLY, 0666, $DB_HASH) ){
	return redirect("/man.pl/$1/")
	    if( $ENV{PATH_INFO} =~ m|^/([\w\-]+?)$| );
	print header(-status => 404);
	return;
    }

    charset('');
    print header();

    $isgb = (param('gb') ? 1 : 0);

    if( ($key = param('key')) ){
	$rh = search($key);
    }
    else{
	$rh = (($fpath =~ m|/$|) ? dirmode($fpath) : articlemode($fpath));
    }
    $rh->{brdname} = $brdname;
    $tmpl = Template->new({INCLUDE_PATH => '.',
			   ABSOLUTE => 0,
			   RELATIVE => 0,
			   RECURSION => 0,
			   EVAL_PERL => 0,
			   COMPILE_EXT => '.tmpl',
			   COMPILE_DIR => $MANCACHE,
		       })
	if( !$tmpl );

    if( $rh->{gb} = $isgb ){
	$rh->{encoding} = 'gb2312';
	$rh->{lang} = 'zh-CN';
	$rh->{charset} = 'gb2312';
    }
    else{
	$rh->{encoding} = 'Big5';
	$rh->{lang} = 'zh-TW';
	$rh->{charset} = 'big5';
    }

    if( !$rh->{gb} ){
	$tmpl->process($rh->{tmpl}, $rh);
    }
    else{
	my $output;
	$tmpl->process($rh->{tmpl}, $rh, \$output);
	b2g::big5togb($output);
	print $output;
    }
}

sub dirmode
{
    my(%th, $isdir);
    my $serial = Data::Serializer->new(serializer => 'Storable',
				       digester   => 'MD5',
				       compress   => 0,
				       );
    foreach( @{$serial->deserialize($db{$brdname}{$fpath}) || []} ){
	$isdir = (($_->[0] =~ m|/$|) ? 1 : 0);
	push @{$th{dat}}, {isdir => $isdir,
			   fn    => "man.pl/$brdname$_->[0]",
			   title => $_->[1]};
    }

    $th{tmpl} = 'dir.html';
    $th{isroot} = ($fpath eq '/') ? 1 : 0;
    $th{buildtime} = POSIX::ctime($db{$brdname}{_buildtime} || 0);
    return \%th;
}

sub articlemode
{
    my(%th);
    $th{tmpl} = 'article.html';

    # ㄓ unzip, 璶ぃ礛穦年奔 :p
    $th{content} = $db{$brdname}{$fpath};
    $th{content} = Compress::Zlib::memGunzip($th{content})
	if( $db{$brdname}{_gzip} );

    $th{content} =~ s/\033\[.*?m//g;

    $th{content} =~ s|(http://[\w\-\.\:\/\,@\?=~]+)|<a href="$1">$1</a>|gs;
    $th{content} =~ s|(ftp://[\w\-\.\:\/\,@~]+)|<a href="$1">$1</a>|gs;
    $th{content} =~
	s|ptt\.cc|<a href="telnet://ptt.cc">ptt.cc</a>|gs;
    $th{content} =~
	s|ptt\.twbbs\.org|<a href="telnet://ptt.cc">ptt.twbbs.org</a>|gs;
    $th{content} =~
	s|у金金ㄟ|<a href="http://blog.ptt2.cc">у金金ㄟ</a>|gs;
    $th{content} =~
	s|祇獺: у金金龟穨|祇獺: <a href="http://ptt.cc">у金金龟穨</a>|gs;

    return \%th;
}

sub search($)
{
    my($key) = @_;
    my(%th, $idx, $k, $t0);
    $t0 = [gettimeofday()];
    $idx = OurNet::FuzzyIndex->new("$MANIDX/$brdname.idx");
    my %result = $idx->query($th{key} = $key, MATCH_FUZZY);
    foreach my $t (sort { $result{$b} <=> $result{$a} } keys(%result)) {
	$k = $idx->getkey($t);
	push @{$th{search}}, {title => $db{$brdname}{"title-$k"},
			      fn    => $k,
			      score => $result{$t} / 10};
    }

    $th{elapsed} = tv_interval($t0);
    $th{key} = $key;
    $th{tmpl} = 'search.html';
    undef $idx;
    return \%th;
}

main();
1;
