/*
 * Tests of the modlog of bof.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 */

#include "bbs.h"
#include "bof_test.h"

int bof_test_failures;

static char test_dir[] = "/tmp/bof_test_XXXXXX";

static char *
slurp(const char *path, char *buf, size_t sz)
{
    FILE *fp = fopen(path, "r");
    size_t n = 0;

    if (fp) {
        n = fread(buf, 1, sz - 1, fp);
        fclose(fp);
    }
    buf[n] = '\0';
    return buf;
}

static void
test_records(void)
{
    bof_modlog_t m;
    bof_actor_t mod = {
        .name = "alice", .from = "10.0.0.1", .bbs_role = "BM",
    };
    char *msg, *amsg;

    bof_modlog_init(&m, "lock %d", 1);
    bof_modlog_add(&m, "post", "%s", "a \033[1mtitle\033[m\nnext");
    bof_modlog_add_audit(&m, "content", "%s", "secret");
    EXPECT_INT(bof_modlog_format(&mod, "spam", 0, &m, &msg, &amsg), 0);
    // Color codes and line breaks are stripped from the fields.
    EXPECT(strstr(msg, " BM lock 1: spam\n  post: a title next\n") != NULL);
    EXPECT(strstr(msg, "secret") == NULL);
    EXPECT(strstr(msg, "alice") == NULL);
    EXPECT(strstr(amsg, "  content: secret\n") != NULL);
    EXPECT(strstr(amsg, "alice (10.0.0.1)") != NULL);
    free(msg);
    free(amsg);

    // The reason only in the audit record; the police hidden there too.
    mod.bbs_hidden_in_audit = 1;
    EXPECT_INT(bof_modlog_format(&mod, "spam", 1, &m, &msg, &amsg), 0);
    EXPECT(strstr(msg, "spam") == NULL);
    EXPECT(strstr(amsg, "spam") != NULL);
    EXPECT(strstr(amsg, "alice") == NULL);
    free(msg);
    free(amsg);
}

static void
test_write(void)
{
    bof_modlog_t m;
    bof_actor_t mod = { .name = "alice", .from = "here", .bbs_role = "BM" };
    char path[PATHLEN], buf[4096];

    mkdir("boards", 0755);
    mkdir("boards/T", 0755);
    mkdir("boards/T/Test", 0755);
    bof_modlog_set_change(bof_modlog_init(&m, "set"), "title", "old", "new");
    EXPECT_INT(bof_modlog_write("Test", &mod, NULL, 0, &m), 0);
    bof_get_modlog_path("Test", 0, path, sizeof(path));
    EXPECT_STR(path, "boards/T/Test/" FN_MODLOG);
    EXPECT(strstr(slurp(path, buf, sizeof(buf)),
                  " BM set" STR_MODLOG_LQUOTE "title" STR_MODLOG_RQUOTE
                  "old => new\n") != NULL);
    EXPECT_INT(bof_modlog_note_edit("Test", &mod), 0);
    bof_get_modlog_path("Test", 1, path, sizeof(path));
    slurp(path, buf, sizeof(buf));
    EXPECT(strstr(buf, STR_MODLOG_EDITED) != NULL);
    EXPECT(strstr(buf, "alice (here)") != NULL);
}

int
main(void)
{
    char cmd[PATHLEN];

    if (!mkdtemp(test_dir) || chdir(test_dir) != 0) {
        perror("setup");
        return 1;
    }
    RUN(test_records);
    RUN(test_write);
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", test_dir);
    if (system(cmd) != 0)
        fprintf(stderr, "could not remove %s\n", test_dir);
    return bof_test_report();
}
