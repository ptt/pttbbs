#!/bin/sh
# $Id: buildAnnounce.sh,v 1.1 2002/03/07 15:13:45 in2 Exp $
#
bin/buildAnnounce > etc/ALLBRDLIST
bin/post Record 全站看板列表 [自動站長] etc/ALLBRDLIST   
