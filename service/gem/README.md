# gem.svc

The gem areas (精華區, `man/`) of the boards, kept in one Pebble database
instead of a directory and a `.DIR` per folder and a file per article.

On PTT in 2026 `man/` held 36 million files (124 GB of articles, 207 GB on
disk) in 3.6 million directories; each directory costs about 8 KB (its inode,
its block, its `.DIR`) for the 128 bytes of its record, and the paste of a
directory copies the whole tree, which some users repeat until a board holds
millions of empty directories. gem.svc keeps:

- each distinct content once (SHA-256), however many entries have it;
- each directory as one record (its `.DIR` entries, as they were);
- copies as references: a copied tree is shared until either side changes
  (copy on write), so a paste of a directory is O(1);
- the totals of each subtree, so that quotas are checked on every change.

## Model

A directory is a *listing*: its entries in order, each with its
`fileheader_t` (128 bytes, kept as they are: title, owner, date, filemode)
and what it is: a file (the SHA-256 of its content), a directory (its
listing), a symbolic link (its target) or missing (a `.DIR` record whose file
was gone). Listings and contents are reference counted.

A change to a directory copies the listings from the first shared one down to
the changed one (path copying) and leaves whoever else shares them as they
were. A listing whose last reference goes away is freed in the background,
with what only it referred to.

Lemmy, Reddit and Discourse have no gem area (Reddit's subreddit wiki is the
closest: pages by path, no order, no collections of posts), so the model is
PTT specific. A gem area is a *root*:

| Root | Area | Files it was imported from |
|---|---|---|
| `board:<name>` | of a board | `man/boards/<c>/<name>` |
| `user:<id>` | the mail folder of a person | `home/<c>/<id>/man` |
| `site` | of the site (`Announce`) | `man/` (its own `.DIR`) |

An entry is known by its root and the path of names
(`fileheader_t.filename`) from the top of the area, as in `man/`: names are
unique within a directory, and a copy of a tree keeps the names below its top
entry. Listing IDs are internal. Each entry also keeps when it was made or
last changed (the time of its file, which mbbsd shows in the list).

### Keys

| Key | Value |
|---|---|
| `M next`, `M blob` | next listing ID, next content serial |
| `R` lower(root) | root listing ID, root name |
| `L` id | listing: totals, entries |
| `N` id | references to the listing |
| `B` sha256 | references to the content, its size, its serial |
| `C` serial index | content, 256 KB per key (in `contents.pebble`) |
| `M version` | layout of the keys (3) |
| `Q` id | listing with no reference left, to free |

Contents are found by their hash (`B`) but kept under a serial in the order
they were stored (`C`), in a Pebble of their own (`contents.pebble`, next to
the index `gem.pebble`): an import writes them in order, in tables that
overlap no other, which Pebble moves down its levels instead of merging the
whole key space again (keyed by hash in one database, an import of ptt2
piled up 736 tables in L0 and stalled). Contents are written before what
refers to them and removed after; an import writes them outside the
writer's lock (`StageBlobs`), which it then holds only to record them and
the listing. The lookups of `B` use the bloom filters of the last level
too (`Tx.lookupBlob`), which `DB.Get` skips. A database of an older layout
is refused: import again.

### Totals and quotas

Each listing keeps the totals of its subtree (files, directories, links,
missing, bytes, depth) as users see them: a shared tree counts in every place
that has it. The totals of a board are those of its root. A change that would
take a total over its quota is refused (`bbs_gem_quota_exceeded`), unless it
does not make the total grow: an area already over its quota may still
shrink.

| Quota | Default | Boards over it in 2026 |
|---|---|---|
| `nodes`: entries in the area | 100000 | 48 |
| `dirs`: directories in the area | 20000 | 17 |
| `bytes`: bytes of the files | 1G | 17 |
| `width`: entries in one directory | 5000 | 60 |
| `depth`: levels of directories | 12 | 103 |
| `copy`: entries one copy may bring | 10000 | |

The defaults are 2.5 to 10 times the 99th percentile of the boards. Moves
within an area are not copies. Imports ignore quotas. Boards may have their
own (`[board <name>]` in `etc/gem.svc.conf`); the mail folders of people have
`[user_quota]` (20000 entries, 2000 directories, 200 MB by default); the site
has none. A directory deleted in mbbsd moves to the area of `deleted` (or
`junk`), so give those boards no quota.

## Build and run

```
make
make install                   # gem.svc, gem.ctl -> $BBSHOME/bin
cp ../../sample/etc/gem.svc.conf $BBSHOME/etc/   # optional
$BBSHOME/bin/gem.svc           # forks into the background; -D for foreground
```

The database is in `$BBSHOME/db/gem.db/` (`gem.pebble/` the index,
`contents.pebble/` the contents, backups in `backup/<name>/{index,contents}`). Flags: `-bbshome`, `-config`, `-datadir`, `-socket`
(`$BBSHOME/run/gem.svc.sock`), `-log` (`$BBSHOME/log/gem.svc.log`),
`-cache-mb`, `-workers`.

## Importing

```
gem.ctl import-all -j 8        # the site and every board (existing ones skipped)
gem.ctl import-all -users      # ... and the mail folders of people
gem.ctl import Board           # one area (board:Board, user:id, site); -replace
gem.ctl verify                 # reference counts and totals
gem.ctl export Board /tmp/x    # back to files: .DIR, M.*, D*/ as before
gem.ctl index Board            # the table of contents (what mandex made)
gem.ctl resolve-links          # links -> what they point to (import-all does it)
```

The files are left as they are. An import keeps every `.DIR` record (empty or
bad names too, as missing), so `export` writes back the same `.DIR` files,
articles and symbolic links; files that no `.DIR` lists are counted
(`unlisted`) and not imported. The table of contents util/mandex wrote
(`.index`) is not kept either: the entry that linked to it (or named it)
becomes an entry of kind `index`, named `S.index` (mandex named it
`S<hex time>`, as any link `stamplink()` makes), titled without the size and
owned by `自動更新` instead of `每天自動更新`; it is listed with the time it
is listed (the date mbbsd shows), mbbsd shows what `INDEX` makes, and
`export` writes it as a file, as it is then. New areas get no such entry:
the key `i` of the gem menu shows the table of contents anywhere.
Board names are case-insensitive: of two
directories differing only in case, `import-all` imports the first.

Symbolic links are imported as they are (their targets), then
`resolve-links` (`RESOLVE_LINKS`; `import-all` runs it at the end unless
`-no-resolve`) replaces each by what it points to as it is then, keeping its
header: a directory by the same listing, shared until either side changes
(as a copy), a file by the same content, `.index` by the table of contents.
Absolute targets are taken under BBSHOME or an old BBSHOME (`-old-home`,
default `/home/bbs`), relative ones from the directory of the link, and
`etc/SONGBOOK` and `etc/SONGO` as the links of `util/initbbs`.
A link pointing nowhere, out of `man/` and `home/*/*/man`, or to a directory
it is in (which would then hold itself) becomes missing, and is reported.
The links of man/ were live: a change of the target showed through them; the
copies are not, and gem.svc has no links of its own.

## Deleted entries

An entry deleted in mbbsd (`DELETE`, `gem.ctl delete`) is not removed: it
goes to the trash of its area, `trash:<root>` (e.g. `trash:board:Test`),
with where it was (directory and index), who deleted it and when, as Lemmy
marks a post deleted and an admin purges it later. The directories, their
numbering, totals and quotas are those of what is not deleted. The top of
the trash lists the deleted entries, the last deleted first, under names of
their own (`T.<time>.<hex>`); `LIST` there gives who deleted each in place
of its owner, and when in place of its time. A trash is read as any area
(`LIST`, `GET`, `INDEX`; `COPY` out of it), and changed only by:

```
gem.ctl trash Board              # what was deleted, by whom, when, from where
gem.ctl restore Board T.1.ABC    # back where it was (its index; the end of
                                 # the top if its directory is gone; a new
                                 # name if its own is taken)
gem.ctl purge -days 30           # for cron: remove for good what every trash
                                 # has had for more than 30 days
gem.ctl purge Board T.1.ABC      # one entry, now
```

Renaming an area moves its trash, deleting it drops it. `REMOVE`
(`gem.ctl rm`) still removes for good, as for the songs ordered past the
limit, and so does `EXPIRE` (`gem.ctl expire -days 5 Note SONGO`, for cron):
the files of a directory older than some days, as `find etc/SONGO -mtime +5`
removed them.

## Backups

```
gem.ctl backup [name]          # online checkpoint: db/gem.db/backup/<name>/{index,contents}
gem.ctl daily-backup -dest /backup/gem -verify     # for cron
gem.ctl verify-backup /backup/gem/daily-20261126-0300
gem.ctl sharing /backup/gem/daily-20261126-0300   # what is shared, and could be
```

`daily-backup` works as `nest.ctl daily-backup`: a checkpoint (hard-linked
on the same disk, in seconds), an incremental copy to `-dest` (tables the
previous copy has are hard-linked, so each copy is complete but only new
tables take space; `rsync -aH` keeps the links), a read-only verification
(`-verify`), and the pruning of old `daily-*` backups (`-keep` copies,
`-keep-local` checkpoints). A backup that does not verify prunes nothing.

To restore: stop gem.svc, move `gem.pebble` and `contents.pebble` of the data
directory away, copy the backup's `index` to `gem.pebble` and `contents` to
`contents.pebble`, start gem.svc.

## mbbsd (CONFIG_GEM)

With `CONFIG_GEM` in `pttbbs.conf`, mbbsd shows the gem areas with
`mbbsd/gem.c` (the menus, keys and prompts of `announce.c`, which is left out)
through the client in `common/bbs/gem.c` (`include/gem.h`); `shmctl init`
starts `gem.svc`. Directories are still named by the paths of `man/`, which
`gem_path_parse()` maps to a root and a path, so the callers of `a_menu()`
did not change. Articles are read and edited through a file in
`tmp/gem.<pid>/`. A paste of a copied directory shares it (copy on write). The
entry of kind `index`, and the key `i`, show what `INDEX` makes (one
snapshot, each directory read once, cut at 16 MB); `util/mandex` does
nothing. Links are not followed: one left by an import (before
`resolve-links`) shows as broken. The films of the `Note` board, the songs ordered (`etc/SONGO`), the
creation, renaming and removal of boards follow gem.svc as well.

A new site: `util/initbbs` still makes the gem areas it starts with in
`man/` (the songbook and the moving board of `Note`); after `shmctl init`
starts gem.svc, `gem.ctl import-all` imports them once (`docs/INSTALL`;
gem.svc logs a reminder while it has no area), and `man/` is no longer used.

To switch: import everything (`gem.ctl import-all -users`), build with
`CONFIG_GEM` and restart. To switch back: `gem.ctl export` each area into its
directory, build without `CONFIG_GEM`.

## Protocol

One command per connection on the UNIX socket: a line of words, then the
payload it announces. Replies are `OK ...` (then a payload) or
`ERR <code> [detail]`, with `bbs_` codes as Lemmy has no gem area
(`bbs_couldnt_find_gem_area`, `bbs_couldnt_find_gem`,
`bbs_gem_already_exists`, `bbs_gem_quota_exceeded`, ...). A directory is
`<root> <path>` (a bare board name is `board:<name>`; names joined by `/`,
`.` for the top); an entry is `<root> <path> <index> <name>`, the index being
where the client last saw it (-1 if unknown). Adding at the top of an area
that has none makes it.

Entries are sent as 152-byte records (`gem_entry_t` of `include/gem.h`): the
`fileheader_t`, then `uint8 kind` (1 file, 2 dir, 3 link, 4 missing, 5 index), 3 bytes
of padding, `uint32 nodes` (dir: entries below), `uint64 bytes` (file: size;
dir: bytes below) and `int64 mtime`, little endian.

| Command | Reply |
|---|---|
| `PING` | `PONG` |
| `ROOTS` | `OK <n>`, then a root per line |
| `STAT <root>` | `OK <files> <dirs> <links> <missing> <bytes> <depth> <nodes>` |
| `QUOTA <root>` | `OK <nodes> <dirs> <bytes> <width> <depth> <copy>` |
| `LIST <root> <path> [<offset> [<limit>]]` | `OK <count> <n> <files> <dirs> <links> <missing> <bytes> <depth>`, then n records |
| `ENTRY <entry>` | `OK <index> <target bytes>`, record, link target |
| `GET <entry>` | `OK <bytes>`, content |
| `ADD <file\|dir> <root> <path> <pos> <bytes>` + header + content | `OK <index>`, record (an empty name is made up) |
| `SET_HEADER <entry>` + header | `OK`, record |
| `PUT <entry> <bytes>` + content | `OK` |
| `REMOVE <entry>` | `OK` (for good) |
| `DELETE <entry> <by>` | `OK <name in the trash>` |
| `TRASH <root>` | `OK <n>`, then n lines: name, kind, deleted at, by, index, name it had, path, tab, title |
| `TRASH_ENTRY <root> <name>` | `OK <index> <deleted at> <by> <name it had>`, then the path |
| `RESTORE <root> <name>` | `OK <index> <name>`, then the path |
| `PURGE <days>` or `PURGE <root> <name>` | `OK <n>` |
| `EXPIRE <root> <path> <days>` | `OK <n>` (files older than days days, for good) |
| `REORDER <entry> <to>` | `OK <index>` |
| `COPY <entry> <to root> <to path> <pos> <0\|1>` [+ header] | `OK <index>`, record |
| `MOVE <entry> <to root> <to path> <pos> <0\|1>` [+ header] | `OK <index>`, record |
| `CREATE_ROOT`, `DELETE_ROOT <root>`, `RENAME_ROOT <from> <to>` | `OK` |
| `IMPORT <root> [replace] [<abs dir>]` | `OK <ms> <stats>` |
| `EXPORT <root> <abs dir>` | `OK <stats>` |
| `VERIFY [fix]` | `OK <ok> <listings> <blobs> <entries> <stored bytes> <pending> <n>`, then n problems |
| `INDEX <root>` | `OK <bytes>`, the table of contents |
| `RESOLVE_LINKS [<old BBSHOME>...]` | `OK <dirs> <files> <indexes> <broken> <loops> <n>`, then n problems |
| `PENDING` | `OK <listings waiting to be freed>` |
| `BACKUP [<name>\|<abs path>]` | `OK <dir>` (Pebble checkpoint) |
