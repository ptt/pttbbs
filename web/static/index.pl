#!/usr/bin/env perl
# $Id$
use lib qw/./;
use LocalVars;
use CGI qw/:cgi :html2/;
use strict;
use Template;
use b2g;
use DB_File;
use Data::Serializer;
use vars qw/$serializer $tmpl %brdlist/;

sub deserialize  
{   
    my($what) = @_;
    $serializer = Data::Serializer->new(serializer => 'Storable',
					digester   => 'MD5',
					compress   => 0,
					)
	if( !$serializer );
    return $serializer->deserialize($what);
}

sub main
{
    my(%rh, $bid) = ();

    if( param('gb') ){
	$rh{gb} = 1;
	$rh{encoding} = 'gb2312';
	$rh{lang} = 'zh-CN';
	$rh{charset} = 'gb2312';    }
    else{
	print redirect("/man.pl/$1/")
	    if( $ENV{REDIRECT_REQUEST_URI} =~ m|/\?(.*)| );
	$rh{encoding} = 'Big5';
	$rh{lang} = 'zh-TW';
	$rh{charset} = 'big5';
    }

    return redirect('/index.pl/'.($rh{gb}?'?gb=1':''))
	if( $ENV{REQUEST_URI} eq '/' );

    charset('');
    print header();
    tie %brdlist, 'DB_File', 'boardlist.db', O_RDONLY, 0666, $DB_HASH
	if( !%brdlist );

    ($bid) = $ENV{PATH_INFO} =~ m|.*/(\d+)/$|;
    $bid ||= 1;
    $rh{isroot} = ($bid == 1);

    if( !exists $brdlist{"class.$bid"} ){
	print "sorry, this bid $bid not found :(";
	return ;
    }

    foreach( @{deserialize($brdlist{"class.$bid"})} ){
	next if( $brdlist{"$_.isboard"} &&
		 !-e "$MANDATA/".$brdlist{"tobrdname.$_"}.'.db' );

	push @{$rh{dat}}, [$brdlist{"$_.isboard"} ? -1 : $_,
			   $brdlist{"$_.brdname"},
			   $brdlist{"$_.title"},
			   ];
    }

    my $path = '';
    foreach( $ENV{PATH_INFO} =~ m|(\w+)|g ){
	push @{$rh{class}}, {path => "$path/$_/",
			     title => $brdlist{"$_.title"}};
	$path .= "/$_";
    }
    $rh{exttitle} = ($rh{class} ? 
		     $rh{class}[ $#{@{$rh{class}}} ]{title} : '­º­¶');

    $tmpl = Template->new({INCLUDE_PATH => '.',
			   ABSOLUTE => 0,
			   RELATIVE => 0,
			   RECURSION => 0,
			   EVAL_PERL => 0,
			   COMPILE_EXT => '.tmpl',
			   COMPILE_DIR => $MANCACHE,
		       })
	if( !$tmpl );

    if( !$rh{gb} ){
	$tmpl->process('index.html', \%rh);
    }
    else{
	my $output;
	$tmpl->process('index.html', \%rh, \$output);
	b2g::big5togb($output);
	print $output;
    }
}

main();
1;
