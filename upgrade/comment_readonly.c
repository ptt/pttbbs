/*
 * comment_readonly - keep the comments of boards with moderators as they are.
 *
 * Sets BRD_COMMENT_READONLY (people may neither edit nor delete their own
 * comments) on the boards that have moderators, as many moderators want for
 * the boards they already run when the comments move to nest.svc
 * (CONFIG_NEST). New boards keep the default (edit and delete allowed),
 * and a moderator can change it in the board settings. Run it by hand, once,
 * when switching (see docs/nest_migration.md).
 *
 * The board headers are written through BoF (bof_edit_community), which
 * updates .BRD and the board cache, and tells nest.svc where it is the
 * backend; nest.svc also takes the flag from .BRD when it starts or imports
 * the boards (nest.ctl import-boards).
 *
 * PTT-specific: Lemmy, Reddit and Discourse let a creator edit and delete
 * their comments, and have no community setting against it.
 *
 * Usage: make comment_readonly; ./comment_readonly [-w] [-v] [-b BBSHOME]
 *   Dry run unless -w; group boards and links are skipped.
 */
#define _UTIL_C_
#include "bbs.h"

#include <getopt.h>

static void
usage(const char *prog)
{
    printf("Usage: %s [-w] [-v] [-b BBSHOME]\n\n"
           "Sets BRD_COMMENT_READONLY on every board with moderators (not\n"
           "group boards or links), so that people may neither edit nor\n"
           "delete their own comments there; moderators still may.\n"
           "Dry run unless -w (--write).\n", prog);
}

static int
has_moderators(const boardheader_t *bh)
{
    for (int m = 0; m < MAX_BMs; m++)
        if (bh->moderators[m].unum > 0)
            return 1;
    return 0;
}

int
main(int argc, char **argv)
{
    static struct option opts[] = {
        { "write",   no_argument,       0, 'w' },
        { "verbose", no_argument,       0, 'v' },
        { "bbshome", required_argument, 0, 'b' },
        { "help",    no_argument,       0, 'h' },
        { 0, 0, 0, 0 }
    };
    const char *bbshome = BBSHOME;
    int write_mode = 0, verbose = 0, c;
    int scanned = 0, moderated = 0, already = 0, changed = 0, failed = 0;

    while ((c = getopt_long(argc, argv, "wvb:h", opts, NULL)) != -1) {
        switch (c) {
        case 'w': write_mode = 1; break;
        case 'v': verbose++; break;
        case 'b': bbshome = optarg; break;
        case 'h': usage(argv[0]); return 0;
        default: usage(argv[0]); return 1;
        }
    }

    now = time(NULL);
    drop_privileges();
    if (chdir(bbshome) != 0) {
        fprintf(stderr, "Error: cannot chdir to %s: %s\n", bbshome,
                strerror(errno));
        return 1;
    }
    if (!attach_check_SHM()) {
        fprintf(stderr, "Error: unable to attach the shared memory of the "
                "BBS (is it set up?)\n");
        return 1;
    }
    resolve_boards();

    int nboards = num_boards();
    if (nboards <= 0 || nboards > MAX_BOARD)
        nboards = MAX_BOARD;
    for (int bid = 1; bid <= nboards; bid++) {
        boardheader_t bh;

        if (get_boardheader(&bh, bid) <= 0 || !bh.brdname[0] ||
            (bh.brdattr & (BRD_SYMBOLIC | BRD_GROUPBOARD)))
            continue;
        scanned++;
        if (!has_moderators(&bh))
            continue;
        moderated++;
        if (bh.brdattr & BRD_COMMENT_READONLY) {
            already++;
            continue;
        }
        if (verbose)
            printf("%s %s\n", write_mode ? "set" : "would set", bh.brdname);
        if (!write_mode) {
            changed++;
            continue;
        }
        bh.brdattr |= BRD_COMMENT_READONLY;
        bof_edit_community_t ed = { .bbs_id = bid, .bbs_header = &bh };
        if (bof_edit_community(NULL, &ed) == BOF_OK) {
            changed++;
        } else {
            failed++;
            fprintf(stderr, "Error: cannot write the header of %s (bid %d)\n",
                    bh.brdname, bid);
        }
    }

    printf("%s: %d boards, %d with moderators: %d already read-only, "
           "%d %s, %d failed.\n", write_mode ? "Done" : "Dry run", scanned,
           moderated, already, changed, write_mode ? "set" : "to set", failed);
    if (write_mode && changed)
        log_filef("log/comment_readonly.log",
                  "%s set BRD_COMMENT_READONLY on %d boards with moderators\n",
                  Cdatelite(&now), changed);
    return failed ? 1 : 0;
}
