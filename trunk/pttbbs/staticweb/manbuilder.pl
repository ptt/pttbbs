#!/usr/bin/perl
# $Id: manbuilder.pl,v 1.9 2003/07/04 03:40:02 in2 Exp $
use lib '/home/bbs/bin/';
use strict;
use OurNet::FuzzyIndex;
use Getopt::Std;
use DB_File;
use BBSFileHeader;
use Data::Serializer;

my(%db, $idx, $serial);

sub main
{
    die usage() unless( getopts('n') || !@ARGV );

    $serial = Data::Serializer->new(serializer => 'Storable',
				    digester   => 'MD5',
                                    compress   => 0,
                                    );

    foreach( @ARGV ){
	if( /\.db$/ ){
	    next if( $Getopt::Std::opt_n );

	    print "building idx for $_\n";
	    tie %db, 'DB_File', $_, O_RDONLY, 0666, $DB_HASH;
	    $idx = OurNet::FuzzyIndex->new(substr($_, 0, -3). '.idx');
	    buildidx();
	}
	else{
	    tie %db, 'DB_File', "$_.db", O_CREAT | O_RDWR, 0666, $DB_HASH;
	    $idx = OurNet::FuzzyIndex->new("$_.idx")
		if( !$Getopt::Std::opt_n );
	    build("/home/bbs/man/boards/".substr($_, 0, 1)."/$_", '');
	    untie %db;
	}
    }
}

sub buildidx
{
    foreach( keys %db ){
	next if( /^title/ || /\/$/ ); # 是 title 或目錄的都跳過
	$idx->insert($_, $db{"title-$_"}. "\n". $db{$_});
    }
}

sub build($$)
{
    my($basedir, $doffset) = @_;
    my(%bfh, $fn, @tdir);

    print "building $basedir\n";
    tie %bfh, 'BBSFileHeader', $basedir;
    foreach( 0..($bfh{num} - 1) ){
	next if( $bfh{"$_.filemode"} & 32 ); # skip HIDDEN

	$fn = $bfh{"$_.filename"};
	if( $bfh{"$_.isdir"} ){
	    push @tdir, ["$doffset/$fn/", # 目錄結尾要加 /
			 $db{"title-$doffset/$fn/"} = $bfh{"$_.title"}];
	    build("$basedir/$fn", "$doffset/$fn");
	}
	else{
	    push @tdir, ["$doffset/$fn",
			 $db{"title-$doffset/$fn"} = $bfh{"$_.title"}];
	    my $c = $bfh{"$_.content"};
	    $idx->insert("$doffset/$fn", $bfh{"$_.title"}. "\n$c")
		if( !$Getopt::Std::opt_n );
	    $db{"$doffset/$fn"} = $c;
	}
    }
    $db{"$doffset/"} = $serial->serialize(\@tdir);
}

sub usage
{
    print ("$0 [-n] boardname ...\n".
	   "\t -n for .db only (no .idx)\n");
    exit(0);
}

main();
1;
