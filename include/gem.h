/*
 * gem.h - the client of gem.svc (service/gem), which keeps the gem areas
 * (精華區) when CONFIG_GEM is on: those of the boards, of the site and the
 * mail folders of people.
 *
 * A gem area is a root: "board:<name>", "user:<id>" or "site". Within it a
 * directory is a path of names ("" for the top), and an entry is named by
 * its directory, the index it was last seen at (-1 if unknown) and its name
 * (fileheader_t.filename). gem_path_parse() maps the paths of man/
 * (man/boards/<c>/<board>/..., man/..., home/<c>/<id>/man/..., and the
 * links of etc/ into them) to a root and a path.
 *
 * Calls return 0 (or a count) on success and -1 on failure;
 * gem_last_error() is then the error of gem.svc ("bbs_gem_quota_exceeded
 * nodes 100001 > 100000", ...) or of the connection.
 */

#ifndef INCLUDE_GEM_H
#define INCLUDE_GEM_H

#include <stddef.h>
#include <stdint.h>

#define GEM_KIND_FILE     1
#define GEM_KIND_DIR      2
#define GEM_KIND_LINK     3   /* left by an import: not followed (resolve-links) */
#define GEM_KIND_MISSING  4
#define GEM_KIND_INDEX    5   /* the table of contents (gem_index()) */

#define GEM_ROOT_SIZE  (IDLEN + 8)   /* "board:" or "user:" and a name */

/* An entry as gem.svc sends it (service/gem/protocol). */
typedef struct gem_entry_t {
    fileheader_t header;    /* as in .DIR */
    uint8_t  kind;          /* GEM_KIND_* */
    uint8_t  pad[3];
    uint32_t nodes;         /* dir: entries below it */
    uint64_t bytes;         /* file: its size; dir: bytes of the files below */
    int64_t  mtime;         /* made or last changed */
} PACKSTRUCT gem_entry_t;

/* The totals of a directory with all below it (or of a whole area). */
typedef struct gem_totals_t {
    uint64_t files, dirs, links, missing, bytes, depth;
} gem_totals_t;

/* Error codes of gem.svc that callers act on. */
#define GEM_ERR_NO_ROOT   "bbs_couldnt_find_gem_area"
#define GEM_ERR_NOT_FOUND "bbs_couldnt_find_gem"
#define GEM_ERR_EXISTS    "bbs_gem_already_exists"
#define GEM_ERR_QUOTA     "bbs_gem_quota_exceeded"
#define GEM_ERR_INTO_SELF "bbs_gem_move_into_itself"

/* Talks to gem.svc at path (NULL: $BBSHOME/run/gem.svc.sock), e.g. tests. */
void gem_set_socket(const char *path);

/* The last error ("" if none), and whether it has the code. */
const char *gem_last_error(void);
int gem_last_error_is(const char *code);

/* Maps a path of man/ (relative to BBSHOME, or under it) to a root and the
 * path within it. Returns 1 if path is in a gem area, 0 if not (e.g. a post
 * or a mail, which are files). */
int gem_path_parse(const char *path, char *root, size_t rootsz,
                   char *sub, size_t subsz);

/* Lists up to limit entries of root/sub from offset (limit 0: only count).
 * Returns the number listed; *count and *totals (if given) cover the whole
 * directory. A missing area lists as empty. */
int gem_list(const char *root, const char *sub, int offset, int limit,
             gem_entry_t *out, int *count, gem_totals_t *totals);

/* The entry name; its index in *index, a link's target in target. */
int gem_entry(const char *root, const char *sub, int idx, const char *name,
              gem_entry_t *out, int *index, char *target, size_t targetsz);

/* The content of a file, to free(), NUL terminated (its length in *len). */
char *gem_get(const char *root, const char *sub, int idx, const char *name,
              size_t *len);

/* Writes the content of a file to fpath. */
int gem_get_to_file(const char *root, const char *sub, int idx,
                    const char *name, const char *fpath);

/* Adds an entry of kind (GEM_KIND_FILE with data, or GEM_KIND_DIR) at pos
 * (-1: at the end). An empty header.filename is made up; the entry is in
 * *out, its index the result. */
int gem_add(int kind, const char *root, const char *sub, int pos,
            const fileheader_t *h, const void *data, size_t len,
            gem_entry_t *out);

/* gem_add() of a file with the content of fpath. */
int gem_add_file(const char *root, const char *sub, int pos,
                 const fileheader_t *h, const char *fpath, gem_entry_t *out);

/* Replaces the header of an entry (an empty filename keeps the name). */
int gem_set_header(const char *root, const char *sub, int idx,
                   const char *name, const fileheader_t *h, gem_entry_t *out);

/* Replaces the content of a file, from data or from the file fpath. */
int gem_put(const char *root, const char *sub, int idx, const char *name,
            const void *data, size_t len);
int gem_put_file(const char *root, const char *sub, int idx,
                 const char *name, const char *fpath);

int gem_remove(const char *root, const char *sub, int idx, const char *name);

/* Moves an entry to index to within its directory; returns where. */
int gem_reorder(const char *root, const char *sub, int idx, const char *name,
                int to);

/* Copies (or moves) an entry to droot/dsub at pos, with header h (NULL:
 * the source's). gem.svc shares a copied directory until either side
 * changes. Returns the index of the new entry, in *out. */
int gem_copy(int move, const char *root, const char *sub, int idx,
             const char *name, const char *droot, const char *dsub, int pos,
             const fileheader_t *h, gem_entry_t *out);

/* The table of contents of an area (what util/mandex wrote to .index), to
 * free(). */
char *gem_index(const char *root, size_t *len);

/* The totals of a whole area. */
int gem_stat(const char *root, gem_totals_t *totals);

/* Whether root has an area; whether userid has a mail folder. */
int gem_root_exists(const char *root);
int gem_user_folder_exists(const char *userid);

/* Areas: made empty, removed (with all in them), renamed. */
int gem_root_create(const char *root);
int gem_root_delete(const char *root);
int gem_root_rename(const char *from, const char *to);

#endif /* INCLUDE_GEM_H */
