#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Move the board mute history (boards/B/BOARD/muted.history) to the modlog audit
record (boards/B/BOARD/modlog.audit), as mutes are now recorded in the modlog.

The contents are kept as they are. If modlog.audit already exists, the mute
history (older) is put in front of it.

Stop the BBS (or at least all mbbsd) before running this.

Usage:
    migrate_muted_history_to_modlog.py [options]

Options:
    -b, --bbshome <dir>   Path to BBSHOME (default: auto-detected)
    --board <board>       Process only a specific board
    -n, --dry-run         Show what would be done without changing files
    -v, --verbose         Verbose logging
    -h, --help            Show this help message
"""

import argparse
import glob
import os
import sys

FN_MUTED_HISTORY = 'muted.history'
FN_MODLOG_AUDIT = 'modlog.audit'


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


def migrate_board(board_dir, dry_run=False, verbose=False):
    """Returns True if the history was merged into an existing audit record."""
    history = os.path.join(board_dir, FN_MUTED_HISTORY)
    audit = os.path.join(board_dir, FN_MODLOG_AUDIT)
    merge = os.path.exists(audit)

    if verbose or dry_run:
        print(f"{os.path.basename(board_dir)}: "
              f"{'merge into' if merge else 'rename to'} {FN_MODLOG_AUDIT}")
    if dry_run:
        return merge

    if merge:
        # Write the merged record next to it and move it over, so the audit
        # record is never seen missing or half written.
        tmp = audit + '.migrating'
        with open(tmp, 'wb') as out:
            for path in (history, audit):
                with open(path, 'rb') as f:
                    out.write(f.read())
        st = os.stat(audit)
        os.chmod(tmp, st.st_mode & 0o7777)
        try:
            os.chown(tmp, st.st_uid, st.st_gid)
        except PermissionError:
            pass
        os.rename(tmp, audit)
        os.unlink(history)
    else:
        os.rename(history, audit)
    return merge


def main():
    parser = argparse.ArgumentParser(
        description='Move board muted.history to modlog.audit.')
    parser.add_argument('-b', '--bbshome', help='Path to BBSHOME')
    parser.add_argument('--board', help='Process only a specific board')
    parser.add_argument('-n', '--dry-run', action='store_true',
                        help='Show what would be done without changing files')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Verbose logging')
    args = parser.parse_args()

    bbshome = find_bbshome(args.bbshome)
    if not os.path.isdir(os.path.join(bbshome, 'boards')):
        print(f"Error: {bbshome}/boards not found.", file=sys.stderr)
        return 1

    if args.board:
        pattern = os.path.join(bbshome, 'boards', args.board[0], args.board,
                               FN_MUTED_HISTORY)
    else:
        pattern = os.path.join(bbshome, 'boards', '*', '*', FN_MUTED_HISTORY)
    files = sorted(glob.glob(pattern))

    print(f"Moving {len(files)} {FN_MUTED_HISTORY} in {bbshome}/boards"
          f"{' [DRY-RUN]' if args.dry_run else ''}...")
    renamed = merged = failed = 0
    for history in files:
        board_dir = os.path.dirname(history)
        try:
            if migrate_board(board_dir, args.dry_run, args.verbose):
                merged += 1
            else:
                renamed += 1
        except OSError as e:
            print(f"Error: {board_dir}: {e}", file=sys.stderr)
            failed += 1

    print(f"\nFinished{' (DRY-RUN)' if args.dry_run else ''}:\n"
          f"  Renamed: {renamed}\n"
          f"  Merged:  {merged}\n"
          f"  Failed:  {failed}")
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
