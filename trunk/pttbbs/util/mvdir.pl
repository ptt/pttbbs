#!/usr/bin/perl
if( !@ARGV ){
    print "usage: mvdir.pl which prefix from_id to_id\n";
    exit;
}

($which, $prefix, $from_id, $to_id) = @ARGV;

$fromdir = "/scsi$from_id/bbs/$which/$prefix";
$todir = "/scsi$to_id/bbs/$which/$prefix";
$sym = ($which eq 'man' ? "/home/bbs/man/boards/$prefix" : "/home/bbs/$which/$prefix");

if( !-e $fromdir ){
    print "from dir ($fromdir) not found\n";
    exit;
}

mkdir($todir, 0755);

chdir $fromdir;
foreach( <*> ){
    next if( /^\./ );
    symlink("$fromdir/$_", "$todir/$_");
    push @dirs, $_;
}

unlink $sym;
symlink($todir, $sym);

foreach( @dirs ){
    printf("processing %-20s (%04d/%04d)\n", $_, $index++, $#dirs);
    unlink "$todir/$_";
    `mv $fromdir/$_ $todir/$_`;
}

rmdir "$fromdir";
