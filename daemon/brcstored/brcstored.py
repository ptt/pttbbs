#!/usr/bin/env python
# Copyright (c) 2012 Hung-Te Lin <piaip@csie.ntu.edu.tw>. All rights reserved.
# Use of this source code is governed by a BSD license.

"""
brcstored: Board RC (brc) storage daemon.

Provides a way to manage per-user BRC object into isolated database.
"""

# Configuration of backends. 

# (Network) Use gevent of eventlet.
USE_GEVENT = True

# (Database) Use KyotoCabinet or LevelDB.
USE_KYOTO = False

#-----------------------------------------------------------------------

if USE_GEVENT:
    from gevent import monkey; monkey.patch_all()
    import gevent
    import gevent.server
else:
    import eventlet

if USE_KYOTO:
    import kyotocabinet
else:
    import leveldb

import StringIO
import logging
import os
import re
import struct
import sys
import time


_SERVER_ADDR = '127.0.0.1'
_SERVER_PORT = 5133
_DB_PATH = '/home/bbs/brcstore/database'


# Performance counters

class OperationPerf(object):

    def __init__(self, _type, _timeout=0.5):
        self._type = _type
        self._timeout = _timeout

    def __enter__(self):
        self._clock = time.time()

    def __exit__(self, *args, **kargs):
        delta = time.time() - self._clock
        if delta > self._timeout:
            sys.stderr.write("[%s %.3f]" % (self._type, delta))


class Perf(object):

    def __init__(self):
        self.read = 0
        self.write = 0
        self.req = 0
        self.clock = 0

    def add_req(self):
        if self.clock == 0:
            self.clock = time.time()
        self.req += 1

    def report(self):
        if self.req % 100 == 0:
            sys.stderr.write(
                    '+' if self.read > self.write else
                    '-' if self.read < self.write else
                    '=')
            self.read = self.write = 0
        if self.req % 7000 == 0:
            sys.stderr.write("[%.2f req/s]%s\n" %
                    (self.req / (time.time() - self.clock),
                    time.strftime('%H:%M')))
            self.clock = time.time()
            self.req = 0

    def add_read(self):
        self.read += 1

    def add_write(self):
        self.write += 1


g_perf = Perf()


def open_database(db_path):
    global g_db

    if USE_KYOTO:
        g_db = kyotocabinet.DB()
        if not g_db.open(db_path,
                         kyotocabinet.DB.OWRITER | kyotocabinet.DB.OCREATE):
            sys.stderr.write("open error: " + str(g_db.error()))
            sys.exit(1)
    else:
        # BRCv3 max size = 49152 (8192*3*2), so let's increase block size.
        # LevelDB default I/O buffer size: R=8M, W=2M.
        db = leveldb.LevelDB(db_path,
                #block_size=49152,
                # block_cache_size=(16 * (2 << 20)),
                write_buffer_size=(32 * (2 << 20)),
                )

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

        g_db = LevelDBWrapper(db)


def handle_request(socket, _):
    # WRITE: 'w' + UID + '\n' + int32_t len, BYTE[len]
    #  Returns: NUL
    # READ: 'r' + UID + '\n'
    #  Returns: int32_t len, BYTE[len]   (len=-1 if UID does not exist)
    fd = socket.makefile('rw')
    fmt_len = '@i'
    g_perf.add_req()
    g_perf.report()
    try:
        command = fd.read(1)
        uid = fd.readline()
        uid = uid.strip()
        if command == 'r':
            g_perf.add_read()
            with OperationPerf('R'):
                msg = g_db.get(uid)
            if msg is None:
                fd.write(struct.pack(fmt_len, -1))
                logging.info('Read : %s: (NOT FOUND)', uid)
            else:
                fd.write(struct.pack(fmt_len, len(msg)))
                fd.write(msg)
                logging.info('Read : %s: size=%d', uid, len(msg))
        elif command == 'w':
            g_perf.add_write()
            msglen = struct.unpack(fmt_len,
                                   fd.read(struct.calcsize(fmt_len)))[0]
            msg = fd.read(msglen)
	    if len(msg) != msglen:
		logging.warn('Write: incomplete: %d/%d, %s',
			len(msg), msglen, uid)
            logging.info('Write: %s: size=%d', uid, len(msg))
            with OperationPerf('W'):
                g_db.set(uid, msg)
            # Sync byte, anything should work.
            fd.write(chr(0))
        elif not command:
            raise ValueError('Incomplete request (no command).')
        else:
            raise ValueError('Unknown request: 0x%02X' % ord(command))
    except:
        logging.exception("handle_request")
    finally:
        try:
            fd.close()
            socket.close()
        except:
            pass


def main(myname, argv):
    level = logging.WARNING
    # level = logging.INFO
    # level = logging.DEBUG
    logging.basicConfig(level=level, format='%(asctime)-15s %(message)s')
    if len(argv) not in [0, 1]:
        print "Usage: %s [db_path]" % myname
        exit(1)
    db_path = argv[0] if len(argv) > 0 else _DB_PATH
    if USE_KYOTO:
        db_path += '.kch'
    logging.warn("Serving via %s at %s:%s [db:%s:%s][pid:%d]...",
                 "gevent" if USE_GEVENT else "eventlet",
                 _SERVER_ADDR, _SERVER_PORT, 
                 'KyotoCabinet' if USE_KYOTO else 'LevelDB',
                 db_path, os.getpid())
    open_database(db_path)

    if USE_GEVENT:
        server = gevent.server.StreamServer(
                (_SERVER_ADDR, _SERVER_PORT), handle_request)
        server.serve_forever()
    else:
        server = eventlet.listen((_SERVER_ADDR, _SERVER_PORT))
        pool = eventlet.GreenPool()
        while True:
            try:
                new_sock, address = server.accept()
                pool.spawn_n(handle_request, new_sock, address)
            except (SystemExit, KeyboardInterrupt):
                break


if __name__ == '__main__':
    main(sys.argv[0], sys.argv[1:])
