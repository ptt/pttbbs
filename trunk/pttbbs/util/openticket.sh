#!/bin/sh
# $Id: openticket.sh,v 1.1 2002/03/07 15:13:46 in2 Exp $

bin/openticket > etc/jackpot
bin/post Record "彩券中獎名單" "[賭場報告]" etc/jackpot
bin/post Record "猜數字中獎名單" "[猜數字報告]" etc/winguess.log
bin/post Record "黑白棋對戰記錄" "[猜數字報告]" etc/othello.log
rm -f etc/winguess.log
rm -f etc/loseguess.log
rm -f etc/othello.log
