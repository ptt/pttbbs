/*
 * modlog.c - bof (Birds Of Feather): the records of the modlog of boards.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Every moderation action on user generated content and every change of
 * board settings is appended, as a record of a few lines, to two files in
 * the board directory (Lemmy keeps a table per kind of action, read as one
 * modlog):
 *  - FN_MODLOG: public (shown in board config); moderators may edit or
 *    delete anything in it. It shows the role of who did it, not the
 *    account, as Lemmy does with hide_modlog_mod_names.
 *  - FN_MODLOG_AUDIT: the audit record; the BBS only appends to it. It
 *    also has who did it and from where, and fields only for it.
 * The records are made here; the backend writes them (fs/modlog.c).
 */

#include "bof_int.h"

_Static_assert(sizeof(((bof_modlog_t *)0)->action) == STRLEN, "STRLEN");
_Static_assert(sizeof(((bof_modlog_t *)0)->first) == ANSILINELEN,
               "ANSILINELEN");

// The columns that the lines of a record should fit in (Big5: one per byte).
// Longer lines are left for pmore to wrap.
#define MODLOG_COLS (79)

static void
modlog_strip(char *s)
{
    strip_control_sequence(s, s);
    for (; *s; s++) {
        if (*s == '\n' || *s == '\r' || *s == '\t')
            *s = ' ';
    }
}

bof_modlog_t *
bof_modlog_init(bof_modlog_t *m, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(m->action, sizeof(m->action), fmt, ap);
    va_end(ap);
    modlog_strip(m->action);
    m->fields[0] = '\0';
    m->audit_lines[0] = '\0';
    m->audit_fields = NULL;
    m->first[0] = '\0';
    m->first_lines[0] = '\0';
    return m;
}

static void
modlog_add_line(bof_modlog_t *m, const char *line)
{
    strlcat(m->fields, MODLOG_INDENT, sizeof(m->fields));
    strlcat(m->fields, line, sizeof(m->fields));
    strlcat(m->fields, "\n", sizeof(m->fields));
}

void
bof_modlog_add(bof_modlog_t *m, const char *label, const char *fmt, ...)
{
    char value[ANSILINELEN], line[ANSILINELEN + STRLEN];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(value, sizeof(value), fmt, ap);
    va_end(ap);
    modlog_strip(value);
    snprintf(line, sizeof(line), "%s: %s", label, value);
    modlog_add_line(m, line);
}

// Appends to buf the field "label: from => to" (already stripped). If that
// is too long, to is put on the next line, below from.
static void
modlog_change_lines(char *buf, size_t sz, const char *label, const char *a,
                    const char *b)
{
    size_t indent = strlen(MODLOG_INDENT) + strlen(label) + strlen(": ");
    size_t len = strlen(buf);

    if (len >= sz)
        return;
    if (indent + strlen(a) + strlen(" => ") + strlen(b) <= MODLOG_COLS)
        snprintf(buf + len, sz - len, MODLOG_INDENT "%s: %s => %s\n",
                 label, a, b);
    else
        snprintf(buf + len, sz - len, MODLOG_INDENT "%s: %s =>\n%*s%s\n",
                 label, a, (int)indent, "", b);
}

void
bof_modlog_add_change(bof_modlog_t *m, const char *label, const char *from,
                      const char *to)
{
    char a[ANSILINELEN], b[ANSILINELEN];

    strlcpy(a, from, sizeof(a));
    modlog_strip(a);
    strlcpy(b, to, sizeof(b));
    modlog_strip(b);
    modlog_change_lines(m->fields, sizeof(m->fields), label, a, b);
}

void
bof_modlog_set_field(bof_modlog_t *m, const char *label, const char *fmt, ...)
{
    char value[ANSILINELEN];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(value, sizeof(value), fmt, ap);
    va_end(ap);
    modlog_strip(value);
    snprintf(m->first, sizeof(m->first),
             STR_MODLOG_LQUOTE "%s" STR_MODLOG_RQUOTE, value);
    snprintf(m->first_lines, sizeof(m->first_lines), MODLOG_INDENT "%s: %s\n",
             label, value);
}

void
bof_modlog_set_change(bof_modlog_t *m, const char *label, const char *from,
                      const char *to)
{
    char a[ANSILINELEN], b[ANSILINELEN];

    strlcpy(a, from, sizeof(a));
    modlog_strip(a);
    strlcpy(b, to, sizeof(b));
    modlog_strip(b);
    snprintf(m->first, sizeof(m->first),
             STR_MODLOG_LQUOTE "%s" STR_MODLOG_RQUOTE "%s => %s", label, a, b);
    m->first_lines[0] = '\0';
    modlog_change_lines(m->first_lines, sizeof(m->first_lines), label, a, b);
}

// Formats the bits changed from from to to as "+name -name", by their names
// (names[i] for bit i). Empty if none changed.
static void
modlog_format_flags(char *value, size_t sz, uint32_t from, uint32_t to,
                    const char * const *names, int nnames)
{
    char item[STRLEN];
    uint32_t changed = from ^ to;

    value[0] = '\0';
    for (int i = 0; i < 32; i++) {
        uint32_t bit = (uint32_t)1 << i;
        if (!(changed & bit))
            continue;
        if (i < nnames && names[i])
            snprintf(item, sizeof(item), "%s%c%s", value[0] ? " " : "",
                     (to & bit) ? '+' : '-', names[i]);
        else
            snprintf(item, sizeof(item), "%s%c0x%08x", value[0] ? " " : "",
                     (to & bit) ? '+' : '-', bit);
        strlcat(value, item, sz);
    }
}

void
bof_modlog_add_flags(bof_modlog_t *m, const char *label, uint32_t from,
                     uint32_t to, const char * const *names, int nnames)
{
    char value[ANSILINELEN];

    modlog_format_flags(value, sizeof(value), from, to, names, nnames);
    if (value[0])
        bof_modlog_add(m, label, "%s", value);
}

void
bof_modlog_set_flags(bof_modlog_t *m, const char *label, uint32_t from,
                     uint32_t to, const char * const *names, int nnames)
{
    char value[ANSILINELEN];

    modlog_format_flags(value, sizeof(value), from, to, names, nnames);
    if (value[0])
        bof_modlog_set_field(m, label, "%s", value);
}

void
bof_modlog_add_audit(bof_modlog_t *m, const char *label, const char *fmt, ...)
{
    char value[ANSILINELEN];
    size_t len = strlen(m->audit_lines);
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(value, sizeof(value), fmt, ap);
    va_end(ap);
    modlog_strip(value);
    if (len < sizeof(m->audit_lines))
        snprintf(m->audit_lines + len, sizeof(m->audit_lines) - len,
                 MODLOG_INDENT "%s: %s\n", label, value);
}

void
bof_modlog_format_post(char *buf, size_t sz, const fileheader_t *fh)
{
    const char *date = fh->date;

    while (*date == ' ')
        date++;
    snprintf(buf, sz, "%.5s %s %s", date, fh->owner, fh->title);
    modlog_strip(buf);
}

void
bof_modlog_add_post(bof_modlog_t *m, const fileheader_t *fh)
{
    char post[ANSILINELEN];

    bof_modlog_format_post(post, sizeof(post), fh);
    bof_modlog_add(m, STR_MODLOG_POST, "%s", post);
}

// Formats a record: the line "date role action: reason", the fields, those
// only for the audit record (if audit) and "by: who" if by is not NULL.
// What is set by bof_modlog_set_*() and the reason (if not empty) are on
// the first line if they fit there, else as fields: the reason as the last
// of the fields for both records. Returns a buffer to free(), or NULL.
static char *
modlog_record(time4_t now, const char *role, const bof_modlog_t *m,
              const char *why, bool audit, const char *by)
{
    const char *by_label = MODLOG_INDENT STR_MODLOG_BY ": ";
    const char *why_label = MODLOG_INDENT STR_MODLOG_WHY ": ";
    const char *alines = audit ? m->audit_lines : "";
    const char *extra = audit && m->audit_fields ? m->audit_fields : "";
    char date[32];
    size_t len;
    char *s;

    strlcpy(date, CdateHM(&now), sizeof(date));
    // The first line takes what fits: both, the reason, or the change.
    size_t head = strlen(date) + strlen(role) + strlen(m->action) + 2;
    size_t wlen = why[0] ? strlen(": ") + strlen(why) : 0;
    size_t flen = strlen(m->first);
    bool first_in = false, why_in = false;
    if (flen && head + flen + wlen <= MODLOG_COLS)
        first_in = why_in = true;
    else if (head + wlen <= MODLOG_COLS)
        why_in = true;
    else if (flen && head + flen <= MODLOG_COLS)
        first_in = true;
    const char *change = first_in ? m->first : "";
    const char *lines = (flen && !first_in) ? m->first_lines : "";
    bool why_line = why[0] && !why_in;

    len = strlen(date) + strlen(role) + strlen(m->action) + strlen(change) +
          strlen(why_label) + strlen(why) + strlen(lines) + strlen(m->fields) +
          strlen(alines) + strlen(extra) + strlen(by_label) +
          (by ? strlen(by) : 0) + 8;
    if (!(s = malloc(len)))
        return NULL;
    snprintf(s, len, "%s %s %s%s%s%s\n%s%s%s%s%s%s%s%s%s%s", date, role,
             m->action, change, why[0] && why_in ? ": " : "",
             why_in ? why : "", lines, m->fields,
             why_line ? why_label : "", why_line ? why : "",
             why_line ? "\n" : "", alines, extra, by ? by_label : "",
             by ? by : "", by ? "\n" : "");
    return s;
}

int
bof_modlog_format(const bof_actor_t *actor, const char *reason,
                  int reason_audit_only, const bof_modlog_t *m, char **msg,
                  char **amsg)
{
    char why[ANSILINELEN] = "", by[IDLEN + STRLEN + 4];
    const char *role = actor->bbs_role ? actor->bbs_role : "";
    // Both records of the same moment.
    time4_t now = time4(NULL);

    if (reason && *reason) {
        strlcpy(why, reason, sizeof(why));
        modlog_strip(why);
    }
    // The audit record also has who did it and from where, unless they
    // are hidden there too.
    snprintf(by, sizeof(by), "%s (%s)", actor->name ? actor->name : "",
             actor->from ? actor->from : "");
    *msg = modlog_record(now, role, m, reason_audit_only ? "" : why, false,
                         NULL);
    *amsg = modlog_record(now, role, m, why, true,
                          actor->bbs_hidden_in_audit ? NULL : by);
    if (!*msg || !*amsg) {
        free(*msg);
        free(*amsg);
        *msg = *amsg = NULL;
        return -1;
    }
    return 0;
}
