#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Migrate board ban history (banned.history -> muted.history) to user muted.history,
and migrate user banned/ directories to muted/.

Usage:
    convert_banned_muted.py [options]

Options:
    -b, --bbshome <dir>   Path to BBSHOME (default: auto-detected)
    --user-home <dir>     Path to custom user home directory (writes to PATH/u/userid/muted.history)
    -n, --dry-run         Simulate run without writing files
    -v, --verbose         Verbose logging
    -f, --overwrite       Overwrite muted.history instead of appending
    --clean               Delete existing muted.history before migrating
    --no-rename           Do not rename board history or user banned/ directories
    --board <board>       Process only a specific board
    --user <userid>       Process only a specific user
    -h, --help            Show this help message
"""

import argparse
import datetime
import glob
import os
import re
import struct
import sys

ANSI_ESCAPE = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
PATTERN_DATE = re.compile(r'^(\d{2}/\d{2}/\d{4} \d{2}:\d{2}:\d{2})')
PATTERN_ACTION = re.compile(r'(?:暫停|限制|解除)\s+([A-Za-z0-9_-]+)')

KANBAN_BIG5 = '看板'.encode('big5')
ACTION_WORDS = ('暫停', '限制', '解除')
ACTION_WORDS_BIG5 = tuple(w.encode('big5') for w in ACTION_WORDS)
RAW_BM_REPLACE = re.compile(
    rb'((?:\x1b\[[0-9;]*m)*)([A-Za-z0-9_.-]+)(\s*(?:\x1b\[[0-9;]*m)*\s*)('
    + rb'|'.join(re.escape(w) for w in ACTION_WORDS_BIG5)
    + rb')'
)


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


def scan_target_user_ids(history_files, target_user=None):
    """
    Fast pre-scan of board history files to collect user IDs who actually have records.
    Allows load_passwds to load only relevant users, reducing memory usage from ~500MB to ~25MB.
    """
    if target_user:
        return {target_user.lower()}

    target_uids = set()
    for p in history_files:
        try:
            with open(p, 'rb') as f:
                for line in f:
                    if any(w in line for w in ACTION_WORDS_BIG5):
                        clean = ANSI_ESCAPE.sub('', line.decode('big5', errors='replace'))
                        m = PATTERN_ACTION.search(clean)
                        if m:
                            target_uids.add(m.group(1).lower())
        except Exception:
            pass
    return target_uids


def load_passwds(bbshome, target_uids=None, verbose=False):
    passwds_path = os.path.join(bbshome, '.PASSWDS')
    if not os.path.isfile(passwds_path):
        print(f"Error: {passwds_path} not found.", file=sys.stderr)
        return {}

    users = {}
    with open(passwds_path, 'rb') as f:
        while True:
            chunk = f.read(512)
            if len(chunk) < 512:
                break
            uid = chunk[4:17].split(b'\x00')[0].decode('latin1', errors='ignore')
            if not uid:
                continue
            uid_lower = uid.lower()
            if target_uids is not None and uid_lower not in target_uids:
                continue
            firstlogin = struct.unpack('<I', chunk[96:100])[0]
            users[uid_lower] = (uid, firstlogin)

    if verbose:
        print(f"Loaded {len(users)} user accounts from .PASSWDS")
    return users


def parse_banned_history(file_path, users, target_user=None, verbose=False):
    """
    Parses a board's ban history file (banned.history or muted.history).
    Returns:
        valid_records: dict of user_lower -> list of (timestamp, line1_bytes, line2_bytes)
        stats: dict of counters
    """
    stats = {
        'total_records': 0,
        'matched': 0,
        'skipped_firstlogin': 0,
        'skipped_not_in_passwds': 0,
    }
    user_records = {}

    board_name = os.path.basename(os.path.dirname(file_path))
    board_bytes = board_name.encode('big5', errors='ignore')

    try:
        with open(file_path, 'rb') as f:
            lines = f.readlines()
    except Exception as e:
        print(f"Warning: Failed to read {file_path}: {e}", file=sys.stderr)
        return user_records, stats

    i = 0
    while i < len(lines):
        line1 = lines[i]
        line2 = lines[i + 1] if i + 1 < len(lines) else b''

        clean_line1 = ANSI_ESCAPE.sub('', line1.decode('big5', errors='replace')).strip()
        if not clean_line1 or clean_line1.startswith('理由:'):
            i += 1
            continue

        stats['total_records'] += 1
        m_date = PATTERN_DATE.match(clean_line1)
        m_act = PATTERN_ACTION.search(clean_line1)

        if m_date and m_act:
            dt_str = m_date.group(1)
            target = m_act.group(1)

            if target_user and target.lower() != target_user.lower():
                i += 2
                continue

            try:
                dt = datetime.datetime.strptime(dt_str, '%m/%d/%Y %H:%M:%S')
                ban_ts = int(dt.timestamp())
            except Exception:
                ban_ts = 0

            target_lower = target.lower()
            if target_lower in users:
                canonical_uid, firstlogin = users[target_lower]
                if ban_ts <= firstlogin:
                    stats['skipped_firstlogin'] += 1
                    if verbose:
                        print(f"  [Skip <= firstlogin] {canonical_uid}: ban_time={dt_str} <= firstlogin={datetime.datetime.fromtimestamp(firstlogin)}")
                else:
                    stats['matched'] += 1
                    user_line1 = RAW_BM_REPLACE.sub(
                        lambda m: m.group(1) + board_bytes + b'\x1b[m' + KANBAN_BIG5 + b' ' + m.group(4),
                        line1,
                        count=1
                    )
                    user_records.setdefault(target_lower, []).append((ban_ts, user_line1, line2))
            else:
                stats['skipped_not_in_passwds'] += 1
                if verbose:
                    print(f"  [Skip not found] {target}: not in .PASSWDS")

            i += 2
        else:
            i += 1

    return user_records, stats


def clean_all_muted_history(bbshome, user_home=None, dry_run=False, verbose=False):
    """
    Finds and deletes all existing home/*/*/muted.history files.
    """
    base_dir = user_home if user_home else os.path.join(bbshome, 'home')
    pattern = os.path.join(base_dir, '*', '*', 'muted.history')
    existing_files = glob.glob(pattern)
    print(f"Found {len(existing_files)} existing muted.history file(s) under {base_dir} to clean.")
    for f in existing_files:
        if verbose or dry_run:
            print(f"  [Clean] {'Would remove' if dry_run else 'Removing'} {f}")
        if not dry_run:
            try:
                os.remove(f)
            except Exception as e:
                print(f"Warning: Failed to remove {f}: {e}", file=sys.stderr)
    return len(existing_files)


def write_user_muted_history(bbshome, canonical_uid, records, user_home=None, overwrite=False, dry_run=False, verbose=False):
    """
    Writes or appends records to home/<c>/<canonical_uid>/muted.history
    (or user_home/<c>/<canonical_uid>/muted.history if user_home is set).
    Avoids duplicate entries when appending.
    """
    if user_home:
        home_dir = os.path.join(user_home, canonical_uid[0], canonical_uid)
    else:
        home_dir = os.path.join(bbshome, 'home', canonical_uid[0], canonical_uid)
    muted_file = os.path.join(home_dir, 'muted.history')

    existing_content = b''
    if os.path.isfile(muted_file) and not overwrite:
        with open(muted_file, 'rb') as f:
            existing_content = f.read()

    new_records = []
    for ts, l1, l2 in records:
        entry_bytes = l1 if l1.endswith(b'\n') else l1 + b'\n'
        if l2:
            entry_bytes += l2 if l2.endswith(b'\n') else l2 + b'\n'
        if not overwrite and entry_bytes in existing_content:
            continue
        new_records.append((ts, entry_bytes))

    if not new_records:
        return 0

    if dry_run:
        if verbose:
            print(f"[Dry-run] Would write {len(new_records)} records to {muted_file}")
        return len(new_records)

    os.makedirs(home_dir, exist_ok=True)
    mode = 'wb' if overwrite else 'ab'
    with open(muted_file, mode) as f:
        for ts, entry_bytes in new_records:
            f.write(entry_bytes)

    if verbose:
        print(f"Wrote {len(new_records)} records to {muted_file}")
    return len(new_records)


def merge_board_history(banned_path, muted_path, dry_run=False, verbose=False):
    """
    Merges banned.history (older records) into muted.history (newer records),
    saves into muted.history, and removes banned.history.
    """
    if dry_run:
        if verbose:
            print(f"  [Merge] [Dry-run] Would merge {banned_path} into {muted_path} and remove {banned_path}")
        return True
    try:
        with open(banned_path, 'rb') as f1:
            old_bytes = f1.read()
        with open(muted_path, 'rb') as f2:
            new_bytes = f2.read()

        if old_bytes in new_bytes:
            os.remove(banned_path)
            if verbose:
                print(f"  [Merge] Removed redundant {banned_path} (already in {muted_path})")
            return True

        combined = old_bytes
        if not combined.endswith(b'\n'):
            combined += b'\n'
        combined += new_bytes

        tmp_path = muted_path + '.tmp'
        with open(tmp_path, 'wb') as f_tmp:
            f_tmp.write(combined)
        try:
            st = os.stat(muted_path)
            os.chmod(tmp_path, st.st_mode)
        except Exception:
            pass
        os.replace(tmp_path, muted_path)
        os.remove(banned_path)
        if verbose:
            print(f"  [Merge] Merged {banned_path} into {muted_path} and removed {banned_path}")
        return True
    except Exception as e:
        print(f"Warning: Failed to merge {banned_path} into {muted_path}: {e}", file=sys.stderr)
        return False


def rename_board_file(banned_path, muted_path, dry_run=False, verbose=False):
    """
    Renames banned.history to muted.history.
    """
    if dry_run:
        if verbose:
            print(f"  [Rename] [Dry-run] Would rename {banned_path} -> {muted_path}")
        return True
    try:
        os.rename(banned_path, muted_path)
        if verbose:
            print(f"  [Rename] Renamed {banned_path} -> {muted_path}")
        return True
    except Exception as e:
        print(f"Warning: Failed to rename {banned_path} to {muted_path}: {e}", file=sys.stderr)
        return False


def prepare_board_history_files(bbshome, target_board=None, dry_run=False, no_rename=False, verbose=False):
    """
    Initial search of boards for history files:
    - Checks both old (banned.history) and new (muted.history).
    - If both exist: merges banned.history and muted.history, saves into muted.history, removes banned.history.
    - If only banned.history exists: renames to muted.history.
    - If only muted.history exists: keeps it.
    Returns:
        history_files: list of paths to load for exporting user history (muted.history).
        stats: counters for boards found, merged, renamed, already_new.
    """
    board_dirs = set()
    if target_board:
        b_target = target_board.lower()
        for pattern in (
            os.path.join(bbshome, 'boards', '*', '*'),
            os.path.join(bbshome, 'boards', '*'),
        ):
            for d in glob.glob(pattern):
                if os.path.isdir(d) and os.path.basename(d).lower() == b_target:
                    board_dirs.add(d)
        if not board_dirs:
            b_name = target_board
            for d in (
                os.path.join(bbshome, 'boards', b_name[0], b_name),
                os.path.join(bbshome, 'boards', b_name),
            ):
                if os.path.isdir(d):
                    board_dirs.add(d)
    else:
        for pattern in (
            os.path.join(bbshome, 'boards', '*', '*', 'banned.history'),
            os.path.join(bbshome, 'boards', '*', '*', 'muted.history'),
            os.path.join(bbshome, 'boards', '*', 'banned.history'),
            os.path.join(bbshome, 'boards', '*', 'muted.history'),
        ):
            for p in glob.glob(pattern):
                board_dirs.add(os.path.dirname(p))

    stats = {
        'total_boards': len(board_dirs),
        'merged': 0,
        'renamed': 0,
        'already_new': 0,
    }

    history_files = []

    for d in sorted(board_dirs):
        banned_path = os.path.join(d, 'banned.history')
        muted_path = os.path.join(d, 'muted.history')

        has_banned = os.path.isfile(banned_path)
        has_muted = os.path.isfile(muted_path)

        if has_banned and has_muted:
            stats['merged'] += 1
            if not no_rename:
                merge_board_history(banned_path, muted_path, dry_run=dry_run, verbose=verbose)
            if dry_run or no_rename:
                # In dry-run or no-rename, disk wasn't updated, so parse both in memory
                history_files.extend([banned_path, muted_path])
            else:
                # On disk, merged into muted.history
                history_files.append(muted_path)

        elif has_banned and not has_muted:
            stats['renamed'] += 1
            if not no_rename:
                rename_board_file(banned_path, muted_path, dry_run=dry_run, verbose=verbose)
            if dry_run or no_rename:
                history_files.append(banned_path)
            else:
                history_files.append(muted_path)

        elif has_muted and not has_banned:
            stats['already_new'] += 1
            history_files.append(muted_path)

    return history_files, stats


def rename_user_banned_to_muted(banned_path, dry_run=False, verbose=False):
    """
    Renames a user's banned/ directory to muted/.
    If muted/ already exists, merges the contents and removes banned/.
    """
    if not (os.path.exists(banned_path) or os.path.islink(banned_path)):
        return False
    parent = os.path.dirname(banned_path)
    muted_path = os.path.join(parent, 'muted')

    if not os.path.exists(muted_path):
        if dry_run:
            if verbose:
                print(f"  [User] [Dry-run] Would rename {banned_path} -> {muted_path}")
            return True
        try:
            os.rename(banned_path, muted_path)
            if verbose:
                print(f"  [User] Renamed {banned_path} -> {muted_path}")
            return True
        except Exception as e:
            print(f"Warning: Failed to rename {banned_path} to {muted_path}: {e}", file=sys.stderr)
            return False
    else:
        # Both exist: merge contents
        if dry_run:
            if verbose:
                print(f"  [User] [Dry-run] Would merge {banned_path} into {muted_path} and remove {banned_path}")
            return True
        try:
            if os.path.isdir(banned_path) and os.path.isdir(muted_path):
                for item in os.listdir(banned_path):
                    src_item = os.path.join(banned_path, item)
                    dst_item = os.path.join(muted_path, item)
                    if not os.path.exists(dst_item):
                        os.rename(src_item, dst_item)
                    else:
                        if os.path.isfile(src_item) or os.path.islink(src_item):
                            os.remove(src_item)
                try:
                    os.rmdir(banned_path)
                except OSError:
                    pass
            elif os.path.islink(banned_path):
                os.remove(banned_path)
            if verbose:
                print(f"  [User] Merged {banned_path} into {muted_path}")
            return True
        except Exception as e:
            print(f"Warning: Failed to merge {banned_path} into {muted_path}: {e}", file=sys.stderr)
            return False


def migrate_all_user_banned_to_muted(bbshome, user_home=None, target_user=None, dry_run=False, verbose=False):
    """
    Finds and renames all user banned/ directories to muted/.
    """
    base_dir = user_home if user_home else os.path.join(bbshome, 'home')
    banned_dirs = set()

    if target_user:
        u_name = target_user
        for candidate in (
            os.path.join(base_dir, u_name[0].lower(), u_name, 'banned'),
            os.path.join(base_dir, u_name[0].upper(), u_name, 'banned'),
            os.path.join(base_dir, u_name[0], u_name, 'banned'),
            os.path.join(base_dir, u_name, 'banned'),
        ):
            if os.path.exists(candidate) or os.path.islink(candidate):
                banned_dirs.add(candidate)
    else:
        for p in glob.glob(os.path.join(base_dir, '*', '*', 'banned')):
            banned_dirs.add(p)
        for p in glob.glob(os.path.join(base_dir, '*', 'banned')):
            banned_dirs.add(p)

    renamed_count = 0
    for p in sorted(banned_dirs):
        if rename_user_banned_to_muted(p, dry_run=dry_run, verbose=verbose):
            renamed_count += 1

    return renamed_count


def main():
    parser = argparse.ArgumentParser(
        description="Migrate board banned.history to muted.history and user banned/ to muted/"
    )
    parser.add_argument('-b', '--bbshome', help="BBSHOME directory (default: auto-detected)")
    parser.add_argument('--user-home', help="Path to custom user home directory (writes to PATH/u/userid/muted.history)")
    parser.add_argument('-n', '--dry-run', action='store_true', help="Preview actions without modifying files")
    parser.add_argument('-v', '--verbose', action='store_true', help="Verbose output")
    parser.add_argument('-f', '--overwrite', action='store_true', help="Overwrite matched users' muted.history instead of appending")
    parser.add_argument('--clean', action='store_true', help="Delete all existing home/*/*/muted.history before migrating")
    parser.add_argument('--no-rename', action='store_true', help="Do not rename board history or user banned/ directories")
    parser.add_argument('--board', help="Only process specified board")
    parser.add_argument('--user', help="Only process specified user ID")

    args = parser.parse_args()

    bbshome = find_bbshome(args.bbshome)
    print(f"Using BBSHOME: {bbshome}")
    user_home = os.path.abspath(os.path.expanduser(args.user_home)) if args.user_home else None
    if user_home:
        print(f"Using custom user home root: {user_home}")
        if not args.dry_run:
            os.makedirs(user_home, exist_ok=True)
    if args.dry_run:
        print("Running in DRY-RUN mode (no files will be written)")

    if args.board and args.overwrite:
        print("Warning: using --overwrite with --board will overwrite matched users' muted.history", file=sys.stderr)
        print("         with only records from this board, losing records from other boards!", file=sys.stderr)

    if args.clean:
        clean_all_muted_history(bbshome, user_home=user_home, dry_run=args.dry_run, verbose=args.verbose)

    # Stage 1: Initial search of boards for history files:
    # Check both old and new names, merge if both exist, rename if only old, load muted.history.
    history_files, board_stats = prepare_board_history_files(
        bbshome, target_board=args.board, dry_run=args.dry_run,
        no_rename=args.no_rename, verbose=args.verbose
    )

    print(f"Board history preparation: {board_stats['total_boards']} board(s) found.")
    if board_stats['merged']:
        m_word = "Would merge" if args.dry_run else "Merged"
        print(f"  {m_word} {board_stats['merged']} board(s) having both banned.history and muted.history.")
    if board_stats['renamed']:
        r_word = "Would rename" if args.dry_run else "Renamed"
        print(f"  {r_word} {board_stats['renamed']} board(s) from banned.history to muted.history.")
    if board_stats['already_new']:
        print(f"  {board_stats['already_new']} board(s) already using muted.history.")
    print(f"Loading {len(history_files)} history file(s) for exporting user history.")

    # Stage 2: Export user history
    target_uids = scan_target_user_ids(history_files, target_user=args.user)
    users = load_passwds(bbshome, target_uids=target_uids, verbose=args.verbose)
    if not users and target_uids:
        print("Warning: None of the target users were found in .PASSWDS.", file=sys.stderr)

    total_stats = {
        'files_processed': 0,
        'total_records': 0,
        'matched': 0,
        'skipped_firstlogin': 0,
        'skipped_not_in_passwds': 0,
    }

    # Aggregate records by target user
    aggregated_user_records = {}

    for file_path in history_files:
        total_stats['files_processed'] += 1
        records, stats = parse_banned_history(
            file_path, users, target_user=args.user, verbose=args.verbose
        )
        for k in ('total_records', 'matched', 'skipped_firstlogin', 'skipped_not_in_passwds'):
            total_stats[k] += stats[k]

        for u, recs in records.items():
            aggregated_user_records.setdefault(u, []).extend(recs)

    print(f"Scanned {total_stats['files_processed']} file(s):")
    print(f"  Total records scanned:            {total_stats['total_records']}")
    print(f"  Valid records matched:            {total_stats['matched']}")
    print(f"  Skipped (<= firstlogin):          {total_stats['skipped_firstlogin']}")
    print(f"  Skipped (user not in .PASSWDS):   {total_stats['skipped_not_in_passwds']}")
    print(f"  Distinct users to update:         {len(aggregated_user_records)}")

    # Write records to users' muted.history
    total_written = 0
    for target_lower, recs in aggregated_user_records.items():
        canonical_uid, _ = users[target_lower]
        # Sort chronologically by ban timestamp
        recs.sort(key=lambda x: x[0])
        written = write_user_muted_history(
            bbshome, canonical_uid, recs,
            user_home=user_home,
            overwrite=args.overwrite,
            dry_run=args.dry_run,
            verbose=args.verbose
        )
        total_written += written

    write_action = "Would write" if args.dry_run else "Successfully wrote"
    print(f"{write_action} {total_written} record(s) across {len(aggregated_user_records)} user(s).")

    # Stage 3: Migrate all user banned/ directories to muted/
    if not args.no_rename:
        user_migrated = migrate_all_user_banned_to_muted(
            bbshome, user_home=user_home, target_user=args.user,
            dry_run=args.dry_run, verbose=args.verbose
        )
        u_action = "Would rename" if args.dry_run else "Successfully renamed"
        print(f"{u_action} {user_migrated} user banned/ target(s) to muted/.")

    return 0


if __name__ == '__main__':
    sys.exit(main())
