#!/usr/bin/perl
package LocalVars;
require Exporter;
@ISA = qw/Exporter/;
@EXPORT = qw/
    $hostname $MYHOSTNAME $FQDN $SMTPSERVER
    $BBSHOME $JOBSPOOL $TMP
    $TAR $LYNX $GREP/;

# host
$hostname = 'ptt';
$MYHOSTNAME = 'ptt.cc';
$FQDN = 'ptt.csie.ntu.edu.tw';
$SMTPSERVER = 'ptt2.csie.ntu.edu.tw';

# dir
$BBSHOME = '/home/bbs';
$JOBSPOOL = "$BBSHOME/jobspool";
$TMP = '/tmp';

# program
$TAR = '/usr/bin/tar';
$LYNX = '/usr/local/bin/lynx';   # /usr/ports/www/lynx
$GREP = '/usr/bin/grep';
