#!/usr/bin/perl
use strict;
use Getopt::Std;
use LocalVars;
use IO::Handle;
use Data::Dumper;
use BBSFileHeader;
use DB_File;

sub main
{
    my($fh);
    die usage() unless( getopts('ca') );
    die usage() if( !@ARGV );
    builddb($_) foreach( @ARGV );
}

sub usage
{
    return ("$0 [-ca] [board ...]\n".
	    "\t-c build configure\n".
	    "\t-a rebuild all files\n");
}

sub builddb($)
{
    my($board) = @_;
    my(%bh, %ch);
    
    print "building $board\n";
    return if( !getdir("$BBSHOME/man/boards/".substr($board,0,1)."/$board",
		       \%bh, \%ch) );
    buildconfigure($board, \%ch)
	if( $Getopt::Std::opt_c || $Getopt::Std::opt_a );
    builddata($board, \%bh, $Getopt::Std::opt_a);
}

sub buildconfigure($$)
{
    my($board, $rch) = @_;
    my($outdir, $fn, $flag, %config, %attr);

    $outdir = "$BLOGROOT/$board";
    `/bin/rm -rf $outdir; /bin/mkdir -p $outdir`;

    tie(%config, 'DB_File', "$outdir/config.db",
	O_CREAT | O_RDWR, 0666, $DB_HASH);
    tie(%attr, 'DB_File', "$outdir/attr.db",
	O_CREAT | O_RDWR, 0666, $DB_HASH);

    for ( 0..($rch->{num} - 1) ){
	print "\texporting ".$rch->{"$_.title"}."\n";
	if( $rch->{"$_.title"} =~ /^config$/i ){
	    foreach( split("\n", $rch->{"$_.content"}) ){
		$config{$1} = $2 if( /(.*?):\s*(.*)/ );
	    }
	}
	else{
	    my(@ls, $t);

	    @ls = split("\n", $rch->{"$_.content"});
	    open FH, ">$outdir/". $rch->{"$_.title"};
	    if( $rch->{"$_.title"} =~ /\.html$/ ){
		while( $t = shift @ls ){
		    last if( $t !~ /^\*/ );
		    $attr{($rch->{"$_.title"}. ".$1")} = $2
			if( $t =~ /^\*\s+(\w+): (.*)/ );
		}
		unshift @ls, $t;
	    }
	    print FH "$_\n"
		foreach( @ls );
	}
    }
    print Dumper(\%config);
    print Dumper(\%attr);
}

sub builddata($$$)
{
    my($board, $rbh, $rebuild) = @_;
    my(%dat, $dbfn, $y, $m, $d, $t, $currid);

    $dbfn = "$BLOGROOT/$board.db";
    unlink $dbfn if( $rebuild );
    
    tie %dat, 'DB_File', $dbfn, O_CREAT | O_RDWR, 0666, $DB_HASH;
    foreach( 1..($rbh->{num} - 1) ){
	if( ($y, $m, $d, $t) =
	    $rbh->{"$_.title"} =~ /(\d+)\.(\d+).(\d+),(.*)/ ){

	    $currid = sprintf('%04d%02d%02d', $y, $m, $d);
	    if( $currid <= $dat{last} ){
		print "\t$currid skipped\n";
	    }
	    else{
		$dat{ sprintf('%04d%02d', $y, $m) } = 1;
		$dat{"$currid.title"} = $t;
		$dat{"$currid.content"} = $rbh->{"$_.content"};
		$dat{"$currid.author"} = $rbh->{"$_.owner"};
		$dat{"$currid.prev"} = $dat{'last'};
		$dat{"$dat{last}.next"} = $currid
		    if( $dat{'last'} );
		$dat{'last'} = $currid;
		$dat{head} = $currid if( !$dat{head} );
		print "\t${currid} built\n";
	    }
	}
    }
    untie %dat;
}

sub getdir($$$$$)
{
    my($bdir, $rh_bh, $rh_ch) = @_;
    my(%h);
    tie %h, 'BBSFileHeader', "$bdir/";
    if( $h{"-1.title"} !~ /blog/i || !$h{"-1.isdir"} ){
	print "blogdir not found\n";
	return;
    }

    tie %{$rh_bh}, 'BBSFileHeader', "$bdir/". $h{'-1.filename'}.'/';
    if( $rh_bh->{'0.title'} !~ /configure/i ||
	!$rh_bh->{'0.isdir'} ){
	print "configure not found\n";
	return;
    }

    tie %{$rh_ch}, 'BBSFileHeader', $rh_bh->{dir}. '/'. $rh_bh->{'0.filename'};
    return 1;
}

main();
1;
