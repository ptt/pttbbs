#!/usr/bin/env python
# Copyright (c) 2012 Hung-Te Lin <piaip@csie.ntu.edu.tw>. All rights reserved.
# Use of this source code is governed by a BSD license.

"""
banipd: Daemon to check if an IP is banned, and sending to reason.
"""

import eventlet
import getopt
import logging
import pprint
import re
import signal
import string
import struct
import sys


_SERVER_ADDR = '127.0.0.1'
_SERVER_PORT = 5134
_CONFIG_FILE = "/home/bbs/etc/banip.conf"

g_tbl = {}


def check_ipv4(text):
    ip = text.split('.')
    if len(ip) != 4:
        raise ValueError('Not valid IPV4: %s' % text)
    for i in ip:
        ii = int(i)
        if ii < 0 or ii > 255:
            raise ValueError('Not valid IPV4: %s' % text)
    return True


def LoadConfigTable(filename):
    table = {}

    class Entry(object):
        def __init__(self):
            self.clear()

        def clear(self):
            self.ip = ''
            self.text = ''

        def complete(self):
            ips = self.build_ip()
            for ip in ips:
                if ip in table:
                    raise ValueError('duplicated IP: %s' % ip)
                table[ip] = self.text.rstrip() + "\n"
            self.clear()

        def add_ip(self, s):
            if '#' in s:
                s = s.partition('#')[0]
            self.ip += ' ' + s

        def add_text(self, s):
            self.text += s

        def build_ip(self):
            ips = self.ip.split()
            if not all((check_ipv4(ip) for ip in ips)):
                raise Exception('Bad IP.')
            return ips

    entry = Entry()
    with open(filename, 'rt') as f:
        for line in f:
            t = line.strip()
            if t.startswith('#'):
                continue
            if t and t[0] in string.digits:
                # create new entry, or continue
                if entry.text:
                    entry.complete()
                entry.add_ip(t)
            elif entry.ip:
                # more text
                entry.add_text(line)
            elif not t:
                continue
            else:
                raise ValueError("Text before IP definition: %s" % t)
    if entry.ip:
        entry.complete()
    return table


def handle_request(sock, fd):
    # Input: IP\n
    # Output: int32_t len, BYTE[len]
    fmt_len = '@i'
    try:
        ip = fd.readline().strip()
        msg = g_tbl.get(ip, '')
        logging.debug('Query [%s]: %s', ip, '[BANNED]' if msg else '[PASS]')
        fd.write(struct.pack(fmt_len, len(msg)))
        fd.write(msg)
    except:
        logging.exception("handle_request")
    finally:
        try:
            fd.close()
            sock.close()
        except:
            pass


def main(myname, argv):
    global g_tbl

    def handle_hup(signum, stack):
        logging.warn("Reload configuration table: %s" % config_file)
        try:
            newtbl = LoadConfigTable(config_file)
            g_tbl.clear()
            g_tbl.update()
            logging.warn("Successfully updated table.")
        except:
            logging.exception("Failed loading new config: %s" % config_file)
            return

    def usage():
        print ("Usage: %s [options] config_file\n"
               "-c: check only.\n"
               "-d: enable debug output.\n"
               "config_file: default to %s" % _CONFIG_FILE)
    try:
        opts, args = getopt.getopt(argv, "cd")
    except getopt.GetoptError, err:
        print str(err)
        usage()
        exit(2)

    level = logging.WARNING
    check_only = False
    test = None
    for o, a in opts:
        if o == '-c':
            check_only = True
        elif o == '-d':
            level = logging.DEBUG
        else:
            assert False, "Unkown param: %s" % o
    if len(args) > 1:
        print "Too many config files: %s" % args
        usage()
        exit(2)
    config_file = args[0] if args else _CONFIG_FILE
    logging.basicConfig(level=level, format='%(asctime)-15s %(message)s')
    logging.info('Checking config file: %s', config_file)
    g_tbl.clear()
    g_tbl.update(LoadConfigTable(config_file))
    if check_only:
        # pprint.pprint(g_tbl)
        print "%s: PASSED." % config_file
        return
    logging.warn("Serving at %s:%s [config:%s]...", _SERVER_ADDR, _SERVER_PORT,
                 config_file)
    signal.signal(signal.SIGHUP, handle_hup)
    server = eventlet.listen((_SERVER_ADDR, _SERVER_PORT))
    pool = eventlet.GreenPool()
    while True:
        try:
            new_sock, address = server.accept()
            pool.spawn_n(handle_request, new_sock, new_sock.makefile('rw'))
        except (SystemExit, KeyboardInterrupt):
            break


if __name__ == '__main__':
    main(sys.argv[0], sys.argv[1:])
