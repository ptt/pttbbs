#!/usr/bin/env python
#-*- coding: big5 -*-

import collections
import math
import os
import time

BBSHOME = '/home/bbs'
INPUT_FILE = '%s/log/angel_perf.txt' % (BBSHOME)
# A regular week = 590 samples.
SAMPLE_MINIMAL = 200

Entry = collections.namedtuple('Entry', 'sample pause1 pause2 max avg std')
DEBUG = None
REPORT_SAMPLE_STAT = False

PREFIX_DOC = '''== 本週小天使查翅膀建議名單 (系統自動產生: %s) ==

說明: 天使公會在新小主人找天使時即時統計所有的小天使狀態，
      確認 (1) 小天使當時是否在線上 (2) 神諭呼叫器是否停收/關閉。
      依據天使之書對於查翅膀的定義「抽查到太多次都沒開啟呼叫器」，
      若上線時間過少或是關呼叫時間過高則會出現在每週統計的名單中，
      以供品管及大天使參考。 ([關呼叫] 現在為「關閉」與「停收」的合計)

  ** 為幫助理解名單產生方法及數字意義，簡單例子如下: 若有小天使 ABC 三名,
     A 在線上開呼叫, B 在線上但關呼叫, C 不在線上:
     某日早上 10:20 有某使用者(無天使)按下 hh 找新天使，系統就會記錄:
                     [ID] [線上] [關呼叫] 
                     A    1      0        
                     B    1      1        
                     C    0      0        
              10:21 有一使用者換天使；但為避免有人洗數字，所以統計
                    每10分鐘最多一次；因此這次呼叫不會改變統計結果
	      10:30 十分鐘已到，系統已可再次統計 (但不主動執行)
              10:31 該使用者與小天使A在互丟水球 (丟水球不統計)
              10:32 C 上線了 (也還不統計 - 只在有人找/換天使才算)
              10:33 A 改為停收 (也還不統計 - 只在有人找/換天使才算)
              10:35 又有一使用者找新天使，系統更改記錄如下:
	            (ABC都在線上，AB關呼叫，C開呼叫)
		     [ID] [線上] [關呼叫] 
		     A    1+1=2  0+1=1    
		     B    1+1=2  1+1=2    
		     C    0+1=1  0+0=0    
		    且下次可統計時間為 10:45 之後。

''' % (time.ctime())
# 小天使名稱後面的數字為該小天使被統計到的次數 (以下稱 SAMPLE 數)，
# 因為 SAMPLE 數字是有人呼叫時才會更新，且為避免惡意洗數字造成結果不公，
# 統計每 600 秒最多更新一次(否則有人會趁自己上線開分身狂換小天使)，
# 所以此數字大略上接近(但不等於)小天使實際上線時間比例。
#
# 換句話說，即使為零也不代表此小天使都沒上線過，可能只是上線停留時間都
# 過短或是上線時都沒有新使用者要呼叫小天使。

def is_lazy(e):
    # 'LAZY'
    '\033[1;33m以下是[線上]統計結果過低(小於5)的小天使\033[m'
    # return e.sample < (e.avg - 1.0 * e.std)
    return e.sample < 5

def is_all_reject2(e):
    # 'ALL_REJECT2'
    '\033[1;31m以下是[關呼叫]統計比例過高([線上]減[關呼叫]小於2)的小天使\033[m'
    return (e.pause2 + e.pause1 >= e.sample - 1)

def parse_perf_file(filename):
    data = {}
    max_sample = 0
    sum_sample = 0
    sum_sample_square = 0
    with open(filename, 'r') as f:
	for l in f:
	    ls = l.strip()
	    if ls.startswith('#') or ls == '':
		continue
            # format: no. uid sample pause1 pause
	    no, uid, sample, pause1, pause2 = ls.split()
	    data[uid] = map(int, (sample, pause1, pause2))
	    sample = int(sample)
	    sum_sample += sample
	    sum_sample_square += sample * sample
	    if sample > max_sample:
		max_sample = sample
    N = len(data) or 1
    avg_sample = sum_sample / N
    std_sample = math.sqrt((sum_sample_square -
			    (N * avg_sample * avg_sample)) / N)
    return max_sample, avg_sample, std_sample, data

def get_nick(uid):
    fn = '%s/home/%c/%s/angelmsg' % (BBSHOME, uid[0], uid)
    nick = ''
    if os.path.exists(fn):
	nick = open(fn).readline().strip().decode('big5').strip('%%[')
    else:
	nick = uid
    return (nick + '小天使'.decode('big5')).encode('big5')

def build_badges(max_sample, avg_sample, std_sample, data):
    result = {}
    filters = [is_all_reject2]
    for uid, e in data.items():
	nick = '%s (線上%d 關呼叫%d)' % (get_nick(uid), e[0], e[1]+e[2])
	if DEBUG:
	    nick += ' {%s/%d/%d/%d}' % (uid, e[0], e[1], e[2])
	entry = Entry(e[0], e[1], e[2], max_sample, avg_sample, std_sample)
	if is_lazy(entry):
	    badges = [is_lazy.__doc__]
	else:
	    badges = [f.__doc__ for f in filters if f(entry)]
	for b in badges:
	    if b not in result:
		result[b] = []
	    result[b] += [nick]
    return result

def main():
    max_sample, avg_sample, std_sample, data = parse_perf_file(INPUT_FILE)
    # print 'max=%f, avg=%f, std=%f' % (max_sample, avg_sample, std_sample)
    if max_sample < SAMPLE_MINIMAL:
    	exit()

    print PREFIX_DOC
    if DEBUG:
	print 'max, avg, std: %d, %d, %d' % (max_sample, avg_sample, std_sample)
    elif REPORT_SAMPLE_STAT:
	print ' SAMPLE 數最大值 / 平均 / 標準差: %d / %d / %d\n' % (
	       max_sample, avg_sample, std_sample)
    else:
	pass
    result = build_badges(max_sample, avg_sample, std_sample, data)
    for k, v in result.items():
	print '%s:\n  %s\n' % (k, '\n  '.join(v))

if __name__ == '__main__':
    main()
