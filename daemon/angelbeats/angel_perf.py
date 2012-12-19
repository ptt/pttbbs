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

PREFIX_DOC = '''
== 本週小天使活動資料統計結果 (系統自動產生: %s) ==

說明: 天使公會在新小主人找天使時即時統計所有的小天使狀態，
      得到 (1) 小天使當時是否在線上 (2) 神諭呼叫器是否停收/關閉;
      基於上面結果可分析出下列名單。

      小天使名稱後面的數字為該小天使被統計到的次數 (以下稱 SAMPLE 數)，
      因為 SAMPLE 數字是有人呼叫時才會更新，且為避免惡意洗數字造成結果不公，
      統計每 600 秒最多更新一次(否則有人會趁自己上線開分身狂換小天使)，
      所以此數字大略上接近(但不等於)小天使實際上線時間比例。

      換句話說，即使為零也不代表此小天使都沒上線過，可能只是上線停留時間都
      過短或是上線時都沒有新使用者要呼叫小天使。

''' % (time.ctime())

def is_lazy(e):
    # 'LAZY'
    '\033[1;33m以下是這段時間內SAMPLE數過低的小天使:\033[m'
    # return e.sample < (e.avg - 1.0 * e.std)
    return e.sample < 5

def is_all_reject2(e):
    # 'ALL_REJECT2'
    '\033[1;31m以下是呼叫器關閉/停收時間比例過高(與SAMPLE相差小於2)的小天使:\033[m'
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
	nick = '%s (%d)' % (get_nick(uid), e[0])
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
    else:
	print ' SAMPLE 數最大值 / 平均 / 標準差: %d / %d / %d\n' % (
	       max_sample, avg_sample, std_sample)
    result = build_badges(max_sample, avg_sample, std_sample, data)
    for k, v in result.items():
	print '%s:\n  %s\n' % (k, '\n  '.join(v))

if __name__ == '__main__':
    main()
