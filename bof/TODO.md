# BoF TODO

What Lemmy (then Reddit, then Discourse) has that BoF does not yet, and
what BoF does only in part. See `README.md` for what BoF is.

## Lemmy calls not in BoF yet

### Personal lists of posts
- **SavePost** (`SavePost {post_id, save}`, Reddit's "save"): a person
  bookmarks posts, listed with `GetPosts(saved_only)`. PTT has nothing
  alike: favorites are boards only, and people mail posts to themselves
  or keep them in their own announcement area. Keep it per person, next
  to the read records (`readmarks.c`).
- **SaveComment**: the same for comments.
- **HidePost** (`HidePost {post_ids, hide}`): a person hides posts from
  their own listings only; others still see them, and nothing goes to
  the modlog. Unlike RemovePost, which moderators do for everyone with a
  reason. `bof_get_posts` would take an option to skip them.
- **MarkPostAsRead** for many posts at once, and `read: false` (unread
  again) for one post; BoF marks one post read, or a whole board.

### Votes
- **CreatePostLike** without a comment: `bof_create_post_like` is in the
  API for backends that keep votes apart (post.svc); the files backend
  does not have it, as on PTT a vote is always a comment (push or boo).
- **CreateCommentLike**: votes on comments do not exist.
- **ListPostLikes / ListCommentLikes** (who voted): needs votes kept by
  person, as post.svc does.

### Restoring
- **No UI to restore posts**: `bof_restore_post` is in the API (and
  tested), but mbbsd never had restoring in its UI.
- **Restored posts lose their marks and money**: the time capsule keeps
  only the author, date, title and content (`bof_restore_post`); copies
  in ALLPOST are not restored either.
- **DeleteComment** (by its creator): gives `bbs_storage_not_supported`
  for posts in files, as a comment taken out of a file can be neither
  restored nor keep its history; works once comments live in post.svc.
  The UI is there.
- **RemoveComment with `removed: false`**: same reason.

### Comments
- **Comment IDs**: comments in files are numbered by floor; post.svc keys
  them by (post ID, floor). Multi-line comments (post.svc) are one line
  in files.
- **Edit history of a comment**: in files, the whole post is kept in the
  time capsule at each edit; post.svc keeps revisions per comment.

### Reports
- **A menu for admins**: the site's reports of all boards are reached
  from the queue of a board (`s`), not from an admin menu.
- **Alerts** to moderators online when a report comes; they are told
  when they enter the board.
- **Private message reports** (`CreatePrivateMessageReport`): mail.

### Others
- **GetSiteMetadata** (previews of links): not for a terminal BBS.
- **Communities** (boards): CreateCommunity, EditCommunity,
  FollowCommunity (favorites), BlockCommunity, AddModToCommunity,
  BanFromCommunity (muting), TransferCommunity; the board cache (SHM) and
  the board header are still mbbsd's.
- **Persons**: GetPersonDetails, BlockPerson (the BBS reject list),
  BanPerson, accounts in a database (person IDs).
- **Federation** (ActivityPub): `ap_id`, `actor_id`, inboxes.
- **Search** (`Search`): mbbsd searches the list itself.

## Parts of BoF to finish
- The aggregate boards (ALLPOST and others) are kept as copies; Lemmy and
  Reddit list such feeds without copies.
- `readmarks.c` is from PTT BBS and only under the GPL version 2; rewrite
  it to have all of BoF under both licenses.
- **The post.svc backend**: the calls of `postsvc.c` in pttbbs.cmd as a
  backend of the storage virtualization (`bof_storage.h`), made of post.svc
  and the files backend (its cache), and set in `pttbbs.conf`.
- **The read records** (`readmarks.c`) are kept by BoF for the session,
  not through the storage virtualization yet.
