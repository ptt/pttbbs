#!/bin/sh
# $Id: opendice.sh,v 1.1 2002/03/07 15:13:46 in2 Exp $

bin/countalldice > etc/dice.dis
bin/post  Record   "骰子中獎名單"  "[骰子報告]"   etc/windice.log
bin/post  Security   "骰子失敗名單"  "[骰子報告]"   etc/lostdice.log
bin/post  Security "骰子期望值"    "[骰子報告]"   etc/dice.dis
rm -f etc/windice.log
rm -f etc/lostdice.log
rm -f etc/dice.dis
