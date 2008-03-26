#!/bin/sh

bin/bbsctl permreport > etc/permrpt.log
if [ -s etc/permrpt.log ] ; then 
    bin/post  Administor "特殊權限使用者名單 `date +'%Y%m%d'`"  "[權限報告]"   etc/permrpt.log
fi
/bin/rm -f etc/permrpt.log
