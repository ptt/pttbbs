#!/usr/bin/perl
# $Id: manbuilder.pl,v 1.1 2003/07/03 05:49:45 in2 Exp $
use lib '/home/bbs/bin/';
use strict;
use OurNet::FuzzyIndex;
use Getopt::Std;
use DB_File;
use BBSFileHeader;
use Data::Serializer;

my(%db, %idx, $serial);

sub main
{
    die usage() unless( @ARGV );

    $serial = Data::Serializer->new(serializer => 'Storable',
				    digester   => 'MD5',
                                    compress   => 0,
                                    );

    foreach( @ARGV ){
	tie %db, 'DB_File', "$_.db", O_CREAT | O_RDWR, 0666, $DB_HASH;
	build("/home/bbs/man/boards/".substr($_, 0, 1)."/$_", '');
	untie %db;
    }
}

sub build($$)
{
    my($basedir, $doffset) = @_;
    my(%bfh, $fn, @tdir);

    print "$basedir, $doffset\n";
    tie %bfh, 'BBSFileHeader', $basedir;
    foreach( 0..($bfh{num} - 1) ){
	next if( $bfh{"$_.filemode"} & 32 ); # skip HIDDEN

	$fn = $bfh{"$_.filename"};
	if( $bfh{"$_.isdir"} ){
	    push @tdir, ["$doffset/$fn/", $bfh{"$_.title"}];
	    build("$basedir/$fn", "$doffset/$fn");
	}
	else{
	    push @tdir, ["$doffset/$fn", $bfh{"$_.title"}];
	    $db{"$doffset/$fn"} = $bfh{"$_.content"};
	}
    }
    $db{"$doffset/"} = $serial->serialize(\@tdir);
}

sub usage
{
    print ("$0 boardname ...\n");
    exit(0);
}

main();
1;
