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
REQ_ADD = 1
REQ_QUERY = 2
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

def PackComment(comment):
    return struct.pack(CommentFormatString, *comment)

def LoadComment(key):
    blob = g_db.get(key)
    if blob is None:
	return ''
    else:
	return UnpackComment(g_db.get(key))

def SaveComment(keypak, comment):
    logging.debug("SaveComment: %r => %r" % (keypak, comment))
    key = '%s/%s' % (keypak.board, keypak.file)
    num = g_db.get(key)
    num = 1 if (num is None) else (int(num) + 1)
    g_db.set(key, '%d' % num)
    key += '#%08d' % (num)
    g_db.set(key, PackComment(comment))
    logging.info('Saved comment: %s', key)

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
	elif req.operation == REQ_QUERY:
	    raise NotImplementedError('REQ_QUERY is not implemented')
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
