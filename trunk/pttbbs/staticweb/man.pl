#!/usr/bin/perl
# $Id: man.pl,v 1.4 2003/07/04 05:58:45 in2 Exp $
use CGI qw/:standard/;
use lib qw/./;
use LocalVars;
use DB_File;
use strict;
use Data::Dumper;
use Date::Calc qw(:all);
use Template;
use HTML::Calendar::Simple;
use OurNet::FuzzyIndex;
use Data::Serializer;
use vars qw/%db $brdname $fpath/;

sub main
{
    my($tmpl, $rh, $key);

    if( !(($brdname, $fpath) = $ENV{PATH_INFO} =~ m|^/([\w\-]+?)(/.*)|) ||
	!(tie %db, 'DB_File',
	  "$MANDATA/$brdname.db", O_RDONLY, 0666, $DB_HASH) ){
	return redirect("/man.pl/$1/")
	    if( $ENV{PATH_INFO} =~ m|^/([\w\-]+?)$| );
	print header(-status => 404);
	return;
    }

    charset('');
    print header();

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
			   COMPILE_DIR => $MANCACHE});
    $tmpl->process($rh->{tmpl}, $rh);
}

sub dirmode
{
    my(%th);
    my $serial = Data::Serializer->new(serializer => 'Storable',
				       digester   => 'MD5',
				       compress   => 0,
				       );
    foreach( @{$serial->deserialize($db{$fpath})} ){
	push @{$th{dat}}, {isdir => (($_->[0] =~ m|/$|) ? 1 : 0),
			   fn    => "/man.pl/$brdname$_->[0]",
			   title => $_->[1]};
    }

    $th{tmpl} = 'dir.html';
    $th{isroot} = ($fpath eq '/') ? 1 : 0;
    return \%th;
}

sub articlemode
{
    my(%th);
    $th{tmpl} = 'article.html';
    $th{content} = $db{$fpath};
    $th{content} =~ s/\033\[.*?m//g;

    $th{content} =~ s|(http://[\w\-\.\:\/\,@]+)|<a href="$1">$1</a>|gs;
    $th{content} =~ s|(ftp://[\w\-\.\:\/\,@]+)|<a href="$1">$1</a>|gs;
    $th{content} =~
	s|у金金ㄟ|<a href="http://blog.ptt2.cc">у金金ㄟ</a>|gs;
    $th{content} =~
	s|祇獺: у金金龟穨|祇獺: <a href="http://blog.ptt.cc">у金金龟穨</a>|gs;
    $th{content} =~
	s|ptt\.csie\.ntu\.edu\.tw|<a href="telnet://ptt.csie.ntu.edu.tw">ptt.csie.ntu.edu.tw</a>|gs;
    $th{content} =~
	s|ptt\.twbbs\.org|<a href="telnet://ptt.csie.ntu.edu.tw">ptt.twbbs.org</a>|gs;
    return \%th;
}

sub search($)
{
    my($key) = @_;
    my(%th, $idx, $k);
    $idx = OurNet::FuzzyIndex->new("$MANDATA/$brdname.idx");
    my %result = $idx->query($th{key} = $key, MATCH_FUZZY);
    foreach my $t (sort { $result{$b} <=> $result{$a} } keys(%result)) {
	$k = $idx->getkey($t);
	push @{$th{search}}, {title => $db{"title-$k"},
			      fn    => $k,
			      score => $result{$t} / 10};
    }

    $th{key} = $key;
    $th{tmpl} = 'search.html';
    return \%th;
}

main();
1;
