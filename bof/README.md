# BoF - Birds Of Feather

*The meta layer between BBS and community systems.*

> Birds of a feather flock together.

**BoF** is a new layer, designed and built from scratch by
**[Hung-Te Lin (piaip)](http://github.com/hungte/)**, that sits between the user
interfaces of a community -- a telnet BBS, a web front end, a Lemmy app -- and
the systems that keep its data. Every interface talks to its community through
the same small, well-defined API; every storage system serves the same API from
behind. People who gather around the same things (the birds of a feather) should
be able to meet whichever door they come in by.

BoF grew up inside [PTT BBS](https://github.com/ptt/pttbbs), the largest
BBS in Taiwan, as the way to take the BBS apart: to let the telnet UI
(`mbbsd`) stop reaching into files on disk, and to let the same boards be
served to modern community clients and, one day, to the Fediverse.

## Why

A classic BBS is one big program in which the user interface, the
business rules and the on-disk formats (`.DIR`, `boards/`, `.PASSWDS`, ...)
are woven together. That made it fast and robust for decades, but it also
means every new client must reimplement the BBS, and every change to the
storage must touch the UI.

BoF cuts the BBS in two:

```mermaid
flowchart LR
    subgraph UI["User interfaces"]
        mbbsd["mbbsd (telnet)"]
        web["Web / apps"]
        lemmy["Lemmy clients (via a gateway)"]
    end
    subgraph BOF["BoF"]
        api["Lemmy-shaped API: posts, comments"]
    end
    subgraph STORE["Storage backends"]
        files["BBS files (.DIR, boards/, time capsule)"]
        svc["services (post.svc, ...)"]
    end
    mbbsd --> api
    web --> api
    lemmy --> api
    api --> files
    api --> svc
```

## Design principles

- **Lemmy first.** The API follows [Lemmy](https://join-lemmy.org/docs/)
  (API v3): its entities (person, community, post, comment, post_like), its
  field and method names (`CreatePost`, `DeletePost`, `RemovePost`,
  `LockPost`, `CreateCommentLike`, ...), its ID semantics (global, stable,
  never reused) and its error names (`couldnt_find_post`, ...). A BBS that
  speaks Lemmy can be served to Lemmy clients and can federate.
- **Then Reddit, then Discourse.** What Lemmy cannot express follows
  Reddit (scores, crossposts, archived threads, ...), then Discourse (edit
  history, anonymous posting, slow mode, trust levels, ...). Only what none
  of them has is designed anew. BBS-only fields are prefixed with `bbs_`.
- **Identity is logical, layout is physical.** IDs and names follow
  Lemmy; how data is laid out on disk follows how it is used (for example,
  a post's comments are kept together, as a BBS shows them all at once).
- **In-process and fast.** For a telnet UI, BoF is a C library linked into
  the program, not a remote service: a key press must not wait for IPC.
  Gateways for other clients may expose the same API over the network.
- **Many backends, one API.** The first backend is the files of a classic
  PTT BBS; services such as `post.svc` (Pebble and SQLite) come next. The
  user interface does not know or care which one it talks to.
- **Private stays private.** The real person behind an anonymous post,
  IP addresses and hidden boards are never exposed through BoF views.

## The API at a glance

The C API is declared in [`include/bof.h`](../include/bof.h). Every call
takes the actor (the authenticated local user) and a request struct, fills
a response struct, and returns a `bof_error_t`:

```c
bof_actor_t me = { .name = "piaip", .from = "140.112.1.1" };
bof_remove_post_t req = { .post = ref, .removed = 1, .reason = "spam" };
bof_post_result_t res;

bof_error_t err = bof_remove_post(&me, &req, &res);
if (err != BOF_OK)
    printf("failed: %s\n", bof_error_name(err));   /* e.g. couldnt_find_post */
```

| Area | Lemmy | BoF |
|---|---|---|
| Errors | `couldnt_find_post`, ... | `bof_error_t`, `bof_error_name()` |
| People | `person`, `local_user` | `bof_person_id_t`, `bof_actor_t` |
| Posts | `DeletePost`, `RemovePost`, ... | `bof_delete_post()`, `bof_remove_post()`, ... |

The API grows one feature at a time, as `mbbsd` moves onto it.

## Status and roadmap

BoF is new. It is being introduced into PTT BBS step by step, each step
keeping the BBS working exactly as before:

1. **Skeleton** -- library, error names, tests. *(done)*
2. **Posts** -- delete, remove, edit, lock, pin (feature), mark; `mbbsd` no
   longer writes `.DIR` itself.
3. **Comments and votes.**
4. **Listings** -- the post list.
5. **Storage virtualization** -- the files backend behind it, then the
   `post.svc` backend.
6. **Communities** -- boards and moderators.
7. **People** -- accounts in a database, with Lemmy person IDs.
8. **Gateways** for Lemmy clients.

## Lineage

The idea of BoF goes back to piaip's earlier design notes for a successor
to PTT BBS ("The New Architecture" and the "BOF 1.0 API Spec"). Several of
their ideas carry on here, now expressed in Lemmy's terms:

- Every entity has a sequential, enumerable ID per type (users, forums,
  posts), and deletion is a mark inside the item rather than a removal --
  as Lemmy's serial IDs and its `deleted`/`removed` flags.
- A user is separate from the name it shows, with a display name and a
  status line -- as Lemmy's `person.name` and `display_name`.
- A trust ("credit") system earned by logging in and posting, replacing
  PTT's many posting limits -- as Discourse's trust levels.
- API namespaces for users, friend requests, sessions, forums and groups.

## Building and testing

Inside the PTT BBS tree, BoF is built with the rest of it:

```sh
make -C bof         # builds libbof.a
make -C bof test    # runs the tests
```

The sources:

| Directory | What | Header |
|---|---|---|
| `bof/` | The core: the API, its checks, error names (`bof.c`) | [`include/bof.h`](../include/bof.h) |

BoF sources are plain ASCII C (Big5 texts are macros in
`include/common.h`); they use the shared headers in `include/` and the
libraries in `common/`, through `bof_int.h`. They do not include the
headers of mbbsd (`bbs.h`, `proto.h`, `var.h`), so they cannot use the
globals of a session (the current user, board, SHM): callers pass what
BoF needs.

## Author

BoF is designed and written by [Hung-Te Lin (piaip)](http://github.com/hungte)
[<hungte@gmail.com>](mailto:hungte@gmail.com).

## License

BoF is dual-licensed: you may use it under either the
[BSD 2-Clause License](LICENSE) or the
[GNU General Public License, version 2](COPYING.GPL-2), at your choice
(SPDX: `BSD-2-Clause OR GPL-2.0-only`). See [LICENSE](LICENSE).
