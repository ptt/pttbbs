#!/usr/bin/env python

import collections
import json
import logging
import os
import struct
import sys
import time

from gevent import monkey; monkey.patch_all()
import gevent
import gevent.server
import leveldb

from pyutil import pttstruct
from pyutil import pttpost
from pyutil import big5

# Ref: ../../include/daemons.h
RequestFormatString = 'HH'
Request = collections.namedtuple('Request', 'cb operation')
PostKeyFormatString = '%ds%ds' % (pttstruct.IDLEN + 1, pttstruct.FNLEN + 1)
PostKey = collections.namedtuple('PostKey', 'board file')
# TODO in future we don't need userref, and ctime should be automatically set.
AddRecordFormatString = 'III%ds' % (pttstruct.IDLEN + 1)
AddRecord = collections.namedtuple('AddRecord', 'userref ctime ipv4 userid')
CONTENT_LEN_FORMAT = 'I'
REQ_ADD = 1
REQ_IMPORT = 2
REQ_GET_CONTENT = 3
REQ_IMPORT_REMOTE = 4
_SERVER_ADDR = '127.0.0.1'
_SERVER_PORT = 5135
_DB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
			'db_posts.db')
BBSHOME = '/home/bbs'

def serialize(data):
    return json.dumps(data)

def deserialize(data):
    return json.loads(data)

def DecodeFileHeader(blob):
    header = pttstruct.unpack_data(blob, pttstruct.FILEHEADER_FMT)
    return header

def EncodeFileHeader(header):
    blob = pttstruct.pack_data(header, pttstruct.FILEHEADER_FMT)
    return blob

def SavePost(content, is_legacy, keypak, data, extra=None):
    if extra:
	data.update(extra._asdict())
    logging.debug("SavePost: %r => %r", keypak, data)
    key = '%s/%s' % (keypak.board, keypak.file)
    g_db.set(key, serialize(data))
    logging.debug(' Saved: %s', key)
    content_file = os.path.join(BBSHOME, 'boards', keypak.board[0],
				keypak.board, keypak.file)
    comments = []
    if content is None:
	content_len = os.path.getsize(content_file)
	content = open(content_file).read()
    else:
	content_len = len(content)

    if is_legacy:
	(content, comments) = pttpost.ParsePost(content)
	content_len = len(content)
	ResetPostComment(keypak)

    start = time.time()
    g_db.set(key + ':content', content)
    exec_time = time.time() - start
    logging.debug(' Content (%d) save time: %.3fs.', content_len, exec_time)
    if exec_time > 0.1:
	logging.error('%s/%s: save time (%d bytes): %.3fs.',
		      keypak.board, keypak.file, content_len, exec_time)
    for comment in comments:
	SavePostComment(keypak, comment)
    return content_len

def GetPostContent(keypak):
    logging.debug("GetPostContent: %r", keypak)
    key = '%s/%s' % (keypak.board, keypak.file)
    content = g_db.get(key + ':content') or ''
    logging.debug(' content: %d bytes.' % len(content))
    comment_num = int(g_db.get(key + ':comment') or '0')
    for i in range(comment_num):
	comment = deserialize(g_db.get('%s:comment#%08d' % (key, i + 1)))
	content += '<%s> %s: %s %s\n' % tuple(map(big5.encode, comment))
    return content

def SavePostComment(keypak, comment):
    logging.debug("SavePostComment: %r => %r", keypak, comment)
    key = '%s/%s:comment' % (keypak.board, keypak.file)
    num = int(g_db.get(key) or '0')
    num += 1
    g_db.set(key, str(num))
    key += '#%08d' % (num)
    g_db.set(key, serialize(comment))

def ResetPostComment(keypak):
    key = '%s/%s:comment' % (keypak.board, keypak.file)
    g_db.set(key, '0')

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

    def strip_if_string(v):
	return v.strip(chr(0)) if type(v) == str else v

    def read_and_unpack(fd, fmt, names=None):
	data = map(strip_if_string,
		   struct.unpack(fmt, fd.read(struct.calcsize(fmt))))
	return names._make(data) if names else data

    fd = socket.makefile('rw')
    try:
	req = read_and_unpack(fd, RequestFormatString, Request)
	logging.debug('Found request: %d' % req.operation)
	if req.operation == REQ_ADD:
	    header = DecodeFileHeader(fd.read(pttstruct.FILEHEADER_SIZE))
	    extra = read_and_unpack(fd, AddRecordFormatString, AddRecord)
	    key = read_and_unpack(fd, PostKeyFormatString, PostKey)
	    content_len = SavePost(None, False, key, header, extra)
	    fd.write(struct.pack(CONTENT_LEN_FORMAT, content_len))
	elif req.operation == REQ_IMPORT:
	    header = DecodeFileHeader(fd.read(pttstruct.FILEHEADER_SIZE))
	    extra = read_and_unpack(fd, AddRecordFormatString, AddRecord)
	    key = read_and_unpack(fd, PostKeyFormatString, PostKey)
	    content_len = SavePost(None, True, key, header, extra)
	    fd.write(struct.pack(CONTENT_LEN_FORMAT, content_len))
	elif req.operation == REQ_IMPORT_REMOTE:
	    header = DecodeFileHeader(fd.read(pttstruct.FILEHEADER_SIZE))
	    extra = read_and_unpack(fd, AddRecordFormatString, AddRecord)
	    key = read_and_unpack(fd, PostKeyFormatString, PostKey)
	    content_len = read_and_unpack(fd, CONTENT_LEN_FORMAT)[0]
	    content = fd.read(content_len)
	    content_len = SavePost(content, True, key, header, extra)
	    fd.write(struct.pack(CONTENT_LEN_FORMAT, content_len))
	elif req.operation == REQ_GET_CONTENT:
	    key = read_and_unpack(fd, PostKeyFormatString, PostKey)
	    content = GetPostContent(key)
	    fd.write(struct.pack(CONTENT_LEN_FORMAT, len(content)))
	    fd.write(content)
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
