#!/usr/bin/env python

import struct
import collections
import sys
import logging
import os

from gevent import monkey; monkey.patch_all()
import gevent
import gevent.server
import leveldb

# Ref: ../../include/pttstruct.h
IDLEN = 12
FNLEN = 28
COMMENTLEN = 80


# Ref: ../../include/daemons.h
RequestFormatString = 'HH';
Request = collections.namedtuple('Request', 'cb operation')
CommentFormatString = "IIII%ds%ds" % (IDLEN + 1, COMMENTLEN + 1)
Comment = collections.namedtuple('Comment',
	'time ipv4 userref type userid msg')
CommentKeyFormatString = '%ds%ds' % (IDLEN + 1, FNLEN + 1)
CommentKey = collections.namedtuple('CommentKey', 'board file')
QueryFormatString = 'I%ds%ds' % (IDLEN + 1, FNLEN + 1)
Query = collections.namedtuple('Query', 'start board file')
REQ_ADD = 1
REQ_QUERY_COUNT = 2
REQ_QUERY_BODY = 3
REQ_MARK_DELETE = 4
REQ_LIST = 0x204c # ' L' for console debug.
_SERVER_ADDR = '127.0.0.1'
_SERVER_PORT = 5134
_DB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
			'db_comments.db')


def UnpackComment(blob):
    def strip_if_string(v):
	return v.strip(chr(0)) if type(v) == str else v
    data = struct.unpack(CommentFormatString, blob)
    logging.debug("UnpackComment: %r" % (data,))
    return Comment._make(map(strip_if_string, data))

def UnpackCommentKey(blob):
    def strip_if_string(v):
	return v.strip(chr(0)) if type(v) == str else v
    data = struct.unpack(CommentKeyFormatString, blob)
    logging.debug("UnpackCommentKey: %r" % (data,))
    return CommentKey._make(map(strip_if_string, data))

def UnpackQuery(blob):
    def strip_if_string(v):
	return v.strip(chr(0)) if type(v) == str else v
    data = struct.unpack(QueryFormatString, blob)
    logging.debug("Query: %r" % (data,))
    return Query._make(map(strip_if_string, data))

def PackComment(comment):
    return struct.pack(CommentFormatString, *comment)

def LoadComment(query):
    logging.debug("LoadComment: %r", query)
    key = '%s/%s' % (query.board, query.file)
    num = int(g_db.get(key) or '0')
    if query.start >= num:
	return None
    key += '#%08d' % (query.start + 1)
    data = g_db.get(key)
    logging.debug(" => %r", UnpackComment(data))
    return UnpackComment(data)

def LoadCommentCount(query):
    key = '%s/%s' % (query.board, query.file)
    num = int(g_db.get(key) or '0')
    logging.debug('LoadCommentCount: key: %s, value: %r', key, g_db.get(key))
    return num

def MarkCommentDeleted(query):
    logging.debug("MarkCommentDeleted: %r", query)
    key = '%s/%s' % (query.board, query.file)
    num = int(g_db.get(key) or '0')
    if query.start >= num:
	return -1
    key += '#%08d' % (query.start + 1)
    data = g_db.get(key)
    comment = UnpackComment(data)
    if comment.type & 0x80000000:
	return -2
    # Comment is a named tuple, sorry. We have to reconstruct one.
    comment = comment._replace(type = (comment.type | 0x80000000))
    g_db.set(key, PackComment(comment))
    logging.debug(' Deleted: %s', key)
    return 0

def SaveComment(keypak, comment):
    logging.debug("SaveComment: %r => %r", keypak, comment)
    key = '%s/%s' % (keypak.board, keypak.file)
    num = g_db.get(key)
    num = 1 if (num is None) else (int(num) + 1)
    g_db.set(key, '%d' % num)
    key += '#%08d' % (num)
    g_db.set(key, PackComment(comment))
    logging.debug(' Saved: %s', key)

def ListComments():
    logging.debug("ListComments")
    for i in g_db.RangeIter():
	key = i[0]
	#if '#' in key:
	#    continue
	logging.info(' %s', key)

def open_database(db_path):
    global g_db

    class LevelDBWrapper(object):
	def __init__(self, db):
	    self.db = db

	def get(self, key):
	    try:
		return self.db.Get(key)
	    except KeyError:
		return None

	def set(self, key, value):
	    self.db.Put(key, value)

	def RangeIter(self, **args):
	    return self.db.RangeIter(*args)

    g_db = LevelDBWrapper(leveldb.LevelDB(db_path))
    return g_db

def handle_request(socket, _):
    fd = socket.makefile('rw')
    fmt_len = '@i'
    try:
        req = fd.read(struct.calcsize(RequestFormatString))
	req = Request._make(struct.unpack(RequestFormatString, req))
	logging.debug('Found request: %d' % req.operation)
        # TODO check req.cb
	if req.operation == REQ_ADD:
	    blob = fd.read(struct.calcsize(CommentFormatString))
	    keyblob = fd.read(struct.calcsize(CommentKeyFormatString))
	    SaveComment(UnpackCommentKey(keyblob), UnpackComment(blob))
	elif req.operation == REQ_QUERY_COUNT:
	    blob = fd.read(struct.calcsize(QueryFormatString))
	    num = LoadCommentCount(UnpackQuery(blob))
	    fd.write(struct.pack('I', num))
	    logging.debug('response: %d', num)
	elif req.operation == REQ_QUERY_BODY:
	    blob = fd.read(struct.calcsize(QueryFormatString))
	    comment = LoadComment(UnpackQuery(blob))
	    logging.debug('Got comment %r', comment)
	    if comment is None:
		fd.write(struct.pack('H', 0))
	    else:
		data = PackComment(comment)
		fd.write(struct.pack('H', len(data)))
		fd.write(data)
	elif req.operation == REQ_MARK_DELETE:
	    blob = fd.read(struct.calcsize(QueryFormatString))
	    ret = MarkCommentDeleted(UnpackQuery(blob))
	    logging.debug('Marked comment as deleted: %d.', ret)
	    fd.write(struct.pack('i', ret))
	elif req.operation == REQ_LIST:
	    ListComments()
	else:
	    raise ValueError('Unkown operation %d' % req.operation)
    except:
        logging.exception("handle_request")
    finally:
        try:
            fd.close()
            socket.close()
        except:
            pass

def main(myname, argv):
    level = logging.INFO
    level = logging.WARNING
    level = logging.DEBUG
    logging.basicConfig(level=level, format='%(asctime)-15s %(message)s')
    if len(argv) not in [0, 1]:
        print "Usage: %s [db_path]" % myname
        exit(1)
    db_path = argv[0] if len(argv) > 0 else _DB_PATH
    logging.warn("Serving at %s:%s [db:%s][pid:%d]...",
                 _SERVER_ADDR, _SERVER_PORT, db_path, os.getpid())
    open_database(db_path)
    server = gevent.server.StreamServer(
	    (_SERVER_ADDR, _SERVER_PORT), handle_request)
    server.serve_forever()

if __name__ == '__main__':
    main(sys.argv[0], sys.argv[1:])
