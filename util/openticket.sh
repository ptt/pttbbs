#!/bin/sh

bin/openticket > etc/jackpot
bin/post Record "彩券中獎名單" "[派彩報告]" etc/jackpot
