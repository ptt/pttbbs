#!/usr/bin/perl
# $Id$
use lib '/home/bbs/bin/';
use strict;
use Getopt::Std;
use LocalVars;
use IO::Handle;
use Data::Dumper;
use BBSFileHeader;
use DB_File;
use OurNet::FuzzyIndex;

sub main
{
    my($fh);
    die usage() unless( getopts('cdaofn:') );
    die usage() if( !@ARGV );
    builddb($_) foreach( @ARGV );
}

sub usage
{
    return ("$0 [-acdfo] [-n NUMBER] [board ...]\n".
	    "\t-a\t\trebuild all files\n".
	    "\t-c\t\tbuild configure\n".
	    "\t-d\t\tprint debug message\n".
	    "\t-f\t\tforce build\n".
	    "\t-o\t\tonly build content(not building link)\n".
	    "\t-n NUMBER\tonly build \#NUMBER article\n");
}

sub debugmsg($)
{
    print "$_\n" if( $Getopt::Std::opt_d );
}

sub builddb($)
{
    my($board) = @_;
    my(%bh, %ch);

    debugmsg("building $board");
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

    $outdir = "$BLOGDATA/$board";
    `/bin/rm -rf $outdir; /bin/mkdir -p $outdir`;

    tie(%config, 'DB_File', "$outdir/config.db",
	O_CREAT | O_RDWR, 0666, $DB_HASH);
    tie(%attr, 'DB_File', "$outdir/attr.db",
	O_CREAT | O_RDWR, 0666, $DB_HASH);

    for ( 0..($rch->{num} - 1) ){
	debugmsg("\texporting ".$rch->{"$_.title"});
	if( $rch->{"$_.title"} =~ /^config$/i ){
	    foreach( split("\n", $rch->{"$_.content"}) ){
		$config{$1} = $2 if( !/^\#/ && /(.*?):\s*(.*)/ );
	    }
	}
	else{
	    my(@ls, $c, $a, $fn);
	    
	    $fn = $rch->{"$_.title"};
	    if( $fn !~ /\.(css|htm|html|js)$/i ){
		print "not supported filetype ". $rch->{"$_.title"}. "\n";
		next;
	    }
	    
	    $c = $rch->{"$_.content"};
	    $c =~ s/<meta http-equiv=\"refresh\".*?\n//g;
	    open FH, ">$outdir/$fn";
	    
	    if( $c =~ m|<attribute>(.*?)\n\s*</attribute>\s*\n(.*)|s ){
		($a, $c) = ($1, $2);
		$a =~ s/^\s*\#.*?\n//gm;
		foreach( split("\n", $a) ){
		    $attr{"$fn.$1"} = $2 if( /^\s*(\w+):\s+(.*)/ );
		}
	    }
	    print FH $c;
	}
    }
    debugmsg(Dumper(\%config));
    debugmsg(Dumper(\%attr));
}

sub builddata($$$$$$)
{
    my($board, $rbh, $rebuild, $contentonly, $number, $force) = @_;
    my(%dat, $dbfn, $idxfn, $y, $m, $d, $t, $currid, $idx);

    $dbfn = "$BLOGDATA/$board.db";
    $idxfn = "$BLOGDATA/$board.idx";
    if( $rebuild ){
	unlink $dbfn;
	unlink $idxfn;
    }

    tie %dat, 'DB_File', $dbfn, O_CREAT | O_RDWR, 0666, $DB_HASH;
    $idx = OurNet::FuzzyIndex->new($idxfn);
    foreach( $number ? $number : (1..($rbh->{num} - 1)) ){
	if( !(($y, $m, $d, $t) =
	      $rbh->{"$_.title"} =~ /(\d+)\.(\d+).(\d+),(.*)/) ){
	    debugmsg("\terror parsing $_: ".$rbh->{"$_.title"});
	}
	else{
	    $currid = sprintf('%04d%02d%02d', $y, $m, $d);
	    if( $dat{$currid} && !$force ){
		debugmsg("\t$currid is already in db");
		next;
	    }

	    debugmsg("\tbuilding $currid content");
	    $dat{ sprintf('%04d%02d', $y, $m) } = 1;
	    $dat{"$currid.title"} = $t;
	    $dat{"$currid.author"} = $rbh->{"$_.owner"};
	    # $dat{"$currid.content"} = $rbh->{"$_.content"};
	    # ugly code for making short
	    my @c = split("\n",
			  $dat{"$currid.content"} = $rbh->{"$_.content"});
	    $dat{"$currid.short"} = ("$c[0]\n$c[1]\n$c[2]\n$c[3]\n");

	    $idx->delete($currid) if( $idx->findkey($currid) );
	    $idx->insert($currid, ($dat{"$currid.title"}. "\n".
				   $rbh->{"$_.content"}));

	    if( !$contentonly ){
		debugmsg("\tbuilding $currid linking... ");
		if( $dat{$currid} ){
		    debugmsg("\t\talready linked");
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
		    my($p, $c);
		    for( $p = $dat{last} ; $p>$currid ; $p = $dat{"$p.prev"} ){
			;
		    }
		    $c = $dat{"$p.next"};
		    
		    $dat{"$currid.next"} = $c;
		    $dat{"$currid.prev"} = $p;
		    $dat{"$p.next"} = $currid;
		    $dat{"$c.prev"} = $currid;
		}
		$dat{$currid} = 1;
	    }
	}
    }
    untie %dat;
    $idx->sync();
    undef $idx;
    chmod 0666, $idxfn;
}

sub getdir($$$$$)
{
    my($bdir, $rh_bh, $rh_ch) = @_;
    my(%h);
    tie %h, 'BBSFileHeader', "$bdir/";
    if( $h{"-1.title"} !~ /blog/i || !$h{"-1.isdir"} ){
	debugmsg("blogdir not found");
	return;
    }

    tie %{$rh_bh}, 'BBSFileHeader', "$bdir/". $h{'-1.filename'}.'/';
    if( $rh_bh->{'0.title'} !~ /config/i ||
	!$rh_bh->{'0.isdir'} ){
	debugmsg("configure not found");
	return;
    }

    tie %{$rh_ch}, 'BBSFileHeader', $rh_bh->{dir}. '/'. $rh_bh->{'0.filename'};
    return 1;
}

main();
1;
