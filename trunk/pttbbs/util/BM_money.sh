#!/bin/sh
# $Id: BM_money.sh,v 1.1 2002/03/07 15:13:45 in2 Exp $

bin/BM_money > etc/BM_money
bin/post Record 星期五' '版主發薪日 [財金消息] etc/BM_money
