#!/usr/bin/perl
# $Id$
use lib qw/./;
use LocalVars;
use CGI qw/:standard/;
use strict;
use Template;
#use boardlist;
use b2g;
use DB_File;
use Data::Serializer;

sub deserialize  
{   
    my($what) = @_;
    my $obj = Data::Serializer->new(serializer => 'Storable',
                                    digester   => 'MD5',
                                    compress   => 0,
                                    );
    return $obj->deserialize($what);
}

sub main
{
    my($tmpl, %rh, $bid, %brd);

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
    tie %brd, 'DB_File', 'boardlist.db', O_RDONLY, 0666, $DB_HASH;

    ($bid) = $ENV{PATH_INFO} =~ m|.*/(\d+)/$|;
    $bid ||= 0;
    $rh{isroot} = ($bid == 0);

    if( !exists $brd{"class.$bid"} ){
	print "sorry, this bid $bid not found :(";
	return ;
    }

    foreach( @{deserialize($brd{"class.$bid"})} ){
	next if( $brd{"$_.isboard"} &&
		 !-e "$MANDATA/".$brd{"tobrdname.$_"}.'.db' );

	push @{$rh{dat}}, [$brd{"$_.isboard"} ? -1 : $_,
			   $brd{"$_.brdname"},
			   $brd{"$_.title"},
			   ];
    }

    my $path = '';
    foreach( $ENV{PATH_INFO} =~ m|(\w+)|g ){
	push @{$rh{class}}, {path => "$path/$_/",
			     title => $brd{"$_.title"}};
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
		       });
    if( !$rh{gb} ){
	$tmpl->process('index.html', \%rh);
    }
    else{
	my $output;
	$tmpl->process('index.html', \%rh, \$output);
	b2g::big5togb($output);
	print $output;
    }
    untie %brd;
}

main();
1;
