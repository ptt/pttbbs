#!/usr/bin/perl
# $Id$
use strict;
use BBSFileHeader;

my($nDels, $prefix) = ();

foreach( @ARGV ){
    print "cleaning: $_\n";
    cleandir($_);
    print "\n";
}

sub toclean
{
    unlink("$prefix/$_[0]");
    print "$_[0]\t";
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
	if( /^M\.\d+\.A/ ){
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
