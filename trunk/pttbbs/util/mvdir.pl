#!/usr/bin/perl
if( !@ARGV ){
    print "usage: mvdir.pl which prefix from_id to_id\n";
    exit;
}

($which, $prefix, $from_id, $to_id) = @ARGV;
$which = 'man/boards' if( $which eq 'man' );

$fromdir = "/scsi$from_id/bbs/$which/$prefix";
$todir = "/scsi$to_id/bbs/$which/$prefix";

if( !-e $fromdir ){
    print "from dir ($fromdir) not found\n";
    exit;
}

mkdir $todir;
chdir $fromdir;
foreach( <*> ){
    next if( /^\./ );
    symlink("$fromdir/$_", "$todir/$_");
    push @dirs, $_;
}

unlink "/home/bbs/$which/$prefix";
symlink($todir, "/home/bbs/$which/$prefix");

foreach( @dirs ){
    printf("processing %-20s (%04d/%04d)\n", $_, ++$index, $#dirs);
    unlink "$todir/$_";
    `mv $fromdir/$_ $todir/$_`;
}

rmdir "$fromdir";
