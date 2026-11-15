#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Convert the records of muting and unmuting in the modlog audit records
(boards/B/BOARD/modlog.audit) that came from the old board mute history
(muted.history, moved there by migrate_muted_history_to_modlog.py) to the
format of the modlog, e.g.

    \\e[1m11/25/2016 21:08:47 \\e[33mmarkban\\e[m 暫停 \\e[1;31mmaingjo\\e[m 發言，期限為 7 天
      理由: 板規一

becomes

    11/25/2016 21:08 管理人員 暫停發言: maingjo 7天: 板規一
      執行: markban

The account of who did it is kept in the 執行 field, but the old records
do not tell the role it was done as (moderator, group op, sysop...), so it
is shown as 管理人員, nor where it was done from (no IP after the account).
Other lines are kept as they are.
The files are in Big5 (or whatever bytes they have; only the records
converted are decoded).

The board mute history shown by mbbsd is made from modlog.audit again when
that is newer, so it follows.

Better stop the BBS (or at least all mbbsd) before running this. Records
appended while a file is being converted are not lost (the file is
converted again), but a mbbsd that opened it just before it is replaced may
append to the old file.

Usage:
    convert_modlog_audit_mutes.py [options]

Options:
    -b, --bbshome <dir>   Path to BBSHOME (default: auto-detected)
    --board <board>       Process only a specific board
    -k, --keep            Keep the original as modlog.audit.orig
    -n, --dry-run         Show what would be done without changing files
    -v, --verbose         Show the records converted
    -h, --help            Show this help message
"""

import argparse
import glob
import os
import re
import sys

FN_MODLOG_AUDIT = 'modlog.audit'
ENCODING = 'big5hkscs'

# As modlog_record() in mbbsd/acl.c.
MODLOG_COLS = 79
MODLOG_INDENT = '  '
MODLOG_MUTE = '暫停發言'
MODLOG_UNMUTE = '開放發言'
ROLE = '管理人員'

ANSI = re.compile(r'\x1b\[[0-9;]*m')
# The first line of an old record, without colors. Some have the date twice
# (once by log_filef()), and the oldest say 限制 instead of 暫停.
LEGACY = re.compile(
    r'^(?P<date>\d\d/\d\d/\d{4}) (?P<time>\d\d:\d\d):\d\d '
    r'(?:\d\d/\d\d/\d{4} \d\d:\d\d:\d\d )?'
    r'(?P<error>\[系統錯誤\] )?(?P<by>\S+) (?:'
    r'(?:暫停|限制) (?P<uid>\S+) 發言，期限為 (?P<days>-?\d+) 天(?P<extra>.*)'
    r'|解除 (?P<uid2>\S+) 的禁言限制 \(距原期限尚有 (?P<left>-?\d+) 天\)'
    r'(?P<extra2>.*))$')
REASON = re.compile(r'^ +理由:(?P<reason>.*)$')


def find_bbshome(custom_path=None):
    if custom_path:
        return os.path.abspath(custom_path)
    if os.environ.get('BBSHOME'):
        return os.path.abspath(os.environ['BBSHOME'])
    if os.path.isdir('/v/bbshome'):
        return '/v/bbshome'
    home = os.path.expanduser('~')
    if os.path.isdir(os.path.join(home, 'boards')):
        return home
    return '.'


def decode(line):
    return line.decode(ENCODING, 'surrogateescape')


def encode(text):
    return text.encode(ENCODING, 'surrogateescape')


def width(text):
    """Bytes in Big5, as MODLOG_COLS counts."""
    return len(encode(text))


def strip(text):
    """As modlog_strip(): no colors or control characters."""
    text = ANSI.sub('', text)
    return re.sub(r'[\x00-\x1f\x7f]', ' ', text).strip()


def convert_record(m, reason, others):
    """The lines (str) of the record matched by m, as modlog_record()."""
    if m.group('uid'):
        action = f"{MODLOG_MUTE}: {m.group('uid')} {m.group('days')}天"
        extra = m.group('extra')
    else:
        action = f"{MODLOG_UNMUTE}: {m.group('uid2')} 原剩{m.group('left')}天"
        extra = m.group('extra2')
    extra = strip(extra or '')
    if extra:
        action += ' ' + extra
    if m.group('error'):
        action += ' [系統錯誤]'
    head = f"{m.group('date')} {m.group('time')} {ROLE} {action}"
    why = strip(reason or '')
    # The reason on the first line if it fits, else as the last field.
    why_in = why and width(head) + width(': ') + width(why) <= MODLOG_COLS
    lines = [f"{head}: {why}" if why_in else head]
    lines.extend(others)
    if why and not why_in:
        lines.append(f"{MODLOG_INDENT}理由: {why}")
    lines.append(f"{MODLOG_INDENT}執行: {m.group('by')}")
    return lines


def convert(data):
    """Returns the converted bytes of data, and the number of records."""
    out = []
    lines = data.split(b'\n')
    # A last line without a new line is kept so.
    i, n = 0, 0
    while i < len(lines):
        line = lines[i]
        m = LEGACY.match(strip(decode(line))) if line[:1] != b' ' else None
        if not m:
            out.append(line)
            i += 1
            continue
        i += 1
        reason, others = None, []
        while i < len(lines) and lines[i][:1] == b' ':
            text = decode(lines[i])
            r = REASON.match(text)
            if r and reason is None:
                reason = r.group('reason')
            else:
                others.append(MODLOG_INDENT + strip(text))
            i += 1
        out.extend(encode(x) for x in convert_record(m, reason, others))
        n += 1
    return b'\n'.join(out), n


def convert_board(audit, keep=False, dry_run=False, verbose=False):
    """Returns the number of records converted."""
    for _ in range(5):
        st = os.stat(audit)
        with open(audit, 'rb') as f:
            data = f.read()
        new, n = convert(data)
        if verbose and n:
            print(f"{audit}: {n} records")
        if dry_run or not n:
            return n
        tmp = audit + '.converting'
        with open(tmp, 'wb') as f:
            f.write(new)
        os.chmod(tmp, st.st_mode & 0o7777)
        try:
            os.chown(tmp, st.st_uid, st.st_gid)
        except PermissionError:
            pass
        # Converted again if something was appended meanwhile.
        st2 = os.stat(audit)
        if (st2.st_size, st2.st_mtime_ns) != (st.st_size, st.st_mtime_ns):
            os.unlink(tmp)
            continue
        if keep:
            os.link(audit, audit + '.orig')
        os.rename(tmp, audit)
        return n
    raise OSError(f"{audit} keeps changing")


def main():
    parser = argparse.ArgumentParser(
        description='Convert old mute records in modlog.audit to the '
                    'modlog format.')
    parser.add_argument('-b', '--bbshome', help='Path to BBSHOME')
    parser.add_argument('--board', help='Process only a specific board')
    parser.add_argument('-k', '--keep', action='store_true',
                        help='Keep the original as modlog.audit.orig')
    parser.add_argument('-n', '--dry-run', action='store_true',
                        help='Show what would be done without changing files')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Show the boards converted')
    args = parser.parse_args()

    bbshome = find_bbshome(args.bbshome)
    if not os.path.isdir(os.path.join(bbshome, 'boards')):
        print(f"Error: {bbshome}/boards not found.", file=sys.stderr)
        return 1

    if args.board:
        pattern = os.path.join(bbshome, 'boards', args.board[0], args.board,
                               FN_MODLOG_AUDIT)
    else:
        pattern = os.path.join(bbshome, 'boards', '*', '*', FN_MODLOG_AUDIT)
    files = sorted(glob.glob(pattern))

    print(f"Converting {len(files)} {FN_MODLOG_AUDIT} in {bbshome}/boards"
          f"{' [DRY-RUN]' if args.dry_run else ''}...")
    boards = records = failed = 0
    for audit in files:
        try:
            n = convert_board(audit, args.keep, args.dry_run, args.verbose)
        except OSError as e:
            print(f"Error: {audit}: {e}", file=sys.stderr)
            failed += 1
            continue
        if n:
            boards += 1
            records += n

    print(f"\nFinished{' (DRY-RUN)' if args.dry_run else ''}:\n"
          f"  Boards:  {boards}\n"
          f"  Records: {records}\n"
          f"  Failed:  {failed}")
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
