#!/usr/bin/perl
# $Id: man.pl,v 1.3 2003/07/03 13:49:07 in2 Exp $
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
