#!/usr/bin/perl -w
# for saving memory, filter out unnecessary environment variables
use strict;

########################################
# mbbsd 會用到的 external program:
# tar rm cat mv cp stty
# bin/railway_wrapper.pl -> lynx -> LANG
# bin/buildir bin/builddb.pl bin/xchatd
# mutt -> TMPDIR
# /usr/bin/uuencode /usr/sbin/sendmail
##########################################
# 若無 getpwuid(2), mutt 會需要 HOME,USER

$ENV{PATH}="/bin:/usr/bin:/usr/local/bin";
#$ENV{SHELL}="/bin/sh";
my @acceptenv=qw(
        ^PATH$
        ^USER$ ^HOME$
        ^TZ$ ^TZDIR$ ^TMPDIR$
        ^MALLOC_
        );
# TERM SHELL PWD LANG LOGNAME
for my $env(keys %ENV) {
    delete $ENV{$env} if !grep { $env =~ $_ } @acceptenv;
}

exec { $ARGV[0] } @ARGV;
