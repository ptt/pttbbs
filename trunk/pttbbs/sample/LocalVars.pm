#!/usr/bin/perl
package LocalVars;
require Exporter;
@ISA = qw/Exporter/;
@EXPORT = qw/
    $hostname $MYHOSTNAME $FQDN $SMTPSERVER
    $BBSHOME $JOBSPOOL $TMP
    $TAR $LYNX $GREP
    $BLOGDATA $BLOGCACHE
/;

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

# BLOG:
#    $BLOGDATA 是用來放置 Blog 資料的路徑
#    $BLOGCACHE是用來放置 Template compiled 資料的路徑,
#              須為 apache owner 可寫入
$BLOGDATA = '/home/bbs/blog/data';
$BLOGCACHE = '/home/bbs/blog/cache';
