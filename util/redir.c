
#include "bbs.h"
#define SKIP_FILE_CHECK

// Site spam patterns, one per line: "title <text>" or "owner <userid>".
#define FN_REDIR_SPAM BBSHOME "/etc/redir_spam"

static char **spam_titles, **spam_owners;
static size_t num_spam_titles, num_spam_owners;

static void
load_spam_patterns(void)
{
    char buf[256];
    FILE *fp = fopen(FN_REDIR_SPAM, "r");
    if (!fp)
        return;
    while (fgets(buf, sizeof(buf), fp)) {
        char *p = strchr(buf, '\n');
        if (p)
            *p = 0;
        if (strncmp(buf, "title ", 6) == 0 && buf[6]) {
            spam_titles = realloc(spam_titles,
                                  sizeof(char *) * (num_spam_titles + 1));
            spam_titles[num_spam_titles++] = strdup(buf + 6);
        } else if (strncmp(buf, "owner ", 6) == 0 && buf[6]) {
            spam_owners = realloc(spam_owners,
                                  sizeof(char *) * (num_spam_owners + 1));
            spam_owners[num_spam_owners++] = strdup(buf + 6);
        }
    }
    fclose(fp);
}

static int
in_site_spam(const fileheader_t *fh)
{
    for (size_t i = 0; i < num_spam_titles; i++)
        if (strcasestr(fh->title, spam_titles[i]))
            return 1;
    for (size_t i = 0; i < num_spam_owners; i++)
        if (strcmp(fh->owner, spam_owners[i]) == 0)
            return 1;
    return 0;
}

int in_spam(const fileheader_t *fh) {
    const char *title = fh->title;
    return strcasestr(title, "[SPAM] FILL YOUR SPAM HERE") ||
	   in_site_spam(fh) ||

	   // legacy return receipt
	   (strcmp(fh->owner, "MAILER-DAEMON") == 0 &&
	    strcmp(fh->title, "Undelivered Mail Returned to Sender") == 0) ||
	   (strcmp(fh->owner, "MAILER-DAEMON") == 0 &&
	    strcmp(fh->title, "Returned mail: see transcript for details") == 0) ||

	   // let's also remove dangerous birthday links
	   (strcmp(fh->owner, BBSNAME) == 0 &&
	    strcmp(fh->title, "!! 生日快樂 !!") == 0) ||
	   0;
}

// What to remove, by the options.
static int remove_spam, remove_days, remove_deleted, remove_modified_days;

// The board whose list (.DIR) it is, if any: its posts go only through BoF
// (with post.svc the files are just its cache). "" for other lists.
static char board[BRDNAME_SIZE];

// Whether the file of a post is gone. With a backend that keeps the posts
// (BOF_FILES_ARE_CACHE), a file not in the cache yet is normal (it is
// written when read): only a post the backend cannot give either is gone.
static int
is_missing(const char *index_path, int index, const fileheader_t *fh)
{
    char file_path[PATHLEN];

    if (!fh->filename[0])
        return 1;
    setdirpath(file_path, index_path, fh->filename);
    if (dashf(file_path))
        return 0;
    if (BOF_FILES_ARE_CACHE && board[0]) {
        bof_post_ref_t ref = {
            .community = board, .bbs_dir = index_path,
            .bbs_index = index, .bbs_header = fh,
        };
        if (bof_bbs_get_post_file(&ref, file_path, sizeof(file_path)) ==
                BOF_OK && dashf(file_path))
            return 0;
    }
    return 1;
}

static int
should_remove(const char *index_path, int index, const fileheader_t *fh,
              time4_t now)
{
    time4_t modified = fh->modified;

    if (remove_spam && in_spam(fh))
        return 1;

    // workaround... (some anonymous posts have modified = 0)
    if (remove_modified_days > 0 && modified == 0)
        modified = atoi(fh->filename + 2);

    if (remove_modified_days > 0 && modified) {
        if (time4_days_elapsed(now, modified) >= remove_modified_days)
            return 1;
    }

    if (remove_days > 0 && strlen(fh->filename) > 2 &&
        fh->filename[1] == '.') {
        time4_t ts = get_fhdr_stamp_ts(fh->filename);
        if (time4_days_elapsed(now, ts) >= remove_days)
            return 1;
    }

    if (remove_deleted && is_missing(index_path, index, fh))
        return 1;
    return 0;
}

// The posts to remove, by their file names (sorted).
typedef struct {
    char (*names)[FNLEN];
    int n;
} remove_set_t;

static int
cmp_name(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static int
is_selected(const fileheader_t *fh, int index GCC_UNUSED, void *ctx)
{
    const remove_set_t *set = ctx;
    char name[FNLEN];

    strlcpy(name, fh->filename, sizeof(name));
    return bsearch(name, set->names, set->n, FNLEN, cmp_name) != NULL;
}

// Picks the posts to remove (and prints them). Returns how many, or -1.
static int
pick(const char *index_path, remove_set_t *set, int *pkeep)
{
    char file_path[PATHLEN];
    fileheader_t fh;
    time4_t now = time4(NULL);
    int total, index;
    FILE *fin;

    *pkeep = 0;
    if (!(fin = fopen(index_path, "rb")))
        return -1;
    fseek(fin, 0, SEEK_END);
    total = ftell(fin) / sizeof(fh);
    rewind(fin);
    set->n = 0;
    set->names = calloc(total > 0 ? total : 1, FNLEN);
    if (!set->names) {
        fclose(fin);
        return -1;
    }
    for (index = 1; set->n < total && fread(&fh, sizeof(fh), 1, fin) == 1;
         index++) {
        if (!should_remove(index_path, index, &fh, now)) {
            (*pkeep)++;
            continue;
        }
        if (fh.filename[0])
            setdirpath(file_path, index_path, fh.filename);
        else
            strlcpy(file_path, index_path, sizeof(file_path));
        printf("%s: %lld[%s] %s\n", file_path,
               (long long)time4_to_time(fh.modified),
               Cdate_mdHMS(&fh.modified), fh.title);
        strlcpy(set->names[set->n++], fh.filename, FNLEN);
    }
    fclose(fin);
    qsort(set->names, set->n, FNLEN, cmp_name);
    return set->n;
}

// A list that is not of a board (e.g. of the time capsule): rewritten here,
// as it always was.
static int
prune_files(const char *index_path, const remove_set_t *set)
{
    char file_path[PATHLEN];
    fileheader_t fh;
    FILE *fin, *fout;
    int index, n = 0;

    AUTO_STR(tmp_index_path, "%s.tmp", index_path);
    if (!(fin = fopen(index_path, "rb")))
        return -1;
    if (!(fout = fopen(tmp_index_path, "wb"))) {
        fprintf(stderr, "failed to create ouptut file.\n");
        fclose(fin);
        return -1;
    }
    for (index = 1; fread(&fh, sizeof(fh), 1, fin) == 1; index++) {
        if (!is_selected(&fh, index, (void *)set)) {
            fwrite(&fh, sizeof(fh), 1, fout);
            continue;
        }
        if (fh.filename[0]) {
            setdirpath(file_path, index_path, fh.filename);
            unlink(file_path);
        }
        n++;
    }
    fclose(fout);
    fclose(fin);
    Rename(tmp_index_path, index_path);
    return n;
}

void die_usage(const char *myname) {
    printf("usage: %s [-options] PATH_TO_FN_DIR\n"
           " -s: remove spam (also patterns in etc/redir_spam)\n"
           " -e: check existence (with post.svc: of the post in post.svc)\n"
           " -d days: remove posts > days\n"
           " -m days: remove posts not modified for days\n"
           " -v: verbose\n"
           "The posts of a board are purged through BoF.\n"
           , myname);
    exit(1);
}

int main(int argc, char **argv)
{
    const char *dirpath, *myname = argv[0];
    char index_path[PATHLEN];
    int opt;
    int num_keep = 0, num_remove = 0;
    remove_set_t set = { NULL, 0 };
    int verbose GCC_UNUSED = 0;

    while ((opt = getopt(argc, argv, "vsed:m:")) != -1) {
        switch (opt) {
            case 's':
                remove_spam = 1;
                load_spam_patterns();
                break;

            case 'd':
                remove_days = atoi(optarg);
                break;

            case 'm':
                remove_modified_days = atoi(optarg);
                break;

            case 'e':
                remove_deleted = 1;
                break;

            case 'v':
                verbose++;
                break;

            default:
                die_usage(myname);
                break;
        }
    }

    if (optind != argc - 1 || optind < 2 ||
        remove_days < 0 || remove_modified_days < 0) {
        die_usage(myname);
    }

    dirpath = argv[optind];
    if (!dashd(dirpath)) {
        fprintf(stderr, "invalid directory: %s\n", dirpath);
        return 1;
    }
    if (bpath_to_brdname(dirpath, board, sizeof(board))) {
        // BoF takes the list of a board as boards/<c>/<name>/.DIR.
        if (chdir(BBSHOME) != 0) {
            perror(BBSHOME);
            return 1;
        }
        setbfile(index_path, board, FN_DIR);
    } else {
        board[0] = 0;
        snprintf(index_path, sizeof(index_path), "%s/%s", dirpath, FN_DIR);
    }

    if (dashs(index_path) <= 0) {
        // no archive. abort.
        // fprintf(stderr, "missing time capsule archive: %s\n", index_path);
        return 1;
    }

    if ((num_remove = pick(index_path, &set, &num_keep)) < 0) {
        fprintf(stderr, "failed to open index file.\n");
        return 2;
    }

    if (num_remove > 0 && board[0]) {
        // Lemmy's PurgePost, as expire does: BoF rewrites the list under
        // its lock (and tells post.svc).
        bof_actor_t sys = { .name = "system" };
        bof_purge_posts_t req = {
            .community = board, .bbs_dir = index_path,
            .bbs_selected = is_selected, .bbs_ctx = &set, .reason = "redir",
        };
        int n = 0;
        if (bof_purge_posts(&sys, &req, &n) != BOF_OK) {
            fprintf(stderr, "%s: could not purge the posts.\n", board);
            free(set.names);
            return 2;
        }
        num_remove = n;
    } else if (num_remove > 0) {
        if ((num_remove = prune_files(index_path, &set)) < 0) {
            free(set.names);
            return 2;
        }
    }
    free(set.names);

    printf("%s: %d entries kept, %d entries removed.\n",
           index_path, num_keep, num_remove);
    return 0;
}
