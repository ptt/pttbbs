/* $Id$ */

#include "bbs.h"

// check timecap.c
#ifndef TIME_CAPSULE_ARCHIVE_INDEX_NAME
#define TIME_CAPSULE_ARCHIVE_INDEX_NAME "archive.idx"
#endif
#ifndef TIME_CAPSULE_REVISION_INDEX_NAME
#define TIME_CAPSULE_REVISION_INDEX_NAME ".rev"
#endif

int main(int argc, char **argv)
{
    const char *dirpath;
    char archive_path[PATHLEN], tmp_archive_path[PATHLEN];
    FILE *fin, *fout;
    fileheader_t fh;
    int num_keep = 0, num_remove = 0;

    if (argc != 2) {
        printf("syntax: %s PATH_TO_TIMECAP\n", argv[0]);
        return 1;
    }

    dirpath = argv[1];
    if (!dashd(dirpath)) {
        fprintf(stderr, "invalid directory: %s\n", dirpath);
        return 1;
    }

    snprintf(archive_path, sizeof(archive_path), "%s/%s",
             dirpath, TIME_CAPSULE_ARCHIVE_INDEX_NAME);
    if (dashs(archive_path) <= 0) {
        // no archive. abort.
        // fprintf(stderr, "missing time capsule archive: %s\n", archive_path);
        return 1;
    }

    strlcpy(tmp_archive_path, archive_path, sizeof(tmp_archive_path));
    strlcat(tmp_archive_path, ".tmp", sizeof(tmp_archive_path));

    fin = fopen(archive_path, "rb");
    fout = fopen(tmp_archive_path, "wb");

    if (!fin || !fout) {
        fprintf(stderr, "failed to create temporary index file for output.\n");
        return 2;
    }

    while (fread(&fh, sizeof(fh), 1, fin) == 1) {
        // build file name
        char rev_index_path[PATHLEN];
        setdirpath(rev_index_path, archive_path, fh.filename);
        strlcat(rev_index_path, TIME_CAPSULE_REVISION_INDEX_NAME,
                sizeof(rev_index_path));
        if (!dashf(rev_index_path)) {
            num_remove++;
            continue;
        }
        num_keep++;
        fwrite(&fh, sizeof(fh), 1, fout);
    }

    fclose(fin);
    fclose(fout);
    printf("%s: %d entries kept, %d entries removed.\n",
           archive_path, num_keep, num_remove);
    return Rename(tmp_archive_path, archive_path);
}
