#!/usr/bin/perl
@r = split(' ', $t = `/usr/bin/vmstat -n 0 -c 5 1`);
open FH, '>/tmp/mrtg.cs';
print FH (($r[52]+$r[69]+$r[86]+$r[103])/4);
close FH;
open FH, '>/tmp/mrtg.flt';
print FH (($r[44]+$r[61]+$r[78]+$r[95])/4);
close FH;
