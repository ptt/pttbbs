#!/usr/bin/perl
# $Id: man.pl,v 1.1 2003/07/03 06:49:23 in2 Exp $
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
    my($tmpl, $rh);

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

    $rh = (($fpath =~ m|/$|) ? dirmode($fpath) : articlemode($fpath));
    $tmpl = Template->new({INCLUDE_PATH => '.',
			   ABSOLUTE => 0,
			   RELATIVE => 0,
			   RECURSION => 0,
			   EVAL_PERL => 0});
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
    return \%th;
}

sub articlemode
{
    my(%th);
    $th{tmpl} = 'article.html';
    $th{content} = $db{$fpath};
    return \%th;
}

main();
1;
