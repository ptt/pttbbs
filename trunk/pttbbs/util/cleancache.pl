#!/usr/bin/perl
use lib '/home/bbs/bin/';
use LocalVars;

chdir '/home/bbs/';
opendir(DIR, "cache") || die "can't open cache/ ($!) ";
foreach( readdir(DIR) ){
    if( index($_, $MYHOSTNAME) == 0 ){
	$hash{$_} = 1;
    }
}

closedir DIR;

open USEDPID, "bin/shmctl listpid |";
while( <USEDPID> ){
    chomp;
    delete $hash{"${MYHOSTNAME}b$_"};
    delete $hash{"${MYHOSTNAME}f$_"};
    delete $hash{"${MYHOSTNAME}z$_"};
}

foreach( keys %hash ){
    print "$_\n";
    unlink "cache/$_";
}
