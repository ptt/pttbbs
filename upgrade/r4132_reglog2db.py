#!/usr/bin/env python
#
# parse register.log and create reglog.db
#
# please first test and fix your register.log, then 
# try writing to database.
# 
# Create: piaip

# python 2.5:
#import sqlite3
# python 2.4 or prior:
from pysqlite2 import dbapi2 as sqlite3

import sys
import time

def initdb(dbfn) :
    db = sqlite3.connect(dbfn)
    c = db.cursor()
    # separate the tables to 'approved' and 'rejected',
    # since the 'rejected' is rarely used...
    c.execute('create table if not exists approved ' +
            '(uid text, name text, career text, ' +
            ' addr text, phone text, date integer, approved text)');
    c.execute('create table if not exists rejected ' +
            '(uid text, name text, career text, ' +
            ' addr text, phone text, date integer, rejected text)');
    return (db, c)

def processblk(m) :
    #print "process blk!"
    if not m.has_key('uid') :
        print "no uid record at line: ", lineno
        sys.exit(-1)
    if (not m.has_key('approved')) and (not m.has_key('rejected')) :
        print "invalid record (action?) at line: ", lineno
        sys.exit(-1)
    if c != None :
        type = 'rejected';
        if m.has_key('approved') : type = 'approved';
        # write into database
        sql = 'insert into ' + type + ' values (?,?,?,?,?,?,?)';
        # follow the orders
        c.execute(sql, (m['uid'], m['name'], m['career'], m['addr'], m['phone'], m['date'], m[type]));

# default input
fn = "register.log" 
dbfn= ""
db = None
c = None
if len (sys.argv) < 1 :
    print "syntax:", sys.argv[0], "register.log [reglog.db]"
    sys.exit(0)
if len (sys.argv) > 1 :
    fn = sys.argv[1]
if len (sys.argv) > 2 :
    dbfn = sys.argv[2]

print "Opening ", fn, "..."
f = open(fn, "rt")

if dbfn != '' :
    (db, c) = initdb(dbfn)

m={}
lineno = 0
tickets = 0
for s in f :
    lineno = lineno +1
    # for each line...
    if s.startswith("num:")   : continue
    if s.startswith("email:") : continue
    if s.startswith("ident:") : continue
    if s.startswith("]") : continue
    if s.startswith("----")   :
            # ready to build new one
            processblk(m)
            m = {}
            tickets = tickets +1
            if tickets % 10000 == 0 :
                if db : db.commit()
                print tickets, "forms processed."
            continue
    v = s.split(': ', 1)
    if len(v) != 2 :
        print "error at line ", lineno
        sys.exit(-1)
    k = v[0].lower()
    if (m.has_key(k)) :
        print "key duplicated at line ", lineno
        sys.exit(-1)
    # convert timestamp
    v = v[1].strip()
    if (k == 'date') :
        # strip final weekday
        v = v.split(' ') # mdy HMS abrv
        if len(v) < 2 or len(v) > 3 :
            print "invalid date entry at line ", lineno
            sys.exit(-1)
        v = v[0] + ' ' + v[1]
        # build timestamp
        v = int(time.mktime(time.strptime(v, "%m/%d/%Y %H:%M:%S")))
    m[k] = v

f.close()

# vim:sw=4:expandtab
