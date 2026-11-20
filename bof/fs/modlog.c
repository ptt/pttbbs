/*
 * modlog.c - bof (Birds Of Feather), the files backend: the modlog of
 * boards in their files.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * The records (made by modlog.c) are appended to two files in the board
 * directory: FN_MODLOG, public, which moderators may edit, and
 * FN_MODLOG_AUDIT, the audit record, which the BBS only appends to.
 */

#include "../bof_int.h"
#include "bof_fs.h"

int
bof_fs_write_modlog(void *ctx GCC_UNUSED, const char *community,
                    const bof_actor_t *actor, const char *reason,
                    int reason_audit_only, const bof_modlog_t *m)
{
    char *msg, *amsg, path[PATHLEN];

    if (!community || !*community || !actor || !m)
        return -1;
    if (bof_modlog_format(actor, reason, reason_audit_only, m, &msg,
                          &amsg) < 0)
        return -1;
    setbfile(path, community, FN_MODLOG);
    file_append(path, msg);
    setbfile(path, community, FN_MODLOG_AUDIT);
    file_append(path, amsg);
    free(msg);
    free(amsg);
    return 0;
}

int
bof_fs_note_modlog_edit(void *ctx GCC_UNUSED, const char *community,
                        const bof_actor_t *actor)
{
    char path[PATHLEN];
    time4_t now = time4(NULL);

    if (!community || !*community || !actor)
        return -1;
    setbfile(path, community, FN_MODLOG_AUDIT);
    return file_appendf(path, "%s %s " STR_MODLOG_EDITED "\n"
                        MODLOG_INDENT STR_MODLOG_BY ": %s (%s)\n",
                        CdateHM(&now), actor->bbs_role ? actor->bbs_role : "",
                        actor->name ? actor->name : "",
                        actor->from ? actor->from : "");
}

void
bof_get_modlog_path(const char *community, int audit, char *path,
                    size_t sz)
{
    char buf[PATHLEN];

    setbfile(buf, community, audit ? FN_MODLOG_AUDIT : FN_MODLOG);
    strlcpy(path, buf, sz);
}
