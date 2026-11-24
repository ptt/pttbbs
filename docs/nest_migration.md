# Switching the posts to nest.svc (CONFIG_NEST_SVC)

By default the files of the boards are all there is: `boards/<c>/<name>/.DIR`
and the article files (`M.*`). With `CONFIG_NEST_SVC`, BoF (`bof/`,
`include/bof.h`) uses its nest.svc backend (`service/nest`): nest.svc keeps
the posts, comments and votes, and the files of the boards become **its
cache**. The board headers (`.BRD`) stay a file, and BoF tells nest.svc of
every header it writes, so nest.svc mirrors them.

Everything that changes a post must therefore go through BoF. mbbsd does;
so do the tools in `util/` that change posts (see below). Anything that
writes `.DIR` or an article file directly diverges from nest.svc: the next
render or `sync-dir` of nest.svc reverts it, or nest.svc never learns of it.

## Before you start

1. Build and install nest.svc (`service/nest`) and `nest.ctl`, and make sure
   it runs: `nest.ctl ping`. It keeps its databases in `$BBSHOME/db/nest.db/`
   (`--datadir`, or `engine` in `etc/nest.svc.conf`, to put them
   elsewhere).
2. Run the upgrade tools your site still needs (`upgrade/`, see
   `upgrade/README`) **before** importing. The ones that rewrite posts or
   `.DIR` (`convert_utf8`, `convert_sgr_66`, `fix_2026h`,
   `dir_recommend_to_rating`) write the files directly; once nest.svc keeps
   the posts, their changes would be reverted.
3. Turn off `USE_COMMENTD` in `pttbbs.conf`. commentd keeps a copy of each
   comment and is never told of edits, deletes or removes; nest.svc keeps the
   comments itself. `include/config.h` refuses to build with both. The
   comments commentd has can be imported with `--commentd-db` (below).
4. Plan to stop the cron jobs that write the files of the boards while you
   switch (see "Cron jobs").

## Steps

1. Stop the BBS (mbbsd, and the cron jobs that write boards).
2. Run the upgrade tools (above).
3. Start nest.svc.
4. Import the board headers: `nest.ctl import-boards`
   (`nest.ctl import-boards --dry-run` to see what it would do).
   If the moderators want the comments of the boards they run kept as they
   are, also run `upgrade/comment_readonly` by hand (`make -C upgrade
   comment_readonly`, then `upgrade/comment_readonly -w`; without `-w` it
   only tells what it would do). It sets BRD_COMMENT_READONLY on every
   board with moderators: people may neither edit nor delete their own
   comments there, and moderators remove comments with Ctrl-E, `V`. A
   moderator can turn it off in the board settings; boards created later
   allow both. It writes the board headers through BoF, which tells
   nest.svc; nest.svc also takes the flag from `.BRD` when it starts or
   imports the boards.
5. Import the posts, comments and votes of every board:
   `nest.ctl import-board --all`. Useful options: `--dry-run`,
   `--workers N`, `--commentd-db PATH` (the comments of commentd).
   A single board: `nest.ctl import-board <board>`.
6. Write the scores and numbers of comments of nest.svc into the `.DIR` of
   every board: `nest.ctl sync-dir --all`. The files backend never kept the
   number of comments in `.DIR`, and with nest.svc the post list shows it
   instead of the score on boards that do not rate posts (see "Comments
   and votes").
7. Enable `CONFIG_NEST_SVC` in `pttbbs.conf` (`#define CONFIG_NEST_SVC 1`),
   check that `USE_COMMENTD` is not defined, then rebuild everything (the
   setting is read by `include/config.h`, so `bof/`, `common/`, `mbbsd/`
   and `util/` all change) and install it.
8. Check a few boards: `nest.ctl sync-dir <board> --dry-run` should report
   nothing to change.
9. Start the BBS and the cron jobs again (with the changes below).

## Comments and votes

nest.svc keeps votes apart from comments, as Lemmy does (CreatePostLike),
so `CONFIG_NEST_SVC` always turns on `CONFIG_RATING` (`include/config.h`):

- A comment (`X`) is only a comment: no push, boo or arrow, of up to 15
  lines (one floor; a comment of the same author within three minutes is
  merged into it), shown in the format nest.svc writes comments in.
- `CONFIG_COMMENT_EDIT` is always on too: people edit or delete their own
  comments with `E` on the post (its author chooses between the post and
  their comments).
- `CONFIG_COMMENT_MODERATION` is always on too: moderators remove comments
  one by one with Ctrl-E, `V` (also on boards where people may not edit or
  delete their own).
- Boards that rate posts (`BRD_SCORE`, set in the board settings) take
  votes with `%` and list the score; the others list the number of
  comments, and `%` comments there.

The files backend is unchanged: without `CONFIG_RATING`, pushes and boos
are comments and the list shows their score.

## Tools after the switch

| Tool | With nest.svc |
|---|---|
| `util/expire` | Purges expired posts through BoF (`bof_purge_posts`), as before. |
| `util/redir` | Purges the posts of a board through BoF. With `-e`, a post whose file is missing is kept if nest.svc still has it (the file is just not rendered yet). |
| `util/buildir` | On a board directory, makes the list again from nest.svc (as `nest.ctl sync-dir <board> --rebuild`) instead of from the files. Other directories (mail, digest) as before. |
| `util/buildir2`, `util/mergedir2` | Refuse to write their output into a board directory. To restore posts from a backup, import them into nest.svc (`nest.ctl import-board`) and run `nest.ctl sync-dir <board> --rebuild`. |
| `util/chkfnlen --repair` | Inspects the lists of the boards but does not repair them (renaming file names would part them from nest.svc). Mail and digest lists are repaired as before. |
| `util/viewdir` | Read-only by default when built with `CONFIG_NEST_SVC` (`L` does nothing); mbbsd sets FILE_SOLVED through BoF. |
| `util/initbbs`, `util/merge_board`, `upgrade/board_split_title`, `upgrade/migrate_board_moderators` | Write `.BRD` directly: run `nest.ctl import-boards` afterwards. |
| `util/cleandir.pl` | Deletes article files not in `.DIR`: only the cache, but it may race with a post being created. Avoid it, or run it with the BBS stopped. |

`util/post`, `util/account`, `util/angel` and `util/openticket` post through
BoF (`bof_create_post`) and remove the file they wrote if that fails.

## Cron jobs

Review the site crontab. In the production crontab:

- `bin/redir -m 6 boards/A/ALLPOST` (daily): fine after rebuilding `redir`
  (it purges through BoF). Run it from `BBSHOME`, as now.
- `bin/expire_security -d 500 .../boards/S/Security` (weekly): a legacy
  program whose source is not in this tree, which presumably rewrites `.DIR`
  and unlinks files itself. Replace it with `bin/redir -d 500
  boards/S/Security` (posts older than 500 days), which goes through BoF;
  or, to keep a number of posts rather than days, with `bin/expire
  Security` and an entry for `Security` in `etc/expire2.conf`.
- `find .../boards/ -name "SR.*" ... | xargs rm -f`: search caches only;
  fine.
- Any `buildir`, `mergedir2`, `chkfnlen --repair`, or job that moves,
  creates or deletes files under `boards/` (e.g. recreating `ALLPOST` by
  `mv` and `mkdir`): remove them, or replace them with `nest.ctl` commands.

## What stays in files

nest.svc keeps the posts, their comments and votes (and their history). These
stay files, with either backend:

- `.BRD` (mirrored into nest.svc by BoF) and the board cache in shared memory.
- Mail (`home/`), and the digest (`man/`), including copies made into it.
- Files of a board that are not posts: ACL, post types and samples, notes,
  polls, lottery tickets, and the modlog and reports.
- The time capsule (the recycle bin of deleted posts), which is part of the
  cache.
- Read marks (BRC) and search caches (`SR.*`).
