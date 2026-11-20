# BoF TODO

What Lemmy (then Reddit, then Discourse) has that BoF does not yet, and
what BoF does only in part. See `README.md` for what BoF is.

## Lemmy calls not in BoF yet

### Personal lists of posts
- **SavePost** (`SavePost {post_id, save}`, Reddit's "save"): a person
  bookmarks posts, listed with `GetPosts(saved_only)`. PTT has nothing
  alike: favorites are boards only, and people mail posts to themselves
  or keep them in their own announcement area.
- **SaveComment**: the same for comments.
- **HidePost** (`HidePost {post_ids, hide}`): a person hides posts from
  their own listings only; others still see them, and nothing goes to
  the modlog. Unlike RemovePost, which moderators do for everyone with a
  reason. `bof_get_posts` would take an option to skip them.

### Votes
- **CreatePostLike / CreateCommentLike** without a comment: on PTT a vote
  is always a comment (push or boo). Votes on comments do not exist.
- **ListPostLikes / ListCommentLikes** (who voted): needs votes kept by
  person, as post.svc does.

### Comments
- **Comment IDs**: comments in files are numbered by floor; post.svc keys
  them by (post ID, floor). Multi-line comments (post.svc) are one line
  in files.
- **Edit history of a comment**: in files, the whole post is kept in the
  time capsule at each edit; post.svc keeps revisions per comment.

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
- **The post.svc backend**: the calls of `postsvc.c` in pttbbs.cmd as a
  backend of the storage virtualization (`bof_storage.h`), made of post.svc
  and the files backend (its cache), and set in `pttbbs.conf`.
