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
  API for backends that keep votes apart (nest.svc); the files backend
  does not have it, as on PTT a vote is always a comment (push or boo).
- **CreateCommentLike**: votes on comments do not exist.
- **ListPostLikes / ListCommentLikes** (who voted): needs votes kept by
  person, as nest.svc does.

### Restoring
- **No UI to restore posts**: `bof_bbs_restore_post` is in the API (and
  tested), but mbbsd never had restoring in its UI.
- **Restored posts lose their marks and money**: the time capsule keeps
  only the author, date, title and content (`bof_bbs_restore_post`); copies
  in ALLPOST are not restored either.
- **DeleteComment** (by its creator): gives `bbs_storage_not_supported`
  for posts in files, as a comment taken out of a file can be neither
  restored nor keep its history; works once comments live in nest.svc.
  The UI is there.
- **RemoveComment with `removed: false`**: same reason.

### Comments
- **IDs in files**: post and comment IDs (`bof_post_id_t`,
  `bof_comment_id()`) are nest.svc's; the files keep none (a record has no
  room), so the files backend gives `bbs_storage_not_supported` for them
  and a Lemmy gateway needs nest.svc as the backend. In files, comments
  are numbered by floor, as nest.svc keys them by (post ID, floor).
  Multi-line comments (nest.svc) are one line in files.
- **Edit history of a comment**: `bof_get_comment_revisions()` gives the
  revisions where the backend keeps them per comment (nest.svc); in
  files, the whole post is kept in the time capsule at each edit, so the
  files backend gives `bbs_storage_not_supported`.

### Reports
- **A menu for admins**: the site's reports of all boards are reached
  from the queue of a board (`s`), not from an admin menu.
- **Alerts** to moderators online when a report comes; they are told
  when they enter the board.
- **Private message reports** (`CreatePrivateMessageReport`): mail.

### Others
- **GetSiteMetadata** (previews of links): not for a terminal BBS.
- **Communities** (boards), beyond GetCommunity, ListCommunities,
  CreateCommunity, EditCommunity and RemoveCommunity: FollowCommunity
  (favorites), BlockCommunity, AddModToCommunity, BanFromCommunity
  (muting), TransferCommunity. EditCommunity takes the whole board header
  as mbbsd made it, not Lemmy's fields. AddModToCommunity (added:
  false) is done only for deleted accounts (bof_bbs_remove_moderator());
  mbbsd still sets the moderators in the board header.
  RemoveCommunity with `removed: false` cannot be done: the name of a
  removed board is free for a new one right away.
- **Persons**, beyond reading one (GetPersonDetails): BlockPerson (the BBS
  reject list), BanPerson, accounts in a database (person IDs).
- **Federation** (ActivityPub): `ap_id`, `actor_id`, inboxes.
- **Search** (`Search`): mbbsd searches the list itself.

## Parts of BoF to finish
- The aggregate boards (ALLPOST and others) are kept as copies; Lemmy and
  Reddit list such feeds without copies.
- `readmarks.c` is from PTT BBS and only under the GPL version 2; rewrite
  it to have all of BoF under both licenses.
- **The nest.svc backend** (`bof_storage_nest`, `bof/nest/`): done, with
  the files of the boards as its cache (nest.svc writes the articles; the
  backend updates `.DIR` with the files backend, `include/bof_fs.h`). Still to do: tests against a running nest.svc, and
  a tool to bring the cache in step again from what it logs
  (`BOF_STORAGE_LOG`; nest.svc has `sync-dir`).
- **The read records** (`readmarks.c`) are kept by BoF for the person in
  session (a gateway switches it, `bof_bbs_read_marks_switch()`), in the
  files of their homes, not through the storage virtualization yet
  (Lemmy keeps `post_read` in its database).
- **The UI and the files of posts**: mbbsd reads posts from the file
  `bof_bbs_get_post_file()` gives and writes new ones aside (`tmp/`),
  created by `bof_create_post()`; forwarding, quoting in a mail, copying
  into the announcement area and `delete_allpost` go through bof too.
  Mails, the digest and the announcement area are not posts of a board
  and keep their files.
- **The rules of the BBS** (`policy.c`): mbbsd takes posting permission,
  the limits of boards, closed comments and who may read a board
  (`HasBoardPerm`) from them. The moderator mode of a session
  (`CBRD_IS_BM`) is still mbbsd's: a group operator gets it only after
  entering through the group, where bof (`bof_bbs_may_moderate()`) goes
  by the groups the board is in.
