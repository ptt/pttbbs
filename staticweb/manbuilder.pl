#!/usr/bin/perl
# $Id$
use lib '/home/bbs/bin/';
use strict;
use OurNet::FuzzyIndex;
use Getopt::Std;
use DB_File;
use BBSFileHeader;
use Data::Serializer;
use Compress::Zlib;

my(%db, $idx, $serial, $idxname);

sub main
{
    die usage() unless( getopts('nz') || !@ARGV );

    $serial = Data::Serializer->new(serializer => 'Storable',
				    digester   => 'MD5',
                                    compress   => 0,
                                    );

    foreach( @ARGV ){
	undef $idx;
	if( /\.db$/ ){
	    next if( $Getopt::Std::opt_n );

	    $idxname = substr($_, 0, -3). '.idx';
	    print "building idx for $_\n";
	    tie %db, 'DB_File', $_, O_RDONLY, 0664, $DB_HASH;
	    $idx = OurNet::FuzzyIndex->new($idxname);
	    buildidx();
	}
	else{
	    tie %db, 'DB_File', "$_.db", O_CREAT | O_RDWR, 0664, $DB_HASH;
	    $idxname = "$_.idx";
	    $idx = OurNet::FuzzyIndex->new($idxname)
		if( !$Getopt::Std::opt_n );
	    build("/home/bbs/man/boards/".substr($_, 0, 1)."/$_", '');
	    $db{_buildtime} = time();
	    $db{_gzip} = 1 if( $Getopt::Std::opt_z );
	    untie %db;
	}

	if( $idx ){
	    undef $idx;
	    chmod 0664, $idxname;
	}
    }
}

sub buildidx
{
    my($gzipped, $content);
    $gzipped = $db{_gzip};
    foreach( keys %db ){
	next if( /^title/ || /\/$/ ); # 是 title 或目錄的都跳過
	$content = $db{$_};
	$content = Compress::Zlib::memGunzip($content)
	    if( $gzipped );

	$idx->insert($_,
		     ($db{"title-$_"}. "\n$content"));
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
	next if( !($fn = $bfh{"$_.filename"}) ); # skip empty filename

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
	    $db{"$doffset/$fn"} = ($Getopt::Std::opt_z ?
				   Compress::Zlib::memGzip($c) : $c);
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
