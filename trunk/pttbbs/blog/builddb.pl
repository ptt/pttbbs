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
    die usage() unless( getopts('caofn:') );
    die usage() if( !@ARGV );
    builddb($_) foreach( @ARGV );
}

sub usage
{
    return ("$0 [-cfao] [-n NUMBER] [board ...]\n".
	    "\t-c\t\tbuild configure\n".
	    "\t-a\t\trebuild all files\n".
	    "\t-o\t\tonly build content(not building link)\n".
	    "\t-f\t\tforce build\n".
	    "\t-n NUMBER\tonly build \#NUMBER article\n");
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
    builddata($board, \%bh,
	      $Getopt::Std::opt_a,
	      $Getopt::Std::opt_o,
	      $Getopt::Std::opt_n,
	      $Getopt::Std::opt_f,);
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
		$config{$1} = $2 if( !/^\#/ && /(.*?):\s*(.*)/ );
	    }
	}
	else{
	    my(@ls, $t, $c);

	    $c = $rch->{"$_.content"};
	    $c =~ s/^\#.*?\n//g;

	    @ls = split("\n", $c);
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

sub builddata($$$$$$)
{
    my($board, $rbh, $rebuild, $contentonly, $number, $force) = @_;
    my(%dat, $dbfn, $y, $m, $d, $t, $currid);

    $dbfn = "$BLOGROOT/$board.db";
    unlink $dbfn if( $rebuild );

    tie %dat, 'DB_File', $dbfn, O_CREAT | O_RDWR, 0666, $DB_HASH;
    foreach( $number ? $number : (1..($rbh->{num} - 1)) ){
	if( !(($y, $m, $d, $t) =
	      $rbh->{"$_.title"} =~ /(\d+)\.(\d+).(\d+),(.*)/) ){
	    print "\terror parsing $_: ".$rbh->{"$_.title"}."\n";
	}
	else{
	    $currid = sprintf('%04d%02d%02d', $y, $m, $d);
	    if( $dat{$currid} && !$force ){
		print "\t$currid is already in db\n";
		next;
	    }

	    print "\tbuilding $currid content\n";
	    $dat{ sprintf('%04d%02d', $y, $m) } = 1;
	    $dat{"$currid.title"} = $t;
	    $dat{"$currid.author"} = $rbh->{"$_.owner"};
	    # $dat{"$currid.content"} = $rbh->{"$_.content"};
	    # ugly code for making short
	    my @c = split("\n",
			  $dat{"$currid.content"} = $rbh->{"$_.content"});
	    $dat{"$currid.short"} = ("$c[0]\n$c[1]\n$c[2]\n$c[3]\n");

	    if( !$contentonly ){
		print "\tbuilding $currid linking... ";
		if( $dat{$currid} ){
		    print "already linked";
		}
		elsif( !$dat{head} ){ # first article
		    $dat{head} = $currid;
		    $dat{last} = $currid;
		}
		elsif( $currid < $dat{head} ){ # before head ?
		    $dat{"$currid.next"} = $dat{head};
		    $dat{"$dat{head}.prev"} = $currid;
		    $dat{head} = $currid;
		}
		elsif( $currid > $dat{last} ){ # after last ?
		    $dat{"$currid.prev"} = $dat{last};
		    $dat{"$dat{last}.next"} = $currid;
		    $dat{last} = $currid;
		}
		else{ # inside ? @_@;;;
		    print "not implement yet";
		}
		$dat{$currid} = 1;
		print "\n";
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
