
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

int process(FILE *fin, FILE *fout, const char *index_path,
            int *pkeep, int remove_spam, int remove_days,
            int remove_deleted, int remove_modified_days) {

    char file_path[PATHLEN];
    fileheader_t fh;
    int num_remove = 0;
    time4_t now = time4(NULL);

    *pkeep = 0;
    while (fread(&fh, sizeof(fh), 1, fin) == 1) {
        int should_remove = 0;
        // build file name
        setdirpath(file_path, index_path, fh.filename);

	if (!should_remove && remove_spam && in_spam(&fh))
            should_remove = 1;

	// workaround... (some anonymous posts have modified = 0)
	if (remove_modified_days > 0 && fh.modified == 0)
	    fh.modified = atoi(fh.filename + 2);

        if (!should_remove && remove_modified_days > 0 && fh.modified) {
            if (time4_days_elapsed(now, fh.modified) >= remove_modified_days)
                should_remove = 1;
        }

        if (!should_remove && remove_days > 0 && strlen(fh.filename) > 2 &&
            fh.filename[1] == '.') {
            time4_t ts = get_fhdr_stamp_ts(fh.filename);
            if (time4_days_elapsed(now, ts) >= remove_days)
                should_remove = 1;
        }

        if (!should_remove && remove_deleted && !dashf(file_path)) {
            should_remove = 1;
        }

        if (should_remove) {
	    if (!fout)
		return 1;

            printf("%s: %lld[%s] %s\n", file_path, (long long)time4_to_time(fh.modified),
                   Cdate_mdHMS(&fh.modified), fh.title);
	    unlink(file_path);
	    num_remove++;
	    if (!fout)
		return 1;
            continue;
        }
	(*pkeep)++;
	if (fout)
	    fwrite(&fh, sizeof(fh), 1, fout);
    }
    return num_remove;
}

void die_usage(const char *myname) {
    printf("usage: %s [-options] PATH_TO_FN_DIR\n"
           " -s: remove spam (also patterns in etc/redir_spam)\n"
           " -e: check existence\n"
           " -d days: remove posts > days\n"
           " -v: verbose\n"
           , myname);
    exit(1);
}

int main(int argc, char **argv)
{
    const char *dirpath, *myname = argv[0];
    int opt;
    int num_keep = 0, num_remove = 0;
    FILE *fin;
    int remove_spam = 0, remove_days = 0, remove_deleted = 0,
        remove_modified_days = 0;
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

    AUTO_STR(index_path, "%s/%s", dirpath, FN_DIR);
    if (dashs(index_path) <= 0) {
        // no archive. abort.
        // fprintf(stderr, "missing time capsule archive: %s\n", index_path);
        return 1;
    }

    fin = fopen(index_path, "rb");
    if (!fin) {
        fprintf(stderr, "failed to open index file.\n");
        return 2;
    }

    if ((num_remove = process(fin, NULL, index_path, &num_keep,
                              remove_spam, remove_days, remove_deleted,
                              remove_modified_days))) {
	FILE *fout;

	AUTO_STR(tmp_index_path, "%s.tmp", index_path);
	fout = fopen(tmp_index_path, "wb");
	if (!fout) {
	    fprintf(stderr, "failed to create ouptut file.\n");
	    return 2;
	}
	rewind(fin);
	num_remove = process(fin, fout, index_path, &num_keep,
                             remove_spam, remove_days, remove_deleted,
                             remove_modified_days);
	fclose(fout);
	fclose(fin);
	Rename(tmp_index_path, index_path);
    } else {
	fclose(fin);
    }

    printf("%s: %d entries kept, %d entries removed.\n",
           index_path, num_keep, num_remove);
    return 0;
}
