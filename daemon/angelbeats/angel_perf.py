#!/usr/bin/env python2
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

PREFIX_DOC = '''== ���g�p�ѨϬd�ͻH��ĳ�W�� (�t�Φ۰ʲ���: %s) ==

����: �ѨϤ��|�C�Q�����|�έp�@���Ҧ����p�ѨϪ��A�A
      �T�{ (1) �p�ѨϷ�ɬO�_�b�u�W (2) ���٩I�s���O�_����/�����C
      �̾ڤѨϤ��ѹ��d�ͻH���w�q�u��d��Ӧh�����S�}�ҩI�s���v�A
      �Y�W�u�ɶ��L�֩άO���I�s�ɶ��L���h�|�X�{�b�C�g�έp���W�椤�A
      �H�ѫ~�ޤΤj�ѨϰѦҡC

  ** �����U�z�ѦW�沣�ͤ�k�μƦr�N�q�A²��Ҥl�p�U: �Y���p�Ѩ� ABC �T�W,
     A �b�u�W�}�I�s, B �b�u�W�������I�s��, C ���b�u�W:
     �Y�馭�W 10:30 �t�ζi��έp�A�t�δN�|�O��:
                     [ID] [�u�W] [����] [����]
                     A    1      0      0     
                     B    1      0      1     
                     C    0      0      0     
              10:31 �ϥΪ̻P�p�Ѩ�A�b������y (����y���έp)
              10:32 C �W�u�F (�٤��έp - �Q�����~��@��)
              10:33 A �אּ���� (�٤��έp - �Q�����~��@��)
              10:40 �t�ΦA���i��έp�A���O���p�U:
	            (ABC���b�u�W�AA�����AB�����AC�}�I�s)
		     [ID] [�u�W] [����] [����]
		     A    1+1=2  0+1=1  0+0=0  
		     B    1+1=2  0+0=0  1+1=2 
		     C    0+1=1  0+0=0  0+0=0  
		    �B�U���έp�ɶ��� 10:50�C

     ���y�ܻ��A(�Ʀr/6) �N�j���O�u�W/����/�������`�p�ɼơC
''' % (time.ctime())

def is_lazy(e):
    # 'LAZY'
    '\033[1;33m�H�U�O[�u�W]�έp���G�L�C(�p��5)���p�Ѩ�\033[m'
    # return e.sample < (e.avg - 1.0 * e.std)
    return e.sample < 5

def is_all_reject2(e):
    # 'ALL_REJECT2'
    '\033[1;31m�H�U�O���I�s�έp��ҹL��([�u�W]��[����]�P[����]�p��5)���p�Ѩ�\033[m'
    return (e.pause2 + e.pause1 >= e.sample - 4)

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
    return (nick + '�p�Ѩ�'.decode('big5')).encode('big5')

def build_badges(max_sample, avg_sample, std_sample, data):
    result = {}
    filters = [is_all_reject2]
    for uid, e in data.items():
	nick = '%s (�u�W%d ����%d ����%d)' % (get_nick(uid), e[0], e[1], e[2])
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
	print ' SAMPLE �Ƴ̤j�� / ���� / �зǮt: %d / %d / %d\n' % (
	       max_sample, avg_sample, std_sample)
    else:
	pass
    result = build_badges(max_sample, avg_sample, std_sample, data)
    for k, v in result.items():
	print '%s:\n  %s\n' % (k, '\n  '.join(v))

if __name__ == '__main__':
    main()
