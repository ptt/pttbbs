#!/usr/bin/perl

use strict;
use lib '/home/bbs/bin/';
use BBSFileHeader;

my($nDels, $old, $prefix) = ();
$old = $nDels = 0;
foreach( @ARGV ){
    print "cleaning: $_\n";
    cleandir($_);
    print ("\t".($nDels - $old). " files deleted\n");
    $old = $nDels;
}

print "$nDels files deleted\n";

sub toclean
{
    unlink("$prefix/$_[0]");
    ++$nDels;
}

sub cleandir($)
{
    my($dir) = @_;
    my(%files, %dotDIR, $now, $counter) = ();
    $now = time();
    $prefix = $dir;

    opendir DIR, $dir;
    foreach( readdir(DIR) ){
	if( /^\./ ){
	    next;
	} elsif( -d $_ ){
	    print "dir? $_";
	    `/bin/rm -rf $prefix/$_`;
	} elsif( /^M\.\d+\.A/ ){
	    $files{$_} = 1;
	} elsif( (/^SR\./) && (stat($_))[2] < ($now - 86400) ){
	    toclean($_);
	}
    }
    close DIR;

    tie %dotDIR, 'BBSFileHeader', $dir;
    foreach( 0..($dotDIR{num} - 1) ){
	my $fn = $dotDIR{"$_.filename"};
	delete $files{$fn};
    }
    untie %dotDIR;

    toclean($_)
	foreach( keys %files );
}
