#!/bin/sh
# $Id: openticket.sh,v 1.1 2002/03/07 15:13:46 in2 Exp $

bin/openticket > etc/jackpot
bin/post Record "彩券中獎名單" "[賭場報告]" etc/jackpot
